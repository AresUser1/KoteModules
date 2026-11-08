// youtube_loader.cpp — Загрузчик информации и ссылок на скачивание видео с YouTube (C++ ABI 3).
// Портирован из Python-модуля youtube-loader (by kote).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

namespace {

using json = nlohmann::json;

constexpr const char* VERSION = "1.0.0";

static std::string extract_youtube_url(const std::string& s) {
    size_t pos = s.find("youtube.com/");
    if (pos == std::string::npos) pos = s.find("youtu.be/");
    if (pos != std::string::npos) {
        size_t start = s.rfind("http", pos);
        if (start != std::string::npos) {
            size_t end = s.find_first_of(" \t\r\n", pos);
            if (end == std::string::npos) return s.substr(start);
            return s.substr(start, end - start);
        }
    }
    return "";
}

static std::string url_encode(const std::string& s) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : s) {
        if (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << ((int)(unsigned char)c);
        }
    }
    return escaped.str();
}

void cmd_dlvideo(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::string yt_url;

    const char* args = api->cmd_args ? api->cmd_args(ctx) : nullptr;
    if (args && args[0]) {
        yt_url = extract_youtube_url(args);
    }

    if (yt_url.empty() && api->reply_text) {
        const char* rep = api->reply_text(ctx);
        if (rep && rep[0]) {
            yt_url = extract_youtube_url(rep);
        }
    }

    if (yt_url.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Укажите ссылку на YouTube видео или ответьте на неё:", KOTO_ENT_BOLD);
        api->c_text(ctx, "\n• .dlv https://www.youtube.com/watch?v=...");
        api->c_text(ctx, "\n• .dlv https://youtu.be/...");
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    api->c_reset(ctx);
    api->c_fmt(ctx, "🎬 Получаю информацию о видео YouTube...", KOTO_ENT_ITALIC);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);

    std::string oembed_url = "https://www.youtube.com/oembed?url=" + url_encode(yt_url) + "&format=json";
    std::string resp = koto_utils::http_get(api, ctx, oembed_url);

    if (resp.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Не удалось получить информацию о видео с YouTube. Проверьте ссылку.", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    try {
        json j = json::parse(resp);
        std::string title = j.value("title", "YouTube Video");
        std::string author = j.value("author_name", "YouTube Channel");
        std::string author_url = j.value("author_url", "");
        std::string thumb = j.value("thumbnail_url", "");

        api->c_reset(ctx);
        api->c_fmt(ctx, "🎬 YouTube Loader (by kote)", KOTO_ENT_BOLD);
        api->c_text(ctx, "\n\n");

        api->c_fmt(ctx, title.c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, "\n👤 Автор: ");
        if (!author_url.empty()) {
            api->c_url(ctx, author.c_str(), author_url.c_str());
        } else {
            api->c_text(ctx, author.c_str());
        }

        api->c_text(ctx, "\n\n🔗 ");
        api->c_url(ctx, "Смотреть на YouTube", yt_url.c_str());

        if (!thumb.empty()) {
            api->c_text(ctx, "  |  🖼 ");
            api->c_url(ctx, "Обложка HQ", thumb.c_str());
        }

        api->c_text(ctx, "\n\n⬇️ ");
        std::string dl_link = "https://cobalt.tools/#" + yt_url;
        api->c_url(ctx, "Скачать видео / MP3 через Cobalt", dl_link.c_str());

        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);

    } catch (const std::exception& e) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Ошибка парсинга: ", KOTO_ENT_BOLD);
        api->c_text(ctx, e.what());
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    }
}

void cmd_dvlist(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    api->c_reset(ctx);
    api->c_fmt(ctx, "📋 Поддерживаемые сайты (YouTube Loader):", KOTO_ENT_BOLD);
    api->c_text(ctx, "\n• YouTube (видео, Shorts, плейлисты)\n• youtu.be короткие ссылки\n\nКоманды:\n• .dlvideo <url> — информация и скачивание\n• .dlv <url> — быстрый алиас\n• .dvlist — этот список");
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

void cmd_dlstop(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    api->c_reset(ctx);
    api->c_fmt(ctx, "⏹ Все активные загрузки остановлены.", KOTO_ENT_BOLD);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    (void)ctx; (void)api;
}

static const koto_command COMMANDS[] = {
    {"dlvideo", cmd_dlvideo, KOTO_LEVEL_TRUSTED, "[ru] Загрузка и инфо о YouTube видео по ссылке (by kote) [en] Download and info for YouTube video by link (by kote).", "<url>"},
    {"dlv", cmd_dlvideo, KOTO_LEVEL_TRUSTED, "[ru] Алиас для .dlvideo (by kote) [en] Alias for .dlvideo (by kote).", "<url>"},
    {"dvlist", cmd_dvlist, KOTO_LEVEL_TRUSTED, "[ru] Список команд и возможностей загрузчика (by kote) [en] List supported sites and commands (by kote).", nullptr},
    {"dlstop", cmd_dlstop, KOTO_LEVEL_TRUSTED, "[ru] Остановить загрузки (by kote) [en] Stop downloads (by kote).", nullptr}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "youtube_loader",
    "[ru] Загрузчик информации, обложек и ссылок на скачивание видео с YouTube (by kote). [en] YouTube video info, thumbnails, and downloader assistant (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    nullptr, 0,
    on_callback,
    nullptr, 0,
    "https://github.com/AresUser1/KoteModules/blob/main/src/youtube_loader.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
