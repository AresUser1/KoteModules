// moomod.cpp — Модуль для взаимодействия с @moolokobot (MooBot).
// C++ ABI 3 port of moomod.py (v1.3.1).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include <string>
#include <chrono>

namespace {

static constexpr long long ICON_WAIT  = 5891211339170326418LL; // ⏳
static constexpr long long ICON_INFO  = 5393123749924984737LL; // 🐮
static constexpr long long ICON_ERROR = 6030563507299160824LL; // ⚠️

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

static void start_request(koto_ctx* ctx, const koto_api* api, const char* bot_cmd, const char* title) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    // 1. Показываем статус ожидания
    api->c_reset(ctx);
    api->c_emoji(ctx, "⏳", ICON_WAIT);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, (std::string("Запрос к ") + title + "...").c_str(), KOTO_ENT_BOLD);
    send_result(ctx, api);

    // 2. Сохраняем состояние в БД модуля
    long long cid = api->chat_id(ctx);
    long long mid = api->msg_id(ctx);
    api->db_set(ctx, "pending_chat_id", std::to_string(cid).c_str());
    api->db_set(ctx, "pending_msg_id", std::to_string(mid).c_str());
    api->db_set(ctx, "pending_title", title);

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    api->db_set(ctx, "pending_ts", std::to_string(now).c_str());

    // 3. Отправляем запрос боту
    long long bot_id = api->resolve ? api->resolve(ctx, "moolokobot") : 0;
    if (bot_id != 0) {
        api->db_set(ctx, "bot_id", std::to_string(bot_id).c_str());
        api->c_reset(ctx);
        api->c_text(ctx, bot_cmd);
        api->c_send(ctx, bot_id, 0);
    }
}

static void cmd_forest(koto_ctx* ctx, const koto_api* api) {
    start_request(ctx, api, "/forest", "Лес");
}

static void cmd_task(koto_ctx* ctx, const koto_api* api) {
    start_request(ctx, api, "/task", "Задание");
}

static void cmd_craft(koto_ctx* ctx, const koto_api* api) {
    start_request(ctx, api, "/craft", "Крафт");
}

static void cmd_moon(koto_ctx* ctx, const koto_api* api) {
    start_request(ctx, api, "муни", "Муни");
}

static void cmd_crystal(koto_ctx* ctx, const koto_api* api) {
    start_request(ctx, api, "мукри", "Кристаллы");
}

static void cmd_blood(koto_ctx* ctx, const koto_api* api) {
    start_request(ctx, api, "мукр", "Кровь");
}

static void cmd_profile(koto_ctx* ctx, const koto_api* api) {
    start_request(ctx, api, "бб", "Профиль");
}

static void cmd_donate(koto_ctx* ctx, const koto_api* api) {
    const char* raw = api->cmd_args(ctx);
    std::string arg = raw ? koto_utils::trim(raw) : "";
    std::string cmd = arg.empty() ? "мудон" : ("мудон " + arg);
    start_request(ctx, api, cmd.c_str(), "Донат");
}

