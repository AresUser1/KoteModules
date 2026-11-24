// KoteModules_repo/src/pinterest_dl.cpp
// Полноценный нативный модуль Pinterest Downloader на C++ (ABI 3).
// Скачивает видео (MP4), анимации (GIF), аудио и изображения (JPG/PNG/WEBP) из Pinterest.
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <ctime>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>
#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"

#define PAW_ID         5084923566848213749LL
#define INFO_ID        5879813604068298387LL
#define USAGE_ID       5197195523794157505LL
#define OK_ID          PAW_ID

namespace {

struct PinSession {
    std::string sid;
    std::string original_url;
    std::string pin_id;
    std::string mode;    // "video", "gif", "audio"
    std::string quality; // "best", "720", "480"
    long long chat_id = 0;
    long long msg_id = 0;
    long long owner_id = 0;
    std::time_t created_at = 0;
};

static std::mutex g_pin_mu;
static std::map<std::string, PinSession> g_pin_sessions;

static bool is_url_stop_char(unsigned char c) {
    return c == '\0' || std::isspace(c) || c == '"' || c == '\'' || c == '<' || c == '>' || c == ')' || c == '\\';
}

static bool is_escaped_url_stop_char(const std::string& s, size_t idx) {
    unsigned char c = static_cast<unsigned char>(s[idx]);
    if (c == '\0' || std::isspace(c) || c == '"' || c == '\'' || c == '<' || c == '>' || c == ')') return true;
    if (c == '\\') {
        return !(idx + 1 < s.size() && s[idx + 1] == '/');
    }
    return false;
}

static bool is_token_char(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_';
}

static bool is_hex_str(const char* p, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(p[i]))) return false;
    }
    return true;
}

static std::string to_lower_ascii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::string decode_html_entities(std::string s) {
    s = koto_utils::replace_all(std::move(s), "&amp;", "&");
    s = koto_utils::replace_all(std::move(s), "&quot;", "\"");
    s = koto_utils::replace_all(std::move(s), "&#39;", "'");
    s = koto_utils::replace_all(std::move(s), "&#x27;", "'");
    s = koto_utils::replace_all(std::move(s), "&lt;", "<");
    s = koto_utils::replace_all(std::move(s), "&gt;", ">");
    return s;
}

static std::string unescape_slashes(std::string s) {
    return koto_utils::replace_all(std::move(s), "\\/", "/");
}

static bool is_sprite_or_avatar(const std::string& url) {
    return url.find("d53b014d86a6b6761bf649a0ed813c2b") != std::string::npos ||
           url.find("default_user") != std::string::npos ||
           url.find("favicon") != std::string::npos;
}

static std::string upgrade_to_originals(std::string u) {
    const char* sizes[] = { "/736x/", "/1200x/", "/564x/", "/474x/", "/236x/" };
    for (const char* sz : sizes) {
        size_t pos = u.find(sz);
        if (pos != std::string::npos) {
            u.replace(pos, std::strlen(sz), "/originals/");
            break;
        }
    }
    return u;
}

static std::string extract_ext_from_url(const std::string& url, const std::string& def_ext) {
    size_t q = url.find_first_of("?#");
    std::string path = (q == std::string::npos) ? url : url.substr(0, q);
    size_t dot = path.rfind('.');
    size_t slash = path.rfind('/');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        std::string ext = to_lower_ascii(path.substr(dot));
        if (ext == ".mp4" || ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp" || ext == ".gif") {
            return ext;
        }
    }
    return def_ext;
}

