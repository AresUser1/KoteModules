// giftgif.cpp — Подарок Telegram → анимированная карточка MP4 или GIF (C++20 ABI 3).
// Порт GiftGif v1.4.27 (@n3post) для KoteLoader (Android arm64-v8a / armeabi-v7a).
// Нативный рендер: rlottie (.tgs) + libwebp (#giftPattern) + Roboto Medium (stb_truetype)
// + аппаратное кодирование H.264 MP4 через Android NDK AMediaCodec/AMediaMuxer (+ GIF89a фоллбэк).
// Автор порта: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "giftgif_assets/stb_truetype.h"

#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "giftgif_assets/stb_image.h"

#include "giftgif_assets/giftgif_assets.hpp"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#ifdef __ANDROID__
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>
#endif

// Минимальные C-прототипы статически влинкованных rlottie и libwebpdecoder
extern "C" {
typedef struct Lottie_Animation_S Lottie_Animation;
Lottie_Animation* lottie_animation_from_data(const char* data, const char* key, const char* resource_path);
void lottie_animation_destroy(Lottie_Animation* animation);
void lottie_animation_render(Lottie_Animation* animation, size_t frame_num,
                             uint32_t* buffer, size_t width, size_t height, size_t bytes_per_line);
uint8_t* WebPDecodeRGBA(const uint8_t* data, size_t data_size, int* width, int* height);
void WebPFree(void* ptr);
}

