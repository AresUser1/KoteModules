// wikisearch.cpp — Быстрый поиск статей на Wikipedia с кэшированием и изображениями (C++ ABI 3).
// Портирован из Python-модуля WikiSearch (by kote).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <list>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr const char* VERSION = "1.0.0";
constexpr int RESULT_CACHE_TTL_SEC = 600;
constexpr int IMAGE_CACHE_TTL_SEC  = 300;
constexpr size_t RESULT_CACHE_MAX  = 256;
constexpr size_t IMAGE_CACHE_MAX   = 64;
constexpr size_t MIN_IMAGE_BYTES   = 1024;
constexpr size_t MAX_IMAGE_BYTES   = 5 * 1024 * 1024;

struct WikiResult {
    std::string title;
    std::string extract;
    std::string url;
    std::string image_url;
};

struct ResultCacheEntry {
    std::string key;
    std::chrono::steady_clock::time_point expires;
    WikiResult data;
};

struct ImageCacheEntry {
    std::string url;
    std::chrono::steady_clock::time_point expires;
    std::string path;
};

std::mutex g_cache_mu;
std::list<ResultCacheEntry> g_result_lru;
std::unordered_map<std::string, std::list<ResultCacheEntry>::iterator> g_result_map;

std::list<ImageCacheEntry> g_image_lru;
std::unordered_map<std::string, std::list<ImageCacheEntry>::iterator> g_image_map;

static std::string ascii_lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

static bool is_valid_lang(const std::string& lang) {
    if (lang.size() < 2 || lang.size() > 12) return false;
    for (char c : lang) {
        if (c < 'a' || c > 'z') return false;
    }
    return true;
}

static uint64_t fnv1a_64(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

static std::string normalize_whitespace(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool in_ws = true;
    for (unsigned char c : s) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!in_ws) {
                out.push_back(' ');
                in_ws = true;
            }
        } else {
            out.push_back(static_cast<char>(c));
            in_ws = false;
        }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

static size_t utf16_len(const std::string& s) {
    size_t units = 0;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            i += 1;
            units += 1;
        } else if ((c & 0xE0) == 0xC0) {
            i += 2;
            units += 1;
        } else if ((c & 0xF0) == 0xE0) {
            i += 3;
            units += 1;
        } else if ((c & 0xF8) == 0xF0) {
            i += 4;
            units += 2;
        } else {
            i += 1;
            units += 1;
        }
    }
    return units;
}

// Безопасная обрезка UTF-8 по границе символов и лимиту UTF-16 code units.
static std::string utf8_truncate_utf16(const std::string& s, size_t max_utf16, bool prefer_sentence) {
    if (utf16_len(s) <= max_utf16) return s;

    size_t units = 0;
    size_t cut_byte = 0;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t adv = 1;
        size_t u = 1;
        if (c < 0x80) adv = 1;
        else if ((c & 0xE0) == 0xC0) adv = 2;
        else if ((c & 0xF0) == 0xE0) adv = 3;
        else if ((c & 0xF8) == 0xF0) { adv = 4; u = 2; }
        if (i + adv > s.size()) break;
        if (units + u > max_utf16) break;
        units += u;
        i += adv;
        cut_byte = i;
    }

    std::string trimmed = s.substr(0, cut_byte);
    if (prefer_sentence) {
        size_t best = std::string::npos;
        for (const char* sep : {". ", "! ", "? "}) {
            size_t p = trimmed.rfind(sep);
            if (p != std::string::npos && (best == std::string::npos || p > best)) {
                best = p + 1;
            }
        }
        if (best == std::string::npos) {
            best = trimmed.rfind(' ');
        }
        if (best != std::string::npos && best > (trimmed.size() * 3) / 5) {
            trimmed = koto_utils::trim(trimmed.substr(0, best));
        }
    } else {
        size_t sp = trimmed.rfind(' ');
        if (sp != std::string::npos && sp > (trimmed.size() * 3) / 5) {
            trimmed = koto_utils::trim(trimmed.substr(0, sp));
        }
    }
    while (!trimmed.empty() && trimmed.back() == '.') trimmed.pop_back();
    return trimmed + "...";
}

