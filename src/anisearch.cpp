// anisearch.cpp — Поиск аниме по кадру / скриншоту через Trace.moe (C++ ABI 3).
// Портирован из Python-модуля AniSearch (by kote).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

namespace {

using json = nlohmann::json;

constexpr const char* VERSION = "1.0.0";

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

static std::string format_time(double sec) {
    int total = (int)std::round(sec);
    int m = total / 60;
    int s = total % 60;
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return std::string(buf);
}

static std::string extract_url(const std::string& s) {
    size_t pos = s.find("http://");
    if (pos == std::string::npos) pos = s.find("https://");
    if (pos == std::string::npos) return "";

    size_t end = s.find_first_of(" \t\r\n", pos);
    if (end == std::string::npos) return s.substr(pos);
    return s.substr(pos, end - pos);
}

void cmd_anisearch(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::string img_url;

    const char* args = api->cmd_args ? api->cmd_args(ctx) : nullptr;
    if (args && args[0]) {
        img_url = extract_url(args);
    }

    if (img_url.empty() && api->reply_text) {
        const char* rep = api->reply_text(ctx);
        if (rep && rep[0]) {
            img_url = extract_url(rep);
        }
    }

    if (img_url.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Укажите ссылку на картинку или ответьте на сообщение со ссылкой:", KOTO_ENT_BOLD);
        api->c_text(ctx, "\n• .aser https://example.com/screenshot.jpg");
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    // Сообщаем о поиске
    api->c_reset(ctx);
    api->c_fmt(ctx, "🔍 Ищу аниме по скриншоту в Trace.moe...", KOTO_ENT_ITALIC);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);

    // Выполняем запрос к API Trace.moe
    std::string req_url = "https://api.trace.moe/search?anilistInfo&url=" + url_encode(img_url);
    std::string resp = koto_utils::http_get(api, ctx, req_url);

    if (resp.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Ошибка запроса к API Trace.moe. Проверьте ссылку на изображение.", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    try {
        json j = json::parse(resp);
        if (!j.contains("result") || !j["result"].is_array() || j["result"].empty()) {
            api->c_reset(ctx);
            api->c_fmt(ctx, "🗑 Не удалось определить аниме по данному скриншоту.", KOTO_ENT_BOLD);
            if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
            else api->c_reply(ctx, 0);
            return;
        }

        const auto& best = j["result"][0];
        double similarity = best.value("similarity", 0.0) * 100.0;
        int episode = best.value("episode", 1);
        double time_from = best.value("from", 0.0);
        double time_to = best.value("to", 0.0);
        std::string frame_img = best.value("image", "");
        std::string preview_video = best.value("video", "");

        std::string title = "Неизвестное аниме";
        std::string genres_str;
        std::string studio_str;

        if (best.contains("anilist") && best["anilist"].is_object()) {
            const auto& al = best["anilist"];
            if (al.contains("title") && al["title"].is_object()) {
                const auto& t = al["title"];
                if (t.contains("romaji") && !t["romaji"].is_null()) {
                    title = t["romaji"].get<std::string>();
                } else if (t.contains("native") && !t["native"].is_null()) {
                    title = t["native"].get<std::string>();
                } else if (t.contains("english") && !t["english"].is_null()) {
                    title = t["english"].get<std::string>();
                }
            }

            if (al.contains("genres") && al["genres"].is_array()) {
                for (size_t g = 0; g < al["genres"].size(); ++g) {
                    if (g > 0) genres_str += ", ";
                    genres_str += al["genres"][g].get<std::string>();
                }
            }

            if (al.contains("studios") && al["studios"].is_object()) {
                if (al["studios"].contains("edges") && al["studios"]["edges"].is_array() && !al["studios"]["edges"].empty()) {
                    const auto& edge = al["studios"]["edges"][0];
                    if (edge.contains("node") && edge["node"].contains("name")) {
                        studio_str = edge["node"]["name"].get<std::string>();
                    }
                }
            }
        }

        std::ostringstream sim_ss;
        sim_ss << std::fixed << std::setprecision(1) << similarity;

        api->c_reset(ctx);
        api->c_fmt(ctx, ("🎬 " + title).c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, "\n\n");

        api->c_text(ctx, "• Серия: ");
        api->c_fmt(ctx, std::to_string(episode).c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, "\n• Таймкод: ");
        api->c_fmt(ctx, (format_time(time_from) + " — " + format_time(time_to)).c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, "\n• Сходство: ");
        api->c_fmt(ctx, (sim_ss.str() + "%").c_str(), KOTO_ENT_BOLD);

        if (!genres_str.empty()) {
            api->c_text(ctx, "\n• Жанры: ");
            api->c_fmt(ctx, genres_str.c_str(), KOTO_ENT_ITALIC);
        }
        if (!studio_str.empty()) {
            api->c_text(ctx, "\n• Студия: ");
            api->c_text(ctx, studio_str.c_str());
        }

        api->c_text(ctx, "\n\n");
        if (!frame_img.empty()) {
            api->c_url(ctx, "🖼 Точный кадр", frame_img.c_str());
            api->c_text(ctx, "  ");
        }
        if (!preview_video.empty()) {
            api->c_url(ctx, "▶️ Видеофрагмент", preview_video.c_str());
        }

        api->c_text(ctx, "\n\n");
        api->c_fmt(ctx, "ℹ️ Найдено через trace.moe (by kote)", KOTO_ENT_ITALIC);

        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);

    } catch (const std::exception& e) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Ошибка обработки ответа: ", KOTO_ENT_BOLD);
        api->c_text(ctx, e.what());
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    }
}

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    (void)ctx; (void)api;
}

static const koto_command COMMANDS[] = {
    {"anisearch", cmd_anisearch, KOTO_LEVEL_TRUSTED, "[ru] Найти название и серию аниме по скриншоту кадра (by kote) [en] Search anime and episode by frame screenshot (by kote).", "<url|reply>"},
    {"aser", cmd_anisearch, KOTO_LEVEL_TRUSTED, "[ru] Алиас для .anisearch (by kote) [en] Alias for .anisearch (by kote).", "<url|reply>"}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "anisearch",
    "[ru] Поиск названия, серии и тайм-кода аниме по скриншоту кадра через Trace.moe (by kote). [en] Search anime title, episode, and timecode by screenshot via Trace.moe (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    nullptr, 0,
    on_callback,
    nullptr, 0,
    "https://github.com/AresUser1/KoteModules/blob/main/src/anisearch.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