namespace {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

static constexpr const char* MOD_VERSION = "1.0.0";

// ── Премиум эмодзи ───────────────────────────────────────────────────────────
static constexpr long long EMO_DIAMOND   = 5471952986970267163LL; // 💎
static constexpr long long EMO_SPARKLES  = 5325547803936572038LL; // ✨
static constexpr long long EMO_CHECK     = 5776375003280838798LL; // ✅
static constexpr long long EMO_CROSS     = 5778527486270770928LL; // ❌
static constexpr long long EMO_HOURGLASS = 5456591761558245861LL; // ⌛
static constexpr long long EMO_BOLT      = 5456140674028019486LL; // ⚡
static constexpr long long EMO_BOOK      = 5386596911463541476LL; // 📚
static constexpr long long EMO_CHART     = 5231200819986047254LL; // 📊
static constexpr long long EMO_GEAR      = 5222142557865128918LL; // ⚙️

// ── Калиброванные константы геометрии (1-в-1 с GiftGif v1.4.27) ─────────────
static constexpr int CARD_SIZE_MIN = 64;
static constexpr int CARD_SIZE_MAX = 1536;
static constexpr int MAX_BATCH = 10;
static constexpr int MAX_TILES = 9;
static constexpr float GRID_GAP = 0.03f;
static constexpr float GRID_MARGIN = 0.03f;
static constexpr uint8_t PAGE_BG[3] = {16, 16, 16};
static constexpr float TILE_WIDTH = 0.8139f;
static constexpr float ASPECT = 1.1735f;
static constexpr float TILE_TOP = 0.0777f;
static constexpr float NAME_CAP = 0.0540f;
static constexpr float NUMBER_CAP = 0.0380f;
static constexpr float NAME_GAP = 0.075f;
static constexpr float CAPTION_CAP = 0.032f;
static constexpr float TEXT_GAP = 0.021f;
static constexpr float CAP_TO_SIZE = 0.711f;
static constexpr float SQUARE_MODEL_INSET = 0.6155f;
static constexpr float SQUARE_MODEL_DY = -0.006f;
static constexpr float MODEL_PAD = 0.12f;
static constexpr float RADIAL_R = 0.6965f;
static constexpr float PATTERN_GAIN = 1.22f;
static constexpr int GRAD_SHIFT[3] = {-4, 0, 1};

struct PatternSpot {
    float cx, cy, side, opacity;
};

static constexpr PatternSpot SQUARE_PATTERN_SPOTS[21] = {
    {0.217346f, 0.004801f, 0.095502f, 0.153366f},
    {0.498945f, 0.033132f, 0.092167f, 0.216002f},
    {0.782023f, 0.004807f, 0.095501f, 0.155126f},
    {0.317400f, 0.150328f, 0.120370f, 0.222858f},
    {0.678638f, 0.151802f, 0.120397f, 0.222539f},
    {0.022000f, 0.196000f, 0.096000f, 0.155000f},
    {0.978000f, 0.196000f, 0.096000f, 0.155000f},
    {0.181353f, 0.291079f, 0.095377f, 0.219557f},
    {0.817783f, 0.291050f, 0.095197f, 0.219311f},
    {0.115508f, 0.481714f, 0.132913f, 0.225252f},
    {0.881112f, 0.482535f, 0.134446f, 0.225086f},
    {0.002093f, 0.670301f, 0.093999f, 0.156008f},
    {0.998084f, 0.670440f, 0.096290f, 0.159052f},
    {0.203282f, 0.755457f, 0.111121f, 0.222788f},
    {0.794572f, 0.753880f, 0.110541f, 0.221525f},
    {0.498897f, 0.874323f, 0.098530f, 0.218100f},
    {0.325292f, 0.910353f, 0.092161f, 0.163350f},
    {0.674127f, 0.910358f, 0.092201f, 0.164187f},
    {0.075000f, 0.912000f, 0.094000f, 0.153000f},
    {0.925000f, 0.912000f, 0.094000f, 0.153000f},
    {0.499170f, 1.008728f, 0.095164f, 0.158498f},
};

static const char* UA_POOL[3] = {
    "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15",
    "User-Agent: TelegramBot (like TwitterBot)",
};

// Сериализация тяжёлого рендера + кэш ассетов
static std::mutex g_render_mtx;
static std::mutex g_asset_mtx;
static std::vector<uint8_t> g_roboto_ttf;
static stbtt_fontinfo g_roboto_font{};
static bool g_roboto_ready = false;
static std::vector<uint8_t> g_fallback_alpha;
static int g_fallback_w = 0, g_fallback_h = 0;
static std::unordered_map<std::string, std::vector<uint8_t>> g_pattern_bin_cache;
static std::unordered_map<std::string, std::vector<uint8_t>> g_emoji_png_cache;

// ── Утилиты ──────────────────────────────────────────────────────────────────

struct RGB {
    uint8_t r, g, b;
};

struct RGBAImage {
    int w = 0, h = 0;
    std::vector<uint8_t> px; // RGBA8888, size = w*h*4
    RGBAImage() = default;
    RGBAImage(int width, int height, uint8_t r = 0, uint8_t g = 0, uint8_t b = 0, uint8_t a = 0)
        : w(width), h(height), px(static_cast<size_t>(width) * height * 4) {
        for (size_t i = 0; i < px.size(); i += 4) {
            px[i] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = a;
        }
    }
};

struct GrayImage {
    int w = 0, h = 0;
    std::vector<uint8_t> px; // 1 byte per pixel
};

static uint8_t clamp_u8(int v) {
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static std::string to_lower_utf8_ru(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c0 = static_cast<unsigned char>(s[i]);
        if (c0 < 0x80) {
            out.push_back(static_cast<char>(std::tolower(c0)));
            i += 1;
        } else if (c0 == 0xD0 && i + 1 < s.size()) {
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            if (c1 >= 0x90 && c1 <= 0x9F) { out.push_back(static_cast<char>(0xD0)); out.push_back(static_cast<char>(c1 + 0x20)); }
            else if (c1 >= 0xA0 && c1 <= 0xAF) { out.push_back(static_cast<char>(0xD1)); out.push_back(static_cast<char>(c1 - 0x20)); }
            else if (c1 == 0x81) { out.push_back(static_cast<char>(0xD1)); out.push_back(static_cast<char>(0x91)); }
            else { out.push_back(static_cast<char>(c0)); out.push_back(static_cast<char>(c1)); }
            i += 2;
        } else {
            out.push_back(static_cast<char>(c0));
            i += 1;
        }
    }
    return out;
}

static std::string get_prefix(koto_ctx* ctx, const koto_api* api) {
    char pbuf[32] = ".";
    if (api && api->setting_get) api->setting_get(ctx, "prefix", pbuf, sizeof(pbuf) - 1);
    return pbuf[0] ? std::string(pbuf) : std::string(".");
}

static std::string get_cfg_str(koto_ctx* ctx, const koto_api* api, const char* key, const char* def) {
    if (api && api->cfg_get) {
        char buf[512] = {0};
        int n = api->cfg_get(ctx, key, buf, sizeof(buf) - 1);
        if (n > 0 && buf[0]) return std::string(buf, static_cast<size_t>(n));
    }
    return def;
}

static int get_cfg_int(koto_ctx* ctx, const koto_api* api, const char* key, int def, int lo, int hi) {
    std::string v = get_cfg_str(ctx, api, key, "");
    if (v.empty()) return def;
    int x = std::atoi(v.c_str());
    return std::max(lo, std::min(hi, x));
}

static bool get_cfg_bool(koto_ctx* ctx, const koto_api* api, const char* key, bool def) {
    std::string v = to_lower_utf8_ru(koto_utils::trim(get_cfg_str(ctx, api, key, "")));
    if (v.empty()) return def;
    if (v == "1" || v == "true" || v == "yes" || v == "on" || v == "да") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off" || v == "нет") return false;
    return def;
}

static std::string get_db_str(koto_ctx* ctx, const koto_api* api, const char* key) {
    if (api && api->db_get) {
        char buf[1024] = {0};
        int n = api->db_get(ctx, key, buf, sizeof(buf) - 1);
        if (n > 0) return std::string(buf, static_cast<size_t>(n));
    }
    return "";
}

static std::string get_cache_root(koto_ctx* ctx, const koto_api* api) {
    if (api && api->cache_dir) {
        char buf[512] = {0};
        int n = api->cache_dir(ctx, buf, sizeof(buf) - 1);
        if (n > 0) return std::string(buf, static_cast<size_t>(n)) + "/giftgif";
    }
    return "/data/local/tmp/giftgif_cache";
}

static void respond(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing && api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else if (api->c_reply) api->c_reply(ctx, 0);
    else api->c_edit(ctx, 0);
}

static std::string human_size(uint64_t bytes) {
    char buf[64];
    if (bytes >= 950ULL * 1024ULL) {
        std::snprintf(buf, sizeof(buf), "%.1f МБ", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f КБ", static_cast<double>(bytes) / 1024.0);
    }
    return buf;
}

static bool zlib_decompress_buf(const uint8_t* src, size_t src_len, std::vector<uint8_t>& out, int window_bits = MAX_WBITS) {
    z_stream zs{};
    if (inflateInit2(&zs, window_bits) != Z_OK) return false;
    zs.next_in = const_cast<Bytef*>(src);
    zs.avail_in = static_cast<uInt>(src_len);

    out.clear();
    uint8_t chunk[32768];
    int ret = Z_OK;
    while (ret == Z_OK) {
        zs.next_out = chunk;
        zs.avail_out = sizeof(chunk);
        ret = inflate(&zs, 0);
        size_t have = sizeof(chunk) - zs.avail_out;
        if (have > 0) {
            if (out.size() + have > 32 * 1024 * 1024) {
                inflateEnd(&zs);
                return false;
            }
            out.insert(out.end(), chunk, chunk + have);
        }
    }
    inflateEnd(&zs);
    return ret == Z_STREAM_END;
}

static void ensure_embedded_assets() {
    std::lock_guard<std::mutex> lk(g_asset_mtx);
    if (!g_roboto_ready) {
        if (zlib_decompress_buf(GIFTGIF_ROBOTO_Z, sizeof(GIFTGIF_ROBOTO_Z), g_roboto_ttf, MAX_WBITS)) {
            if (stbtt_InitFont(&g_roboto_font, g_roboto_ttf.data(), stbtt_GetFontOffsetForIndex(g_roboto_ttf.data(), 0))) {
                g_roboto_ready = true;
            }
        }
    }
    if (g_fallback_alpha.empty()) {
        std::vector<uint8_t> png;
        if (zlib_decompress_buf(GIFTGIF_PATTERN_Z, sizeof(GIFTGIF_PATTERN_Z), png, MAX_WBITS)) {
            int w = 0, h = 0, ch = 0;
            unsigned char* dec = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &ch, 1);
            if (dec && w > 0 && h > 0) {
                g_fallback_w = w;
                g_fallback_h = h;
                g_fallback_alpha.assign(dec, dec + static_cast<size_t>(w) * h);
                stbi_image_free(dec);
            }
        }
    }
}

// ── HTTP загрузка (бинарная и текстовая, через Host ABI 118+ http_get_ex / http_get) ─

static bool fetch_bytes(koto_ctx* ctx, const koto_api* api, const std::string& url,
                        const std::string& headers_str, std::vector<uint8_t>& out, size_t max_bytes = 6 * 1024 * 1024) {
    out.clear();
    // 1. Прямой бинарный буфер через host ABI (работает на Android без curl!)
    if (api) {
        const size_t cap = std::min(max_bytes, static_cast<size_t>(4 * 1024 * 1024));
        std::vector<uint8_t> buf(cap);
        long long n = -1;
        if (!headers_str.empty() && api->host_version && api->host_version(ctx) >= 118 && api->http_get_ex) {
            n = api->http_get_ex(ctx, url.c_str(), headers_str.c_str(), buf.data(), static_cast<int>(buf.size()));
        } else if (api->http_get) {
            n = api->http_get(ctx, url.c_str(), buf.data(), static_cast<int>(buf.size()));
        }
        if (n > 0) {
            size_t actual = std::min(static_cast<size_t>(n), buf.size());
            out.assign(buf.begin(), buf.begin() + static_cast<ptrdiff_t>(actual));
            return true;
        }
    }
    // 2. Fallback через koto_utils::download_file (для host-тестов / curl)
    std::string tmp_path = get_cache_root(ctx, api) + "/tmp_dl_" + std::to_string(std::hash<std::string>{}(url)) + ".bin";
    std::error_code ec;
    fs::create_directories(fs::path(tmp_path).parent_path(), ec);
    std::vector<std::string> hdrs;
    if (!headers_str.empty()) hdrs.push_back(headers_str);
    if (koto_utils::download_file(api, ctx, url, tmp_path, hdrs, 25)) {
        std::ifstream f(tmp_path, std::ios::binary);
        if (f) {
            out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }
        fs::remove(tmp_path, ec);
        return !out.empty();
    }
    fs::remove(tmp_path, ec);
    return false;
}

// ── Парсинг страницы подарка t.me/nft/<slug> ─────────────────────────────────

struct Card {
    std::string slug;
    std::string name;
    int64_t number = -1;
    std::string tgs_url;
    std::string pattern_url;
    RGB grad_top{0x6c, 0x9e, 0xdd};
    RGB grad_bot{0x5c, 0x6e, 0xc9};
    RGB pattern_rgb{0x12, 0x39, 0xa2};
    std::string caption;
    bool show_footer = true;
};

static RGB parse_hex_rgb(const std::string& hex) {
    size_t off = (!hex.empty() && hex[0] == '#') ? 1 : 0;
    if (hex.size() < off + 6) return {0x6c, 0x9e, 0xdd};
    auto h2 = [&](size_t p) -> uint8_t {
        return static_cast<uint8_t>(std::strtol(hex.substr(p, 2).c_str(), nullptr, 16));
    };
    return {h2(off), h2(off + 2), h2(off + 4)};
}

static RGB shift_rgb(RGB c) {
    return {
        clamp_u8(static_cast<int>(c.r) + GRAD_SHIFT[0]),
        clamp_u8(static_cast<int>(c.g) + GRAD_SHIFT[1]),
        clamp_u8(static_cast<int>(c.b) + GRAD_SHIFT[2]),
    };
}

static std::string html_unescape(std::string s) {
    auto rep = [&](const char* a, const char* b) {
        size_t pos = 0;
        while ((pos = s.find(a, pos)) != std::string::npos) {
            s.replace(pos, std::strlen(a), b);
            pos += std::strlen(b);
        }
    };
    rep("&amp;", "&");
    rep("&quot;", "\"");
    rep("&#39;", "'");
    rep("&apos;", "'");
    rep("&lt;", "<");
    rep("&gt;", ">");
    return s;
}

static std::string resolve_slug(const std::string& raw) {
    std::string s = koto_utils::trim(raw);
    std::smatch m;
    static const std::regex re_url(R"(t\.me/nft/([A-Za-z0-9_-]+))", std::regex::icase);
    if (std::regex_search(s, m, re_url)) return m[1].str();
    while (!s.empty() && s.back() == '/') s.pop_back();
    size_t slash = s.find_last_of('/');
    if (slash != std::string::npos) s = s.substr(slash + 1);
    return s;
}

static bool parse_card_html(const std::string& html, const std::string& slug, Card& out, std::string& err) {
    out = Card{};
    out.slug = slug;

    std::smatch m;
    static const std::regex re_tgs1(R"(type="application/x-tgsticker"[^>]*srcset="([^"]+))");
    static const std::regex re_tgs2(R"(srcset="(https://[^"]*\.tgs[^"]*))");
    if (std::regex_search(html, m, re_tgs1)) {
        out.tgs_url = html_unescape(m[1].str());
    } else if (std::regex_search(html, m, re_tgs2)) {
        out.tgs_url = html_unescape(m[1].str());
    } else {
        err = "на странице нет анимации подарка (.tgs)";
        return false;
    }

    static const std::regex re_title(R"(og:title"\s+content="([^"]+))");
    if (std::regex_search(html, m, re_title)) {
        std::string full = html_unescape(m[1].str());
        static const std::regex re_num(R"(^(.*?)\s*#(\d+)\s*$)");
        std::smatch mn;
        if (std::regex_match(full, mn, re_num)) {
            out.name = koto_utils::trim(mn[1].str());
            out.number = std::atoll(mn[2].str().c_str());
        } else {
            out.name = koto_utils::trim(full);
        }
    }
    if (out.name.empty()) out.name = slug;

    static const std::regex re_pat1(R"(<image[^>]*id="giftPattern"[^>]*xlink:href="([^"]+))");
    static const std::regex re_pat2(R"(<image[^>]*id="giftPattern"[^>]*href="([^"]+))");
    if (std::regex_search(html, m, re_pat1) || std::regex_search(html, m, re_pat2)) {
        out.pattern_url = html_unescape(m[1].str());
    }

    static const std::regex re_stop(R"re(<stop[^>]*stop-color="(#[0-9a-fA-F]{6})")re");
    std::vector<RGB> stops;
    auto begin = std::sregex_iterator(html.begin(), html.end(), re_stop);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end && stops.size() < 2; ++it) {
        stops.push_back(shift_rgb(parse_hex_rgb((*it)[1].str())));
    }
    if (stops.size() >= 2) {
        out.grad_top = stops[0];
        out.grad_bot = stops[1];
    }

    static const std::regex re_flood(R"re(flood-color="(#[0-9a-fA-F]{6})")re");
    if (std::regex_search(html, m, re_flood)) {
        out.pattern_rgb = parse_hex_rgb(m[1].str());
    }
    return true;
}

static bool load_card_assets(koto_ctx* ctx, const koto_api* api, const std::string& raw,
                             Card& card, std::string& lottie_json, GrayImage& pattern_alpha, std::string& err) {
    std::string slug = resolve_slug(raw);
    if (slug.empty()) { err = "пустой слаг подарка"; return false; }

    bool parsed = false;
    std::string last_err = "не удалось загрузить страницу подарка";
    for (int attempt = 0; attempt < 4; ++attempt) {
        std::vector<uint8_t> html_bytes;
        if (fetch_bytes(ctx, api, "https://t.me/nft/" + slug, UA_POOL[attempt % 3], html_bytes, 1024 * 1024)) {
            std::string html(reinterpret_cast<const char*>(html_bytes.data()), html_bytes.size());
            if (parse_card_html(html, slug, card, last_err)) {
                parsed = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(450 * (attempt + 1)));
    }
    if (!parsed) { err = last_err; return false; }

    // Скачиваем и распаковываем .tgs (gzip)
    std::vector<uint8_t> tgs;
    if (!fetch_bytes(ctx, api, card.tgs_url, UA_POOL[0], tgs, 4 * 1024 * 1024)) {
        err = "не удалось скачать .tgs анимацию";
        return false;
    }
    if (tgs.size() >= 2 && tgs[0] == 0x1f && tgs[1] == 0x8b) {
        std::vector<uint8_t> raw_json;
        if (!zlib_decompress_buf(tgs.data(), tgs.size(), raw_json, 16 + MAX_WBITS)) {
            err = "ошибка распаковки .tgs (gzip)";
            return false;
        }
        lottie_json.assign(reinterpret_cast<const char*>(raw_json.data()), raw_json.size());
    } else {
        lottie_json.assign(reinterpret_cast<const char*>(tgs.data()), tgs.size());
    }

    // Загружаем узор #giftPattern (WebP -> alpha mask) с кэшем и фоллбэком на встроенный силуэт
    ensure_embedded_assets();
    pattern_alpha.w = 0;
    pattern_alpha.h = 0;
    pattern_alpha.px.clear();

    if (!card.pattern_url.empty()) {
        std::vector<uint8_t> pblob;
        {
            std::lock_guard<std::mutex> lk(g_asset_mtx);
            auto it = g_pattern_bin_cache.find(card.pattern_url);
            if (it != g_pattern_bin_cache.end()) pblob = it->second;
        }
        if (pblob.empty()) {
            if (fetch_bytes(ctx, api, card.pattern_url, UA_POOL[0], pblob, 512 * 1024)) {
                std::lock_guard<std::mutex> lk(g_asset_mtx);
                if (g_pattern_bin_cache.size() >= 32) g_pattern_bin_cache.erase(g_pattern_bin_cache.begin());
                g_pattern_bin_cache[card.pattern_url] = pblob;
            }
        }
        if (!pblob.empty()) {
            int pw = 0, ph = 0;
            uint8_t* rgba = WebPDecodeRGBA(pblob.data(), pblob.size(), &pw, &ph);
            if (rgba && pw > 0 && ph > 0) {
                pattern_alpha.w = pw;
                pattern_alpha.h = ph;
                pattern_alpha.px.resize(static_cast<size_t>(pw) * ph);
                uint8_t min_a = 255, max_a = 0;
                for (int i = 0; i < pw * ph; ++i) {
                    uint8_t a = rgba[i * 4 + 3];
                    if (a < min_a) min_a = a;
                    if (a > max_a) max_a = a;
                    pattern_alpha.px[i] = a;
                }
                if (min_a == 255 || max_a == 0) {
                    // Плоская картинка без альфы -> яркость
                    for (int i = 0; i < pw * ph; ++i) {
                        pattern_alpha.px[i] = static_cast<uint8_t>(
                            (77 * rgba[i * 4] + 150 * rgba[i * 4 + 1] + 29 * rgba[i * 4 + 2]) >> 8);
                    }
                }
                WebPFree(rgba);
            }
        }
    }
    if (pattern_alpha.px.empty() && !g_fallback_alpha.empty()) {
        pattern_alpha.w = g_fallback_w;
        pattern_alpha.h = g_fallback_h;
        pattern_alpha.px = g_fallback_alpha;
    }
    return true;
}

// ── Графика: градиент, узор, суперэллипс, текст Roboto + Twemoji ─────────────

static GrayImage resize_gray_bilinear(const GrayImage& src, int dst_w, int dst_h) {
    GrayImage dst;
    dst.w = dst_w;
    dst.h = dst_h;
    dst.px.resize(static_cast<size_t>(dst_w) * dst_h, 0);
    if (src.w <= 0 || src.h <= 0) return dst;
    float sx = static_cast<float>(src.w) / dst_w;
    float sy = static_cast<float>(src.h) / dst_h;
    for (int y = 0; y < dst_h; ++y) {
        float fy = (y + 0.5f) * sy - 0.5f;
        int y0 = std::max(0, std::min(src.h - 1, static_cast<int>(std::floor(fy))));
        int y1 = std::min(src.h - 1, y0 + 1);
        float wy = std::max(0.0f, std::min(1.0f, fy - y0));
        for (int x = 0; x < dst_w; ++x) {
            float fx = (x + 0.5f) * sx - 0.5f;
            int x0 = std::max(0, std::min(src.w - 1, static_cast<int>(std::floor(fx))));
            int x1 = std::min(src.w - 1, x0 + 1);
            float wx = std::max(0.0f, std::min(1.0f, fx - x0));
            float v00 = src.px[static_cast<size_t>(y0) * src.w + x0];
            float v10 = src.px[static_cast<size_t>(y0) * src.w + x1];
            float v01 = src.px[static_cast<size_t>(y1) * src.w + x0];
            float v11 = src.px[static_cast<size_t>(y1) * src.w + x1];
            float v = (v00 * (1 - wx) + v10 * wx) * (1 - wy) + (v01 * (1 - wx) + v11 * wx) * wy;
            dst.px[static_cast<size_t>(y) * dst_w + x] = clamp_u8(static_cast<int>(std::lround(v)));
        }
    }
    return dst;
}

static void alpha_blend_rgba_over(RGBAImage& dst, const RGBAImage& src, int ox, int oy) {
    int x0 = std::max(0, -ox);
    int y0 = std::max(0, -oy);
    int x1 = std::min(src.w, dst.w - ox);
    int y1 = std::min(src.h, dst.h - oy);
    for (int y = y0; y < y1; ++y) {
        uint8_t* dp = &dst.px[(static_cast<size_t>(oy + y) * dst.w + ox + x0) * 4];
        const uint8_t* sp = &src.px[(static_cast<size_t>(y) * src.w + x0) * 4];
        for (int x = x0; x < x1; ++x, dp += 4, sp += 4) {
            uint32_t sa = sp[3];
            if (sa == 0) continue;
            if (sa == 255) {
                dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = 255;
            } else {
                uint32_t inv = 255 - sa;
                dp[0] = static_cast<uint8_t>((sp[0] * sa + dp[0] * inv + 127) / 255);
                dp[1] = static_cast<uint8_t>((sp[1] * sa + dp[1] * inv + 127) / 255);
                dp[2] = static_cast<uint8_t>((sp[2] * sa + dp[2] * inv + 127) / 255);
                dp[3] = static_cast<uint8_t>(sa + (dp[3] * inv + 127) / 255);
            }
        }
    }
}

static void composite_premul_bgra(RGBAImage& dst, const uint32_t* bgra, int sw, int sh, int ox, int oy) {
    int x0 = std::max(0, -ox);
    int y0 = std::max(0, -oy);
    int x1 = std::min(sw, dst.w - ox);
    int y1 = std::min(sh, dst.h - oy);
    for (int y = y0; y < y1; ++y) {
        uint8_t* dp = &dst.px[(static_cast<size_t>(oy + y) * dst.w + ox + x0) * 4];
        const uint32_t* sp = &bgra[static_cast<size_t>(y) * sw + x0];
        for (int x = x0; x < x1; ++x, dp += 4, ++sp) {
            uint32_t px = *sp;
            uint32_t a = (px >> 24) & 0xFF;
            if (a == 0) continue;
            uint32_t sr = (px >> 16) & 0xFF;
            uint32_t sg = (px >> 8) & 0xFF;
            uint32_t sb = px & 0xFF;
            if (a == 255) {
                dp[0] = static_cast<uint8_t>(sr);
                dp[1] = static_cast<uint8_t>(sg);
                dp[2] = static_cast<uint8_t>(sb);
                dp[3] = 255;
            } else {
                uint32_t inv = 255 - a;
                dp[0] = clamp_u8(static_cast<int>(sr + (dp[0] * inv + 127) / 255));
                dp[1] = clamp_u8(static_cast<int>(sg + (dp[1] * inv + 127) / 255));
                dp[2] = clamp_u8(static_cast<int>(sb + (dp[2] * inv + 127) / 255));
                dp[3] = 255;
            }
        }
    }
}

static RGBAImage build_tile(const Card& card, const GrayImage& pat, int size, float pattern_boost) {
    RGBAImage tile(size, size, 0, 0, 0, 255);
    // 1. Радиальный градиент (RADIAL_R = 0.6965)
    for (int y = 0; y < size; ++y) {
        float dy = (y + 0.5f) / size - 0.5f;
        uint8_t* row = &tile.px[static_cast<size_t>(y) * size * 4];
        for (int x = 0; x < size; ++x) {
            float dx = (x + 0.5f) / size - 0.5f;
            float d = std::min(1.0f, std::sqrt(dx * dx + dy * dy) / RADIAL_R);
            row[x * 4 + 0] = static_cast<uint8_t>(std::lround(card.grad_top.r + (card.grad_bot.r - card.grad_top.r) * d));
            row[x * 4 + 1] = static_cast<uint8_t>(std::lround(card.grad_top.g + (card.grad_bot.g - card.grad_top.g) * d));
            row[x * 4 + 2] = static_cast<uint8_t>(std::lround(card.grad_top.b + (card.grad_bot.b - card.grad_top.b) * d));
            row[x * 4 + 3] = 255;
        }
    }

    // 2. Узор app-square (21 калиброванная метка)
    if (!pat.px.empty()) {
        std::map<int, GrayImage> resized_cache;
        for (const auto& sp : SQUARE_PATTERN_SPOTS) {
            int side = std::max(1, static_cast<int>(std::lround(sp.side * size * PATTERN_GAIN)));
            int left = static_cast<int>(std::lround((sp.cx - sp.side * 0.5f) * size));
            int top  = static_cast<int>(std::lround((sp.cy - sp.side * 0.5f) * size));
            if (left >= size || top >= size || left + side <= 0 || top + side <= 0) continue;

            auto it = resized_cache.find(side);
            if (it == resized_cache.end()) {
                it = resized_cache.emplace(side, resize_gray_bilinear(pat, side, side)).first;
            }
            const GrayImage& mask = it->second;
            float alpha_mul = sp.opacity * pattern_boost;

            int x0 = std::max(0, -left), y0 = std::max(0, -top);
            int x1 = std::min(side, size - left), y1 = std::min(side, size - top);
            for (int y = y0; y < y1; ++y) {
                uint8_t* dp = &tile.px[(static_cast<size_t>(top + y) * size + left + x0) * 4];
                const uint8_t* mp = &mask.px[static_cast<size_t>(y) * side + x0];
                for (int x = x0; x < x1; ++x, dp += 4, ++mp) {
                    int sa = std::min(255, static_cast<int>(std::lround((*mp) * alpha_mul)));
                    if (sa <= 0) continue;
                    int inv = 255 - sa;
                    dp[0] = static_cast<uint8_t>((card.pattern_rgb.r * sa + dp[0] * inv + 127) / 255);
                    dp[1] = static_cast<uint8_t>((card.pattern_rgb.g * sa + dp[1] * inv + 127) / 255);
                    dp[2] = static_cast<uint8_t>((card.pattern_rgb.b * sa + dp[2] * inv + 127) / 255);
                }
            }
        }
    }

    // 3. Маска суперэллипса (inset exponent 0.17 => |x|^p + |y|^p <= 1, p = 2/0.17 ≈ 11.7647) с 2x2 AA
    const float p = 2.0f / 0.17f;
    const float half = size * 0.5f;
    for (int y = 0; y < size; ++y) {
        uint8_t* row = &tile.px[static_cast<size_t>(y) * size * 4];
        for (int x = 0; x < size; ++x) {
            // Быстрая отсечка внутренней области без pow()
            float nx = std::fabs((x + 0.5f - half) / half);
            float ny = std::fabs((y + 0.5f - half) / half);
            if (nx < 0.78f || ny < 0.78f) {
                if (nx < 0.99f && ny < 0.99f) { row[x * 4 + 3] = 255; continue; }
            }
            int inside = 0;
            for (int sy = 0; sy < 2; ++sy) {
                float py = std::fabs((y + 0.25f + sy * 0.5f - half) / half);
                float pyp = std::pow(py, p);
                for (int sx = 0; sx < 2; ++sx) {
                    float px = std::fabs((x + 0.25f + sx * 0.5f - half) / half);
                    if (std::pow(px, p) + pyp <= 1.0f) inside++;
                }
            }
            row[x * 4 + 3] = static_cast<uint8_t>(inside * 255 / 4);
        }
    }
    return tile;
}

// ── UTF-8 и рендер текста (Roboto Medium + Twemoji PNG) ──────────────────────

static std::vector<uint32_t> decode_utf8(const std::string& s) {
    std::vector<uint32_t> cps;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        if (c < 0x80) { cp = c; i += 1; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F); i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6)
                 | (static_cast<unsigned char>(s[i + 2]) & 0x3F); i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
            cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12)
                 | ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6)
                 | (static_cast<unsigned char>(s[i + 3]) & 0x3F); i += 4;
        } else { i += 1; continue; }
        if (cp == 0xFE0F || cp == 0xFE0E || cp == 0x200D) continue;
        cps.push_back(cp);
    }
    return cps;
}

static bool load_twemoji_rgba(koto_ctx* ctx, const koto_api* api, uint32_t cp, RGBAImage& out) {
    char hex[16];
    std::snprintf(hex, sizeof(hex), "%x", cp);
    std::string key(hex);
    std::vector<uint8_t> png;
    {
        std::lock_guard<std::mutex> lk(g_asset_mtx);
        auto it = g_emoji_png_cache.find(key);
        if (it != g_emoji_png_cache.end()) png = it->second;
    }
    if (png.empty()) {
        std::string url = "https://cdn.jsdelivr.net/gh/jdecked/twemoji@16.0.1/assets/72x72/" + key + ".png";
        if (fetch_bytes(ctx, api, url, UA_POOL[0], png, 128 * 1024)) {
            std::lock_guard<std::mutex> lk(g_asset_mtx);
            if (g_emoji_png_cache.size() >= 64) g_emoji_png_cache.erase(g_emoji_png_cache.begin());
            g_emoji_png_cache[key] = png;
        } else {
            return false;
        }
    }
    int w = 0, h = 0, ch = 0;
    unsigned char* dec = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &ch, 4);
    if (!dec || w <= 0 || h <= 0) return false;
    out.w = w;
    out.h = h;
    out.px.assign(dec, dec + static_cast<size_t>(w) * h * 4);
    stbi_image_free(dec);
    return true;
}

static RGBAImage resize_rgba_bilinear(const RGBAImage& src, int dst_w, int dst_h) {
    RGBAImage dst(dst_w, dst_h);
    if (src.w <= 0 || src.h <= 0) return dst;
    float sx = static_cast<float>(src.w) / dst_w;
    float sy = static_cast<float>(src.h) / dst_h;
    for (int y = 0; y < dst_h; ++y) {
        float fy = (y + 0.5f) * sy - 0.5f;
        int y0 = std::max(0, std::min(src.h - 1, static_cast<int>(std::floor(fy))));
        int y1 = std::min(src.h - 1, y0 + 1);
        float wy = std::max(0.0f, std::min(1.0f, fy - y0));
        for (int x = 0; x < dst_w; ++x) {
            float fx = (x + 0.5f) * sx - 0.5f;
            int x0 = std::max(0, std::min(src.w - 1, static_cast<int>(std::floor(fx))));
            int x1 = std::min(src.w - 1, x0 + 1);
            float wx = std::max(0.0f, std::min(1.0f, fx - x0));
            const uint8_t* p00 = &src.px[(static_cast<size_t>(y0) * src.w + x0) * 4];
            const uint8_t* p10 = &src.px[(static_cast<size_t>(y0) * src.w + x1) * 4];
            const uint8_t* p01 = &src.px[(static_cast<size_t>(y1) * src.w + x0) * 4];
            const uint8_t* p11 = &src.px[(static_cast<size_t>(y1) * src.w + x1) * 4];
            uint8_t* dp = &dst.px[(static_cast<size_t>(y) * dst_w + x) * 4];
            for (int c = 0; c < 4; ++c) {
                float v = (p00[c] * (1 - wx) + p10[c] * wx) * (1 - wy) + (p01[c] * (1 - wx) + p11[c] * wx) * wy;
                dp[c] = clamp_u8(static_cast<int>(std::lround(v)));
            }
        }
    }
    return dst;
}

static float measure_text_width(const std::string& text, float cap_px) {
    ensure_embedded_assets();
    if (!g_roboto_ready || text.empty()) return 0.0f;
    float px_size = std::max(6.0f, cap_px / CAP_TO_SIZE);
    float scale = stbtt_ScaleForPixelHeight(&g_roboto_font, px_size);
    auto cps = decode_utf8(text);
    float x = 0.0f;
    int prev_g = 0;
    for (uint32_t cp : cps) {
        int g = stbtt_FindGlyphIndex(&g_roboto_font, static_cast<int>(cp));
        if (g == 0) {
            if (cp >= 0x2000) x += cap_px * 1.15f;
            prev_g = 0;
            continue;
        }
        if (prev_g != 0) x += stbtt_GetGlyphKernAdvance(&g_roboto_font, prev_g, g) * scale;
        int adv = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&g_roboto_font, g, &adv, &lsb);
        x += adv * scale;
        prev_g = g;
    }
    return x;
}

static void draw_text_line(koto_ctx* ctx, const koto_api* api, RGBAImage& canvas,
                           const std::string& text, float x_start, float baseline_y,
                           float cap_px, RGB color) {
    ensure_embedded_assets();
    if (!g_roboto_ready || text.empty()) return;
    float px_size = std::max(6.0f, cap_px / CAP_TO_SIZE);
    float scale = stbtt_ScaleForPixelHeight(&g_roboto_font, px_size);
    auto cps = decode_utf8(text);
    float x = x_start;
    int prev_g = 0;

    for (uint32_t cp : cps) {
        int g = stbtt_FindGlyphIndex(&g_roboto_font, static_cast<int>(cp));
        if (g == 0) {
            if (cp >= 0x2000) {
                RGBAImage emo;
                if (load_twemoji_rgba(ctx, api, cp, emo)) {
                    int es = std::max(8, static_cast<int>(std::lround(cap_px * 1.1f)));
                    RGBAImage scaled = resize_rgba_bilinear(emo, es, es);
                    int ey = static_cast<int>(std::lround(baseline_y - es * 0.88f));
                    alpha_blend_rgba_over(canvas, scaled, static_cast<int>(std::lround(x)), ey);
                }
                x += cap_px * 1.15f;
            }
            prev_g = 0;
            continue;
        }
        if (prev_g != 0) x += stbtt_GetGlyphKernAdvance(&g_roboto_font, prev_g, g) * scale;
        int adv = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&g_roboto_font, g, &adv, &lsb);

        int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
        stbtt_GetGlyphBitmapBox(&g_roboto_font, g, scale, scale, &ix0, &iy0, &ix1, &iy1);
        int bw = ix1 - ix0, bh = iy1 - iy0;
        if (bw > 0 && bh > 0) {
            std::vector<uint8_t> bmp(static_cast<size_t>(bw) * bh, 0);
            stbtt_MakeGlyphBitmap(&g_roboto_font, bmp.data(), bw, bh, bw, scale, scale, g);
            int gx = static_cast<int>(std::floor(x)) + ix0;
            int gy = static_cast<int>(std::round(baseline_y)) + iy0;
            for (int r = 0; r < bh; ++r) {
                int dy = gy + r;
                if (dy < 0 || dy >= canvas.h) continue;
                for (int c = 0; c < bw; ++c) {
                    int dx = gx + c;
                    if (dx < 0 || dx >= canvas.w) continue;
                    uint32_t a = bmp[static_cast<size_t>(r) * bw + c];
                    if (a == 0) continue;
                    uint8_t* dp = &canvas.px[(static_cast<size_t>(dy) * canvas.w + dx) * 4];
                    uint32_t inv = 255 - a;
                    dp[0] = static_cast<uint8_t>((color.r * a + dp[0] * inv + 127) / 255);
                    dp[1] = static_cast<uint8_t>((color.g * a + dp[1] * inv + 127) / 255);
                    dp[2] = static_cast<uint8_t>((color.b * a + dp[2] * inv + 127) / 255);
                    dp[3] = 255;
                }
            }
        }
        x += adv * scale;
        prev_g = g;
    }
}

static int compute_footer_height(int tile_side, const Card& card) {
    if (!card.show_footer) return 0;
    int lines = (!card.name.empty() || card.number >= 0) ? 1 : 0;
    if (!card.caption.empty()) lines += 1;
    if (lines == 0) return 0;
    int h = static_cast<int>(tile_side * (NAME_GAP + NAME_CAP / CAP_TO_SIZE));
    if (lines > 1) {
        h += static_cast<int>(tile_side * (0.034f + CAPTION_CAP / CAP_TO_SIZE));
    }
    h += static_cast<int>(tile_side * NAME_GAP * 0.55f);
    return h;
}

static void draw_footer(koto_ctx* ctx, const koto_api* api, const Card& card,
                        RGBAImage& canvas, int tile_side, int tile_y, int center_x) {
    if (!card.show_footer) return;
    float name_cap = tile_side * NAME_CAP;
    float num_cap = tile_side * NUMBER_CAP;
    std::string badge = (card.number >= 0) ? ("#" + std::to_string(card.number)) : "";
    float pad = std::max(4.0f, tile_side * TEXT_GAP);

    float w_name = measure_text_width(card.name, name_cap);
    float w_num = badge.empty() ? 0.0f : measure_text_width(badge, num_cap);
    float total_w = w_name + (badge.empty() ? 0.0f : (pad + w_num));
    float left = center_x - total_w * 0.5f;

    float cursor_top = tile_y + tile_side + tile_side * NAME_GAP;
    float baseline = cursor_top + name_cap;

    if (!card.name.empty()) {
        draw_text_line(ctx, api, canvas, card.name, left, baseline, name_cap, {255, 255, 255});
    }
    if (!badge.empty()) {
        draw_text_line(ctx, api, canvas, badge, left + w_name + pad, baseline, num_cap, {0x9A, 0x9A, 0xA2});
    }

    if (!card.caption.empty()) {
        float cap_px = tile_side * CAPTION_CAP;
        float w_cap = measure_text_width(card.caption, cap_px);
        float cap_left = center_x - w_cap * 0.5f;
        float cap_base = baseline + tile_side * 0.034f + cap_px;
        draw_text_line(ctx, api, canvas, card.caption, cap_left, cap_base, cap_px, {0xE6, 0xE6, 0xEC});
    }
}

// ── Lottie padding (сохраняет модель без обрезки по краям) ──────────────────

struct PreparedLottie {
    Lottie_Animation* anim = nullptr;
    int ip = 0;
    int op = 180;
    int source_fps = 60;
    int step = 1;
    float ratio = 1.0f;

    ~PreparedLottie() {
        if (anim) lottie_animation_destroy(anim);
    }
    PreparedLottie() = default;
    PreparedLottie(const PreparedLottie&) = delete;
    PreparedLottie& operator=(const PreparedLottie&) = delete;
    PreparedLottie(PreparedLottie&& o) noexcept
        : anim(o.anim), ip(o.ip), op(o.op), source_fps(o.source_fps), step(o.step), ratio(o.ratio) {
        o.anim = nullptr;
    }
};

static int choose_frame_step(int source_fps, int req_fps) {
    if (source_fps <= 0) source_fps = 60;
    req_fps = std::max(1, req_fps);
    int best = 1;
    for (int n = 1; n <= source_fps; ++n) {
        if (source_fps % n == 0 && n <= req_fps) best = n;
    }
    return source_fps / best;
}

static bool prepare_lottie(const std::string& lottie_raw, int req_fps, PreparedLottie& out, std::string& err) {
    try {
        json data = json::parse(lottie_raw);
        int orig_w = data.value("w", 512);
        int orig_h = data.value("h", 512);
        int margin_x = static_cast<int>(std::ceil(orig_w * MODEL_PAD));
        int margin_y = static_cast<int>(std::ceil(orig_h * MODEL_PAD));
        int max_ind = 0;
        if (data.contains("layers") && data["layers"].is_array()) {
            for (const auto& lay : data["layers"]) {
                if (lay.is_object()) max_ind = std::max(max_ind, lay.value("ind", 0));
            }
            int pad_ind = max_ind + 1;
            for (auto& lay : data["layers"]) {
                if (lay.is_object() && !lay.contains("parent")) {
                    lay["parent"] = pad_ind;
                }
            }
            json null_parent = {
                {"ty", 3}, {"ind", pad_ind}, {"nm", "Render canvas padding"}, {"st", 0},
                {"ip", data.value("ip", 0)}, {"op", data.value("op", 180)},
                {"ks", {
                    {"o", {{"a", 0}, {"k", 100}}},
                    {"r", {{"a", 0}, {"k", 0}}},
                    {"p", {{"a", 0}, {"k", {margin_x, margin_y, 0}}}},
                    {"a", {{"a", 0}, {"k", {0, 0, 0}}}},
                    {"s", {{"a", 0}, {"k", {100, 100, 100}}}}
                }}
            };
            data["layers"].insert(data["layers"].begin(), std::move(null_parent));
        }
        int new_w = orig_w + 2 * margin_x;
        int new_h = orig_h + 2 * margin_y;
        data["w"] = new_w;
        data["h"] = new_h;

        std::string dumped = data.dump();
        static std::atomic<uint64_t> s_anim_seq{0};
        std::string key = "gg_" + std::to_string(++s_anim_seq);
        out.anim = lottie_animation_from_data(dumped.c_str(), key.c_str(), "");
        if (!out.anim) {
            err = "rlottie не смог загрузить анимацию";
            return false;
        }
        out.ip = static_cast<int>(data.value("ip", 0.0));
        out.op = static_cast<int>(data.value("op", 180.0));
        if (out.op <= out.ip) out.op = out.ip + 60;
        out.source_fps = std::max(1, static_cast<int>(std::lround(data.value("fr", 60.0))));
        out.step = choose_frame_step(out.source_fps, req_fps);
        out.ratio = static_cast<float>(new_w) / static_cast<float>(std::max(1, orig_w));
        return true;
    } catch (const std::exception& e) {
        err = std::string("ошибка JSON анимации: ") + e.what();
        return false;
    }
}

// ── Потоковый GIF89a кодировщик (0 зависимостей, фоллбэк + WSL тест) ─────────

class GifEncoder {
public:
    bool open(const std::string& path, int w, int h, int fps) {
        w_ = w; h_ = h; fps_ = std::max(1, fps); frame_idx_ = 0;
        fp_ = std::fopen(path.c_str(), "wb");
        if (!fp_) return false;
        // Header GIF89a
        std::fwrite("GIF89a", 1, 6, fp_);
        write_u16(static_cast<uint16_t>(w_));
        write_u16(static_cast<uint16_t>(h_));
        // Logical screen descriptor: global color table = 1 (256 colors), color res = 7
        uint8_t lsd[3] = {0xF7, 0x00, 0x00};
        std::fwrite(lsd, 1, 3, fp_);
        // Placeholder palette (will be patched on first frame or written once on first frame)
        pal_pos_ = std::ftell(fp_);
        std::vector<uint8_t> zeros(768, 0);
        std::fwrite(zeros.data(), 1, 768, fp_);
        // NETSCAPE2.0 infinite loop application extension
        const uint8_t netscape[19] = {
            0x21, 0xFF, 0x0B,
            'N','E','T','S','C','A','P','E','2','.','0',
            0x03, 0x01, 0x00, 0x00, 0x00
        };
        std::fwrite(netscape, 1, sizeof(netscape), fp_);
        return true;
    }

    void write_frame(const RGBAImage& img) {
        if (!fp_) return;
        if (frame_idx_ == 0) build_palette(img);

        int delay_cs = static_cast<int>(std::lround((frame_idx_ + 1) * 100.0 / fps_))
                     - static_cast<int>(std::lround(frame_idx_ * 100.0 / fps_));
        if (delay_cs < 2) delay_cs = 2;

        // Graphic Control Extension
        uint8_t gce[8] = {
            0x21, 0xF9, 0x04, 0x04,
            static_cast<uint8_t>(delay_cs & 0xFF),
            static_cast<uint8_t>((delay_cs >> 8) & 0xFF),
            0x00, 0x00
        };
        std::fwrite(gce, 1, 8, fp_);

        // Image Descriptor
        uint8_t desc[10] = {
            0x2C, 0, 0, 0, 0,
            static_cast<uint8_t>(w_ & 0xFF), static_cast<uint8_t>((w_ >> 8) & 0xFF),
            static_cast<uint8_t>(h_ & 0xFF), static_cast<uint8_t>((h_ >> 8) & 0xFF),
            0x00
        };
        std::fwrite(desc, 1, 10, fp_);

        // Quantize to palette indices
        std::vector<uint8_t> indices(static_cast<size_t>(w_) * h_);
        for (int i = 0; i < w_ * h_; ++i) {
            indices[i] = quantize_rgb(img.px[i * 4], img.px[i * 4 + 1], img.px[i * 4 + 2]);
        }
        write_lzw(indices);
        frame_idx_++;
    }

    bool close() {
        if (!fp_) return false;
        uint8_t trailer = 0x3B;
        std::fwrite(&trailer, 1, 1, fp_);
        std::fclose(fp_);
        fp_ = nullptr;
        return frame_idx_ > 0;
    }

    ~GifEncoder() {
        if (fp_) std::fclose(fp_);
    }

private:
    FILE* fp_ = nullptr;
    int w_ = 0, h_ = 0, fps_ = 30, frame_idx_ = 0;
    long pal_pos_ = 0;
    uint8_t pal_[256][3]{};
    // 5x5x5 -> best palette index lookup cache (32768 bytes)
    std::array<uint8_t, 32768> lut_{};

    void write_u16(uint16_t v) {
        uint8_t b[2] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF)};
        std::fwrite(b, 1, 2, fp_);
    }

    void build_palette(const RGBAImage& first) {
        // 0..215: 6x6x6 RGB cube
        int idx = 0;
        for (int r = 0; r < 6; ++r)
            for (int g = 0; g < 6; ++g)
                for (int b = 0; b < 6; ++b) {
                    pal_[idx][0] = static_cast<uint8_t>(r * 51);
                    pal_[idx][1] = static_cast<uint8_t>(g * 51);
                    pal_[idx][2] = static_cast<uint8_t>(b * 51);
                    idx++;
                }
        // 216..231: 16 grayscale shades (including PAGE_BG 16,16,16 and text shades)
        for (int i = 0; i < 16; ++i) {
            uint8_t v = static_cast<uint8_t>(i == 0 ? 16 : (i * 255 / 15));
            pal_[idx][0] = pal_[idx][1] = pal_[idx][2] = v;
            idx++;
        }
        // 232..255: 24 sampled colors from first frame (makes card gradient silky smooth)
        int total_px = first.w * first.h;
        int stride = std::max(1, total_px / 24);
        for (int i = 0; i < 24; ++i) {
            int p = (i * stride + stride / 2) % total_px;
            pal_[idx][0] = first.px[p * 4 + 0];
            pal_[idx][1] = first.px[p * 4 + 1];
            pal_[idx][2] = first.px[p * 4 + 2];
            idx++;
        }

        // Write global palette back into header
        long cur = std::ftell(fp_);
        std::fseek(fp_, pal_pos_, SEEK_SET);
        std::fwrite(pal_, 1, 768, fp_);
        std::fseek(fp_, cur, SEEK_SET);

        // Precompute 5-bit RGB LUT
        for (int r5 = 0; r5 < 32; ++r5) {
            int rv = (r5 << 3) | (r5 >> 2);
            for (int g5 = 0; g5 < 32; ++g5) {
                int gv = (g5 << 3) | (g5 >> 2);
                for (int b5 = 0; b5 < 32; ++b5) {
                    int bv = (b5 << 3) | (b5 >> 2);
                    int best_i = 0, best_d = 1 << 30;
                    for (int k = 0; k < 256; ++k) {
                        int dr = rv - pal_[k][0], dg = gv - pal_[k][1], db = bv - pal_[k][2];
                        int d = dr * dr + dg * dg + db * db;
                        if (d < best_d) { best_d = d; best_i = k; if (d == 0) break; }
                    }
                    lut_[(r5 << 10) | (g5 << 5) | b5] = static_cast<uint8_t>(best_i);
                }
            }
        }
    }

    uint8_t quantize_rgb(uint8_t r, uint8_t g, uint8_t b) const {
        return lut_[((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)];
    }

    void write_lzw(const std::vector<uint8_t>& data) {
        const int min_code_size = 8;
        const int clear_code = 256;
        const int eoi_code = 257;
        std::fputc(min_code_size, fp_);

        uint8_t block[256];
        int block_len = 0;
        uint32_t bit_buf = 0;
        int bit_cnt = 0;

        auto flush_block = [&]() {
            if (block_len > 0) {
                std::fputc(block_len, fp_);
                std::fwrite(block, 1, static_cast<size_t>(block_len), fp_);
                block_len = 0;
            }
        };
        auto emit_code = [&](int code, int width) {
            bit_buf |= (static_cast<uint32_t>(code) << bit_cnt);
            bit_cnt += width;
            while (bit_cnt >= 8) {
                block[block_len++] = static_cast<uint8_t>(bit_buf & 0xFF);
                bit_buf >>= 8;
                bit_cnt -= 8;
                if (block_len == 255) flush_block();
            }
        };

        // Open-addressing hash table for (prefix << 8 | k) -> code
        constexpr int HTAB_SIZE = 8192;
        std::vector<int32_t> hkey(HTAB_SIZE, -1);
        std::vector<int16_t> hval(HTAB_SIZE, 0);

        int next_code = eoi_code + 1;
        int code_size = min_code_size + 1;
        emit_code(clear_code, code_size);

        int prefix = data.empty() ? -1 : data[0];
        for (size_t i = 1; i < data.size(); ++i) {
            int k = data[i];
            int32_t key = (prefix << 8) | k;
            int slot = ((prefix << 5) ^ (k * 2654435761u)) & (HTAB_SIZE - 1);
            int found = -1;
            while (hkey[slot] != -1) {
                if (hkey[slot] == key) { found = hval[slot]; break; }
                slot = (slot + 1) & (HTAB_SIZE - 1);
            }
            if (found != -1) {
                prefix = found;
            } else {
                emit_code(prefix, code_size);
                if (next_code < 4096) {
                    hkey[slot] = key;
                    hval[slot] = static_cast<int16_t>(next_code++);
                    if (next_code > (1 << code_size) && code_size < 12) code_size++;
                } else {
                    emit_code(clear_code, code_size);
                    std::fill(hkey.begin(), hkey.end(), -1);
                    next_code = eoi_code + 1;
                    code_size = min_code_size + 1;
                }
                prefix = k;
            }
        }
        if (prefix >= 0) emit_code(prefix, code_size);
        emit_code(eoi_code, code_size);
        if (bit_cnt > 0) {
            block[block_len++] = static_cast<uint8_t>(bit_buf & 0xFF);
        }
        flush_block();
        std::fputc(0, fp_); // block terminator
    }
};

// ── Android NDK AMediaCodec + AMediaMuxer H.264 MP4 кодировщик ───────────────

#ifdef __ANDROID__
class AndroidMp4Encoder {
public:
    bool open(const std::string& path, int w, int h, int fps) {
        w_ = w & ~1;
        h_ = h & ~1;
        fps_ = std::max(5, std::min(60, fps));
        if (w_ < 16 || h_ < 16) return false;

        fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd_ < 0) return false;

        struct Candidate { const char* name; int color_fmt; };
        const Candidate cands[] = {
            {"c2.android.avc.encoder", 19},      // Android 10+ Google SW AVC, I420 (точный stride на любых чётных W×H)
            {"OMX.google.h264.encoder", 19},     // Android 8-9 Google SW AVC, I420
            {"c2.android.avc.encoder", 21},      // Android 10+ Google SW AVC, NV12
            {nullptr, 21},                       // Default HW/SW encoder, NV12
            {nullptr, 19},                       // Default HW/SW encoder, I420
        };

        int bitrate = std::max(2500000, std::min(8000000, w_ * h_ * fps_ / 4));

        for (const auto& c : cands) {
            AMediaCodec* codec = c.name ? AMediaCodec_createCodecByName(c.name)
                                        : AMediaCodec_createEncoderByType("video/avc");
            if (!codec) continue;
            AMediaFormat* fmt = AMediaFormat_new();
            AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, "video/avc");
            AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, w_);
            AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, h_);
            AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_COLOR_FORMAT, c.color_fmt);
            AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_BIT_RATE, bitrate);
            AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_FRAME_RATE, fps_);
            AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);

            if (AMediaCodec_configure(codec, fmt, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE) == AMEDIA_OK &&
                AMediaCodec_start(codec) == AMEDIA_OK) {
                AMediaFormat_delete(fmt);
                codec_ = codec;
                color_fmt_ = c.color_fmt;
                stride_ = w_;
                slice_h_ = h_;
                // API 28+: узнаём реальный stride / slice-height / color-format у вендорного кодека
                using GetInFmtFn = AMediaFormat* (*)(AMediaCodec*);
                auto get_in_fmt = reinterpret_cast<GetInFmtFn>(dlsym(RTLD_DEFAULT, "AMediaCodec_getInputFormat"));
                if (get_in_fmt) {
                    AMediaFormat* infmt = get_in_fmt(codec_);
                    if (infmt) {
                        int32_t s = 0, sh = 0, cf = 0;
                        if (AMediaFormat_getInt32(infmt, "stride", &s) && s >= w_) stride_ = s;
                        if (AMediaFormat_getInt32(infmt, "slice-height", &sh) && sh >= h_) slice_h_ = sh;
                        if (AMediaFormat_getInt32(infmt, "color-format", &cf) && (cf == 19 || cf == 21)) color_fmt_ = cf;
                        AMediaFormat_delete(infmt);
                    }
                }
                break;
            }
            AMediaFormat_delete(fmt);
            AMediaCodec_delete(codec);
        }

        if (!codec_) { cleanup(); return false; }

        muxer_ = AMediaMuxer_new(fd_, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
        if (!muxer_) { cleanup(); return false; }
        frame_idx_ = 0;
        samples_written_ = 0;
        track_idx_ = -1;
        muxer_started_ = false;
        return true;
    }

    bool write_frame(const RGBAImage& img) {
        if (!codec_) return false;
        ssize_t in_idx = -1;
        for (int tries = 0; tries < 50; ++tries) {
            in_idx = AMediaCodec_dequeueInputBuffer(codec_, 20000);
            if (in_idx >= 0) break;
            drain_output(false);
        }
        if (in_idx < 0) return false;

        size_t buf_sz = 0;
        uint8_t* dst = AMediaCodec_getInputBuffer(codec_, static_cast<size_t>(in_idx), &buf_sz);
        if (!dst) return false;

        size_t y_plane_sz = static_cast<size_t>(stride_) * slice_h_;
        size_t need_sz = y_plane_sz + static_cast<size_t>(stride_) * (slice_h_ / 2);
        if (buf_sz < need_sz) {
            // Если реальный буфер меньше stride*slice_h, используем плотную раскладку
            stride_ = w_;
            slice_h_ = h_;
            y_plane_sz = static_cast<size_t>(w_) * h_;
            need_sz = y_plane_sz + y_plane_sz / 2;
            if (buf_sz < need_sz) return false;
        }
        std::memset(dst, 16, y_plane_sz);
        std::memset(dst + y_plane_sz, 128, need_sz - y_plane_sz);

        // RGB -> YUV420 (NV12 = 21 или I420 = 19)
        const int uv_h = h_ / 2;
        const int uv_w = w_ / 2;
        uint8_t* y_dst = dst;
        uint8_t* uv_dst = dst + y_plane_sz;
        uint8_t* u_dst = dst + y_plane_sz;
        uint8_t* v_dst = dst + y_plane_sz + static_cast<size_t>(stride_ / 2) * (slice_h_ / 2);

        for (int y = 0; y < h_; ++y) {
            const uint8_t* sp = &img.px[static_cast<size_t>(y) * img.w * 4];
            uint8_t* yp = y_dst + static_cast<size_t>(y) * stride_;
            for (int x = 0; x < w_; ++x) {
                int r = sp[x * 4 + 0], g = sp[x * 4 + 1], b = sp[x * 4 + 2];
                yp[x] = clamp_u8(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
            }
        }
        for (int y = 0; y < uv_h; ++y) {
            const uint8_t* s0 = &img.px[static_cast<size_t>(y * 2) * img.w * 4];
            const uint8_t* s1 = &img.px[static_cast<size_t>(y * 2 + 1) * img.w * 4];
            uint8_t* uvp = uv_dst + static_cast<size_t>(y) * stride_;
            uint8_t* up = u_dst + static_cast<size_t>(y) * (stride_ / 2);
            uint8_t* vp = v_dst + static_cast<size_t>(y) * (stride_ / 2);
            for (int x = 0; x < uv_w; ++x) {
                int x2 = x * 2;
                int r = (s0[x2 * 4] + s0[(x2 + 1) * 4] + s1[x2 * 4] + s1[(x2 + 1) * 4] + 2) >> 2;
                int g = (s0[x2 * 4 + 1] + s0[(x2 + 1) * 4 + 1] + s1[x2 * 4 + 1] + s1[(x2 + 1) * 4 + 1] + 2) >> 2;
                int b = (s0[x2 * 4 + 2] + s0[(x2 + 1) * 4 + 2] + s1[x2 * 4 + 2] + s1[(x2 + 1) * 4 + 2] + 2) >> 2;
                uint8_t u = clamp_u8(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
                uint8_t v = clamp_u8(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
                if (color_fmt_ == 21) {
                    uvp[x * 2] = u;
                    uvp[x * 2 + 1] = v;
                } else {
                    up[x] = u;
                    vp[x] = v;
                }
            }
        }

        int64_t pts_us = static_cast<int64_t>(frame_idx_) * 1000000LL / fps_;
        if (AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(in_idx), 0, need_sz, pts_us, 0) != AMEDIA_OK) {
            return false;
        }
        frame_idx_++;
        drain_output(false);
        return true;
    }

    bool close() {
        if (!codec_) return false;
        ssize_t in_idx = -1;
        for (int tries = 0; tries < 50; ++tries) {
            in_idx = AMediaCodec_dequeueInputBuffer(codec_, 20000);
            if (in_idx >= 0) break;
            drain_output(false);
        }
        if (in_idx >= 0) {
            int64_t pts_us = static_cast<int64_t>(frame_idx_) * 1000000LL / fps_;
            AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(in_idx), 0, 0, pts_us,
                                         AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        }
        drain_output(true);
        bool ok = muxer_started_ && samples_written_ > 0;
        cleanup();
        return ok;
    }

    ~AndroidMp4Encoder() { cleanup(); }

private:
    AMediaCodec* codec_ = nullptr;
    AMediaMuxer* muxer_ = nullptr;
    int fd_ = -1;
    int w_ = 0, h_ = 0, fps_ = 30, color_fmt_ = 21, stride_ = 0, slice_h_ = 0;
    int frame_idx_ = 0, samples_written_ = 0;
    ssize_t track_idx_ = -1;
    bool muxer_started_ = false;

    void drain_output(bool eos) {
        int empty_spins = 0;
        while (true) {
            AMediaCodecBufferInfo info{};
            ssize_t out_idx = AMediaCodec_dequeueOutputBuffer(codec_, &info, eos ? 25000 : 0);
            if (out_idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
                if (!eos || ++empty_spins > 60) break;
            } else if (out_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                AMediaFormat* out_fmt = AMediaCodec_getOutputFormat(codec_);
                if (out_fmt) {
                    if (!muxer_started_) {
                        track_idx_ = AMediaMuxer_addTrack(muxer_, out_fmt);
                        if (track_idx_ >= 0 && AMediaMuxer_start(muxer_) == AMEDIA_OK) {
                            muxer_started_ = true;
                        }
                    }
                    AMediaFormat_delete(out_fmt);
                }
            } else if (out_idx >= 0) {
                empty_spins = 0;
                size_t out_sz = 0;
                uint8_t* out_buf = AMediaCodec_getOutputBuffer(codec_, static_cast<size_t>(out_idx), &out_sz);
                if (out_buf && info.size > 0 && !(info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) && muxer_started_) {
                    AMediaMuxer_writeSampleData(muxer_, static_cast<size_t>(track_idx_), out_buf, &info);
                    samples_written_++;
                }
                AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(out_idx), false);
                if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) break;
            } else {
                break;
            }
        }
    }

    void cleanup() {
        if (muxer_) {
            if (muxer_started_) AMediaMuxer_stop(muxer_);
            AMediaMuxer_delete(muxer_);
            muxer_ = nullptr;
        }
        if (codec_) {
            AMediaCodec_stop(codec_);
            AMediaCodec_delete(codec_);
            codec_ = nullptr;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        muxer_started_ = false;
    }
};
#endif