// Разделение на первое предложение (жирным) и остальной текст.
static std::pair<std::string, std::string> split_first_sentence(const std::string& text) {
    for (size_t i = 0; i + 1 < text.size(); ++i) {
        char ch = text[i];
        if ((ch == '.' || ch == '!' || ch == '?') && text[i + 1] == ' ') {
            if (i + 2 < text.size()) {
                return {text.substr(0, i + 1), text.substr(i + 2)};
            }
        }
    }
    return {"", text};
}

static std::string wiki_url_encode(const std::string& s) {
    std::string fixed = s;
    for (char& c : fixed) {
        if (c == ' ') c = '_';
    }
    return koto_utils::url_encode(fixed);
}

static void prune_caches_locked(bool remove_all_images) {
    auto now = std::chrono::steady_clock::now();

    for (auto it = g_result_lru.begin(); it != g_result_lru.end();) {
        if (it->expires <= now) {
            g_result_map.erase(it->key);
            it = g_result_lru.erase(it);
        } else {
            ++it;
        }
    }
    while (g_result_lru.size() > RESULT_CACHE_MAX) {
        auto last = std::prev(g_result_lru.end());
        g_result_map.erase(last->key);
        g_result_lru.erase(last);
    }

    std::error_code ec;
    for (auto it = g_image_lru.begin(); it != g_image_lru.end();) {
        bool expired = remove_all_images || (it->expires <= now) || !fs::is_regular_file(it->path, ec);
        if (expired) {
            if (!it->path.empty()) fs::remove(it->path, ec);
            g_image_map.erase(it->url);
            it = g_image_lru.erase(it);
        } else {
            ++it;
        }
    }
    while (g_image_lru.size() > IMAGE_CACHE_MAX) {
        auto last = std::prev(g_image_lru.end());
        if (!last->path.empty()) fs::remove(last->path, ec);
        g_image_map.erase(last->url);
        g_image_lru.erase(last);
    }
}

static bool get_cached_result(const std::string& key, WikiResult& out) {
    std::lock_guard<std::mutex> lk(g_cache_mu);
    prune_caches_locked(false);
    auto it = g_result_map.find(key);
    if (it == g_result_map.end()) return false;
    g_result_lru.splice(g_result_lru.begin(), g_result_lru, it->second);
    out = it->second->data;
    return true;
}

static void put_cached_result(const std::string& key, const WikiResult& val) {
    std::lock_guard<std::mutex> lk(g_cache_mu);
    auto exp = std::chrono::steady_clock::now() + std::chrono::seconds(RESULT_CACHE_TTL_SEC);
    auto it = g_result_map.find(key);
    if (it != g_result_map.end()) {
        it->second->expires = exp;
        it->second->data = val;
        g_result_lru.splice(g_result_lru.begin(), g_result_lru, it->second);
    } else {
        g_result_lru.push_front({key, exp, val});
        g_result_map[key] = g_result_lru.begin();
    }
    prune_caches_locked(false);
}

static std::string get_cached_image(const std::string& url) {
    std::lock_guard<std::mutex> lk(g_cache_mu);
    prune_caches_locked(false);
    auto it = g_image_map.find(url);
    if (it == g_image_map.end()) return "";
    std::error_code ec;
    if (!fs::is_regular_file(it->second->path, ec)) {
        g_image_lru.erase(it->second);
        g_image_map.erase(it);
        return "";
    }
    g_image_lru.splice(g_image_lru.begin(), g_image_lru, it->second);
    return it->second->path;
}

static void put_cached_image(const std::string& url, const std::string& path) {
    std::lock_guard<std::mutex> lk(g_cache_mu);
    auto exp = std::chrono::steady_clock::now() + std::chrono::seconds(IMAGE_CACHE_TTL_SEC);
    auto it = g_image_map.find(url);
    if (it != g_image_map.end()) {
        std::error_code ec;
        if (it->second->path != path && !it->second->path.empty()) fs::remove(it->second->path, ec);
        it->second->expires = exp;
        it->second->path = path;
        g_image_lru.splice(g_image_lru.begin(), g_image_lru, it->second);
    } else {
        g_image_lru.push_front({url, exp, path});
        g_image_map[url] = g_image_lru.begin();
    }
    prune_caches_locked(false);
}

