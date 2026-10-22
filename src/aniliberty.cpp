// aniliberty.cpp — Поиск и получение случайного аниме с базы AniLiberty (C++ ABI 3).
// Портирован из Python-модуля aniliberty.py (v1.0.6).
// Автор: Archquise (ported by Kote)

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <sstream>

namespace {

static const std::vector<std::string> API_HOSTS = {
    "https://aniliberty.top/api/v1",
    "https://anilibria.top/api/v1"
};

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

static std::string api_get(const koto_api* api, koto_ctx* ctx, const std::string& path_and_query) {
    static const std::vector<std::string> headers = {
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/129.0.0.0 Safari/537.36",
        "Accept: application/json, text/plain, */*",
        "Accept-Language: ru-RU,ru;q=0.9,en-US;q=0.8,en;q=0.7"
    };
    for (const auto& base : API_HOSTS) {
        std::string url = base + path_and_query;
        std::string res = koto_utils::http_get(api, ctx, url, headers, 8);
        if (!res.empty()) {
            return res;
        }
    }
    return "";
}

// Получение JSON по ID релиза
static std::string fetch_title_by_id(const koto_api* api, koto_ctx* ctx, long long release_id) {
    std::string path = "/anime/releases/" + std::to_string(release_id) +
        "?include=id%2Cgenres.name%2Cname.main%2Cis_ongoing%2Ctype.description%2C"
        "description%2Cadded_in_users_favorites%2Calias%2Cposter.preview%2Cposter.thumbnail";
    return api_get(api, ctx, path);
}

// Получение случайного тайтла
static std::string fetch_random_title(const koto_api* api, koto_ctx* ctx) {
    std::string path = "/anime/releases/random?limit=1&include=id";
    std::string rand_json = api_get(api, ctx, path);
    if (rand_json.empty()) return "";

    try {
        auto j = nlohmann::json::parse(rand_json);
        if (j.is_array() && !j.empty() && j[0].contains("id")) {
            long long id = j[0]["id"].get<long long>();
            return fetch_title_by_id(api, ctx, id);
        }
    } catch (...) {}

    return "";
}

// Поиск тайтлов
static std::string search_titles(const koto_api* api, koto_ctx* ctx, const std::string& query) {
    std::string enc = koto_utils::url_encode(query);
    std::string path = "/app/search/releases?query=" + enc +
        "&include=id%2Cname.main%2Cis_ongoing%2Ctype.description%2Cdescription%2C"
        "added_in_users_favorites%2Calias%2Cposter.preview%2Cposter.thumbnail";
    return api_get(api, ctx, path);
}

// Отрисовка карточки аниме
static bool render_anime_card(koto_ctx* ctx, const koto_api* api, const std::string& json_str) {
    if (json_str.empty()) return false;

    try {
        auto j = nlohmann::json::parse(json_str);

        // Имя
        std::string main_name = "Аниме";
        if (j.contains("name") && j["name"].contains("main")) {
            main_name = j["name"]["main"].get<std::string>();
        }

        // Онгоинг
        bool is_ongoing = false;
        if (j.contains("is_ongoing") && j["is_ongoing"].is_boolean()) {
            is_ongoing = j["is_ongoing"].get<bool>();
        }

        // Тип
        std::string type_desc = "TV";
        if (j.contains("type") && j["type"].contains("description")) {
            type_desc = j["type"]["description"].get<std::string>();
        }

        // Жанры
        std::string genres_str = "Нет";
        if (j.contains("genres") && j["genres"].is_array() && !j["genres"].empty()) {
            genres_str.clear();
            for (size_t i = 0; i < j["genres"].size(); ++i) {
                if (i > 0) genres_str += ", ";
                genres_str += j["genres"][i]["name"].get<std::string>();
            }
        }

        // Описание
        std::string desc = "Нет описания";
        if (j.contains("description") && !j["description"].is_null()) {
            desc = j["description"].get<std::string>();
            if (desc.size() > 400) {
                desc = desc.substr(0, 400) + "...";
            }
        }

        // Избранное
        int fav_count = 0;
        if (j.contains("added_in_users_favorites") && j["added_in_users_favorites"].is_number()) {
            fav_count = j["added_in_users_favorites"].get<int>();
        }

        // Постер
        std::string poster_url;
        if (j.contains("poster") && j["poster"].contains("preview") && !j["poster"]["preview"].is_null()) {
            std::string ppath = j["poster"]["preview"].get<std::string>();
            if (ppath.rfind("http", 0) == 0) {
                poster_url = ppath;
            } else {
                poster_url = "https://anilibria.top" + ppath;
            }
        }

        // Ссылка на просмотр (alias или id)
        std::string alias;
        if (j.contains("alias") && !j["alias"].is_null()) {
            alias = j["alias"].get<std::string>();
        } else if (j.contains("id") && j["id"].is_number()) {
            alias = std::to_string(j["id"].get<long long>());
        }

        std::string web_url = !alias.empty()
            ? ("https://aniliberty.top/anime/releases/release/" + alias + "/episodes")
            : "https://aniliberty.top";

        api->c_reset(ctx);

        // Название
        api->c_fmt(ctx, (main_name + "\n").c_str(), KOTO_ENT_BOLD);

        // Онгоинг
        api->c_fmt(ctx, "Онгоинг: ", KOTO_ENT_BOLD);
        api->c_text(ctx, is_ongoing ? "Да\n\n" : "Нет\n\n");

        // Тип
        api->c_fmt(ctx, "Тип: ", KOTO_ENT_BOLD);
        api->c_text(ctx, (type_desc + "\n").c_str());

        // Жанры
        api->c_fmt(ctx, "Жанры: ", KOTO_ENT_BOLD);
        api->c_text(ctx, (genres_str + "\n\n").c_str());

        // Описание
        api->c_fmt(ctx, (desc + "\n\n").c_str(), KOTO_ENT_CODE);

        // Избранное
        api->c_fmt(ctx, "Избранное ❤️: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (std::to_string(fav_count) + "\n").c_str(), KOTO_ENT_CODE);

        // Баннер превью картинки (если хост поддерживает)
        if (api->c_banner && !poster_url.empty()) {
            api->c_banner(ctx, poster_url.c_str(), KOTO_BANNER_LARGE);
        }

        // Покрашенные инлайн кнопки (KOTO_BTN_PRIMARY, KOTO_BTN_SUCCESS, KOTO_BTN_DANGER)
        // 1. Прямая ссылка на сайт AniLiberty (синяя кнопка)
        koto_utils::add_button(api, ctx, "🌐 Смотреть на сайте", web_url, /*new_row=*/true, KOTO_BTN_PRIMARY);

        // 2. Обновить / случайное аниме (зелёная кнопка)
        koto_utils::add_button(api, ctx, "🔃 Обновить", "anilib_random", /*new_row=*/true, KOTO_BTN_SUCCESS);

        // 3. Закрыть сообщение (красная кнопка)
        koto_utils::add_button(api, ctx, "🚫 Закрыть", "anilib_close", /*new_row=*/false, KOTO_BTN_DANGER);

        return true;
    } catch (...) {
        return false;
    }
}

// ── .arandom ──────────────────────────────────────────────────────────────────

static void cmd_arandom(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    api->c_reset(ctx);
    api->c_text(ctx, "🎲 ");
    api->c_fmt(ctx, "Ищу случайное аниме...", KOTO_ENT_BOLD);
    send_result(ctx, api);

    std::string data = fetch_random_title(api, ctx);
    if (!render_anime_card(ctx, api, data)) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Ошибка API AniLiberty: сервер недоступен или тайтл не найден.\n> Проверьте интернет / DNS / VPN на устройстве.");
    }
    send_result(ctx, api);
}

// ── .anilib_search <query> ───────────────────────────────────────────────────

static void cmd_anilib_search(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string query = raw ? koto_utils::trim(raw) : "";

    if (query.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Укажите название аниме для поиска.");
        send_result(ctx, api);
        return;
    }

    api->c_reset(ctx);
    api->c_text(ctx, "🔍 ");
    api->c_fmt(ctx, "Ищу: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, query.c_str(), KOTO_ENT_CODE);
    send_result(ctx, api);

    std::string search_json = search_titles(api, ctx, query);
    bool found = false;

    try {
        if (!search_json.empty()) {
            auto j = nlohmann::json::parse(search_json);
            if (j.is_array() && !j.empty() && j[0].contains("id")) {
                long long id = j[0]["id"].get<long long>();
                std::string full_data = fetch_title_by_id(api, ctx, id);
                found = render_anime_card(ctx, api, full_data);
            }
        }
    } catch (...) {}

    if (!found) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ По запросу ");
        api->c_fmt(ctx, query.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, " ничего не найдено или сервер недоступен.");
    }

    send_result(ctx, api);
}

// ── .aniliberty [запрос] ──────────────────────────────────────────────────────

static void cmd_aniliberty(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    const char* raw = api->cmd_args(ctx);
    std::string query = raw ? koto_utils::trim(raw) : "";
    if (query.empty()) {
        cmd_arandom(ctx, api);
    } else {
        cmd_anilib_search(ctx, api);
    }
}

// ── Инлайн коллбэки ───────────────────────────────────────────────────────────

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    const char* data = api->cb_data ? api->cb_data(ctx) : nullptr;
    if (!data || strncmp(data, "anilib_", 7) != 0) return;

    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) {
        if (api->cb_answer) api->cb_answer(ctx, "❌ Нет доступа", 1);
        return;
    }
    std::string d = data;

    if (d == "anilib_close") {
        if (api->cb_answer) api->cb_answer(ctx, "Закрыто", 0);
        long long mid = api->cb_message_id ? api->cb_message_id(ctx) : 0;
        long long cid = api->chat_id ? api->chat_id(ctx) : 0;
        if (mid != 0 && cid != 0 && api->delete_message) {
            api->delete_message(ctx, cid, mid);
        } else if (api->delete_msg) {
            api->delete_msg(ctx);
        }
        return;
    }

    if (d == "anilib_random") {
        if (api->cb_answer) api->cb_answer(ctx, "Обновляю...", 0);
        std::string title_data = fetch_random_title(api, ctx);
        if (render_anime_card(ctx, api, title_data)) {
            api->c_edit(ctx, 0);
        }
        return;
    }

    if (d == "anilib_noop") {
        if (api->cb_answer) api->cb_answer(ctx, "Ссылка открывается на сайте", 0);
        return;
    }
}

} // namespace

static const koto_command COMMANDS[] = {
    {
        "aniliberty",
        &cmd_aniliberty,
        KOTO_LEVEL_TRUSTED,
        "AniLiberty: случайное аниме или поиск по названию.",
        "[название]"
    },
    {
        "arandom",
        &cmd_arandom,
        KOTO_LEVEL_TRUSTED,
        "Случайное аниме из базы AniLiberty.",
        nullptr
    },
    {
        "anilib_search",
        &cmd_anilib_search,
        KOTO_LEVEL_TRUSTED,
        "Поиск аниме по названию в базе AniLiberty.",
        "<название>"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "aniliberty",
    "Поиск и выбор случайного аниме из базы AniLiberty с постерами (C++ ABI 3).",
    "1.0.9",
    26,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    &on_callback,
    nullptr, 0,
    nullptr
};

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register(void) {
    return &MODULE;
}
