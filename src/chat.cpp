// chat.cpp — Модуль для управления чатами и получения информации (C++ ABI 3).
// Портирован из Python-модуля chat.py (v1.0.4).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <cctype>
#include <algorithm>

namespace {

static constexpr long long EMO_USER   = 5879770735999717115LL; // 👤
static constexpr long long EMO_CHAT   = 5891243564309942507LL; // 💬
static constexpr long long EMO_ADMIN  = 5879905000972358125LL; // 👮‍♂️
static constexpr long long EMO_BOT    = 5931415565955503486LL; // 🤖
static constexpr long long EMO_INVITE = 5778168620278354602LL; // 📨
static constexpr long long EMO_LEAVE  = 5985478698722136468LL; // 👋

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

static long long resolve_user_target(koto_ctx* ctx, const koto_api* api) {
    long long r_id = api->reply_sender_id ? api->reply_sender_id(ctx) : 0;
    if (r_id > 0) return r_id;

    const char* raw = api->cmd_args(ctx);
    std::string arg = raw ? koto_utils::trim(raw) : "";
    if (!arg.empty()) {
        char* end = nullptr;
        long long val = strtoll(arg.c_str(), &end, 10);
        if (*end == '\0' && val > 0) return val;

        if (arg[0] == '@' && api->resolve) {
            long long res = api->resolve(ctx, arg.c_str() + 1);
            if (res != 0) return res;
        }
    }
    return api->sender_id ? api->sender_id(ctx) : 0;
}

// ── .userid ───────────────────────────────────────────────────────────────────

static void cmd_userid(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long uid = resolve_user_target(ctx, api);
    if (uid == 0 && api->get_me) uid = api->get_me(ctx);

    std::string fname = koto_utils::get_user_first_name(api, ctx, uid);
    if (fname.empty()) fname = koto_utils::get_user_name(api, ctx, uid);
    if (fname.empty()) fname = "Пользователь";

    std::string uname = koto_utils::get_user_username(api, ctx, uid);

    api->c_reset(ctx);
    api->c_emoji(ctx, "👤", EMO_USER);
    api->c_fmt(ctx, " Пользователь:\n", KOTO_ENT_BOLD);
    api->c_fmt(ctx, "Имя: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, (fname + "\n").c_str(), KOTO_ENT_CODE);
    if (!uname.empty()) {
        api->c_fmt(ctx, "Юзернейм: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ("@" + uname + "\n").c_str(), KOTO_ENT_CODE);
    }
    api->c_fmt(ctx, "ID: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, std::to_string(uid).c_str(), KOTO_ENT_CODE);

    send_result(ctx, api);
}

// ── .chatid ───────────────────────────────────────────────────────────────────

static void cmd_chatid(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    if (cid == 0) return;

    // Читаем chat_full_json для получения названия чата
    std::string full_json = koto_utils::get_chat_full_json(api, ctx, cid);
    std::string title = "Чат";

    try {
        if (!full_json.empty() && full_json != "{}") {
            auto j = nlohmann::json::parse(full_json);
            if (j.contains("title")) title = j["title"].get<std::string>();
        }
    } catch (...) {}

    api->c_reset(ctx);
    api->c_emoji(ctx, "💬", EMO_CHAT);
    api->c_fmt(ctx, " Чат:\n", KOTO_ENT_BOLD);
    api->c_fmt(ctx, "Название: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, (title + "\n").c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, "ID: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, std::to_string(cid).c_str(), KOTO_ENT_CODE);

    send_result(ctx, api);
}

// ── .kickme ───────────────────────────────────────────────────────────────────

static void cmd_kickme(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    int type=api->chat_type(ctx);
    long long me=api->get_me?api->get_me(ctx):0;
    if ((type!=KOTO_CHAT_GROUP && type!=KOTO_CHAT_CHANNEL) || !me || !api->kick_user) {
        api->c_reset(ctx);api->c_text(ctx,"❌ Для выхода нужна группа/канал и авторизованный аккаунт.");send_result(ctx,api);return;
    }
    const char* raw = api->cmd_args(ctx);
    std::string reason = raw ? koto_utils::trim(raw) : "";

    api->c_reset(ctx);
    api->c_emoji(ctx, "👋", EMO_LEAVE);
    std::string text = " До связи.";
    if (!reason.empty()) {
        text += "\nПричина: " + reason;
    }
    api->c_fmt(ctx, text.c_str(), KOTO_ENT_BOLD);
    send_result(ctx, api);
    // Core 61 interprets kicking the current account as leave_chat.
    api->kick_user(ctx,api->chat_id(ctx),me);
}

// ── .users ────────────────────────────────────────────────────────────────────

static void cmd_users(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    std::vector<long long> participants = koto_utils::get_chat_participants(api, ctx, cid);

    api->c_reset(ctx);
    api->c_emoji(ctx, "👥", EMO_USER);
    api->c_fmt(ctx, " Список участников чата:\n\n", KOTO_ENT_BOLD);
    api->c_fmt(ctx, "Всего участников: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, (std::to_string(participants.size()) + "\n\n").c_str(), KOTO_ENT_CODE);

    int shown = 0;
    for (long long uid : participants) {
        if (shown >= 50) {
            api->c_text(ctx, "... и ещё ");
            api->c_fmt(ctx, std::to_string(participants.size() - shown).c_str(), KOTO_ENT_CODE);
            api->c_text(ctx, " участников.");
            break;
        }
        std::string fname = koto_utils::get_user_first_name(api, ctx, uid);
        if (fname.empty()) fname = "ID " + std::to_string(uid);

        api->c_text(ctx, "• ");
        std::string url = "tg://user?id=" + std::to_string(uid);
        api->c_url(ctx, fname.c_str(), url.c_str());
        api->c_text(ctx, " | ");
        api->c_fmt(ctx, std::to_string(uid).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n");
        shown++;
    }

    send_result(ctx, api);
}

// ── .admins ───────────────────────────────────────────────────────────────────

static void cmd_admins(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    std::string full_json = koto_utils::get_chat_full_json(api, ctx, cid);

    std::vector<long long> admins;
    try {
        if (!full_json.empty() && full_json != "{}") {
            auto j = nlohmann::json::parse(full_json);
            if (j.contains("admin_ids") && j["admin_ids"].is_array()) {
                for (auto& item : j["admin_ids"]) {
                    admins.push_back(item.get<long long>());
                }
            } else if (j.contains("creator_id")) {
                admins.push_back(j["creator_id"].get<long long>());
            }
        }
    } catch (...) {}

    api->c_reset(ctx);
    api->c_emoji(ctx, "👮‍♂️", EMO_ADMIN);
    api->c_fmt(ctx, " Администраторы чата:\n\n", KOTO_ENT_BOLD);

    if (admins.empty()) {
        api->c_text(ctx, "Не удалось получить список администраторов или чат приватный.");
    } else {
        api->c_fmt(ctx, "Всего админов: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (std::to_string(admins.size()) + "\n\n").c_str(), KOTO_ENT_CODE);

        for (long long uid : admins) {
            std::string fname = koto_utils::get_user_first_name(api, ctx, uid);
            if (fname.empty()) fname = "ID " + std::to_string(uid);

            api->c_text(ctx, "• ");
            std::string url = "tg://user?id=" + std::to_string(uid);
            api->c_url(ctx, fname.c_str(), url.c_str());
            api->c_text(ctx, " | ");
            api->c_fmt(ctx, std::to_string(uid).c_str(), KOTO_ENT_CODE);
            api->c_text(ctx, "\n");
        }
    }

    send_result(ctx, api);
}

// ── .bots ─────────────────────────────────────────────────────────────────────

static void cmd_bots(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    std::string full_json = koto_utils::get_chat_full_json(api, ctx, cid);

    std::vector<long long> bots;
    std::set<long long> seen_bots;

    try {
        if (!full_json.empty() && full_json != "{}") {
            auto j = nlohmann::json::parse(full_json);
            if (j.contains("bot_ids") && j["bot_ids"].is_array()) {
                for (auto& item : j["bot_ids"]) {
                    long long bid = item.get<long long>();
                    if (bid > 0 && seen_bots.insert(bid).second) {
                        bots.push_back(bid);
                    }
                }
            }

            // Fallback: если bot_ids не было в json (например, старый кэш или старая версия ядра),
            // перебираем участников participant_ids и администраторов admin_ids
            if (bots.empty()) {
                std::vector<long long> candidate_ids;
                if (j.contains("participant_ids") && j["participant_ids"].is_array()) {
                    for (auto& item : j["participant_ids"]) candidate_ids.push_back(item.get<long long>());
                }
                if (j.contains("admin_ids") && j["admin_ids"].is_array()) {
                    for (auto& item : j["admin_ids"]) candidate_ids.push_back(item.get<long long>());
                }

                for (long long uid : candidate_ids) {
                    if (uid <= 0 || seen_bots.find(uid) != seen_bots.end()) continue;

                    bool is_bot = false;
                    std::string ujson = koto_utils::get_user_full_json(api, ctx, uid);
                    if (!ujson.empty() && ujson != "{}") {
                        try {
                            auto uj = nlohmann::json::parse(ujson);
                            if (uj.value("is_bot", false)) {
                                is_bot = true;
                            } else {
                                std::string uname = uj.value("username", "");
                                if (!uname.empty()) {
                                    std::string lk = uname;
                                    for (char& c : lk) c = (char)tolower((unsigned char)c);
                                    if (lk.size() >= 3 && lk.rfind("bot") == lk.size() - 3) {
                                        is_bot = true;
                                    }
                                }
                            }
                        } catch (...) {}
                    }
                    if (!is_bot) {
                        std::string uname = koto_utils::get_user_username(api, ctx, uid);
                        if (!uname.empty()) {
                            std::string lk = uname;
                            for (char& c : lk) c = (char)tolower((unsigned char)c);
                            if (lk.size() >= 3 && lk.rfind("bot") == lk.size() - 3) {
                                is_bot = true;
                            }
                        }
                    }

                    if (is_bot) {
                        seen_bots.insert(uid);
                        bots.push_back(uid);
                    }
                }
            }
        }
    } catch (...) {}

    api->c_reset(ctx);
    api->c_emoji(ctx, "🤖", EMO_BOT);
    api->c_fmt(ctx, " Боты в чате:\n\n", KOTO_ENT_BOLD);

    if (bots.empty()) {
        api->c_text(ctx, "Боты в чате не найдены.");
    } else {
        api->c_fmt(ctx, "Всего ботов: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (std::to_string(bots.size()) + "\n\n").c_str(), KOTO_ENT_CODE);

        for (long long uid : bots) {
            std::string fname = koto_utils::get_user_first_name(api, ctx, uid);
            if (fname.empty()) {
                std::string uname = koto_utils::get_user_username(api, ctx, uid);
                fname = (!uname.empty()) ? ("@" + uname) : ("Bot " + std::to_string(uid));
            }

            api->c_text(ctx, "• ");
            std::string url = "tg://user?id=" + std::to_string(uid);
            api->c_url(ctx, fname.c_str(), url.c_str());
            api->c_text(ctx, " | ");
            api->c_fmt(ctx, std::to_string(uid).c_str(), KOTO_ENT_CODE);
            api->c_text(ctx, "\n");
        }
    }

    send_result(ctx, api);
}

const koto_command COMMANDS[] = {
    {
        "userid",
        &cmd_userid,
        KOTO_LEVEL_TRUSTED,
        "Показывает имя, юзернейм и ID пользователя.",
        "[@user/ID/ответ]"
    },
    {
        "chatid",
        &cmd_chatid,
        KOTO_LEVEL_TRUSTED,
        "Показывает название и ID текущего чата.",
        nullptr
    },
    {
        "kickme",
        &cmd_kickme,
        KOTO_LEVEL_TRUSTED,
        "Выйти из чата.",
        "[причина]"
    },
    {
        "users",
        &cmd_users,
        KOTO_LEVEL_TRUSTED,
        "Список участников текущего чата.",
        nullptr
    },
    {
        "admins",
        &cmd_admins,
        KOTO_LEVEL_TRUSTED,
        "Список администраторов текущего чата.",
        nullptr
    },
    {
        "bots",
        &cmd_bots,
        KOTO_LEVEL_TRUSTED,
        "Список ботов в текущем чате.",
        nullptr
    }
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "chat",
    "Управление чатами и получение информации об участниках/админах (C++ ABI 3).",
    "1.0.6",
    61,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    nullptr, 0,
    nullptr
};

} // namespace

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