static std::string get_cache_root(koto_ctx* ctx, const koto_api* api) {
    if (api && api->cache_dir) {
        char buf[512] = {0};
        int n = api->cache_dir(ctx, buf, sizeof(buf) - 1);
        if (n > 0) return std::string(buf, static_cast<size_t>(n)) + "/wikisearch";
    }
    return "/data/local/tmp/wikisearch_cache";
}

static bool fetch_wiki_article(koto_ctx* ctx, const koto_api* api,
                               const std::string& lang, const std::string& query,
                               WikiResult& out) {
    const std::string cache_key = lang + ":" + ascii_lower(query);
    if (get_cached_result(cache_key, out)) return true;

    const std::vector<std::string> headers = {
        "User-Agent: WikiSearchMod/5.0 (+https://wikipedia.org)",
        "Accept: application/json"
    };

    const std::string base = "https://" + lang + ".wikipedia.org/w/api.php";
    std::string target_title = query;

    // 1. Поиск релевантного заголовка
    {
        std::string search_url = base + "?action=query&list=search&srlimit=1&utf8=1&format=json&srsearch="
                                 + koto_utils::url_encode(query);
        std::string resp = koto_utils::http_get(api, ctx, search_url, headers, 10);
        if (!resp.empty()) {
            try {
                json j = json::parse(resp);
                if (j.contains("query") && j["query"].contains("search") && j["query"]["search"].is_array()
                    && !j["query"]["search"].empty()) {
                    std::string found = j["query"]["search"][0].value("title", "");
                    if (!found.empty()) target_title = found;
                }
            } catch (...) {}
        }
    }

    // 2. Получение выдержки и миниатюры
    std::string sum_url = base + "?action=query&prop=extracts|pageimages|info&inprop=url"
                          "&exintro=1&explaintext=1&redirects=1&piprop=thumbnail&pithumbsize=640&format=json&titles="
                          + koto_utils::url_encode(target_title);
    std::string sum_resp = koto_utils::http_get(api, ctx, sum_url, headers, 10);
    if (sum_resp.empty()) return false;

    try {
        json j = json::parse(sum_resp);
        if (!j.contains("query") || !j["query"].contains("pages") || !j["query"]["pages"].is_object()) {
            return false;
        }
        const auto& pages = j["query"]["pages"];
        if (pages.empty()) return false;

        const auto& page = *pages.begin();
        if (!page.is_object() || page.contains("missing")) return false;

        out.title = page.value("title", target_title);
        if (out.title.empty()) out.title = target_title;

        out.extract = normalize_whitespace(page.value("extract", ""));
        if (out.extract.empty()) return false;

        out.url = page.value("fullurl", "");
        if (out.url.empty()) {
            out.url = "https://" + lang + ".wikipedia.org/wiki/" + wiki_url_encode(out.title);
        }
        out.image_url.clear();
        if (page.contains("thumbnail") && page["thumbnail"].is_object()) {
            out.image_url = page["thumbnail"].value("source", "");
        }

        put_cached_result(cache_key, out);
        return true;
    } catch (...) {
        return false;
    }
}

static std::string guess_img_ext(const std::string& url) {
    std::string clean = url;
    size_t q = clean.find('?');
    if (q != std::string::npos) clean = clean.substr(0, q);
    std::string lower = ascii_lower(clean);
    if (lower.ends_with(".png")) return ".png";
    if (lower.ends_with(".gif")) return ".gif";
    if (lower.ends_with(".webp")) return ".webp";
    return ".jpg";
}

static std::string fetch_wiki_image(koto_ctx* ctx, const koto_api* api, const std::string& url) {
    if (url.empty()) return "";
    std::string cached = get_cached_image(url);
    if (!cached.empty()) return cached;

    std::error_code ec;
    std::string dir = get_cache_root(ctx, api);
    fs::create_directories(dir, ec);

    std::ostringstream oss;
    oss << dir << "/wiki_" << std::hex << fnv1a_64(url) << guess_img_ext(url);
    std::string path = oss.str();

    const std::vector<std::string> headers = {
        "User-Agent: WikiSearchMod/5.0 (+https://wikipedia.org)"
    };
    if (!koto_utils::download_file(api, ctx, url, path, headers, 15)) {
        fs::remove(path, ec);
        return "";
    }

    auto sz = fs::file_size(path, ec);
    if (ec || sz < MIN_IMAGE_BYTES || sz > MAX_IMAGE_BYTES) {
        fs::remove(path, ec);
        return "";
    }

    put_cached_image(url, path);
    return path;
}