static std::string find_pin_url(const std::string& text) {
    if (text.empty()) return "";
    size_t pos = 0;
    while (pos < text.size()) {
        size_t h1 = text.find("https://", pos);
        size_t h2 = text.find("http://", pos);
        size_t start = std::min(h1, h2);
        if (start == std::string::npos) break;

        size_t end = start;
        while (end < text.size() && !is_url_stop_char(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        std::string candidate = text.substr(start, end - start);
        while (!candidate.empty() && (candidate.back() == '.' || candidate.back() == ',' || candidate.back() == ';')) {
            candidate.pop_back();
        }
        std::string low = to_lower_ascii(candidate);
        if (low.find("pin.it/") != std::string::npos) {
            return candidate;
        }
        size_t p_idx = low.find("pinterest.");
        if (p_idx != std::string::npos && low.find("/pin/", p_idx) != std::string::npos) {
            return candidate;
        }
        pos = end;
    }
    return "";
}

static std::string extract_pinit_slug(const std::string& url) {
    std::string low = to_lower_ascii(url);
    size_t idx = low.find("pin.it/");
    if (idx == std::string::npos) return "";
    size_t start = idx + 7;
    size_t end = start;
    while (end < url.size() && is_token_char(static_cast<unsigned char>(url[end]))) {
        ++end;
    }
    return (end > start) ? url.substr(start, end - start) : "";
}

static std::string read_digits_after(const std::string& text, size_t start) {
    while (start < text.size() && (text[start] == ' ' || text[start] == ':' || text[start] == '"')) {
        ++start;
    }
    size_t end = start;
    while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) {
        ++end;
    }
    return (end > start) ? text.substr(start, end - start) : "";
}

static std::string extract_pin_id(const std::string& text) {
    if (text.empty()) return "";

    // 1. Приоритет: явный маркер редиректа от socket/direct слоя
    size_t koto_pos = text.find("<!--KOTO_FINAL_URL:");
    if (koto_pos != std::string::npos) {
        size_t start = koto_pos + 19;
        size_t end = text.find("-->", start);
        if (end != std::string::npos) {
            std::string final_url = text.substr(start, end - start);
            size_t pin_pos = final_url.find("/pin/");
            if (pin_pos != std::string::npos) {
                std::string d = read_digits_after(final_url, pin_pos + 5);
                if (d.size() >= 4) return d;
            }
        }
    }

    // 2. oEmbed ссылка (всегда содержит канонический pin id)
    size_t oembed_pos = text.find("oembed.json?url=");
    if (oembed_pos != std::string::npos) {
        size_t pin_pos = text.find("/pin/", oembed_pos);
        if (pin_pos != std::string::npos && pin_pos - oembed_pos < 120) {
            std::string d = read_digits_after(text, pin_pos + 5);
            if (d.size() >= 4) return d;
        }
    }

    // 3. rel="canonical"
    size_t canon_pos = text.find("rel=\"canonical\"");
    if (canon_pos == std::string::npos) canon_pos = text.find("rel=\'canonical\'");
    if (canon_pos != std::string::npos) {
        size_t search_start = (canon_pos > 300) ? canon_pos - 300 : 0;
        size_t search_end = std::min(text.size(), canon_pos + 300);
        size_t pin_pos = text.find("/pin/", search_start);
        if (pin_pos != std::string::npos && pin_pos < search_end) {
            std::string d = read_digits_after(text, pin_pos + 5);
            if (d.size() >= 4) return d;
        }
    }

    // 4. seoUrl
    const char* seo_keys[] = { "\"seoUrl\":\"/pin/", "\"seoUrl\": \"/pin/" };
    for (const char* key : seo_keys) {
        size_t p = text.find(key);
        if (p != std::string::npos) {
            std::string d = read_digits_after(text, p + std::strlen(key));
            if (d.size() >= 4) return d;
        }
    }

    // 5. pinId
    size_t pid_pos = text.find("\"pinId\"");
    while (pid_pos != std::string::npos) {
        size_t colon = text.find(':', pid_pos + 7);
        if (colon != std::string::npos && colon - pid_pos < 20) {
            std::string d = read_digits_after(text, colon + 1);
            if (d.size() >= 4) return d;
        }
        pid_pos = text.find("\"pinId\"", pid_pos + 7);
    }

    // 6. Generic /pin/
    size_t p = text.find("/pin/");
    while (p != std::string::npos) {
        size_t start = p + 5;
        size_t end = start;
        while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        if (end - start >= 5) {
            return text.substr(start, end - start);
        }
        p = text.find("/pin/", start);
    }
    return "";
}

static std::string find_title_in_html(const std::string& html) {
    size_t limit = std::min<size_t>(html.size(), 250000);
    size_t t1 = html.find("<title>");
    if (t1 == std::string::npos || t1 >= limit) return "";
    size_t start = t1 + 7;
    size_t t2 = html.find("</title>", start);
    if (t2 == std::string::npos || t2 - start > 512) return "";
    std::string t = html.substr(start, t2 - start);
    size_t sep = t.find("| Pinterest");
    if (sep != std::string::npos) t = t.substr(0, sep);
    return koto_utils::trim(decode_html_entities(std::move(t)));
}

static std::string find_pinimg_video(const std::string& html, const char* ext_suffix) {
    size_t slen = std::strlen(ext_suffix);
    const char* prefixes[] = { "https://v", "http://v", "https:\\/\\/v", "http:\\/\\/v" };
    for (const char* pfx : prefixes) {
        size_t pos = 0;
        while ((pos = html.find(pfx, pos)) != std::string::npos) {
            size_t end = pos;
            while (end < html.size() && !is_escaped_url_stop_char(html, end)) {
                ++end;
            }
            std::string u = unescape_slashes(html.substr(pos, end - pos));
            size_t pd = u.find(".pinimg.com/videos/");
            if (pd != std::string::npos) {
                size_t q = u.find('?');
                std::string clean = (q == std::string::npos) ? u : u.substr(0, q);
                if (clean.size() > slen && clean.compare(clean.size() - slen, slen, ext_suffix) == 0) {
                    return clean;
                }
            }
            pos = end;
        }
    }
    return "";
}

static std::string extract_attr_in_tag(const std::string& tag, const char* attr) {
    std::string needle = std::string(attr) + "=\"";
    size_t a = tag.find(needle);
    if (a == std::string::npos) return "";
    size_t s = a + needle.size();
    size_t e = tag.find('"', s);
    if (e == std::string::npos) return "";
    return decode_html_entities(tag.substr(s, e - s));
}

static std::string find_preload_image(const std::string& html) {
    size_t idp = html.find("pin-image-preload");
    if (idp == std::string::npos) return "";
    size_t tag_start = html.rfind('<', idp);
    size_t tag_end = html.find('>', idp);
    if (tag_start == std::string::npos || tag_end == std::string::npos || tag_end - tag_start > 1024) {
        return "";
    }
    std::string tag = html.substr(tag_start, tag_end - tag_start + 1);
    std::string href = extract_attr_in_tag(tag, "href");
    if (!href.empty() && href.find("i.pinimg.com/") != std::string::npos && !is_sprite_or_avatar(href)) {
        return href;
    }
    return "";
}

static std::string find_og_image(const std::string& html) {
    size_t limit = std::min<size_t>(html.size(), 250000);
    size_t ogp = html.find("property=\"og:image\"");
    if (ogp == std::string::npos || ogp >= limit) return "";
    size_t tag_start = html.rfind('<', ogp);
    size_t tag_end = html.find('>', ogp);
    if (tag_start == std::string::npos || tag_end == std::string::npos || tag_end - tag_start > 1024) {
        return "";
    }
    std::string content = extract_attr_in_tag(html.substr(tag_start, tag_end - tag_start + 1), "content");
    if (!content.empty() && content.find("i.pinimg.com/") != std::string::npos && !is_sprite_or_avatar(content)) {
        return content;
    }
    return "";
}

static bool match_pinimg_hash_path(const std::string& u, size_t prefix_len) {
    // Expect "/<xx>/<yy>/<zz>/<32hex>.<ext>" after quality segment, or general image path
    if (u.size() < prefix_len + 41) return false;
    const char* p = u.c_str() + prefix_len;
    if (p[2] != '/' || p[5] != '/' || p[8] != '/') return false;
    return is_hex_str(p, 2) && is_hex_str(p + 3, 2) && is_hex_str(p + 6, 2) && is_hex_str(p + 9, 32);
}

static std::string find_pinimg_image_linear(const std::string& html) {
    const char* markers[] = {
        "https://i.pinimg.com/originals/",
        "https://i.pinimg.com/1200x/",
        "https://i.pinimg.com/736x/",
        "https://i.pinimg.com/564x/",
        "https:\\/\\/i.pinimg.com\\/originals\\/",
        "https:\\/\\/i.pinimg.com\\/1200x\\/",
        "https:\\/\\/i.pinimg.com\\/736x\\/",
        "https:\\/\\/i.pinimg.com\\/564x\\/",
    };
    for (const char* m : markers) {
        size_t pos = 0;
        while ((pos = html.find(m, pos)) != std::string::npos) {
            size_t end = pos;
            while (end < html.size() && !is_escaped_url_stop_char(html, end)) {
                ++end;
            }
            std::string u = unescape_slashes(html.substr(pos, end - pos));
            size_t q = u.find_first_of("?#");
            if (q != std::string::npos) u.resize(q);
            if (!is_sprite_or_avatar(u)) {
                size_t slash4 = 0;
                int slashes = 0;
                for (size_t k = 0; k < u.size(); ++k) {
                    if (u[k] == '/') {
                        ++slashes;
                        if (slashes == 4) { slash4 = k + 1; break; }
                    }
                }
                if (slash4 > 0 && match_pinimg_hash_path(u, slash4)) {
                    std::string ext = extract_ext_from_url(u, "");
                    if (!ext.empty() && ext != ".mp4") {
                        return u;
                    }
                }
            }
            pos = end;
        }
    }
    return "";
}

static void render_menu(koto_ctx* ctx, const koto_api* api, const PinSession& st) {
    api->c_reset(ctx);
    api->c_emoji(ctx, "📌", PAW_ID);
    api->c_fmt(ctx, " Pinterest Downloader\n\n", KOTO_ENT_BOLD);

    api->c_emoji(ctx, "⚙️", INFO_ID);
    api->c_fmt(ctx, " Текущие настройки:\n", KOTO_ENT_BOLD);

    std::string mode_str = (st.mode == "gif") ? "🎞 GIF (без звука)" :
                           (st.mode == "audio") ? "🎵 Звук (MP3)" : "🖼 Фото / 🎬 Видео";
    std::string qual_str = (st.mode == "audio") ? "Аудио поток" :
                           (st.quality == "480") ? "📱 480p" :
                           (st.quality == "720") ? "📺 720p" : "🔥 Макс.";

    api->c_text(ctx, "• Формат: ");
    api->c_fmt(ctx, mode_str.c_str(), KOTO_ENT_BOLD);
    api->c_text(ctx, "\n• Качество: ");
    api->c_fmt(ctx, qual_str.c_str(), KOTO_ENT_BOLD);
    api->c_text(ctx, "\n\n");

    api->c_fmt(ctx, "Выберите параметры ниже и нажмите «Скачать»:", KOTO_ENT_ITALIC);

    std::string btn_v = (st.mode == "video" ? "✅ " : "") + std::string("🖼 Фото / 🎬 Видео");
    std::string btn_g = (st.mode == "gif"   ? "✅ " : "") + std::string("🎞 GIF");
    std::string btn_a = (st.mode == "audio" ? "✅ " : "") + std::string("🎵 Звук");
    api->c_button(ctx, btn_v.c_str(), ("pin:m:" + st.sid + ":video").c_str(), 1);
    api->c_button(ctx, btn_g.c_str(), ("pin:m:" + st.sid + ":gif").c_str(), 0);
    api->c_button(ctx, btn_a.c_str(), ("pin:m:" + st.sid + ":audio").c_str(), 0);

    if (st.mode != "audio") {
        std::string btn_best = (st.quality == "best" ? "✅ " : "") + std::string("🔥 Макс.");
        std::string btn_720  = (st.quality == "720"  ? "✅ " : "") + std::string("📺 720p");
        std::string btn_480  = (st.quality == "480"  ? "✅ " : "") + std::string("📱 480p");
        api->c_button(ctx, btn_best.c_str(), ("pin:q:" + st.sid + ":best").c_str(), 1);
        api->c_button(ctx, btn_720.c_str(),  ("pin:q:" + st.sid + ":720").c_str(), 0);
        api->c_button(ctx, btn_480.c_str(),  ("pin:q:" + st.sid + ":480").c_str(), 0);
    }

    api->c_button(ctx, "⬇️ Скачать", ("pin:dl:" + st.sid).c_str(), 1);
    api->c_button(ctx, "❌ Отмена",  ("pin:c:"  + st.sid).c_str(), 0);
}

struct MediaInfo {
    std::string direct_url;
    std::string fallback_url;
    bool is_video = false;
    std::string title;
};

static bool extract_video_from_vlist(const nlohmann::json& vlist, const std::string& quality, MediaInfo& info) {
    if (!vlist.is_object()) return false;
    std::string best_mp4, q720_mp4, q480_mp4, hls_url;
    int best_w = -1;

    for (auto it = vlist.begin(); it != vlist.end(); ++it) {
        if (!it.value().is_object()) continue;
        if (!it.value().contains("url") || !it.value()["url"].is_string()) continue;
        std::string u = it.value()["url"].get<std::string>();
        std::string k = it.key();
        if (u.find(".mp4") != std::string::npos) {
            int w = it.value().value("width", 0);
            if (k.find("720") != std::string::npos || u.find("/720p/") != std::string::npos) q720_mp4 = u;
            else if (k.find("480") != std::string::npos || u.find("/480p/") != std::string::npos) q480_mp4 = u;
            if (best_mp4.empty() || w > best_w) {
                best_mp4 = u;
                best_w = w;
            }
        } else if (u.find(".m3u8") != std::string::npos && hls_url.empty()) {
            hls_url = u;
        }
    }

    if (quality == "480" && !q480_mp4.empty()) info.direct_url = q480_mp4;
    else if (quality == "720" && !q720_mp4.empty()) info.direct_url = q720_mp4;
    else if (!best_mp4.empty()) info.direct_url = best_mp4;

    if (!info.direct_url.empty()) {
        info.fallback_url = !q720_mp4.empty() ? q720_mp4 : best_mp4;
        info.is_video = true;
        return true;
    }

    if (!hls_url.empty()) {
        std::string u = hls_url;
        size_t hpos = u.find("/hls/");
        if (hpos != std::string::npos) {
            u.replace(hpos, 5, (quality == "480") ? "/480p/" : "/720p/");
            size_t mpos = u.rfind(".m3u8");
            if (mpos != std::string::npos) u.replace(mpos, 5, ".mp4");
            info.direct_url = u;
            info.fallback_url = hls_url;
        } else {
            info.direct_url = hls_url;
        }
        info.is_video = true;
        return true;
    }
    return false;
}

static bool extract_from_pinresource_json(const std::string& json_str, const std::string& quality, MediaInfo& info) {
    if (json_str.empty()) return false;
    try {
        auto j = nlohmann::json::parse(json_str);
        if (!j.contains("resource_response") || !j["resource_response"].contains("data")) return false;
        const auto& pdata = j["resource_response"]["data"];
        if (!pdata.is_object()) return false;

        if (pdata.contains("title") && pdata["title"].is_string() && !pdata["title"].get<std::string>().empty()) {
            info.title = pdata["title"].get<std::string>();
        } else if (pdata.contains("description") && pdata["description"].is_string() && !pdata["description"].get<std::string>().empty()) {
            info.title = pdata["description"].get<std::string>();
        } else if (pdata.contains("grid_title") && pdata["grid_title"].is_string()) {
            info.title = pdata["grid_title"].get<std::string>();
        }

        if (pdata.contains("videos") && pdata["videos"].is_object() &&
            pdata["videos"].contains("video_list") && extract_video_from_vlist(pdata["videos"]["video_list"], quality, info)) {
            return true;
        }

        if (pdata.contains("story_pin_data") && pdata["story_pin_data"].is_object() &&
            pdata["story_pin_data"].contains("pages") && pdata["story_pin_data"]["pages"].is_array()) {
            for (const auto& page : pdata["story_pin_data"]["pages"]) {
                if (!page.is_object() || !page.contains("blocks") || !page["blocks"].is_array()) continue;
                for (const auto& blk : page["blocks"]) {
                    if (!blk.is_object()) continue;
                    if (blk.contains("video") && blk["video"].is_object() &&
                        blk["video"].contains("video_list") &&
                        extract_video_from_vlist(blk["video"]["video_list"], quality, info)) {
                        return true;
                    }
                    if (blk.contains("image") && blk["image"].is_object() &&
                        blk["image"].contains("images") && blk["image"]["images"].is_object()) {
                        const auto& imgs = blk["image"]["images"];
                        const char* keys[] = { "originals", "orig", "1200x", "736x", "564x" };
                        for (const char* k : keys) {
                            if (imgs.contains(k) && imgs[k].is_object() && imgs[k].contains("url") && imgs[k]["url"].is_string()) {
                                info.direct_url = imgs[k]["url"].get<std::string>();
                                info.is_video = false;
                                return true;
                            }
                        }
                    }
                }
            }
        }

        if (pdata.contains("images") && pdata["images"].is_object()) {
            const auto& imgs = pdata["images"];
            std::string fb;
            if (imgs.contains("736x") && imgs["736x"].is_object() && imgs["736x"].contains("url") && imgs["736x"]["url"].is_string()) {
                fb = imgs["736x"]["url"].get<std::string>();
            }
            if (imgs.contains("orig") && imgs["orig"].is_object() && imgs["orig"].contains("url") && imgs["orig"]["url"].is_string()) {
                info.direct_url = imgs["orig"]["url"].get<std::string>();
                info.fallback_url = fb;
                info.is_video = false;
                return true;
            }
            if (!fb.empty()) {
                info.direct_url = fb;
                info.is_video = false;
                return true;
            }
        }
    } catch (...) {}
    return false;
}

static bool extract_from_widgets_json(const std::string& json_str, const std::string& quality, MediaInfo& info) {
    if (json_str.empty()) return false;
    try {
        auto j = nlohmann::json::parse(json_str);
        if (!j.contains("data") || !j["data"].is_array() || j["data"].empty()) return false;
        const auto& pin = j["data"][0];
        if (!pin.is_object()) return false;

        if (info.title.empty() && pin.contains("description") && pin["description"].is_string()) {
            info.title = decode_html_entities(pin["description"].get<std::string>());
        }
        if (pin.contains("videos") && pin["videos"].is_object() &&
            pin["videos"].contains("video_list") && extract_video_from_vlist(pin["videos"]["video_list"], quality, info)) {
            return true;
        }
        if (pin.contains("images") && pin["images"].is_object()) {
            const auto& imgs = pin["images"];
            const char* keys[] = { "orig", "736x", "564x", "474x", "236x" };
            for (const char* k : keys) {
                if (imgs.contains(k) && imgs[k].is_object() && imgs[k].contains("url") && imgs[k]["url"].is_string()) {
                    std::string u = imgs[k]["url"].get<std::string>();
                    if (!is_sprite_or_avatar(u)) {
                        info.fallback_url = u;
                        info.direct_url = upgrade_to_originals(u);
                        info.is_video = false;
                        return true;
                    }
                }
            }
        }
    } catch (...) {}
    return false;
}

static std::string http_get_str(koto_ctx* ctx, const koto_api* api, const std::string& url, size_t max_bytes) {
    if (!api || !api->http_get || max_bytes == 0) return "";
    std::vector<unsigned char> buf(max_bytes + 1, 0);
    long long n = api->http_get(ctx, url.c_str(), buf.data(), static_cast<int>(max_bytes));
    if (n <= 0) return "";
    size_t copy_len = std::min<size_t>(static_cast<size_t>(n), max_bytes);
    return std::string(reinterpret_cast<const char*>(buf.data()), copy_len);
}

static MediaInfo resolve_pinterest_media(koto_ctx* ctx, const koto_api* api, PinSession& st) {
    MediaInfo info;
    if (!api || !api->http_get) return info;

    std::string pid = st.pin_id;
    std::string html;

    // Если это короткая ссылка pin.it — разрешаем редирект для получения pin_id (и заодно HTML)
    std::string slug = extract_pinit_slug(st.original_url);
    if (pid.empty()) {
        std::string fetch_url = !slug.empty()
            ? ("https://api.pinterest.com/url_shortener/" + slug + "/redirect/")
            : st.original_url;
        html = http_get_str(ctx, api, fetch_url, 512 * 1024);
        if (!html.empty()) {
            pid = extract_pin_id(html);
            if (!pid.empty()) st.pin_id = pid;
        }
        if (pid.empty() && !slug.empty() && fetch_url != st.original_url) {
            std::string fb_html = http_get_str(ctx, api, st.original_url, 512 * 1024);
            if (!fb_html.empty()) {
                pid = extract_pin_id(fb_html);
                if (!pid.empty()) st.pin_id = pid;
                if (html.empty()) html = std::move(fb_html);
            }
        }
    }

    // Stage A: Быстрый PinResource JSON API (~16 КБ вместо 1.3 МБ HTML)
    if (!pid.empty()) {
        std::string api_url =
            "https://www.pinterest.com/resource/PinResource/get/?data=%7B%22options%22%3A%7B%22field_set_key%22%3A%22unauth_react_main_pin%22%2C%22id%22%3A%22" +
            pid + "%22%7D%7D";
        std::string json_str = http_get_str(ctx, api, api_url, 512 * 1024);
        if (!json_str.empty() && extract_from_pinresource_json(json_str, st.quality, info)) {
            return info;
        }
    }

    // Stage B: Прямой линейный парсинг чистой HTML-страницы пина
    if (!pid.empty() && (html.empty() || html.find("i.pinimg.com") == std::string::npos)) {
        std::string direct_pin_url = "https://www.pinterest.com/pin/" + pid + "/";
        std::string h2 = http_get_str(ctx, api, direct_pin_url, 2 * 1024 * 1024);
        if (!h2.empty()) {
            html = std::move(h2);
        }
    }

    if (!html.empty()) {
        if (info.title.empty()) info.title = find_title_in_html(html);

        std::string mp4 = find_pinimg_video(html, ".mp4");
        if (!mp4.empty()) {
            if (st.quality == "480") {
                size_t p720 = mp4.find("/720p/");
                if (p720 != std::string::npos) mp4.replace(p720, 6, "/480p/");
            }
            info.direct_url = mp4;
            info.is_video = true;
            return info;
        }

        std::string hls = find_pinimg_video(html, ".m3u8");
        if (!hls.empty()) {
            std::string conv = hls;
            size_t hpos = conv.find("/hls/");
            if (hpos != std::string::npos) {
                conv.replace(hpos, 5, (st.quality == "480") ? "/480p/" : "/720p/");
                size_t mpos = conv.rfind(".m3u8");
                if (mpos != std::string::npos) conv.replace(mpos, 5, ".mp4");
                info.direct_url = conv;
                info.fallback_url = hls;
            } else {
                info.direct_url = hls;
            }
            info.is_video = true;
            return info;
        }

        std::string pre = find_preload_image(html);
        if (!pre.empty()) {
            info.fallback_url = pre;
            info.direct_url = upgrade_to_originals(pre);
            info.is_video = false;
            return info;
        }

        std::string og = find_og_image(html);
        if (!og.empty()) {
            info.fallback_url = og;
            info.direct_url = upgrade_to_originals(og);
            info.is_video = false;
            return info;
        }

        std::string img = find_pinimg_image_linear(html);
        if (!img.empty()) {
            info.fallback_url = img;
            info.direct_url = upgrade_to_originals(img);
            info.is_video = false;
            return info;
        }
    }

    // Stage C: Резервный widgets.pinterest.com JSON API
    if (!pid.empty()) {
        std::string w_url = "https://widgets.pinterest.com/v3/pidgets/pins/info/?pin_ids=" + pid;
        std::string wstr = http_get_str(ctx, api, w_url, 256 * 1024);
        if (!wstr.empty() && extract_from_widgets_json(wstr, st.quality, info)) {
            return info;
        }

        // Stage D: Резервный oembed.json
        std::string o_url = "https://www.pinterest.com/oembed.json?url=https://www.pinterest.com/pin/" + pid + "/";
        std::string ostr = http_get_str(ctx, api, o_url, 64 * 1024);
        if (!ostr.empty()) {
            try {
                auto oj = nlohmann::json::parse(ostr);
                if (info.title.empty() && oj.contains("title") && oj["title"].is_string()) {
                    info.title = oj["title"].get<std::string>();
                }
                if (oj.contains("thumbnail_url") && oj["thumbnail_url"].is_string()) {
                    std::string tu = oj["thumbnail_url"].get<std::string>();
                    if (!tu.empty() && !is_sprite_or_avatar(tu)) {
                        info.fallback_url = tu;
                        info.direct_url = upgrade_to_originals(tu);
                        info.is_video = false;
                        return info;
                    }
                }
            } catch (...) {}
        }
    }

    return info;
}

static void cmd_pin(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    long long owner_id = api->get_me ? api->get_me(ctx) : 0;
    if (owner_id == 0) owner_id = api->sender_id ? api->sender_id(ctx) : 0;

    const char* args = api->cmd_args(ctx);
    std::string text_to_check = args ? args : "";
    std::string url = find_pin_url(text_to_check);

    if (url.empty()) {
        const char* rep = api->reply_text ? api->reply_text(ctx) : nullptr;
        if (rep) url = find_pin_url(rep);
    }

    if (url.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Укажите ссылку на пин или ответьте на сообщение со ссылкой.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    std::string sid = std::to_string(std::time(nullptr)) + "_" + std::to_string(std::rand() % 9000 + 1000);
    PinSession st;
    st.sid = sid;
    st.original_url = url;
    st.pin_id = extract_pin_id(url);
    st.mode = "video";
    st.quality = "best";
    st.chat_id = cid;
    st.owner_id = owner_id;
    st.created_at = std::time(nullptr);

    {
        std::lock_guard<std::mutex> lk(g_pin_mu);
        auto it = g_pin_sessions.begin();
        while (it != g_pin_sessions.end()) {
            if (st.created_at - it->second.created_at > 3600) it = g_pin_sessions.erase(it);
            else ++it;
        }
        g_pin_sessions[sid] = st;
    }

    render_menu(ctx, api, st);
    api->c_edit(ctx, 0);
}

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    const char* raw_data = api->cb_data ? api->cb_data(ctx) : nullptr;
    if (!raw_data || std::strncmp(raw_data, "pin:", 4) != 0) return;

    std::string data = raw_data;
    long long cid = api->chat_id(ctx);
    long long mid = api->cb_message_id ? api->cb_message_id(ctx) : 0;

    // pin:c:<sid>
    if (data.rfind("pin:c:", 0) == 0) {
        std::string sid = data.substr(6);
        {
            std::lock_guard<std::mutex> lk(g_pin_mu);
            g_pin_sessions.erase(sid);
        }
        if (api->cb_answer) api->cb_answer(ctx, "Отменено", 0);
        if (mid > 0) api->delete_message(ctx, cid, mid);
        return;
    }

    // pin:m:<sid>:<mode>
    if (data.rfind("pin:m:", 0) == 0) {
        std::string rest = data.substr(6);
        size_t colon = rest.find(':');
        if (colon != std::string::npos) {
            std::string sid = rest.substr(0, colon);
            std::string mode = rest.substr(colon + 1);

            PinSession st;
            bool found = false;
            {
                std::lock_guard<std::mutex> lk(g_pin_mu);
                auto it = g_pin_sessions.find(sid);
                if (it != g_pin_sessions.end()) {
                    it->second.mode = mode;
                    st = it->second;
                    found = true;
                }
            }
            if (found) {
                if (api->cb_answer) api->cb_answer(ctx, "Формат обновлён", 0);
                render_menu(ctx, api, st);
                api->c_edit_message(ctx, cid, mid, 0);
            } else {
                if (api->cb_answer) api->cb_answer(ctx, "⚠️ Сессия устарела", 1);
            }
        }
        return;
    }

    // pin:q:<sid>:<quality>
    if (data.rfind("pin:q:", 0) == 0) {
        std::string rest = data.substr(6);
        size_t colon = rest.find(':');
        if (colon != std::string::npos) {
            std::string sid = rest.substr(0, colon);
            std::string qual = rest.substr(colon + 1);

            PinSession st;
            bool found = false;
            {
                std::lock_guard<std::mutex> lk(g_pin_mu);
                auto it = g_pin_sessions.find(sid);
                if (it != g_pin_sessions.end()) {
                    it->second.quality = qual;
                    st = it->second;
                    found = true;
                }
            }
            if (found) {
                if (api->cb_answer) api->cb_answer(ctx, "Качество обновлено", 0);
                render_menu(ctx, api, st);
                api->c_edit_message(ctx, cid, mid, 0);
            } else {
                if (api->cb_answer) api->cb_answer(ctx, "⚠️ Сессия устарела", 1);
            }
        }
        return;
    }

    // pin:dl:<sid>
    if (data.rfind("pin:dl:", 0) == 0) {
        std::string sid = data.substr(7);
        PinSession st;
        bool found = false;
        {
            std::lock_guard<std::mutex> lk(g_pin_mu);
            auto it = g_pin_sessions.find(sid);
            if (it != g_pin_sessions.end()) {
                st = it->second;
                found = true;
            }
        }

        if (!found) {
            if (api->cb_answer) api->cb_answer(ctx, "⚠️ Сессия устарела. Отправьте .pin заново.", 1);
            return;
        }

        long long target_cid = (st.chat_id != 0) ? st.chat_id : cid;
        if (target_cid == 0 && cid != 0) target_cid = cid;

        // Отправляем toast в Telegram сразу же, чтобы убрать крутящийся спиннер с кнопки
        if (api->cb_answer) api->cb_answer(ctx, "⏳ Скачиваю медиа...", 0);

        // Этап 1/3: Поиск медиа
        api->c_reset(ctx);
        api->c_emoji(ctx, "📌", PAW_ID);
        api->c_fmt(ctx, " Pinterest Downloader\n\n", KOTO_ENT_BOLD);
        api->c_emoji(ctx, "⏳", INFO_ID);
        api->c_fmt(ctx, " Этап 1/3: 🔍 Получение ссылки...", KOTO_ENT_CODE);
        api->c_edit_message(ctx, target_cid, mid, 0);

        MediaInfo info = resolve_pinterest_media(ctx, api, st);
        if (info.direct_url.empty()) {
            api->c_reset(ctx);
            api->c_fmt(ctx, "❌ Не удалось извлечь медиа из ссылки Pinterest.", KOTO_ENT_BOLD);
            api->c_edit_message(ctx, target_cid, mid, 0);
            return;
        }

        // Этап 2/3: Загрузка медиафайла
        api->c_reset(ctx);
        api->c_emoji(ctx, "📌", PAW_ID);
        api->c_fmt(ctx, " Pinterest Downloader\n\n", KOTO_ENT_BOLD);
        api->c_emoji(ctx, "📥", INFO_ID);
        api->c_fmt(ctx, info.is_video ? " Этап 2/3: 📥 Скачивание видео..." : " Этап 2/3: 📥 Скачивание фото...", KOTO_ENT_CODE);
        api->c_edit_message(ctx, target_cid, mid, 0);

        char cdir[512] = "/data/local/tmp";
        if (api->cache_dir) api->cache_dir(ctx, cdir, sizeof(cdir));

        std::string base_path = std::string(cdir) + "/pin_" + st.sid + "_" + std::to_string(std::time(nullptr));
        std::string ext = (st.mode == "audio" && info.is_video)
            ? ".m4a"
            : extract_ext_from_url(info.direct_url, info.is_video ? ".mp4" : ".jpg");
        std::string local_path = base_path + ext;

        int ok = api->download_file ? api->download_file(ctx, info.direct_url.c_str(), local_path.c_str()) : 0;
        if (!ok && !info.fallback_url.empty() && info.fallback_url != info.direct_url) {
            std::string fb_ext = (st.mode == "audio" && info.is_video)
                ? ".m4a"
                : extract_ext_from_url(info.fallback_url, info.is_video ? ".mp4" : ".jpg");
            local_path = base_path + "_fb" + fb_ext;
            ok = api->download_file(ctx, info.fallback_url.c_str(), local_path.c_str());
        }
        if (!ok) {
            api->c_reset(ctx);
            api->c_fmt(ctx, "❌ Ошибка при скачивании файла с сервера Pinterest.", KOTO_ENT_BOLD);
            api->c_edit_message(ctx, target_cid, mid, 0);
            return;
        }

        // Этап 3/3: Отправка файла в чат
        api->c_reset(ctx);
        api->c_emoji(ctx, "📌", PAW_ID);
        api->c_fmt(ctx, " Pinterest Downloader\n\n", KOTO_ENT_BOLD);
        api->c_emoji(ctx, "📤", INFO_ID);
        api->c_fmt(ctx, " Этап 3/3: 📤 Отправка в чат...", KOTO_ENT_CODE);
        api->c_edit_message(ctx, target_cid, mid, 0);

        api->c_reset(ctx);
        api->c_emoji(ctx, "📌", PAW_ID);
        std::string cap = " " + (info.title.empty() ? "Pinterest" : info.title);
        if (st.mode == "audio" && info.is_video) cap = " 🎵 " + (info.title.empty() ? "Pinterest Audio" : info.title);
        api->c_fmt(ctx, cap.c_str(), KOTO_ENT_BOLD);

        int send_flags = 0;
        if (st.mode == "audio" && info.is_video) send_flags = KOTO_SEND_AS_FILE;
        api->c_send_file(ctx, target_cid, local_path.c_str(), send_flags);

        // Удаляем временное меню, заменяя его отправленным файлом
        if (mid > 0) api->delete_message(ctx, target_cid, mid);
        api->c_reset(ctx);
        api->c_emoji(ctx, "✅", OK_ID);
        api->c_fmt(ctx, " Отправлено!", KOTO_ENT_BOLD);
        api->c_edit_message(ctx, target_cid, mid, 0);
        if (api->cb_answer) api->cb_answer(ctx, "✅ Готово!", 0);

        {
            std::lock_guard<std::mutex> lk(g_pin_mu);
            g_pin_sessions.erase(sid);
        }
        return;
    }
}

static const koto_command COMMANDS[] = {
    { "pin",       &cmd_pin, KOTO_LEVEL_TRUSTED, "[ru] Скачать медиа из Pinterest с выбором качества. [en] Download Pinterest media.", "<ссылка или реплай>" },
    { "pindl",     &cmd_pin, KOTO_LEVEL_TRUSTED, "[ru] Алиас для команды .pin. [en] Alias for .pin.", "<ссылка или реплай>" },
    { "pinterest", &cmd_pin, KOTO_LEVEL_TRUSTED, "[ru] Алиас для команды .pin. [en] Alias for .pin.", "<ссылка или реплай>" },
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "pinterest_dl",
    "[ru] Скачивает медиа из Pinterest (видео, GIF, звук, фото) с инлайн-меню. [en] Downloads Pinterest media (video, GIF, audio, photo) with inline menu.",
    "1.1.0",
    0,
    KOTO_MOD_TWINS,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    /*on_watch=*/nullptr,
    /*watch_flags=*/0,
    /*on_callback=*/&on_callback,
    /*settings=*/nullptr,
    /*settings_count=*/0,
    /*update_url=*/"https://github.com/AresUser1/KoteModules/blob/main/src/pinterest_dl.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register(void) {
    return &MODULE;
}
