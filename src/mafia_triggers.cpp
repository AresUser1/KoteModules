// mafia_triggers.cpp — Триггеры для Мафии (Warn/Mute за AFK и выход из игры) (C++ ABI 3).
// Портирован из Python-модуля mafia_triggers.py (v2.1.3).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace {

// ID известных ботов Мафии
static const long long MAFIA_BOTS[] = {
    606933972LL, 1044037207LL, 1050428643LL, 761250017LL, 468253535LL,
    1520369962LL, 6981158759LL, 1634167847LL, 5424831786LL, 7199004377LL
};

static const char* TRIGGERS_AFK[] = {
    "бу-у-у-у-ду", "пообещал", "сон", "просыпаться", "qichqirganini",
    "умер от скуки", "afk"
};

static const char* TRIGGERS_LEAVE[] = {
    "гнетущей", "убежал", "прыгнул", "повесился", "не выдержал", "вышел"
};

static bool is_mafia_bot(long long uid) {
    for (long long b : MAFIA_BOTS) {
        if (b == uid) return true;
    }
    return false;
}

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

static std::set<long long> load_chat_set(const koto_api* api, koto_ctx* ctx, const char* key) {
    std::set<long long> s;
    char buf[16384] = {0};
    int n = api->db_get(ctx, key, buf, sizeof(buf) - 1);
    if (n <= 0) return s;
    buf[n] = '\0';

    std::istringstream iss(buf);
    std::string item;
    while (std::getline(iss, item, '\n')) {
        std::string trimmed = koto_utils::trim(item);
        if (trimmed.empty()) continue;
        long long id = strtoll(trimmed.c_str(), nullptr, 10);
        if (id != 0) s.insert(id);
    }
    return s;
}

static void save_chat_set(const koto_api* api, koto_ctx* ctx, const char* key, const std::set<long long>& s) {
    std::string out;
    for (long long id : s) {
        out += std::to_string(id) + "\n";
    }
    api->db_set(ctx, key, out.c_str());
}

// ── .wtr (Warn Trigger) ───────────────────────────────────────────────────────

static void cmd_wtr(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    if (cid == 0) return;

    std::set<long long> chats = load_chat_set(api, ctx, "warn_chats");
    bool active = false;
    if (chats.find(cid) != chats.end()) {
        chats.erase(cid);
        active = false;
    } else {
        chats.insert(cid);
        active = true;
    }
    save_chat_set(api, ctx, "warn_chats", chats);

    api->c_reset(ctx);
    api->c_fmt(ctx, "MafiTriggers: ", KOTO_ENT_BOLD);
    if (active) {
        api->c_text(ctx, "⚪️ Активирован (Warn)");
    } else {
        api->c_text(ctx, "⚫️ Деактивирован (Warn)");
    }
    send_result(ctx, api);
}

// ── .mtr (Mute Trigger) ───────────────────────────────────────────────────────

static void cmd_mtr(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    if (cid == 0) return;

    std::set<long long> chats = load_chat_set(api, ctx, "mute_chats");
    bool active = false;
    if (chats.find(cid) != chats.end()) {
        chats.erase(cid);
        active = false;
    } else {
        chats.insert(cid);
        active = true;
    }
    save_chat_set(api, ctx, "mute_chats", chats);

    api->c_reset(ctx);
    api->c_fmt(ctx, "MafiTriggers: ", KOTO_ENT_BOLD);
    if (active) {
        api->c_text(ctx, "⚪️ Активирован (Mute)");
    } else {
        api->c_text(ctx, "⚫️ Деактивирован (Mute)");
    }
    send_result(ctx, api);
}

// ── Watcher: слежка за ботами мафии ──────────────────────────────────────────

static int on_watch(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) return 0;

    long long sender = api->sender_id(ctx);
    if (!is_mafia_bot(sender)) return 0;

    long long cid = api->chat_id(ctx);
    std::set<long long> warn_chats = load_chat_set(api, ctx, "warn_chats");
    std::set<long long> mute_chats = load_chat_set(api, ctx, "mute_chats");

    bool has_warn = (warn_chats.find(cid) != warn_chats.end());
    bool has_mute = (mute_chats.find(cid) != mute_chats.end());
    if (!has_warn && !has_mute) return 0;

    const char* text_raw = api->msg_text(ctx);
    if (!text_raw) return 0;
    std::string text = text_raw;

    std::string text_lower;
    for (char c : text) text_lower += static_cast<char>(tolower(static_cast<unsigned char>(c)));

    std::string reason;
    bool is_afk = false;
    bool is_leave = false;

    for (const char* w : TRIGGERS_AFK) {
        if (text_lower.find(w) != std::string::npos) {
            is_afk = true;
            reason = "сон";
            break;
        }
    }

    if (!is_afk) {
        for (const char* w : TRIGGERS_LEAVE) {
            if (text_lower.find(w) != std::string::npos) {
                is_leave = true;
                reason = "выход";
                break;
            }
        }
    }

    if (!is_afk && !is_leave) return 0;

    // Извлечение упомянутого пользователя из entities сообщения
    long long target_id = 0;
    if (api->msg_entity_count && api->msg_entity) {
        int cnt = api->msg_entity_count(ctx);
        for (int i = 0; i < cnt; ++i) {
            int type = 0, off = 0, len = 0;
            long long doc_id = 0;
            char url_buf[512] = {0};
            if (api->msg_entity(ctx, i, &type, &off, &len, &doc_id, url_buf, sizeof(url_buf)) == 0) {
                if (type == KOTO_ENT_URL && strncmp(url_buf, "tg://user?id=", 13) == 0) {
                    target_id = strtoll(url_buf + 13, nullptr, 10);
                    if (target_id > 0) break;
                }
            }
        }
    }

    // Если entity не нашлась, пробуем найти числовой ID или tg://user в сыром тексте
    if (target_id == 0) {
        size_t tg_pos = text.find("tg://user?id=");
        if (tg_pos != std::string::npos) {
            target_id = strtoll(text.c_str() + tg_pos + 13, nullptr, 10);
        }
    }

    if (target_id > 0) {
        std::string cmd;
        if (has_mute) {
            cmd = "!mute " + std::to_string(target_id) + " 2h " + reason;
        } else {
            cmd = "!warn " + std::to_string(target_id) + " " + reason;
        }

        api->c_reset(ctx);
        api->c_text(ctx, cmd.c_str());
        api->c_send(ctx, cid, 0); // Отправляем отдельным сообщением в чат
    }

    return 0;
}

const koto_command COMMANDS[] = {
    {
        "wtr",
        &cmd_wtr,
        KOTO_LEVEL_TRUSTED,
        "Вкл/Выкл Warn-триггер для Мафии (варнит вышедших/afk).",
        nullptr
    },
    {
        "mtr",
        &cmd_mtr,
        KOTO_LEVEL_TRUSTED,
        "Вкл/Выкл Mute-триггер для Мафии (мутит вышедших/afk).",
        nullptr
    }
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "mafia_triggers",
    "Триггеры для Мафии: автоматический Warn/Mute за AFK и выход из игры (C++ ABI 3).",
    "2.1.3",
    26,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    &on_watch,
    KOTO_WATCH_INCOMING,
    nullptr,
    nullptr, 0,
    nullptr
};

} // namespace

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