// Универсальный кодировщик (на Android сначала H.264 MP4 через NDK, при отказе — GIF89a)
class FrameSink {
public:
    bool open(std::string& path, bool want_mp4_container, int w, int h, int fps) {
        w_ = w & ~1;
        h_ = h & ~1;
        fps_ = fps;
#ifdef __ANDROID__
        if (want_mp4_container) {
            if (mp4_.open(path, w_, h_, fps_)) {
                path_ = path;
                using_mp4_ = true;
                return true;
            }
        }
#else
        (void)want_mp4_container;
#endif
        using_mp4_ = false;
        if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".mp4") == 0) {
            path.replace(path.size() - 4, 4, ".gif");
        }
        path_ = path;
        return gif_.open(path_, w_, h_, fps_);
    }

    bool write(const RGBAImage& frame) {
#ifdef __ANDROID__
        if (using_mp4_) return mp4_.write_frame(frame);
#endif
        gif_.write_frame(frame);
        return true;
    }

    bool finish() {
#ifdef __ANDROID__
        if (using_mp4_) return mp4_.close();
#endif
        return gif_.close();
    }

    bool is_mp4() const { return using_mp4_; }

private:
    std::string path_;
    int w_ = 0, h_ = 0, fps_ = 30;
    bool using_mp4_ = false;
