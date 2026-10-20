// spam_ban.cpp — Мощный Спам-Репорт + Блок. Поддержка аргументов (@user) (C++ ABI 3).
// Портирован из Python-модуля spam_ban.py (v1.6.0).
// Автор: Kote

#include "module_abi.h"
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cerrno>

static constexpr long long ICON_BAN  = 6030563507299160824LL; // 🚫
static constexpr long long ICON_DONE = 5774022692642492953LL; // ✅
static constexpr long long ICON_TWIN = 5454386656628991407LL; // 👥

static const char* TWINS_MODE_KEY = "spamban_twins_active";

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

static void cmd_spamtwins(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    api->db_set(ctx, TWINS_MODE_KEY, "False");
    if (api->cfg_set) api->cfg_set(ctx, "spamtwins", "0");
    api->c_reset(ctx);
    api->c_text(ctx, "Репорт и блокировка выполняются текущим аккаунтом. Групповой репорт с твинков недоступен в API; сообщения пользователю не отправляются.");
    send_result(ctx, api);
}

// ─── spamban ───────────────────────────────────────────────────────────────

static void cmd_spamban(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";
    long long target_id = 0;


    if (!args.empty()) {
        std::string uname = (args[0] == '@') ? args.substr(1) : args;
        if (api->resolve) {
            target_id = api->resolve(ctx, uname.c_str());
        }
        if (target_id == 0) {
            char* end = nullptr;
            errno = 0;
            target_id = strtoll(args.c_str(), &end, 10);
            if (errno == ERANGE || end != args.c_str() + args.size() || target_id <= 0) target_id = 0;
        }
    } else {
        long long r_id = api->reply_sender_id(ctx);
        if (r_id > 0) {
            target_id = r_id;
        } else if (api->chat_type(ctx) == KOTO_CHAT_PRIVATE) { // ЛС
            target_id = api->chat_id(ctx);

        }
    }

    if (target_id <= 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Укажите @user, ответьте на сообщение или используйте в ЛС.");
        send_result(ctx, api);
        return;
    }

    api->c_reset(ctx);
    api->c_text(ctx, "⏳ Отправляю запрос на репорт и блокировку...");
    send_result(ctx, api);
    api->ban_user(ctx, api->chat_id(ctx), target_id, KOTO_BAN_REPORT_SPAM);
    api->c_reset(ctx);
    api->c_text(ctx, "✅ Telegram подтвердил репорт и блокировку пользователя.");
    send_result(ctx, api);
}

static const koto_command COMMANDS[] = {
    {
        "spamtwins",
        &cmd_spamtwins,
        KOTO_LEVEL_TRUSTED,
        "Включить/выключить атаку с твинков.",
        nullptr
    },
    {
        "spamban",
        &cmd_spamban,
        KOTO_LEVEL_TRUSTED,
        "Мощная жалоба (Спам) + Блок.",
        "<@user/ID/ответ>"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "spam_ban",
    "Мощный Спам-Репорт + Блок. Поддержка аргументов (@user) (C++ ABI 3).",
    "1.6.2", 60,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    nullptr, 0,
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/spam_ban.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