static int on_watch(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) return 0;

    char buf[128];
    if (api->db_get(ctx, "pending_chat_id", buf, sizeof(buf)) <= 0) return 0;
    long long orig_chat = std::stoll(buf);

    if (api->db_get(ctx, "pending_msg_id", buf, sizeof(buf)) <= 0) return 0;
    long long orig_msg = std::stoll(buf);

    char title_buf[64] = "MooBot";
    api->db_get(ctx, "pending_title", title_buf, sizeof(title_buf));

    const char* text = api->msg_text(ctx);
    if (!text) return 0;

    // Проверяем таймаут ожидания (макс. 30 сек)
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    char ts_buf[64];
    if (api->db_get(ctx, "pending_ts", ts_buf, sizeof(ts_buf)) > 0) {
        try {
            long long ts = std::stoll(ts_buf);
            if (now - ts > 30) {
                api->db_del(ctx, "pending_chat_id");
                api->db_del(ctx, "pending_msg_id");
                api->db_del(ctx, "pending_title");
                api->db_del(ctx, "pending_ts");
                return 0;
            }
        } catch (...) {}
    }

    // Проверяем, что сообщение действительно пришло от @moolokobot
    long long sender = api->sender_id ? api->sender_id(ctx) : 0;
    long long chat = api->chat_id ? api->chat_id(ctx) : 0;

    char bbuf[64];
    long long bot_id = 0;
    if (api->db_get(ctx, "bot_id", bbuf, sizeof(bbuf)) > 0) {
        try { bot_id = std::stoll(bbuf); } catch (...) {}
    }
    if (bot_id == 0 && api->resolve) {
        bot_id = api->resolve(ctx, "moolokobot");
        if (bot_id != 0) {
            api->db_set(ctx, "bot_id", std::to_string(bot_id).c_str());
        }
    }

    bool is_from_bot = (bot_id != 0 && (sender == bot_id || chat == bot_id));
    if (!is_from_bot) {
        // Fallback-проверка: если это ЛС (chat == sender) и содержит текст MooBot
        std::string s = text;
        if (chat > 0 && chat == sender && (
            s.find("🐮") != std::string::npos ||
            s.find("🌳 Лес") != std::string::npos ||
            s.find("верстак") != std::string::npos ||
            s.find("Коров") != std::string::npos ||
            s.find("Телёнок") != std::string::npos ||
            s.find("муни") != std::string::npos ||
            s.find("кристалл") != std::string::npos
        )) {
            is_from_bot = true;
        }
    }

    if (!is_from_bot) {
        // Это сообщение от другого пользователя/чата — игнорируем!
        return 0;
    }

    // Очищаем ожидание
    api->db_del(ctx, "pending_chat_id");
    api->db_del(ctx, "pending_msg_id");
    api->db_del(ctx, "pending_title");
    api->db_del(ctx, "pending_ts");

    // Формируем финальный ответ
    api->c_reset(ctx);
    api->c_emoji(ctx, "🐮", ICON_INFO);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, (std::string(title_buf) + ":\n\n").c_str(), KOTO_ENT_BOLD);
    api->c_markdown(ctx, text);
    api->c_edit_message(ctx, orig_chat, orig_msg, 0);

    // Удаляем сообщение бота из ЛС
    api->delete_message(ctx, api->chat_id(ctx), api->msg_id(ctx));
    return 1;
}

} // namespace

static const koto_command COMMANDS[] = {
    { "лес",   &cmd_forest,  KOTO_LEVEL_TRUSTED, "Отправить /forest боту MooBot.", "" },
    { "таск",  &cmd_task,    KOTO_LEVEL_TRUSTED, "Отправить /task боту MooBot.", "" },
    { "крафт", &cmd_craft,   KOTO_LEVEL_TRUSTED, "Отправить /craft боту MooBot.", "" },
    { "мун",   &cmd_moon,    KOTO_LEVEL_TRUSTED, "Узнать количество муней.", "" },
    { "мукри", &cmd_crystal, KOTO_LEVEL_TRUSTED, "Узнать количество кристаллов.", "" },
    { "мукр",  &cmd_blood,   KOTO_LEVEL_TRUSTED, "Узнать баланс крови.", "" },
    { "бб",    &cmd_profile, KOTO_LEVEL_TRUSTED, "Узнать профиль теленка.", "" },
    { "мудон", &cmd_donate,  KOTO_LEVEL_TRUSTED, "Узнать прайс доната.", "[число]" }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "moomod",
    "Взаимодействие с игрой @moolokobot (MooBot) (C++ ABI 3).",
    "1.3.1",
    26,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    &on_watch,
    KOTO_WATCH_INCOMING,
    nullptr,
    nullptr, 0,
    "https://gitflic.ru/project/koteyka/kotemodules/blob/raw?file=modules/moomod.so"
};

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register(void) {
    return &MODULE;
}