#ifdef __ANDROID__
    AndroidMp4Encoder mp4_;
#endif
    GifEncoder gif_;
};

// ── Сборка одной карточки и сетки ────────────────────────────────────────────

static bool render_single_card(koto_ctx* ctx, const koto_api* api,
                               const std::string& raw_ref, int size, int req_fps,
                               const std::string& fmt, float pattern_boost,
                               bool show_footer, const std::string& card_caption,
                               const std::string& out_path, std::string& actual_path,
                               std::string& err) {
    Card card;
    std::string lottie_json;
    GrayImage pat;
    if (!load_card_assets(ctx, api, raw_ref, card, lottie_json, pat, err)) return false;
    card.show_footer = show_footer;
    card.caption = card_caption;

    PreparedLottie pl;
    if (!prepare_lottie(lottie_json, req_fps, pl, err)) return false;
    int eff_fps = std::max(1, pl.source_fps / pl.step);

    int tile_side = show_footer ? static_cast<int>(size * TILE_WIDTH) : size;
    int tile_x = (size - tile_side) / 2;
    int full_h = show_footer ? static_cast<int>(std::lround(size * ASPECT)) : size;
    int tile_y = show_footer ? static_cast<int>(std::lround(full_h * TILE_TOP)) : 0;
    int crop_h = show_footer ? std::min(full_h, tile_y + tile_side + compute_footer_height(tile_side, card)) : size;
    int out_w = size & ~1;
    int out_h = crop_h & ~1;

    RGBAImage base(out_w, out_h, PAGE_BG[0], PAGE_BG[1], PAGE_BG[2], 255);
    RGBAImage tile = build_tile(card, pat, tile_side, pattern_boost);
    alpha_blend_rgba_over(base, tile, tile_x, tile_y);
    draw_footer(ctx, api, card, base, tile_side, tile_y, out_w / 2);

    int model_side = static_cast<int>(tile_side * SQUARE_MODEL_INSET);
    int frame_side = static_cast<int>(std::lround(model_side * pl.ratio));
    int offset_y = static_cast<int>(std::lround(tile_side * SQUARE_MODEL_DY));
    int mx = tile_x + (tile_side - frame_side) / 2;
    int my = tile_y + (tile_side - frame_side) / 2 + offset_y;

    // Для fmt=="gif" на Android сохраняем тихий H.264 MP4 с расширением .gif ->
    // KoteLoader (Client::send_gif) отправит его с mime="video/mp4" + DocumentAttributeAnimated (автоплей GIF!).
    // Для fmt=="mp4" сохраняем .mp4 -> Client::send_video отправит видео с точными w/h/duration.
    (void)fmt;
    actual_path = out_path;
    bool want_mp4 = true;
    FrameSink sink;
    if (!sink.open(actual_path, want_mp4, out_w, out_h, eff_fps)) {
        err = "не удалось инициализировать кодировщик видео/GIF";
        return false;
    }

    std::vector<uint32_t> lottie_buf(static_cast<size_t>(frame_side) * frame_side);
    RGBAImage frame_img = base;
    for (int f = pl.ip; f < pl.op; f += pl.step) {
        std::fill(lottie_buf.begin(), lottie_buf.end(), 0u);
        lottie_animation_render(pl.anim, static_cast<size_t>(f), lottie_buf.data(),
                                static_cast<size_t>(frame_side), static_cast<size_t>(frame_side),
                                static_cast<size_t>(frame_side) * 4);
        frame_img.px = base.px;
        composite_premul_bgra(frame_img, lottie_buf.data(), frame_side, frame_side, mx, my);
        if (!sink.write(frame_img)) {
            err = "сбой кодирования кадра";
            return false;
        }
    }
    if (!sink.finish()) {
        err = "сбой финализации файла";
        return false;
    }
    return true;
}

