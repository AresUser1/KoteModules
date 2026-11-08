// rule34.cpp — Поиск артов на Rule34 (C++ ABI 3).
// Портирован из Python-модуля rule34 (by kote).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <random>

namespace {

using json = nlohmann::json;

constexpr const char* VERSION = "1.0.0";

static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

static std::string url_encode(const std::string& value) {
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

void cmd_r34(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    if (api->is_outgoing(ctx)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "🔞 Ищу арты на Rule34…", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
    }

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";
    if (args.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Укажите теги для поиска: .r34 <теги>", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    std::string tag_query;
    for (char c : args) tag_query += (c == ' ') ? '+' : c;

    std::string url = "https://api.rule34.xxx/index.php?page=dapi&s=post&q=index&json=1&limit=25&tags=" + url_encode(tag_query);
    std::string resp = koto_utils::http_get(api, ctx, url);

    if (resp.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Сбой подключения к API Rule34.", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    try {
        json j = json::parse(resp);
        if (!j.is_array() || j.empty()) {
            api->c_reset(ctx);
            api->c_fmt(ctx, "🔍 По вашим тегам ничего не найдено.", KOTO_ENT_BOLD);
            if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
            else api->c_reply(ctx, 0);
            return;
        }

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> dist(0, j.size() - 1);
        const auto& post = j[dist(gen)];

        std::string file_url = post.value("file_url", "");
        int score = post.value("score", 0);
        std::string tags = post.value("tags", "");
        if (tags.size() > 100) tags = tags.substr(0, 97) + "…";

        api->c_reset(ctx);
        api->c_fmt(ctx, "🔞 Rule34 (by kote)\n\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "🏷️ Теги: ");
        api->c_fmt(ctx, tags.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n⭐ Оценка: ");
        api->c_fmt(ctx, std::to_string(score).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n\n🔗 ");
        api->c_url(ctx, "Открыть оригинал", file_url.c_str());

        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    } catch (...) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Ошибка парсинга ответа от Rule34.", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    }
}

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    (void)ctx; (void)api;
}

const koto_command COMMANDS[] = {
    {"r34", cmd_r34, KOTO_LEVEL_TRUSTED, "[ru] Поиск артов на Rule34 по тегам (by kote) [en] Search Rule34 art by tags (by kote).", "<теги>"}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "rule34",
    "[ru] Поиск артов и картинок на Rule34 (by kote). [en] Rule34 art searcher (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    nullptr, 0,
    on_callback,
    nullptr, 0,
    "https://github.com/AresUser1/KoteModules/blob/main/src/rule34.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
