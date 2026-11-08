// spotisaver.cpp — Модуль для информации и сохранения треков Spotify (C++ ABI 3).
// Портирован из Python-модуля SpotiSaver (by kote).
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

static std::string extract_spotify_url(const std::string& s) {
    size_t pos = s.find("open.spotify.com/track/");
    if (pos != std::string::npos) {
        size_t start = s.rfind("http", pos);
        if (start != std::string::npos) {
            size_t end = s.find_first_of(" \t\r\n?", pos);
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

void cmd_spotsave(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::string track_url;

    const char* args = api->cmd_args ? api->cmd_args(ctx) : nullptr;
    if (args && args[0]) {
        track_url = extract_spotify_url(args);
    }

    if (track_url.empty() && api->reply_text) {
        const char* rep = api->reply_text(ctx);
        if (rep && rep[0]) {
            track_url = extract_spotify_url(rep);
        }
    }

    if (track_url.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Укажите ссылку на трек Spotify или ответьте на сообщение со ссылкой:", KOTO_ENT_BOLD);
        api->c_text(ctx, "\n• .spotsave https://open.spotify.com/track/...");
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    api->c_reset(ctx);
    api->c_fmt(ctx, "🎧 Загружаю информацию о треке Spotify...", KOTO_ENT_ITALIC);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);

    std::string oembed_url = "https://open.spotify.com/oembed?url=" + url_encode(track_url);
    std::string resp = koto_utils::http_get(api, ctx, oembed_url);

    if (resp.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Не удалось получить данные трека от Spotify.", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    try {
        json j = json::parse(resp);
        std::string title = j.value("title", "Неизвестный трек");
        std::string thumb = j.value("thumbnail_url", "");

        api->c_reset(ctx);
        api->c_fmt(ctx, "🎵 Spotify Track (by kote)", KOTO_ENT_BOLD);
        api->c_text(ctx, "\n\n");

        api->c_text(ctx, "🎼 Название: ");
        api->c_fmt(ctx, title.c_str(), KOTO_ENT_BOLD);

        api->c_text(ctx, "\n\n🔗 ");
        api->c_url(ctx, "Открыть в Spotify", track_url.c_str());

        if (!thumb.empty()) {
            api->c_text(ctx, "  |  🖼 ");
            api->c_url(ctx, "Обложка трека", thumb.c_str());
        }

        api->c_text(ctx, "\n\n💡 ");
        api->c_fmt(ctx, "Для скачивания MP3 в чат отправьте:", KOTO_ENT_ITALIC);
        api->c_text(ctx, "\n");
        std::string download_cmd = ".m " + title;
        api->c_fmt(ctx, download_cmd.c_str(), KOTO_ENT_CODE);

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

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    (void)ctx; (void)api;
}

static const koto_command COMMANDS[] = {
    {"spotsave", cmd_spotsave, KOTO_LEVEL_TRUSTED, "[ru] Получить инфо и обложку трека Spotify с быстрой загрузкой (by kote) [en] Get Spotify track info and cover with quick download (by kote).", "<spotify_url>"},
    {"spoti", cmd_spotsave, KOTO_LEVEL_TRUSTED, "[ru] Алиас для .spotsave (by kote) [en] Alias for .spotsave (by kote).", "<spotify_url>"}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "spotisaver",
    "[ru] Загрузка информации, обложек и подготовка к скачиванию треков из Spotify (by kote). [en] Spotify track info, covers, and downloader assistant (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    nullptr, 0,
    on_callback,
    nullptr, 0,
    "https://github.com/AresUser1/KoteModules/blob/main/src/spotisaver.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
