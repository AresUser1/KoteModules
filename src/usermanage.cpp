// usermanage.cpp — Модуль для управления пользователями и контактами.
// C++ ABI 3 port of usermanage.py (v1.0.5).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <set>
#include <sstream>

namespace {
using nlohmann::json;

static constexpr long long ICON_CONTACT = 5256230583717079814LL; // 📝
static constexpr long long ICON_ERROR   = 5370706614800097423LL; // 🧐
static constexpr long long ICON_SUCCESS = 5774022692642492953LL; // ✅
static constexpr long long ICON_BLOCK   = 6037249452824072506LL; // 🔒
static constexpr long long ICON_UNBLOCK = 6037496202990194718LL; // 🔓
static constexpr long long ICON_LIST    = 5920046907782074235LL; // 🚫
static constexpr long long ICON_TRASH   = 5879896690210639947LL; // 🗑️

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

static std::set<long long> get_blocklist(koto_ctx* ctx, const koto_api* api) {
    std::set<long long> set;
    char buf[4096];
    int len = api->db_get(ctx, "blocklist", buf, sizeof(buf));
    if (len > 0) {
        std::stringstream ss(buf);
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (!item.empty()) {
                try { set.insert(std::stoll(item)); } catch (...) {}
            }
        }
    }
    return set;
}

static void save_blocklist(koto_ctx* ctx, const koto_api* api, const std::set<long long>& set) {
    std::string out;
    for (auto id : set) {
        if (!out.empty()) out += ",";
        out += std::to_string(id);
    }
    api->db_set(ctx, "blocklist", out.c_str());
}

static long long resolve_target(koto_ctx* ctx, const koto_api* api, std::string& name_out) {
    const char* raw = api->cmd_args(ctx);
    std::string arg = raw ? koto_utils::trim(raw) : "";

    // 1. Из аргументов
    if (!arg.empty()) {
        try {
            long long uid = std::stoll(arg);
            if (uid > 0) {
                char nbuf[128] = {0};
                if (api->user_name) api->user_name(ctx, uid, nbuf, sizeof(nbuf));
                name_out = (nbuf[0] != 0) ? nbuf : ("ID: " + std::to_string(uid));
                return uid;
            }
        } catch (...) {}

        if (arg[0] == '@' && api->resolve) {
            long long uid = api->resolve(ctx, arg.substr(1).c_str());
            if (uid > 0) {
                name_out = arg;
                return uid;
            }
        }
    }

    // 2. Из реплая
    if (api->reply_sender_id) {
        long long ruid = api->reply_sender_id(ctx);
        if (ruid > 0) {
            char nbuf[128] = {0};
            if (api->user_name) api->user_name(ctx, ruid, nbuf, sizeof(nbuf));
            name_out = (nbuf[0] != 0) ? nbuf : ("ID: " + std::to_string(ruid));
            return ruid;
        }
    }

    return 0;
}

