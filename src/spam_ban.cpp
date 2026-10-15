// spam_ban.cpp — Мощный Спам-Репорт + Блок. Поддержка аргументов (@user) (C++ ABI 3).
// Портирован из Python-модуля spam_ban.py (v1.6.0).
// Автор: Kote

#include "module_abi.h"
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>

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

static bool get_twins_mode(koto_ctx* ctx, const koto_api* api) {
    char buf[32] = {0};
    if (api->cfg_get && api->cfg_get(ctx, "spamtwins", buf, sizeof(buf)) > 0) {
        return (strcmp(buf, "1") == 0 || strcmp(buf, "True") == 0 || strcmp(buf, "true") == 0);
    }
    int len = api->db_get(ctx, TWINS_MODE_KEY, buf, sizeof(buf));
    if (len > 0 && (strcmp(buf, "True") == 0 || strcmp(buf, "1") == 0)) return true;
    return false;
}

static bool json_get_bool(const std::string& obj, const std::string& key) {
    size_t pos = obj.find("\"" + key + "\"");
    if (pos == std::string::npos) return false;
    pos = obj.find(':', pos);
    if (pos == std::string::npos) return false;
    size_t val_pos = obj.find_first_not_of(" \t\n\r", pos + 1);
    if (val_pos == std::string::npos) return false;
    return obj.compare(val_pos, 4, "true") == 0;
}

static long long json_get_int64(const std::string& obj, const std::string& key) {
    size_t pos = obj.find("\"" + key + "\"");
    if (pos == std::string::npos) return 0;
    pos = obj.find(':', pos);
    if (pos == std::string::npos) return 0;
    size_t val_pos = obj.find_first_not_of(" \t\n\r", pos + 1);
    if (val_pos == std::string::npos) return 0;
    char* end = nullptr;
    return strtoll(obj.c_str() + val_pos, &end, 10);
}

// ─── spamtwins ─────────────────────────────────────────────────────────────

static void cmd_spamtwins(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    bool current = get_twins_mode(ctx, api);
    bool next = !current;
    if (api->cfg_set) api->cfg_set(ctx, "spamtwins", next ? "1" : "0");
    api->db_set(ctx, TWINS_MODE_KEY, next ? "True" : "False");

    std::string status = next ? "ВКЛ" : "ВЫКЛ";

    api->c_reset(ctx);
    api->c_emoji(ctx, "👥", ICON_TWIN);
    api->c_fmt(ctx, (" Атака с твинков: " + status).c_str(), KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// ─── spamban ───────────────────────────────────────────────────────────────

static void cmd_spamban(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";
    long long target_id = 0;
    bool is_dialog = false;

    if (!args.empty()) {
        std::string uname = (args[0] == '@') ? args.substr(1) : args;
        if (api->resolve) {
            target_id = api->resolve(ctx, uname.c_str());
        }
        if (target_id == 0) {
            char* end = nullptr;
            target_id = strtoll(args.c_str(), &end, 10);
            if (end == args.c_str()) target_id = 0;
        }
    } else {
        long long r_id = api->reply_sender_id(ctx);
        if (r_id > 0) {
            target_id = r_id;
        } else if (api->chat_id(ctx) > 0) { // ЛС
            target_id = api->chat_id(ctx);
            is_dialog = true;
        }
    }

    if (target_id == 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Укажите @user, ответьте на сообщение или используйте в ЛС.");
        send_result(ctx, api);
        return;
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "🚫", ICON_BAN);
    api->c_fmt(ctx, " Уничтожение... ", KOTO_ENT_BOLD);
    send_result(ctx, api);

    // --- ОСНОВНОЙ АККАУНТ ---
    if (api->ban_user) {
        api->ban_user(ctx, api->chat_id(ctx), target_id, 0);
    }

    // --- ТВИНКИ ---
    int twins_count = 0;
    if (get_twins_mode(ctx, api)) {
        char tbuf[16384];
        int tlen = api->get_twins_json ? api->get_twins_json(ctx, tbuf, sizeof(tbuf)) : 0;
        if (tlen > 0) {
            std::string json(tbuf, tlen);
            size_t pos = 0;
            while ((pos = json.find('{', pos)) != std::string::npos) {
                size_t end_pos = json.find('}', pos);
                if (end_pos == std::string::npos) break;
                std::string obj = json.substr(pos, end_pos - pos + 1);

                long long uid = json_get_int64(obj, "uid");
                bool is_main = json_get_bool(obj, "is_main");
                bool online = json_get_bool(obj, "online");

                if (!is_main && online && uid != 0) {
                    if (api->twin_send) {
                        api->twin_send(ctx, uid, target_id, "/start");
                    }
                    twins_count++;
                }
                pos = end_pos + 1;
            }
        }
    }

    // --- ОТЧЕТ ---
    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", ICON_DONE);
    api->c_fmt(ctx, " Готово.", KOTO_ENT_BOLD);

    if (twins_count > 0) {
        api->c_text(ctx, ("\n👥 Твинки: " + std::to_string(twins_count)).c_str());
    }

    if (is_dialog) {
        api->c_text(ctx, "\n🗑 Удаляю...");
        if (api->delete_msg) {
            api->delete_msg(ctx);
        }
    }

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

static const koto_setting_field SETTINGS[] = {
    {
        "spamtwins",
        "Атаковать с твинков при spamban",
        KOTO_FIELD_BOOL,
        "0",
        nullptr
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "spam_ban",
    "Мощный Спам-Репорт + Блок. Поддержка аргументов (@user) (C++ ABI 3).",
    "1.6.1",
    27,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    SETTINGS,
    sizeof(SETTINGS) / sizeof(SETTINGS[0]),
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/spam_ban.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