static bool render_grid_card(koto_ctx* ctx, const koto_api* api,
                             const std::vector<std::string>& refs, int size, int req_fps,
                             const std::string& fmt, float pattern_boost,
                             bool show_footer, const std::string& card_caption,
                             std::string& out_path, std::vector<std::string>& skipped,
                             std::string& err) {
    struct GridItem {
        Card card;
        GrayImage pat;
        PreparedLottie pl;
        RGBAImage tile;
        int x = 0, y = 0;
    };
    std::vector<GridItem> items;
    for (size_t i = 0; i < refs.size() && items.size() < MAX_TILES; ++i) {
        GridItem gi;
        std::string ljson, lerr;
        if (!load_card_assets(ctx, api, refs[i], gi.card, ljson, gi.pat, lerr) ||
            !prepare_lottie(ljson, req_fps, gi.pl, lerr)) {
            skipped.push_back(refs[i] + " (" + lerr + ")");
            continue;
        }
        gi.card.show_footer = show_footer;
        gi.card.caption = card_caption;
        items.push_back(std::move(gi));
    }
    if (items.empty()) { err = "ни один подарок не удалось загрузить"; return false; }

    int count = static_cast<int>(items.size());
    int cols = (count == 1) ? 1 : static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
    int rows = (count + cols - 1) / cols;
    int gap = static_cast<int>(std::lround(size * GRID_GAP));
    int margin = static_cast<int>(std::lround(size * GRID_MARGIN));
    int tile_side = (size - 2 * margin - (cols - 1) * gap) / cols;
    int max_fh = 0;
    for (const auto& it : items) max_fh = std::max(max_fh, compute_footer_height(tile_side, it.card));
    int row_side = tile_side + max_fh + static_cast<int>(tile_side * 0.02f);
    int out_w = size & ~1;
    int out_h = (2 * margin + rows * row_side + (rows - 1) * gap) & ~1;

    RGBAImage base(out_w, out_h, PAGE_BG[0], PAGE_BG[1], PAGE_BG[2], 255);
    for (int i = 0; i < count; ++i) {
        int col = i % cols, row = i / cols;
        int in_row = std::min(cols, count - row * cols);
        int row_w = in_row * tile_side + (in_row - 1) * gap;
        items[i].x = (out_w - row_w) / 2 + col * (tile_side + gap);
        items[i].y = margin + row * (row_side + gap);
        items[i].tile = build_tile(items[i].card, items[i].pat, tile_side, pattern_boost);
        alpha_blend_rgba_over(base, items[i].tile, items[i].x, items[i].y);
        draw_footer(ctx, api, items[i].card, base, tile_side, items[i].y, items[i].x + tile_side / 2);
    }

    int min_step = items[0].pl.step;
    int total_frames = (items[0].pl.op - items[0].pl.ip) / items[0].pl.step;
    for (const auto& it : items) {
        min_step = std::min(min_step, it.pl.step);
        total_frames = std::min(total_frames, (it.pl.op - it.pl.ip) / it.pl.step);
    }
    int eff_fps = std::max(1, items[0].pl.source_fps / items[0].pl.step);
    (void)min_step;
    (void)fmt;

    FrameSink sink;
    if (!sink.open(out_path, true, out_w, out_h, eff_fps)) {
        err = "не удалось инициализировать кодировщик сетки";
        return false;
    }

    int model_side = static_cast<int>(tile_side * SQUARE_MODEL_INSET);
    int offset_y = static_cast<int>(std::lround(tile_side * SQUARE_MODEL_DY));
    RGBAImage frame_img = base;

    for (int idx = 0; idx < total_frames; ++idx) {
        frame_img.px = base.px;
        for (auto& it : items) {
            int fside = static_cast<int>(std::lround(model_side * it.pl.ratio));
            std::vector<uint32_t> lbuf(static_cast<size_t>(fside) * fside, 0u);
            size_t fnum = static_cast<size_t>(it.pl.ip + idx * it.pl.step);
            lottie_animation_render(it.pl.anim, fnum, lbuf.data(),
                                    static_cast<size_t>(fside), static_cast<size_t>(fside),
                                    static_cast<size_t>(fside) * 4);
            int mx = it.x + (tile_side - fside) / 2;
            int my = it.y + (tile_side - fside) / 2 + offset_y;
            composite_premul_bgra(frame_img, lbuf.data(), fside, fside, mx, my);
        }
        if (!sink.write(frame_img)) {
            err = "сбой кодирования кадра сетки";
            return false;
        }
    }
    if (!sink.finish()) {
        err = "сбой финализации файла сетки";
        return false;
    }
    return true;
}