static std::string lang_label(bool en, const std::string& lang) {
    if (lang == "ru") return en ? "Russian" : "русской";
    if (lang == "en") return en ? "English" : "английской";
    return lang;
}

static void compose_article(koto_ctx* ctx, const koto_api* api, bool en,
                            const WikiResult& res, bool has_image) {
    api->c_reset(ctx);
    api->c_text(ctx, "📖 ");
    api->c_fmt(ctx, res.title.c_str(), KOTO_ENT_BOLD);
    api->c_text(ctx, "\n\n");

    std::string link_label = en ? "Read full article on Wikipedia" : "Читать полностью на Wikipedia";
    std::string no_img_note = (!has_image) ? (en ? "\n\n🖼 No image for this article" : "\n\n🖼 Статья не имеет изображения") : "";

    size_t header_len = utf16_len("📖 " + res.title + "\n\n");
    size_t footer_len = utf16_len("\n\n🌐 " + link_label + no_img_note);
    size_t max_total = has_image ? 990 : 3800;

    std::string extract = res.extract;
    if (header_len + footer_len + 80 < max_total) {
        size_t avail = max_total - header_len - footer_len;
        extract = utf8_truncate_utf16(extract, avail, true);
        auto parts = split_first_sentence(extract);
        if (!parts.first.empty()) {
            api->c_fmt(ctx, parts.first.c_str(), KOTO_ENT_BOLD);
            api->c_text(ctx, (" " + parts.second).c_str());
        } else {
            api->c_text(ctx, extract.c_str());
        }
    }

    api->c_text(ctx, "\n\n🌐 ");
    api->c_url(ctx, link_label.c_str(), res.url.c_str());
    if (!has_image) {
        api->c_text(ctx, "\n\n🖼 ");
        api->c_fmt(ctx, en ? "Article has no image" : "Статья не имеет изображения", KOTO_ENT_ITALIC);
    }
}

