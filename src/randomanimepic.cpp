// randomanimepic.cpp — Случайная аниме-картинка и список категорий через NekosAPI v4 (C++ ABI 3).
// Портирован из Python-модуля RandomAnimePic (by kote).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr const char* VERSION = "1.1.0";
constexpr const char* RANDOM_API_URL = "https://api.nekosapi.com/v4/images/random";
constexpr const char* IMAGES_API_URL = "https://api.nekosapi.com/v4/images";
constexpr int CATEGORIES_SCAN_LIMIT  = 500;
constexpr int CATEGORIES_CACHE_TTL   = 3600;

constexpr long long EMO_OK   = 4916036072560919511LL;
constexpr long long EMO_LINK = 5877465816030515018LL;
constexpr long long EMO_LOAD = 4911241630633165627LL;
constexpr long long EMO_ERR  = 5116151848855667552LL;

std::mutex g_cat_mu;
bool g_cat_cached = false;
std::chrono::steady_clock::time_point g_cat_ts;
std::vector<std::string> g_cached_cats;

static void write_be32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

static uint32_t crc32_compute(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

static uint32_t adler32_compute(const uint8_t* data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521U;
        b = (b + a) % 65521U;
    }
    return (b << 16) | a;
}

static void append_png_chunk(std::vector<uint8_t>& png, const char type[4],
                             const uint8_t* payload, size_t len) {
    write_be32(png, static_cast<uint32_t>(len));
    size_t type_off = png.size();
    png.insert(png.end(), type, type + 4);
    if (len > 0) png.insert(png.end(), payload, payload + len);
    uint32_t crc = crc32_compute(png.data() + type_off, 4 + len);
    write_be32(png, crc);
}