// ── Поиск подарков в аргументах и реплае ────────────────────────────────────

static bool is_cmd_word(const std::string& w) {
    static const char* WORDS[] = {
        "gif", "гиф", "гифка", "видео", "video", "mp4",
        "сетка", "grid", "коллаж", "статус", "status", "?",
        "текст", "убрать", "сброс", "clear"
    };
    for (const char* x : WORDS) if (w == x) return true;
    return false;
}

static bool is_digits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

static std::vector<std::string> extract_gift_refs(koto_ctx* ctx, const koto_api* api, const std::string& args) {
    std::vector<std::string> refs;
    auto push_unique = [&](const std::string& s) {
        if (!s.empty() && std::find(refs.begin(), refs.end(), s) == refs.end()) refs.push_back(s);
    };
    static const std::regex re_url(R"(t\.me/nft/([A-Za-z0-9_-]+))", std::regex::icase);
    auto collect_urls = [&](const std::string& text) {
        auto it = std::sregex_iterator(text.begin(), text.end(), re_url);
        auto end = std::sregex_iterator();
        for (; it != end; ++it) push_unique((*it)[1].str());
    };

    collect_urls(args);
    if (refs.empty()) {
        std::istringstream iss(args);
        std::string w;
        while (iss >> w) {
            std::string wl = to_lower_utf8_ru(w);
            if (is_cmd_word(wl)) continue;
            if (is_digits(w) && w.size() <= 4) {
                int v = std::atoi(w.c_str());
                if (v >= CARD_SIZE_MIN && v <= CARD_SIZE_MAX) continue;
            }
            if (w.find('-') != std::string::npos) {
                push_unique(resolve_slug(w));
            }
        }
    }

    if (api && api->reply_text) {
        const char* rt = api->reply_text(ctx);
        if (rt && rt[0]) collect_urls(std::string(rt));
    }
    return refs;
}