void cmd_wiki(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    bool en = koto_utils::is_en(ctx, api);

    std::string source;
    if (api->cmd_args) {
        const char* a = api->cmd_args(ctx);
        if (a) source = koto_utils::trim(a);
    }
    if (source.empty() && api->reply_text) {
        const char* r = api->reply_text(ctx);
        if (r) source = koto_utils::trim(r);
    }

    if (source.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, en ? "🚫 Specify a search query\n\n" : "🚫 Укажите поисковый запрос\n\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, en ? "Examples:\n" : "Примеры использования:\n", KOTO_ENT_ITALIC);
        api->c_text(ctx, "• ");
        api->c_fmt(ctx, ".wiki Python", KOTO_ENT_CODE);
        api->c_text(ctx, "\n• ");
        api->c_fmt(ctx, en ? ".wiki Android" : ".wiki Кыргызстан", KOTO_ENT_CODE);
        api->c_text(ctx, "\n• ");
        api->c_fmt(ctx, ".wiki en Linux", KOTO_ENT_CODE);
        api->c_text(ctx, en
            ? "\n\n🌍 Default: English Wikipedia\nFor another language: .wiki ru запрос"
            : "\n\n🇷🇺 По умолчанию поиск в русской Wikipedia\n🌍 Для другого языка: .wiki en запрос");
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    std::string lang = en ? "en" : "ru";
    std::string query = source;
    size_t sp = source.find(' ');
    if (sp != std::string::npos) {
        std::string cand_lang = ascii_lower(koto_utils::trim(source.substr(0, sp)));
        std::string cand_query = koto_utils::trim(source.substr(sp + 1));
        if (!cand_query.empty() && is_valid_lang(cand_lang)) {
            lang = cand_lang;
            query = cand_query;
        }
    }

    const std::string ldisp = lang_label(en, lang);
    if (api->is_outgoing(ctx)) {
        api->c_reset(ctx);
        api->c_text(ctx, "🔍 ");
        api->c_fmt(ctx, en ? ("Searching " + ldisp + " Wikipedia:\n").c_str()
                           : ("Поиск в " + ldisp + " Wikipedia:\n").c_str(), KOTO_ENT_BOLD);
        api->c_fmt(ctx, query.c_str(), KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
    }

    WikiResult res;
    if (!fetch_wiki_article(ctx, api, lang, query, res)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, en ? "😔 Nothing found\n\n" : "😔 Ничего не найдено\n\n", KOTO_ENT_BOLD);
        api->c_text(ctx, en ? "Query: " : "Запрос: ");
        api->c_fmt(ctx, query.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, ("\nWikipedia: " + ldisp + "\n\n").c_str());
        api->c_fmt(ctx, en ? "Try another wording or language" : "Попробуйте изменить формулировку или язык", KOTO_ENT_ITALIC);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    std::string img_path = fetch_wiki_image(ctx, api, res.image_url);
    bool has_image = !img_path.empty();
    compose_article(ctx, api, en, res, has_image);

    if (has_image && api->c_send_file && api->chat_id) {
        api->c_send_file(ctx, api->chat_id(ctx), img_path.c_str(), 0);
        if (api->is_outgoing(ctx) && api->delete_msg) api->delete_msg(ctx);
    } else {
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    }
}

void cmd_wikiclear(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    bool en = koto_utils::is_en(ctx, api);

    {
        std::lock_guard<std::mutex> lk(g_cache_mu);
        g_result_lru.clear();
        g_result_map.clear();
        prune_caches_locked(true);
    }

    api->c_reset(ctx);
    api->c_fmt(ctx, en ? "🧹 WikiSearch cache cleared" : "🧹 Кэш WikiSearch очищен", KOTO_ENT_BOLD);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

void cmd_wikicache(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    bool en = koto_utils::is_en(ctx, api);

    size_t res_sz = 0;
    size_t img_sz = 0;
    {
        std::lock_guard<std::mutex> lk(g_cache_mu);
        prune_caches_locked(false);
        res_sz = g_result_lru.size();
        img_sz = g_image_lru.size();
    }

    api->c_reset(ctx);
    api->c_fmt(ctx, en ? "🧠 WikiSearch Cache Stats\n\n" : "🧠 Статистика кэша WikiSearch\n\n", KOTO_ENT_BOLD);
    api->c_text(ctx, en ? "Results: " : "Результаты: ");
    api->c_fmt(ctx, (std::to_string(res_sz) + "/" + std::to_string(RESULT_CACHE_MAX)).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, en ? "\nImages: " : "\nИзображения: ");
    api->c_fmt(ctx, (std::to_string(img_sz) + "/" + std::to_string(IMAGE_CACHE_MAX)).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, en ? "\nResult TTL: " : "\nTTL результатов: ");
    api->c_fmt(ctx, (std::to_string(RESULT_CACHE_TTL_SEC) + (en ? " sec" : " сек")).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, en ? "\nImage TTL: " : "\nTTL изображений: ");
    api->c_fmt(ctx, (std::to_string(IMAGE_CACHE_TTL_SEC) + (en ? " sec" : " сек")).c_str(), KOTO_ENT_CODE);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    (void)ctx; (void)api;
}

static const koto_command COMMANDS[] = {
    {"wiki", cmd_wiki, KOTO_LEVEL_TRUSTED, "[ru] Поиск статей на Wikipedia с изображениями и кэшем (by kote). [en] Search Wikipedia articles with images and cache (by kote).", "[lang] <query|reply>"},
    {"wikiclear", cmd_wikiclear, KOTO_LEVEL_TRUSTED, "[ru] Очистить кэш WikiSearch (by kote). [en] Clear WikiSearch cache (by kote).", ""},
    {"wikicache", cmd_wikicache, KOTO_LEVEL_TRUSTED, "[ru] Статистика кэша WikiSearch (by kote). [en] Show WikiSearch cache statistics (by kote).", ""}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "wikisearch",
    "[ru] Быстрый поиск статей на Wikipedia с миниатюрами и LRU+TTL-кэшем (by kote). [en] Fast Wikipedia article search with thumbnails and LRU+TTL cache (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    nullptr, 0,
    on_callback,
    nullptr, 0,
    "https://github.com/AresUser1/KoteModules/blob/main/src/wikisearch.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
