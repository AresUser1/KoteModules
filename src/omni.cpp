#include "module_abi.h"
#include "nlohmann/json.hpp"

#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <iomanip>

namespace {
using json = nlohmann::json;

constexpr const char* VERSION = "1.0.0";

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : value) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::uppercase << ((int)(unsigned char)c) << std::nouppercase;
        }
    }
    return escaped.str();
}

// ── Command: .omni / .dl ─────────────────────────────────────────────────────
void cmd_omni(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    if (api->is_outgoing(ctx)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "⏳ Анализирую медиа-ссылку…", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
    }

    const char* raw_args = api->cmd_args(ctx);
    std::string target_url = raw_args ? trim(raw_args) : "";

    if (target_url.empty()) {
        const char* rep = api->reply_text ? api->reply_text(ctx) : nullptr;
        if (rep) target_url = trim(rep);
    }

    if (target_url.empty() || (target_url.rfind("http://", 0) != 0 && target_url.rfind("https://", 0) != 0)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Укажите ссылку на видео/аудио: .omni <ссылка> (by kote)", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    // Call Cobalt API / media resolver
    std::string api_url = "https://api.cobalt.tools/";
    json req_body;
    req_body["url"] = target_url;
    req_body["videoQuality"] = "1080";
    req_body["audioFormat"] = "mp3";
    std::string post_data = req_body.dump();

    std::string headers = "Accept: application/json\r\nContent-Type: application/json\r\nUser-Agent: Mozilla/5.0";
    std::vector<unsigned char> resp(256 * 1024, 0);

    long long n = api->http_post ? api->http_post(ctx, api_url.c_str(), post_data.c_str(), headers.c_str(), resp.data(), (int)resp.size()) : -1;

    std::string download_url;
    std::string filename;

    if (n > 0) {
        try {
            json j = json::parse(std::string(reinterpret_cast<char*>(resp.data()), (size_t)n));
            std::string st = j.value("status", "");
            if (st == "stream" || st == "redirect" || st == "tunnel") {
                download_url = j.value("url", "");
                filename = j.value("filename", "media_file");
            } else if (j.contains("url")) {
                download_url = j.value("url", "");
            }
        } catch (...) {}
    }

    // Fallback: TikWM / universal extractor if cobalt failed
    if (download_url.empty() && target_url.find("tiktok.com") != std::string::npos) {
        std::string twm = "https://www.tikwm.com/api/?url=" + url_encode(target_url);
        std::vector<unsigned char> t_resp(256 * 1024, 0);
        long long tn = api->http_get ? api->http_get(ctx, twm.c_str(), t_resp.data(), (int)t_resp.size()) : -1;
        if (tn > 0) {
            try {
                json tj = json::parse(std::string(reinterpret_cast<char*>(t_resp.data()), (size_t)tn));
                if (tj.value("code", -1) == 0 && tj.contains("data")) {
                    download_url = tj["data"].value("play", "");
                    filename = tj["data"].value("title", "tiktok_video");
                }
            } catch (...) {}
        }
    }

    api->c_reset(ctx);
    if (!download_url.empty()) {
        api->c_fmt(ctx, "📥 OmniLoad — Медиа готово к скачиванию! (by kote)\n\n", KOTO_ENT_BOLD);
        if (!filename.empty()) {
            api->c_text(ctx, "🎬 Название: ");
            api->c_fmt(ctx, filename.c_str(), KOTO_ENT_CODE);
            api->c_text(ctx, "\n\n");
        }
        api->c_text(ctx, "⚡ Прямая ссылка: ");
        api->c_url(ctx, "Скачать файл", download_url.c_str());

        if (api->c_button) {
            api->c_button(ctx, "⬇️ Скачать видео", ("omni:dl:" + download_url).c_str(), 1);
        }
    } else {
        api->c_fmt(ctx, "❌ Не удалось извлечь прямое видео по этой ссылке.\nПроверьте доступность видео или попробуйте другой сервис. (by kote)", KOTO_ENT_BOLD);
    }

    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

const koto_command COMMANDS[] = {
    {"omni", cmd_omni, KOTO_LEVEL_TRUSTED, "[ru] Универсальное скачивание медиа по ссылке (by kote) [en] Universal media downloader (by kote).", "<url>"},
    {"dl", cmd_omni, KOTO_LEVEL_TRUSTED, "[ru] Алиас для .omni (by kote) [en] Alias for .omni (by kote).", "<url>"}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "omni",
    "[ru] Универсальный загрузчик видео и аудио OmniLoad (by kote). [en] Universal OmniLoad media downloader (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    nullptr, // on_watch
    0,
    nullptr, // on_callback
    nullptr,
    0,
    "https://github.com/AresUser1/KoteModules/blob/main/src/omni.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