static std::string normalize_fmt(const std::string& v, const std::string& def = "mp4") {
    std::string l = to_lower_utf8_ru(koto_utils::trim(v));
    if (l.rfind("mp4", 0) == 0 || l.rfind("vid", 0) == 0 || l.find("виде") != std::string::npos) return "mp4";
    if (l.rfind("gif", 0) == 0 || l.find("гиф") != std::string::npos) return "gif";
    return def;
}

static std::string expand_caption_tags(std::string s) {
    struct Map { const char* tag; const char* emo; };
    static const Map MAPS[] = {
        {"{fire:}", "🔥"}, {"{fire}", "🔥"},
        {"{star:}", "⭐"}, {"{star}", "⭐"},
        {"{sparkles:}", "✨"}, {"{sparkles}", "✨"},
        {"{diamond:}", "💎"}, {"{diamond}", "💎"},
        {"{check:}", "✅"}, {"{check}", "✅"},
        {"{gift:}", "🎁"}, {"{gift}", "🎁"},
        {"{rocket:}", "🚀"}, {"{rocket}", "🚀"},
        {"{bolt:}", "⚡"}, {"{bolt}", "⚡"},
    };
    for (const auto& m : MAPS) {
        size_t pos = 0;
        while ((pos = s.find(m.tag, pos)) != std::string::npos) {
            s.replace(pos, std::strlen(m.tag), m.emo);
            pos += std::strlen(m.emo);
        }
    }
    return s;
}

// ── Кэш на диске ─────────────────────────────────────────────────────────────

static std::string cache_file_for_key(koto_ctx* ctx, const koto_api* api, const std::string& key, const std::string& ext) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : key) { h ^= c; h *= 1099511628211ULL; }
    char hex[32];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(h));
    return get_cache_root(ctx, api) + "/card_" + hex + "." + ext;
}

static bool cache_hit(const std::string& path, int cache_minutes) {
    if (cache_minutes <= 0) return false;
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || fs::file_size(path, ec) < 512) return false;
    auto ftime = fs::last_write_time(path, ec);
    if (ec) return false;
    auto now = fs::file_time_type::clock::now();
    auto age_sec = std::chrono::duration_cast<std::chrono::seconds>(now - ftime).count();
    return age_sec >= 0 && age_sec <= static_cast<long long>(cache_minutes) * 60LL;
}

static void prune_cache_dir(const std::string& dir, int cache_minutes) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    auto now = fs::file_time_type::clock::now();
    long long card_ttl = std::max(cache_minutes, 5) * 60LL;
    long long grid_ttl = 300LL;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string name = entry.path().filename().string();
        long long ttl = 0;
        if (name.rfind("grid_", 0) == 0) ttl = grid_ttl;
        else if (name.rfind("card_", 0) == 0) ttl = card_ttl;
        else continue;
        auto ft = entry.last_write_time(ec);
        if (ec) continue;
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - ft).count();
        if (age > ttl) fs::remove(entry.path(), ec);
    }
}

// ── Команды модуля ───────────────────────────────────────────────────────────

static void send_status(koto_ctx* ctx, const koto_api* api) {
    std::string pfx = get_prefix(ctx, api);
    std::string fmt = normalize_fmt(get_cfg_str(ctx, api, "format", "mp4"));
    int sz = get_cfg_int(ctx, api, "size", 512, CARD_SIZE_MIN, CARD_SIZE_MAX);
    int fps = get_cfg_int(ctx, api, "fps", 30, 5, 60);
    int pat = get_cfg_int(ctx, api, "pattern", 100, 40, 250);
    bool card_cap = get_cfg_bool(ctx, api, "card_caption", true);
    bool file_cap = get_cfg_bool(ctx, api, "file_caption", false);
    std::string cap = get_db_str(ctx, api, "caption");

    api->c_reset(ctx);
    api->c_emoji(ctx, "🎁", EMO_DIAMOND);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, "GiftGif", KOTO_ENT_BOLD);
    api->c_text(ctx, " · ");
    api->c_fmt(ctx, fmt.c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, (" · " + std::to_string(sz) + " px · " + std::to_string(fps) + " fps\n").c_str());
#ifdef __ANDROID__
    api->c_text(ctx, "rlottie: ✅ · NDK H.264: ✅ · libwebp: ✅\n\n");
#else
    api->c_text(ctx, "rlottie: ✅ · GIF89a: ✅ · libwebp: ✅\n\n");