// Сжимает RGBA-буфер в PNG через runtime libz.so (либо stored DEFLATE как фоллбэк).
static bool encode_rgba_png(const uint8_t* rgba, int width, int height, size_t stride,
                            std::vector<uint8_t>& out_png) {
    if (!rgba || width <= 0 || height <= 0) return false;

    const size_t row_bytes = static_cast<size_t>(width) * 4;
    std::vector<uint8_t> raw((row_bytes + 1) * static_cast<size_t>(height));
    for (int y = 0; y < height; ++y) {
        raw[static_cast<size_t>(y) * (row_bytes + 1)] = 0; // filter None
        std::memcpy(raw.data() + static_cast<size_t>(y) * (row_bytes + 1) + 1,
                    rgba + static_cast<size_t>(y) * stride,
                    row_bytes);
    }

    std::vector<uint8_t> zdata;
    bool compressed = false;

    if (void* zlib = dlopen("libz.so", RTLD_LAZY)) {
        using CompressFn = int (*)(uint8_t*, unsigned long*, const uint8_t*, unsigned long, int);
        auto fn_compress2 = reinterpret_cast<CompressFn>(dlsym(zlib, "compress2"));
        if (fn_compress2) {
            unsigned long dest_len = static_cast<unsigned long>(raw.size()) + static_cast<unsigned long>(raw.size() / 100) + 64;
            zdata.resize(dest_len);
            if (fn_compress2(zdata.data(), &dest_len, raw.data(),
                             static_cast<unsigned long>(raw.size()), 6) == 0) {
                zdata.resize(dest_len);
                compressed = true;
            }
        }
    }

    if (!compressed) {
        // Валидный zlib-поток без сжатия (DEFLATE stored blocks).
        zdata.clear();
        zdata.push_back(0x78);
        zdata.push_back(0x01);
        size_t off = 0;
        while (off < raw.size()) {
            size_t chunk = std::min<size_t>(65535, raw.size() - off);
            bool last = (off + chunk == raw.size());
            zdata.push_back(last ? 0x01 : 0x00);
            uint16_t len = static_cast<uint16_t>(chunk);
            uint16_t nlen = static_cast<uint16_t>(~len);
            zdata.push_back(static_cast<uint8_t>(len & 0xFF));
            zdata.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
            zdata.push_back(static_cast<uint8_t>(nlen & 0xFF));
            zdata.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
            zdata.insert(zdata.end(), raw.data() + off, raw.data() + off + chunk);
            off += chunk;
        }
        write_be32(zdata, adler32_compute(raw.data(), raw.size()));
    }

    static const uint8_t PNG_SIG[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    out_png.assign(PNG_SIG, PNG_SIG + 8);

    std::vector<uint8_t> ihdr;
    write_be32(ihdr, static_cast<uint32_t>(width));
    write_be32(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8); // bit depth
    ihdr.push_back(6); // RGBA
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    append_png_chunk(out_png, "IHDR", ihdr.data(), ihdr.size());
    append_png_chunk(out_png, "IDAT", zdata.data(), zdata.size());
    append_png_chunk(out_png, "IEND", nullptr, 0);
    return true;
}

// Конвертация WebP -> PNG через Android 11+ AImageDecoder (dlsym без зависимости линковки).
static bool convert_webp_to_png_android(const std::string& webp_path, const std::string& png_path) {
    void* jnig = dlopen("libjnigraphics.so", RTLD_LAZY);
    if (!jnig) return false;

    using CreateFromBufFn = int (*)(const void*, size_t, void**);
    using DeleteFn        = void (*)(void*);
    using GetHeaderFn     = const void* (*)(const void*);
    using GetDimFn        = int32_t (*)(const void*);
    using SetFmtFn        = int (*)(void*, int32_t);
    using SetTargetFn     = int (*)(void*, int32_t, int32_t);
    using GetStrideFn     = size_t (*)(void*);
    using DecodeFn        = int (*)(void*, void*, size_t, size_t);

    auto fn_create   = reinterpret_cast<CreateFromBufFn>(dlsym(jnig, "AImageDecoder_createFromBuffer"));
    auto fn_delete   = reinterpret_cast<DeleteFn>(dlsym(jnig, "AImageDecoder_delete"));
    auto fn_header   = reinterpret_cast<GetHeaderFn>(dlsym(jnig, "AImageDecoder_getHeaderInfo"));
    auto fn_width    = reinterpret_cast<GetDimFn>(dlsym(jnig, "AImageDecoderHeaderInfo_getWidth"));
    auto fn_height   = reinterpret_cast<GetDimFn>(dlsym(jnig, "AImageDecoderHeaderInfo_getHeight"));
    auto fn_set_fmt  = reinterpret_cast<SetFmtFn>(dlsym(jnig, "AImageDecoder_setAndroidBitmapFormat"));
    auto fn_target   = reinterpret_cast<SetTargetFn>(dlsym(jnig, "AImageDecoder_setTargetSize"));
    auto fn_stride   = reinterpret_cast<GetStrideFn>(dlsym(jnig, "AImageDecoder_getMinimumStride"));
    auto fn_decode   = reinterpret_cast<DecodeFn>(dlsym(jnig, "AImageDecoder_decodeImage"));

    if (!fn_create || !fn_delete || !fn_header || !fn_width || !fn_height ||
        !fn_set_fmt || !fn_stride || !fn_decode) {
        return false;
    }

    std::ifstream in(webp_path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    std::streamsize sz = in.tellg();
    if (sz <= 0 || sz > 25 * 1024 * 1024) return false;
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    if (!in.read(reinterpret_cast<char*>(buf.data()), sz)) return false;

    void* decoder = nullptr;
    if (fn_create(buf.data(), buf.size(), &decoder) != 0 || !decoder) return false;

    const void* info = fn_header(decoder);
    int32_t w = fn_width(info);
    int32_t h = fn_height(info);
    if (w <= 0 || h <= 0) {
        fn_delete(decoder);
        return false;
    }

    // Ограничиваем размеры под лимиты Telegram Photo (до 2048 px по длинной стороне).
    if (fn_target && (w > 2048 || h > 2048)) {
        if (w >= h) {
            h = std::max<int32_t>(1, static_cast<int32_t>((static_cast<int64_t>(h) * 2048) / w));
            w = 2048;
        } else {
            w = std::max<int32_t>(1, static_cast<int32_t>((static_cast<int64_t>(w) * 2048) / h));
            h = 2048;
        }
        fn_target(decoder, w, h);
    }

    // ANDROID_BITMAP_FORMAT_RGBA_8888 = 1
    if (fn_set_fmt(decoder, 1) != 0) {
        fn_delete(decoder);
        return false;
    }

    size_t stride = fn_stride(decoder);
    if (stride < static_cast<size_t>(w) * 4) {
        fn_delete(decoder);
        return false;
    }

    std::vector<uint8_t> pixels(stride * static_cast<size_t>(h));
    if (fn_decode(decoder, pixels.data(), stride, pixels.size()) != 0) {
        fn_delete(decoder);
        return false;
    }
    fn_delete(decoder);

    std::vector<uint8_t> png;
    if (!encode_rgba_png(pixels.data(), w, h, stride, png)) return false;

    std::ofstream out(png_path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return out.good();
}

static bool has_webp_magic(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    uint8_t hdr[12] = {0};
    if (!in.read(reinterpret_cast<char*>(hdr), 12)) return false;
    return std::memcmp(hdr, "RIFF", 4) == 0 && std::memcmp(hdr + 8, "WEBP", 4) == 0;
}

static std::string get_cache_root(koto_ctx* ctx, const koto_api* api) {
    if (api && api->cache_dir) {
        char buf[512] = {0};
        int n = api->cache_dir(ctx, buf, sizeof(buf) - 1);
        if (n > 0) return std::string(buf, static_cast<size_t>(n)) + "/rapic";
    }
    return "/data/local/tmp/rapic_cache";
}

static void emit_emo(koto_ctx* ctx, const koto_api* api, const char* fallback, long long doc_id) {
    if (api->c_emoji && doc_id != 0) api->c_emoji(ctx, fallback, doc_id);
    else api->c_text(ctx, fallback);
}

static void respond(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

static void emit_error(koto_ctx* ctx, const koto_api* api, bool en, const std::string& extra = "") {
    api->c_reset(ctx);
    emit_emo(ctx, api, "🚫", EMO_ERR);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, en ? "An unexpected error occurred..." : "Произошла непредвиденная ошибка...", KOTO_ENT_BOLD);
    if (!extra.empty()) {
        api->c_text(ctx, "\n");
        api->c_fmt(ctx, extra.c_str(), KOTO_ENT_CODE);
    }
    respond(ctx, api);
}

void cmd_rapic(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    bool en = koto_utils::is_en(ctx, api);

    std::string category = api->cmd_args ? koto_utils::trim(api->cmd_args(ctx) ? api->cmd_args(ctx) : "") : "";

    if (api->is_outgoing(ctx)) {
        api->c_reset(ctx);
        emit_emo(ctx, api, "✨", EMO_LOAD);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, en ? "Loading image..." : "Загрузка изображения...", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
    }

    std::string req_url = std::string(RANDOM_API_URL) + "?limit=1&rating=safe";
    if (!category.empty()) {
        req_url += "&tags=" + koto_utils::url_encode(category);
    }

    std::string resp = koto_utils::http_get(api, ctx, req_url, {"User-Agent: RandomAnimePic/1.0"}, 15);
    if (resp.empty()) {
        emit_error(ctx, api, en, "Empty API response");
        return;
    }

    std::string img_url;
    long long img_id = 0;
    try {
        json j = json::parse(resp);
        if (!j.is_array() || j.empty()) {
            emit_error(ctx, api, en, en ? "No images for this category" : "Картинки по категории не найдены");
            return;
        }
        const json* chosen = &j[0];
        for (const auto& item : j) {
            if (item.is_object() && item.value("rating", "") == "safe") {
                chosen = &item;
                break;
            }
        }
        img_url = chosen->value("url", "");
        img_id  = chosen->value("id", 0LL);
        if (img_url.empty()) {
            emit_error(ctx, api, en, "Missing url field");
            return;
        }
    } catch (const std::exception& e) {
        emit_error(ctx, api, en, e.what());
        return;
    }

    std::error_code ec;
    std::string dir = get_cache_root(ctx, api);
    fs::create_directories(dir, ec);

    std::string raw_path = dir + "/rapic_" + std::to_string(img_id) + ".webp";
    std::string png_path = dir + "/rapic_" + std::to_string(img_id) + ".png";

    if (!koto_utils::download_file(api, ctx, img_url, raw_path, {}, 20)) {
        fs::remove(raw_path, ec);
        emit_error(ctx, api, en, "Download failed");
        return;
    }

    std::string send_path = raw_path;
    int send_flags = KOTO_SEND_AS_FILE; // WebP без конвертации шлём документом, чтобы сохранить подпись
    const bool modern_host = api->host_version && api->host_version(ctx) >= 118;

    if (!has_webp_magic(raw_path)) {
        std::string jpg_path = dir + "/rapic_" + std::to_string(img_id) + ".jpg";
        fs::rename(raw_path, jpg_path, ec);
        if (!ec) {
            send_path = jpg_path;
            send_flags = modern_host ? (KOTO_SEND_AS_PHOTO | KOTO_REPLY_TO_TARGET) : 0;
        }
    } else if (modern_host) {
        // На хосте 118+ ядро само конвертирует WebP -> PNG через Android BitmapFactory (Android 8+).
        send_flags = KOTO_SEND_AS_PHOTO | KOTO_REPLY_TO_TARGET;
    } else if (convert_webp_to_png_android(raw_path, png_path)) {
        fs::remove(raw_path, ec);
        send_path = png_path;
        send_flags = 0;
    }

    api->c_reset(ctx);
    emit_emo(ctx, api, "✅", EMO_OK);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, en ? "Your anime pic" : "Ваша аниме-картинка", KOTO_ENT_BOLD);
    api->c_text(ctx, "\n");
    emit_emo(ctx, api, "🔗", EMO_LINK);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, en ? "URL: " : "Ссылка: ", KOTO_ENT_BOLD);
    api->c_text(ctx, img_url.c_str());

    if (api->c_send_file && api->chat_id) {
        api->c_send_file(ctx, api->chat_id(ctx), send_path.c_str(), send_flags);
        if (api->is_outgoing(ctx) && api->delete_msg) api->delete_msg(ctx);
    } else {
        respond(ctx, api);
    }
}

void cmd_racategories(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    bool en = koto_utils::is_en(ctx, api);

    if (api->is_outgoing(ctx)) {
        api->c_reset(ctx);
        emit_emo(ctx, api, "✨", EMO_LOAD);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, en ? "Loading categories..." : "Загрузка категорий...", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
    }

    std::vector<std::string> categories;
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lk(g_cat_mu);
        if (g_cat_cached &&
            std::chrono::duration_cast<std::chrono::seconds>(now - g_cat_ts).count() < CATEGORIES_CACHE_TTL) {
            categories = g_cached_cats;
        }
    }

    if (categories.empty()) {
        std::set<std::string> tag_set;
        for (int offset = 0; offset < CATEGORIES_SCAN_LIMIT; offset += 100) {
            std::string url = std::string(IMAGES_API_URL) + "?limit=100&offset=" + std::to_string(offset) + "&rating=safe";
            std::string resp = koto_utils::http_get(api, ctx, url, {"User-Agent: RandomAnimePic/1.0"}, 20);
            if (resp.empty()) break;

            try {
                json j = json::parse(resp);
                const json* items = nullptr;
                if (j.is_object() && j.contains("items") && j["items"].is_array()) items = &j["items"];
                else if (j.is_object() && j.contains("results") && j["results"].is_array()) items = &j["results"];
                else if (j.is_array()) items = &j;
                if (!items || items->empty()) break;

                for (const auto& item : *items) {
                    if (item.is_object() && item.contains("tags") && item["tags"].is_array()) {
                        for (const auto& t : item["tags"]) {
                            if (t.is_string()) {
                                std::string ts = t.get<std::string>();
                                if (!ts.empty()) tag_set.insert(ts);
                            }
                        }
                    }
                }
                if (items->size() < 100) break;
            } catch (...) {
                break;
            }
        }
        categories.assign(tag_set.begin(), tag_set.end());
        if (!categories.empty()) {
            std::lock_guard<std::mutex> lk(g_cat_mu);
            g_cat_cached = true;
            g_cat_ts = now;
            g_cached_cats = categories;
        }
    }

    if (categories.empty()) {
        api->c_reset(ctx);
        emit_emo(ctx, api, "🚫", EMO_ERR);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, en ? "Categories not found" : "Категории не найдены", KOTO_ENT_BOLD);
        respond(ctx, api);
        return;
    }

    api->c_reset(ctx);
    emit_emo(ctx, api, "✅", EMO_OK);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, en ? "Available categories\n" : "Доступные категории\n", KOTO_ENT_BOLD);

    int mark = api->c_mark ? api->c_mark(ctx) : -1;
    for (size_t i = 0; i < categories.size(); ++i) {
        if (i > 0) api->c_text(ctx, ", ");
        api->c_fmt(ctx, categories[i].c_str(), KOTO_ENT_CODE);
    }
    if (mark >= 0 && api->c_wrap) {
        api->c_wrap(ctx, mark, KOTO_ENT_BLOCKQUOTE, 1);
    }
    respond(ctx, api);
}

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    (void)ctx; (void)api;
}

static const koto_command COMMANDS[] = {
    {"rapic", cmd_rapic, KOTO_LEVEL_TRUSTED, "[ru] Получить рандомную аниме-картинку (by kote). [en] Fetch random anime pic (by kote).", "[category]"},
    {"racategories", cmd_racategories, KOTO_LEVEL_TRUSTED, "[ru] Получить список категорий из NekosAPI (by kote). [en] Fetch categories from NekosAPI (by kote).", ""}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "randomanimepic",
    "[ru] Случайные аниме-картинки и категории через NekosAPI v4 (by kote). [en] Random anime pics and categories via NekosAPI v4 (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    nullptr, 0,
    on_callback,
    nullptr, 0,
    "https://github.com/AresUser1/KoteModules/blob/main/src/randomanimepic.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