static void cmd_block(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) return;

    std::string target_name;
    long long uid = resolve_target(ctx, api, target_name);
    if (uid <= 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "🧐", ICON_ERROR);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, "Ошибка: Не удалось найти пользователя.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    // Блокировка через хост
    if (api->host_action_json) {
        json req = {{"user_id", uid}};
        api->host_action_json(ctx, "telegram.block.v1", req.dump().c_str());
    }

    auto bl = get_blocklist(ctx, api);
    bl.insert(uid);
    save_blocklist(ctx, api, bl);

    api->c_reset(ctx);
    api->c_emoji(ctx, "🔒", ICON_BLOCK);
    api->c_text(ctx, " Пользователь ");
    api->c_fmt(ctx, target_name.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, " заблокирован.", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

static void cmd_unblock(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) return;

    std::string target_name;
    long long uid = resolve_target(ctx, api, target_name);
    if (uid <= 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "🧐", ICON_ERROR);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, "Ошибка: Не удалось найти пользователя.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    // Разблокировка через хост
    if (api->host_action_json) {
        json req = {{"user_id", uid}};
        api->host_action_json(ctx, "telegram.unblock.v1", req.dump().c_str());
    }

    auto bl = get_blocklist(ctx, api);
    bl.erase(uid);
    save_blocklist(ctx, api, bl);

    api->c_reset(ctx);
    api->c_emoji(ctx, "🔓", ICON_UNBLOCK);
    api->c_text(ctx, " Пользователь ");
    api->c_fmt(ctx, target_name.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, " разблокирован.", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

static void cmd_blocklist(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) return;

    auto bl = get_blocklist(ctx, api);
    if (bl.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "🚫", ICON_LIST);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, "Список заблокированных пуст.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "🚫", ICON_LIST);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, "Заблокированные через юзербот:\n\n", KOTO_ENT_BOLD);

    int start_mark = api->c_mark(ctx);
    for (auto id : bl) {
        api->c_text(ctx, "• ");
        char nbuf[128] = {0};
        if (api->user_name) api->user_name(ctx, id, nbuf, sizeof(nbuf));
        std::string disp = (nbuf[0] != 0) ? nbuf : ("ID: " + std::to_string(id));
        api->c_fmt(ctx, disp.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, (" (ID: " + std::to_string(id) + ")\n").c_str());
    }
    // Оборачиваем список в свёрнутую цитату
    api->c_wrap(ctx, start_mark, KOTO_ENT_BLOCKQUOTE, 1);
    send_result(ctx, api);
}

static void cmd_contact(koto_ctx* ctx, const koto_api* api, bool mutual) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? koto_utils::trim(raw) : "";
    long long target_uid = 0;
    std::string first_name;

    if (api->reply_sender_id && api->reply_sender_id(ctx) > 0) {
        target_uid = api->reply_sender_id(ctx);
        first_name = args;
        if (first_name.empty()) {
            char nbuf[128] = {0};
            if (api->user_name) api->user_name(ctx, target_uid, nbuf, sizeof(nbuf));
            if (nbuf[0] != 0) first_name = nbuf;
        }
    } else {
        auto space = args.find(' ');
        if (space != std::string::npos) {
            std::string t = args.substr(0, space);
            first_name = koto_utils::trim(args.substr(space + 1));
            try { target_uid = std::stoll(t); } catch (...) {}
            if (target_uid <= 0 && t[0] == '@' && api->resolve) {
                target_uid = api->resolve(ctx, t.substr(1).c_str());
            }
        } else if (!args.empty()) {
            try { target_uid = std::stoll(args); } catch (...) {}
            if (target_uid <= 0 && args[0] == '@' && api->resolve) {
                target_uid = api->resolve(ctx, args.substr(1).c_str());
            }
            if (target_uid > 0) {
                char nbuf[128] = {0};
                if (api->user_name) api->user_name(ctx, target_uid, nbuf, sizeof(nbuf));
                if (nbuf[0] != 0) first_name = nbuf;
            }
        }
    }

    if (target_uid <= 0 || first_name.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "📝", ICON_CONTACT);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, mutual ? "Пример: .mct @user Имя" : "Пример: .ct @user Имя", KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

    if (api->host_action_json) {
        json req = {
            {"user_id", target_uid},
            {"first_name", first_name},
            {"mutual", mutual}
        };
        api->host_action_json(ctx, "telegram.contact.add.v1", req.dump().c_str());
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", ICON_SUCCESS);
    api->c_text(ctx, mutual ? " Взаимный контакт " : " Контакт ");
    api->c_fmt(ctx, first_name.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, " добавлен.", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

static void cmd_ct(koto_ctx* ctx, const koto_api* api) {
    cmd_contact(ctx, api, false);
}

static void cmd_mct(koto_ctx* ctx, const koto_api* api) {
    cmd_contact(ctx, api, true);
}

static void cmd_rct(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) return;

    std::string target_name;
    long long uid = resolve_target(ctx, api, target_name);
    if (uid <= 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "🧐", ICON_ERROR);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, "Пользователь не найден.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    if (api->host_action_json) {
        json req = {{"user_id", uid}};
        api->host_action_json(ctx, "telegram.contact.delete.v1", req.dump().c_str());
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "🗑️", ICON_TRASH);
    api->c_text(ctx, " Контакт ");
    api->c_fmt(ctx, target_name.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, " удалён.", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

} // namespace

static const koto_command COMMANDS[] = {
    { "block",     &cmd_block,     KOTO_LEVEL_OWNER, "Заблокировать пользователя.", "[@user/ID]" },
    { "unblock",   &cmd_unblock,   KOTO_LEVEL_OWNER, "Разблокировать пользователя.", "[@user/ID]" },
    { "blocklist", &cmd_blocklist, KOTO_LEVEL_OWNER, "Показать список заблокированных.", "" },
    { "ct",        &cmd_ct,        KOTO_LEVEL_OWNER, "Добавить в контакты.", "[@user/ID] <Имя>" },
    { "mct",       &cmd_mct,       KOTO_LEVEL_OWNER, "Добавить в контакты взаимно.", "[@user/ID] <Имя>" },
    { "rct",       &cmd_rct,       KOTO_LEVEL_OWNER, "Удалить из контактов.", "[@user/ID]" }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "usermanage",
    "Управление пользователями и контактами (C++ ABI 3).",
    "1.0.5",
    26,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr,
    0,
    nullptr,
    nullptr, 0,
    nullptr
};

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register(void) {
    return &MODULE;
}