#endif
    api->c_fmt(ctx, "Команды:\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "• ");
    api->c_fmt(ctx, (pfx + "gif t.me/nft/gingercookie-89402").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — карточка по ссылке или слагам\n• ");
    api->c_fmt(ctx, (pfx + "gif 512 видео").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " / ");
    api->c_fmt(ctx, (pfx + "gif gif").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — размер и формат\n• ");
    api->c_fmt(ctx, (pfx + "gif сетка <ссылки>").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — сетка до 9 подарков\n• ");
    api->c_fmt(ctx, (pfx + "gifttext С Новым годом! 🎄").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — своя подпись\n• ");
    api->c_fmt(ctx, (pfx + "gifhelp").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — полная справка\n\n");
    api->c_fmt(ctx, "Подпись: ", KOTO_ENT_BOLD);
    api->c_text(ctx, (cap.empty() ? "—" : cap).c_str());
    api->c_text(ctx, (std::string(" · на карточке: ") + (card_cap ? "да" : "нет")
                      + " · под файлом: " + (file_cap ? "да" : "нет") + "\n").c_str());
    api->c_fmt(ctx, "Узор: ", KOTO_ENT_BOLD);
    api->c_text(ctx, (std::to_string(pat) + "% · ").c_str());
    api->c_fmt(ctx, "Версия: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, MOD_VERSION, KOTO_ENT_CODE);
    respond(ctx, api);
}

static void cmd_gifhelp(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    std::string pfx = get_prefix(ctx, api);
    api->c_reset(ctx);
    api->c_emoji(ctx, "💎", EMO_DIAMOND);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, "GiftGif", KOTO_ENT_BOLD);
    api->c_text(ctx, " — подарок Telegram в анимированную карточку MP4 / GIF\n\n");

    api->c_emoji(ctx, "✨", EMO_SPARKLES);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, "Одна карточка\n", KOTO_ENT_BOLD);
    api->c_fmt(ctx, (pfx + "gif").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — реплаем на сообщение со ссылкой на подарок\n");
    api->c_fmt(ctx, (pfx + "gif t.me/nft/gingercookie-89402").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — по ссылке\n");
    api->c_fmt(ctx, (pfx + "gif gingercookie-89402").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — по слагу\n\n");

    api->c_emoji(ctx, "⚡", EMO_BOLT);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, "Размер и формат в команде\n", KOTO_ENT_BOLD);
    api->c_fmt(ctx, (pfx + "gif видео").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " / ");
    api->c_fmt(ctx, (pfx + "gif mp4").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — видео MP4\n");
    api->c_fmt(ctx, (pfx + "gif gif").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — анимация (GIF с автоплеем)\n");
    api->c_fmt(ctx, (pfx + "gif 512").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — сторона: 256 / 512 / 768 / 1024 (до 1536)\n\n");

    api->c_emoji(ctx, "📚", EMO_BOOK);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, "Своя подпись под карточкой\n", KOTO_ENT_BOLD);
    api->c_fmt(ctx, (pfx + "gifttext С Новым годом! 🎄").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — задать текст\n");
    api->c_fmt(ctx, (pfx + "gifttext убрать").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — очистить\n\n");

    api->c_emoji(ctx, "📊", EMO_CHART);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, "Пачка и сетка\n", KOTO_ENT_BOLD);
    api->c_fmt(ctx, (pfx + "gif t.me/nft/x-1 t.me/nft/y-2").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — до 10 карточек подряд\n");
    api->c_fmt(ctx, (pfx + "gif сетка t.me/nft/x-1 t.me/nft/y-2").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — до 9 плиток в одном видео\n\n");

    api->c_emoji(ctx, "⚙️", EMO_GEAR);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, "Настройки в .cfg giftgif:\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "format, size, fps, pattern, footer, card_caption, file_caption, cache_minutes");
    respond(ctx, api);
}

static void cmd_gifttext(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    std::string raw = koto_utils::trim(api->cmd_args ? (api->cmd_args(ctx) ? api->cmd_args(ctx) : "") : "");
    std::string low = to_lower_utf8_ru(raw);

    if (raw.empty() || low == "сброс" || low == "убрать" || low == "clear" || low == "-") {
        api->db_set(ctx, "caption", "");
        api->c_reset(ctx);
        api->c_text(ctx, "🧹 Свой текст убран — будет только название и номер.");
        respond(ctx, api);
        return;
    }
    if (raw.size() > 400) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMO_CROSS);
        api->c_text(ctx, " Слишком длинный текст (максимум 200 символов).");
        respond(ctx, api);
        return;
    }
    std::string expanded = expand_caption_tags(raw);
    api->db_set(ctx, "caption", expanded.c_str());

    bool on_card = get_cfg_bool(ctx, api, "card_caption", true);
    bool in_file = get_cfg_bool(ctx, api, "file_caption", false);
    const char* where = (on_card && in_file) ? "на карточке и под файлом"
                        : on_card ? "на карточке"
                        : in_file ? "под файлом"
                        : "нигде (card_caption и file_caption выключены)";

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", EMO_CHECK);
    api->c_text(ctx, " Текст сохранён:\n");
    api->c_fmt(ctx, expanded.c_str(), KOTO_ENT_BOLD);
    api->c_text(ctx, (std::string("\n\nКуда попадёт: ") + where
                      + "\nМеняется в ").c_str());
    api->c_fmt(ctx, ".cfg giftgif", KOTO_ENT_CODE);
    respond(ctx, api);
}

static void cmd_gif(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    std::string args = koto_utils::trim(api->cmd_args ? (api->cmd_args(ctx) ? api->cmd_args(ctx) : "") : "");
    std::string lower = to_lower_utf8_ru(args);

    if (lower == "статус" || lower == "status" || lower == "?") {
        send_status(ctx, api);
        return;
    }

    // Парсим опции из аргументов
    std::string fmt = normalize_fmt(get_cfg_str(ctx, api, "format", "mp4"));
    int size = get_cfg_int(ctx, api, "size", 512, CARD_SIZE_MIN, CARD_SIZE_MAX);
    int fps = get_cfg_int(ctx, api, "fps", 30, 5, 60);
    int pat_pct = get_cfg_int(ctx, api, "pattern", 100, 40, 250);
    bool show_footer = get_cfg_bool(ctx, api, "footer", true);
    bool card_cap_on = get_cfg_bool(ctx, api, "card_caption", true);
    bool file_cap_on = get_cfg_bool(ctx, api, "file_caption", false);
    int cache_min = get_cfg_int(ctx, api, "cache_minutes", 30, 0, 1440);
    bool as_grid = false;

    {
        std::istringstream iss(lower);
        std::string w;
        while (iss >> w) {
            if (w == "gif" || w.find("гиф") != std::string::npos) fmt = "gif";
            else if (w == "mp4" || w == "video" || w.find("виде") != std::string::npos) fmt = "mp4";
            else if (w == "сетка" || w == "grid" || w == "коллаж") as_grid = true;
            else if (is_digits(w) && w.size() <= 4) {
                int v = std::atoi(w.c_str());
                if (v >= CARD_SIZE_MIN && v <= CARD_SIZE_MAX) size = v;
            }
        }
    }

    auto refs = extract_gift_refs(ctx, api, args);
    if (refs.empty()) {
        send_status(ctx, api);
        return;
    }

    std::string own_cap = get_db_str(ctx, api, "caption");
    std::string card_cap = card_cap_on ? own_cap : "";
    float pat_boost = static_cast<float>(pat_pct) / 100.0f;
    long long cid = api->chat_id ? api->chat_id(ctx) : 0;
    bool is_out = api->is_outgoing && api->is_outgoing(ctx);
    bool has_reply = api->reply_text && api->reply_text(ctx) && api->reply_text(ctx)[0];
    int send_flags = 0;
    if (api->host_version && api->host_version(ctx) >= 118 && is_out) {
        send_flags = has_reply ? KOTO_REPLY_TO_TARGET : KOTO_NO_REPLY;
    }

    std::error_code ec;
    std::string cache_dir = get_cache_root(ctx, api);
    fs::create_directories(cache_dir, ec);
    prune_cache_dir(cache_dir, cache_min);

    auto send_rendered_file = [&](const std::string& path, const std::string& label) {
        api->c_reset(ctx);
        if (file_cap_on) {
            if (!own_cap.empty()) {
                api->c_text(ctx, (own_cap + "\n").c_str());
            }
            uint64_t fsz = fs::file_size(path, ec);
            api->c_text(ctx, "🎁 ");
            api->c_fmt(ctx, label.c_str(), KOTO_ENT_BOLD);
            api->c_text(ctx, (" · " + std::to_string(size) + " px · " + human_size(fsz)).c_str());
        }
        if (api->c_send_file) {
            api->c_send_file(ctx, cid, path.c_str(), send_flags);
        }
    };

    // Режим сетки (.gif сетка)
    if (as_grid && refs.size() > 1) {
        if (is_out) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "⌛", EMO_HOURGLASS);
            api->c_text(ctx, (" Собираю сетку из " + std::to_string(std::min<size_t>(refs.size(), MAX_TILES)) + " подарков…").c_str());
            api->c_edit(ctx, 0);
        }
        std::string ext = (fmt == "gif") ? "gif" : "mp4";
        std::string out_path = cache_dir + "/grid_" + std::to_string(std::time(nullptr)) + "." + ext;
        std::vector<std::string> skipped;
        std::string err;
        bool ok = false;
        {
            std::lock_guard<std::mutex> lk(g_render_mtx);
            ok = render_grid_card(ctx, api, refs, size, fps, fmt, pat_boost, show_footer, card_cap, out_path, skipped, err);
        }
        if (!ok) {
            fs::remove(out_path, ec);
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", EMO_CROSS);
            api->c_text(ctx, (" Сетка не собралась: " + err).c_str());
            respond(ctx, api);
            return;
        }
        // Не удаляем out_path сразу: c_send_file выполняется хостом отложенно после возврата!
        send_rendered_file(out_path, fs::path(out_path).filename().string());
        if (is_out && api->delete_msg) api->delete_msg(ctx);
        return;
    }

    // Одиночная карточка или пачка (до MAX_BATCH)
    size_t total = std::min<size_t>(refs.size(), MAX_BATCH);
    if (is_out) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "⌛", EMO_HOURGLASS);
        if (total == 1) api->c_text(ctx, " Собираю карточку подарка…");
        else api->c_text(ctx, (" Собираю " + std::to_string(total) + " карточек подарков…").c_str());
        api->c_edit(ctx, 0);
    }

    int sent_ok = 0;
    std::vector<std::string> fails;
    std::string ext = (fmt == "gif") ? "gif" : "mp4";

    for (size_t i = 0; i < total; ++i) {
        std::string slug = resolve_slug(refs[i]);
        std::string ckey = slug + "|" + std::to_string(size) + "|" + std::to_string(fps) + "|"
                         + fmt + "|" + std::to_string(pat_pct) + "|" + (show_footer ? "1" : "0") + "|" + card_cap;
        std::string path = cache_file_for_key(ctx, api, ckey, ext);

        if (!cache_hit(path, cache_min)) {
            std::string actual, err;
            bool ok = false;
            {
                std::lock_guard<std::mutex> lk(g_render_mtx);
                ok = render_single_card(ctx, api, slug, size, fps, fmt, pat_boost,
                                        show_footer, card_cap, path, actual, err);
            }
            if (!ok) {
                fs::remove(path, ec);
                fails.push_back(slug + ": " + err);
                continue;
            }
            path = actual;
        }

        // Не удаляем path сразу: c_send_file выполняется хостом отложенно; устаревший кэш чистит prune_cache_dir.
        send_rendered_file(path, slug + fs::path(path).extension().string());
        sent_ok++;
    }

    if (sent_ok > 0 && fails.empty()) {
        if (is_out && api->delete_msg) api->delete_msg(ctx);
    } else {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMO_CROSS);
        api->c_text(ctx, (" Не получилось (" + std::to_string(sent_ok) + "/" + std::to_string(total) + "):\n").c_str());
        for (const auto& f : fails) api->c_text(ctx, ("• " + f + "\n").c_str());
        respond(ctx, api);
    }
}

static const koto_command COMMANDS[] = {
    {
        "gif",
        &cmd_gif,
        KOTO_LEVEL_TRUSTED,
        "Подарок Telegram в карточку MP4/GIF (ссылка, слаг, реплай, размер, сетка).",
        "[t.me/nft/...|слаг] [256..1536] [видео|gif] [сетка|статус]"
    },
    {
        "gifttext",
        &cmd_gifttext,
        KOTO_LEVEL_TRUSTED,
        "Задать или убрать свою подпись под карточкой подарка.",
        "[текст|убрать]"
    },
    {
        "gifhelp",
        &cmd_gifhelp,
        KOTO_LEVEL_TRUSTED,
        "Подробная справка по командам и настройкам GiftGif.",
        ""
    }
};

static const koto_setting_field SETTINGS[] = {
    {"format",        "Формат (mp4 / gif)",                KOTO_FIELD_CHOICE, "mp4", "mp4,gif"},
    {"size",          "Размер карточки (64..1536 px)",     KOTO_FIELD_INT,    "512", nullptr},
    {"fps",           "Кадров в секунду (5..60)",          KOTO_FIELD_INT,    "30",  nullptr},
    {"pattern",       "Яркость узора (40..250 %)",         KOTO_FIELD_INT,    "100", nullptr},
    {"footer",        "Название и номер под плиткой",      KOTO_FIELD_BOOL,   "1",   nullptr},
    {"card_caption",  "Подпись .gifttext на карточке",     KOTO_FIELD_BOOL,   "1",   nullptr},
    {"file_caption",  "Подпись к файлу в чате",            KOTO_FIELD_BOOL,   "0",   nullptr},
    {"cache_minutes", "Кэш карточек в минутах (0 — выкл)", KOTO_FIELD_INT,    "30",  nullptr},
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "giftgif",
    "Подарок Telegram в анимированную карточку MP4/GIF с фоном, узором и текстом (rlottie + NDK H.264, by Kote).",
    MOD_VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    nullptr,
    0,
    nullptr,
    SETTINGS,
    static_cast<int>(sizeof(SETTINGS) / sizeof(SETTINGS[0])),
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/giftgif.so"
};

} // namespace

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
