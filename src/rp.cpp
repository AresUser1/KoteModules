// rp.cpp
// Полнофункциональный Role-Play модуль для Kotogram (C/C++ ABI 3).
// Перенесено из rpmod.py (Kote).
// Автор: Kote

#include <string>
#include <vector>
#include <sstream>
#include <map>
#include <set>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include "module_abi.h"

// ── Вспомогательные строковые функции ─────────────────────────────────────────

static inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static inline std::string to_lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static inline std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> res;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        std::string t = trim(item);
        if (!t.empty()) res.push_back(t);
    }
    return res;
}

// ── Хелперы БД (Namespace 'rp') ──────────────────────────────────────────────

static inline std::string db_get_str(koto_ctx* ctx, const koto_api* api, const std::string& key, const std::string& def = "") {
    char buf[2048] = {0};
    int n = api->db_get(ctx, key.c_str(), buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        return std::string(buf);
    }
    return def;
}

static inline void db_set_str(koto_ctx* ctx, const koto_api* api, const std::string& key, const std::string& val) {
    api->db_set(ctx, key.c_str(), val.c_str());
}

static inline void db_del(koto_ctx* ctx, const koto_api* api, const std::string& key) {
    api->db_del(ctx, key.c_str());
}

static inline bool db_has(koto_ctx* ctx, const koto_api* api, const std::string& key) {
    char buf[16] = {0};
    return api->db_get(ctx, key.c_str(), buf, sizeof(buf) - 1) > 0;
}

// ── Статические эмодзи ────────────────────────────────────────────────────────

struct StaticEmojiDef {
    const char* key;
    long long   doc_id;
    const char* fallback;
};

static const StaticEmojiDef DEFAULT_STATIC_EMOJIS[] = {
    {"ERROR",    5195105446679039811LL, "❌"},
    {"SUCCESS",  5195127277997806826LL, "✅"},
    {"INFO",     6028435952299413210LL, "ℹ️"},
    {"TRASH",    6039522349517115015LL, "🗑️"},
    {"LOCK",     5778570255555105942LL, "🔒"},
    {"CROWN",    5807868868886009920LL, "👑"},
    {"NICK",     6008104758236156926LL, "📛"},
    {"COMMENT",  6034831751308644168LL, "💬"},
    {"LOAD",     5891211339170326418LL, "⏳"},
    {"IN_OUT",   5877307202888273539LL, "📥"},
    {"UPDATE",   5877410604225924969LL, "🔄"},
    {"SKIP",     5253868738251335638LL, "⏭️"},
    {"LIST",     5956561916573782596LL, "📋"},
    {"SETTINGS", 6032742198179532882LL, "⚙️"},
};

static void put_static_emoji(koto_ctx* ctx, const koto_api* api, const std::string& key) {
    std::string key_upper = key;
    for (char& c : key_upper) c = (char)std::toupper((unsigned char)c);

    std::string custom = db_get_str(ctx, api, "se:" + key_upper);
    if (!custom.empty()) {
        size_t pipe = custom.find('|');
        if (pipe != std::string::npos) {
            long long doc_id = 0;
            try { doc_id = std::stoll(custom.substr(0, pipe)); } catch (...) {}
            std::string fb = custom.substr(pipe + 1);
            if (fb.empty()) fb = "✨";
            if (doc_id != 0) {
                api->c_emoji(ctx, fb.c_str(), doc_id);
            } else {
                api->c_text(ctx, fb.c_str());
            }
            return;
        }
    }

    for (const auto& item : DEFAULT_STATIC_EMOJIS) {
        if (key_upper == item.key) {
            if (item.doc_id != 0) {
                api->c_emoji(ctx, item.fallback, item.doc_id);
            } else {
                api->c_text(ctx, item.fallback);
            }
            return;
        }
    }

    api->c_text(ctx, "❔");
}

// ── Встроенный набор RP-действий по умолчанию ─────────────────────────────────

struct DefaultRPAction {
    const char* aliases;
    const char* action;
    long long   doc_id;
    const char* fallback;
};

static const DefaultRPAction DEFAULT_ACTIONS[] = {
    {"обнять/hug", "обнял(а)", 0, "🤗"},
    {"поцеловать/kiss", "поцеловал(а)", 0, "💋"},
    {"кусь/bite", "сделал(а) кусь", 0, "😼"},
    {"ударить/slap", "дал(а) пощечину", 0, "👋"},
    {"погладить/pat", "погладил(а)", 0, "🖐"},
    {"убить/kill", "убил(а)", 0, "🔪"},
    {"чай/tea", "налил(а) ароматного чаю для", 0, "☕️"},
    {"кофе/coffee", "сварил(а) горячий кофе для", 0, "☕️"},
    {"спать/sleep", "лег(ла) спать вместе с", 0, "💤"},
    {"прижать/cuddle", "крепко прижал(а) к себе", 0, "🫂"},
    {"ущипнуть/pinch", "ущипнул(а)", 0, "🤏"},
    {"покормить/feed", "покормил(а)", 0, "🍕"},
    {"лизнуть/lick", "лизнул(а)", 0, "👅"},
    {"пнуть/kick", "пнул(а)", 0, "🦵"},
    {"выпить/drink", "выпил(а) за здоровье", 0, "🍻"},
    {"расстрелять/shoot", "расстрелял(а)", 0, "🔫"},
    {"связать/tie", "связал(а)", 0, "🪢"},
    {"укрыть/blanket", "укрыл(а) теплым пледом", 0, "🛌"},
    {"подарить/gift", "подарил(а) подарок для", 0, "🎁"},
    {"похвалить/praise", "похвалил(а)", 0, "✨"},
    {"испугать/scare", "напугал(а)", 0, "👻"},
    {"утешить/comfort", "утешил(а)", 0, "🥺"},
};

static void ensure_default_commands(koto_ctx* ctx, const koto_api* api) {
    std::string inited = db_get_str(ctx, api, "inited");
    if (!inited.empty()) return;

    std::string cmd_list;
    for (const auto& a : DEFAULT_ACTIONS) {
        auto alist = split(a.aliases, '/');
        for (const auto& alias : alist) {
            std::string al_low = to_lower(alias);
            std::string val = std::string(a.action) + "|" + std::to_string(a.doc_id) + "|" + a.fallback;
            db_set_str(ctx, api, "cmd:" + al_low, val);
            if (!cmd_list.empty()) cmd_list += ",";
            cmd_list += al_low;
        }
    }
    db_set_str(ctx, api, "cmd_list", cmd_list);
    db_set_str(ctx, api, "inited", "1");
}

static bool is_rp_creator(koto_ctx* ctx, const koto_api* api, long long uid) {
    if (api->user_level(ctx) >= KOTO_LEVEL_TRUSTED) return true;
    return db_has(ctx, api, "creator:" + std::to_string(uid));
}

static bool check_rp_enabled_in_chat(koto_ctx* ctx, const koto_api* api, long long cid) {
    return db_has(ctx, api, "enabled:" + std::to_string(cid));
}

// Получение RP-ника: Чат -> Глобал -> Имя Telegram
static std::string get_rp_display_name(koto_ctx* ctx, const koto_api* api, long long uid, long long cid) {
    // 1. Ник для данного чата
    std::string cn = db_get_str(ctx, api, "nick:" + std::to_string(cid) + ":" + std::to_string(uid));
    if (!cn.empty() && cn != "none") return cn;

    // 2. Глобальный ник
    std::string gn = db_get_str(ctx, api, "nick:0:" + std::to_string(uid));
    if (!gn.empty() && gn != "none") return gn;

    // 3. Стандартное имя пользователя Telegram
    char buf[128] = {0};
    if (api->user_name(ctx, uid, buf, sizeof(buf)) > 0 && *buf) {
        return std::string(buf);
    }
    return "User " + std::to_string(uid);
}

// ── 1. Команда .rp [on/off/access] ───────────────────────────────────────────
static void cmd_rp(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    ensure_default_commands(ctx, api);
    long long cid = api->chat_id(ctx);
    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";

    if (args.empty()) {
        api->c_reset(ctx);
        put_static_emoji(ctx, api, "INFO");
        api->c_fmt(ctx, " Управление RP-модулем:\n\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, "• .rp on", KOTO_ENT_CODE);
        api->c_text(ctx, " — Включить RP-команды в текущем чате.\n");
        api->c_fmt(ctx, "• .rp off", KOTO_ENT_CODE);
        api->c_text(ctx, " — Выключить RP-команды в текущем чате.\n");
        api->c_fmt(ctx, "• .rp access list", KOTO_ENT_CODE);
        api->c_text(ctx, " — Показать список пользователей с доступом.\n");
        api->c_fmt(ctx, "• .rp access add <@user/all>", KOTO_ENT_CODE);
        api->c_text(ctx, " — Дать доступ пользователю или всем (all).\n");
        api->c_fmt(ctx, "• .rp access remove <@user/all>", KOTO_ENT_CODE);
        api->c_text(ctx, " — Забрать доступ у пользователя или всех.\n\n");

        bool enabled = check_rp_enabled_in_chat(ctx, api, cid);
        api->c_fmt(ctx, "Статус в этом чате: ", KOTO_ENT_BOLD);
        if (enabled) {
            api->c_fmt(ctx, "ВКЛЮЧЕН ✅", KOTO_ENT_CODE);
        } else {
            api->c_fmt(ctx, "ВЫКЛЮЧЕН ❌", KOTO_ENT_CODE);
        }
        api->c_edit(ctx, 0);
        return;
    }

    auto parts = split(args, ' ');
    std::string sub = to_lower(parts[0]);

    if (sub == "on") {
        db_set_str(ctx, api, "enabled:" + std::to_string(cid), "1");
        api->c_reset(ctx);
        put_static_emoji(ctx, api, "SUCCESS");
        api->c_fmt(ctx, " RP-команды теперь ВКЛЮЧЕНЫ в этом чате!", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    if (sub == "off") {
        db_del(ctx, api, "enabled:" + std::to_string(cid));
        api->c_reset(ctx);
        put_static_emoji(ctx, api, "TRASH");
        api->c_fmt(ctx, " RP-команды теперь ВЫКЛЮЧЕНЫ в этом чате.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    if (sub == "access") {
        if (parts.size() < 2) {
            api->c_reset(ctx);
            put_static_emoji(ctx, api, "ERROR");
            api->c_fmt(ctx, " Использование: ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, ".rp access <add/remove/list> [@user/all]", KOTO_ENT_CODE);
            api->c_edit(ctx, 0);
            return;
        }

        std::string act = to_lower(parts[1]);

        if (act == "list") {
            api->c_reset(ctx);
            if (db_has(ctx, api, "public:" + std::to_string(cid))) {
                put_static_emoji(ctx, api, "SUCCESS");
                api->c_fmt(ctx, " Доступ к RP-командам в этом чате открыт для ВСЕХ (public)!", KOTO_ENT_BOLD);
            } else {
                put_static_emoji(ctx, api, "LOCK");
                api->c_fmt(ctx, " Доступ к RP-командам в этом чате:\n\n", KOTO_ENT_BOLD);
                char kbuf[4096] = {0};
                int kn = api->db_keys(ctx, kbuf, sizeof(kbuf) - 1);
                std::string prefix = "access:" + std::to_string(cid) + ":";
                bool found = false;
                if (kn > 0) {
                    auto keys = split(kbuf, '\n');
                    for (const auto& k : keys) {
                        if (k.rfind(prefix, 0) == 0) {
                            found = true;
                            std::string uid_str = k.substr(prefix.size());
                            long long uid = 0;
                            try { uid = std::stoll(uid_str); } catch (...) {}
                            char name_buf[128] = {0};
                            api->user_name(ctx, uid, name_buf, sizeof(name_buf));
                            std::string dname = *name_buf ? name_buf : ("ID: " + uid_str);
                            api->c_text(ctx, "• ");
                            api->c_url(ctx, dname.c_str(), ("tg://user?id=" + uid_str).c_str());
                            api->c_text(ctx, "\n");
                        }
                    }
                }
                if (!found) {
                    api->c_fmt(ctx, "Никому не выдан индивидуальный доступ.\n", KOTO_ENT_ITALIC);
                }
                api->c_fmt(ctx, "\nВладелец и доверенные (TRUSTED) всегда имеют доступ.", KOTO_ENT_ITALIC);
            }
            api->c_edit(ctx, 0);
            return;
        }

        if (act == "add" || act == "remove") {
            std::string target_str = (parts.size() > 2) ? parts[2] : "";
            long long target_uid = 0;

            if (target_str.empty()) {
                long long reply_uid = api->reply_sender_id(ctx);
                if (reply_uid != 0) {
                    target_uid = reply_uid;
                } else {
                    api->c_reset(ctx);
                    put_static_emoji(ctx, api, "ERROR");
                    api->c_fmt(ctx, " Укажите @user, ID или ответьте на сообщение.", KOTO_ENT_BOLD);
                    api->c_edit(ctx, 0);
                    return;
                }
            } else if (to_lower(target_str) == "all") {
                if (act == "add") {
                    db_set_str(ctx, api, "public:" + std::to_string(cid), "1");
                    api->c_reset(ctx);
                    put_static_emoji(ctx, api, "SUCCESS");
                    api->c_fmt(ctx, " Публичный доступ к RP-командам в этом чате ОТКРЫТ ДЛЯ ВСЕХ!", KOTO_ENT_BOLD);
                } else {
                    db_del(ctx, api, "public:" + std::to_string(cid));
                    api->c_reset(ctx);
                    put_static_emoji(ctx, api, "TRASH");
                    api->c_fmt(ctx, " Публичный доступ к RP-командам в этом чате ЗАКРЫТ.", KOTO_ENT_BOLD);
                }
                api->c_edit(ctx, 0);
                return;
            } else {
                if (target_str.front() == '@') {
                    target_uid = api->resolve(ctx, target_str.substr(1).c_str());
                } else {
                    try { target_uid = std::stoll(target_str); } catch (...) {}
                }
            }

            if (target_uid == 0) {
                api->c_reset(ctx);
                put_static_emoji(ctx, api, "ERROR");
                api->c_fmt(ctx, " Не удалось найти указанного пользователя.", KOTO_ENT_BOLD);
                api->c_edit(ctx, 0);
                return;
            }

            char name_buf[128] = {0};
            api->user_name(ctx, target_uid, name_buf, sizeof(name_buf));
            std::string dname = *name_buf ? name_buf : ("ID: " + std::to_string(target_uid));

            if (act == "add") {
                db_set_str(ctx, api, "access:" + std::to_string(cid) + ":" + std::to_string(target_uid), "1");
                api->c_reset(ctx);
                put_static_emoji(ctx, api, "SUCCESS");
                api->c_fmt(ctx, " Пользователь ", KOTO_ENT_BOLD);
                api->c_fmt(ctx, dname.c_str(), KOTO_ENT_CODE);
                api->c_fmt(ctx, " получил доступ к RP-командам в этом чате.", KOTO_ENT_BOLD);
            } else {
                db_del(ctx, api, "access:" + std::to_string(cid) + ":" + std::to_string(target_uid));
                api->c_reset(ctx);
                put_static_emoji(ctx, api, "TRASH");
                api->c_fmt(ctx, " Пользователь ", KOTO_ENT_BOLD);
                api->c_fmt(ctx, dname.c_str(), KOTO_ENT_CODE);
                api->c_fmt(ctx, " лишен доступа к RP-командам в этом чате.", KOTO_ENT_BOLD);
            }
            api->c_edit(ctx, 0);
            return;
        }
    }

    api->c_reset(ctx);
    put_static_emoji(ctx, api, "ERROR");
    api->c_fmt(ctx, " Неизвестная подкоманда. Используйте on, off или access.", KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

// ── 2. Команда .addrp <алиасы>|<действие>|<эмодзи/ID> ─────────────────────────
static void cmd_addrp(koto_ctx* ctx, const koto_api* api) {
    long long sender = api->sender_id(ctx);
    if (!is_rp_creator(ctx, api, sender)) return;

    ensure_default_commands(ctx, api);
    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";

    auto parts = split(args, '|');
    if (parts.size() < 3) {
        api->c_reset(ctx);
        put_static_emoji(ctx, api, "ERROR");
        api->c_fmt(ctx, " Неверный формат!\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".addrp команда/алиас|действие|эмодзи", KOTO_ENT_CODE);
        api->c_text(ctx, "\nПример: ");
        api->c_fmt(ctx, ".addrp обнять/hug|обнял(а)|🤗", KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    std::string aliases_str = parts[0];
    std::string action = parts[1];
    std::string emoji_str = parts[2];

    auto aliases = split(aliases_str, '/');
    if (aliases.empty()) {
        api->c_reset(ctx);
        put_static_emoji(ctx, api, "ERROR");
        api->c_fmt(ctx, " Укажите хотя бы одно имя команды.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    long long doc_id = 0;
    std::string fallback = emoji_str;

    // Проверяем entity премиум-эмодзи во входящем сообщении
    int ent_cnt = api->msg_entity_count(ctx);
    for (int i = 0; i < ent_cnt; ++i) {
        int type = 0, off = 0, len = 0;
        long long did = 0;
        api->msg_entity(ctx, i, &type, &off, &len, &did, nullptr, 0);
        if (type == KOTO_ENT_CUSTOM_EMOJI && did != 0) {
            doc_id = did;
            break;
        }
    }

    if (doc_id == 0 && !emoji_str.empty()) {
        try {
            if (isdigit(emoji_str[0])) doc_id = std::stoll(emoji_str);
        } catch (...) {}
    }

    std::string val = action + "|" + std::to_string(doc_id) + "|" + fallback;
    std::string cmd_list = db_get_str(ctx, api, "cmd_list");
    std::set<std::string> cur_cmds;
    if (!cmd_list.empty()) {
        auto citems = split(cmd_list, ',');
        cur_cmds.insert(citems.begin(), citems.end());
    }

    std::string added_names;
    for (const auto& a : aliases) {
        std::string al_low = to_lower(a);
        db_set_str(ctx, api, "cmd:" + al_low, val);
        cur_cmds.insert(al_low);
        if (!added_names.empty()) added_names += ", ";
        added_names += al_low;
    }

    std::string new_list;
    for (const auto& c : cur_cmds) {
        if (!new_list.empty()) new_list += ",";
        new_list += c;
    }
    db_set_str(ctx, api, "cmd_list", new_list);

    api->c_reset(ctx);
    put_static_emoji(ctx, api, "SUCCESS");
    api->c_fmt(ctx, " RP-команда(ы) ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, added_names.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, " успешно добавлены!", KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

// ── 3. Команда .delrp <алиас/all/prem/simple> ──────────────────────────────────
static void cmd_delrp(koto_ctx* ctx, const koto_api* api) {
    long long sender = api->sender_id(ctx);
    if (!is_rp_creator(ctx, api, sender)) return;

    ensure_default_commands(ctx, api);
    const char* raw_args = api->cmd_args(ctx);
    std::string target = raw_args ? to_lower(trim(raw_args)) : "";

    if (target.empty()) {
        api->c_reset(ctx);
        put_static_emoji(ctx, api, "ERROR");
        api->c_fmt(ctx, " Укажите команду для удаления (или all / prem / simple).", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    std::string cmd_list = db_get_str(ctx, api, "cmd_list");
    auto cur_cmds = split(cmd_list, ',');

    if (target == "all") {
        for (const auto& c : cur_cmds) db_del(ctx, api, "cmd:" + c);
        db_del(ctx, api, "cmd_list");
        api->c_reset(ctx);
        put_static_emoji(ctx, api, "TRASH");
        api->c_fmt(ctx, " Все RP-команды удалены!", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    std::vector<std::string> remaining;
    int deleted = 0;

    for (const auto& c : cur_cmds) {
        bool del_this = false;
        if (target == "prem") {
            std::string val = db_get_str(ctx, api, "cmd:" + c);
            auto p = split(val, '|');
            if (p.size() >= 2 && p[1] != "0") del_this = true;
        } else if (target == "simple") {
            std::string val = db_get_str(ctx, api, "cmd:" + c);
            auto p = split(val, '|');
            if (p.size() < 2 || p[1] == "0") del_this = true;
        } else if (target == c) {
            del_this = true;
        }

        if (del_this) {
            db_del(ctx, api, "cmd:" + c);
            deleted++;
        } else {
            remaining.push_back(c);
        }
    }

    std::string new_list;
    for (const auto& c : remaining) {
        if (!new_list.empty()) new_list += ",";
        new_list += c;
    }
    db_set_str(ctx, api, "cmd_list", new_list);

    api->c_reset(ctx);
    if (deleted > 0) {
        put_static_emoji(ctx, api, "TRASH");
        api->c_fmt(ctx, " Удалено команд: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, std::to_string(deleted).c_str(), KOTO_ENT_CODE);
    } else {
        put_static_emoji(ctx, api, "INFO");
        api->c_fmt(ctx, " Команда не найдена.", KOTO_ENT_BOLD);
    }
    api->c_edit(ctx, 0);
}

// ── 4. Команда .rplist ────────────────────────────────────────────────────────
static void cmd_rplist(koto_ctx* ctx, const koto_api* api) {
    ensure_default_commands(ctx, api);
    std::string cmd_list = db_get_str(ctx, api, "cmd_list");
    auto cur_cmds = split(cmd_list, ',');

    if (cur_cmds.empty()) {
        api->c_reset(ctx);
        put_static_emoji(ctx, api, "INFO");
        api->c_fmt(ctx, " Список RP-команд пуст!", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    // Группировка по действию
    struct ActionGroup {
        std::vector<std::string> aliases;
        long long doc_id = 0;
        std::string fallback = "✨";
    };
    std::map<std::string, ActionGroup> groups;

    for (const auto& c : cur_cmds) {
        std::string val = db_get_str(ctx, api, "cmd:" + c);
        auto p = split(val, '|');
        if (p.size() >= 3) {
            std::string action = p[0];
            long long did = 0;
            try { did = std::stoll(p[1]); } catch (...) {}
            std::string fb = p[2];
            groups[action].aliases.push_back(c);
            groups[action].doc_id = did;
            groups[action].fallback = fb;
        }
    }

    api->c_reset(ctx);
    put_static_emoji(ctx, api, "LIST");
    api->c_fmt(ctx, " Доступные RP-команды:\n\n", KOTO_ENT_BOLD);

    for (const auto& [action, grp] : groups) {
        api->c_text(ctx, "• ");
        std::string al_str;
        for (size_t i = 0; i < grp.aliases.size(); ++i) {
            if (i > 0) al_str += ", ";
            al_str += grp.aliases[i];
        }
        api->c_fmt(ctx, al_str.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, " — ");
        api->c_text(ctx, action.c_str());
        api->c_text(ctx, " ");
        if (grp.doc_id != 0) {
            api->c_emoji(ctx, grp.fallback.c_str(), grp.doc_id);
        } else {
            api->c_text(ctx, grp.fallback.c_str());
        }
        api->c_text(ctx, "\n");
    }

    api->c_fmt(ctx, "\nВсего действий: ", KOTO_ENT_ITALIC);
    api->c_fmt(ctx, std::to_string(groups.size()).c_str(), KOTO_ENT_CODE);
    api->c_edit(ctx, 0);
}

// ── 5. Команда .setrpnick [-g] [@user] <ник> ──────────────────────────────────
static void cmd_setrpnick(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;
    long long cid = api->chat_id(ctx);

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";
    if (args.empty()) {
        api->c_reset(ctx);
        put_static_emoji(ctx, api, "ERROR");
        api->c_fmt(ctx, " Использование: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".setrpnick [-g] [@user] <ник>", KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    bool is_global = false;
    if (args.rfind("-g ", 0) == 0 || args.rfind("--g ", 0) == 0) {
        is_global = true;
        args = trim(args.substr(args.find(' ')));
    }

    long long target_uid = api->reply_sender_id(ctx);
    std::string nick = args;

    if (!args.empty() && args.front() == '@') {
        size_t sp = args.find(' ');
        if (sp != std::string::npos) {
            std::string ustr = args.substr(1, sp - 1);
            target_uid = api->resolve(ctx, ustr.c_str());
            nick = trim(args.substr(sp));
        }
    }

    if (target_uid == 0) target_uid = api->get_me(ctx);

    if (nick.empty()) {
        api->c_reset(ctx);
        put_static_emoji(ctx, api, "ERROR");
        api->c_fmt(ctx, " Вы не указали никнейм.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    std::string key = is_global ? ("nick:0:" + std::to_string(target_uid))
                                : ("nick:" + std::to_string(cid) + ":" + std::to_string(target_uid));
    db_set_str(ctx, api, key, nick);

    char name_buf[128] = {0};
    api->user_name(ctx, target_uid, name_buf, sizeof(name_buf));
    std::string dname = *name_buf ? name_buf : ("ID: " + std::to_string(target_uid));

    api->c_reset(ctx);
    put_static_emoji(ctx, api, "SUCCESS");
    api->c_fmt(ctx, is_global ? " Глобальный RP-ник для " : " RP-ник в этом чате для ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, dname.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, " установлен на: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, nick.c_str(), KOTO_ENT_CODE);
    api->c_edit(ctx, 0);
}

// ── 6. Команда .delrpnick [-g] [@user] ────────────────────────────────────────
static void cmd_delrpnick(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;
    long long cid = api->chat_id(ctx);

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";

    bool is_global = false;
    if (args.find("-g") != std::string::npos) {
        is_global = true;
    }

    long long target_uid = api->reply_sender_id(ctx);
    if (target_uid == 0 && !args.empty() && args.front() == '@') {
        target_uid = api->resolve(ctx, args.substr(1).c_str());
    }
    if (target_uid == 0) target_uid = api->get_me(ctx);

    char name_buf[128] = {0};
    api->user_name(ctx, target_uid, name_buf, sizeof(name_buf));
    std::string dname = *name_buf ? name_buf : ("ID: " + std::to_string(target_uid));

    api->c_reset(ctx);
    if (is_global) {
        db_del(ctx, api, "nick:0:" + std::to_string(target_uid));
        put_static_emoji(ctx, api, "TRASH");
        api->c_fmt(ctx, " Глобальный RP-ник для ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, dname.c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, " удален.", KOTO_ENT_BOLD);
    } else {
        db_set_str(ctx, api, "nick:" + std::to_string(cid) + ":" + std::to_string(target_uid), "none");
        put_static_emoji(ctx, api, "SUCCESS");
        api->c_fmt(ctx, " Отображение RP-ника для ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, dname.c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, " в этом чате отключено.", KOTO_ENT_BOLD);
    }
    api->c_edit(ctx, 0);
}

// ── 7. Команда .rpnick [@user] ────────────────────────────────────────────────
static void cmd_rpnick(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);
    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";

    long long target_uid = api->reply_sender_id(ctx);
    if (target_uid == 0 && !args.empty() && args.front() == '@') {
        target_uid = api->resolve(ctx, args.substr(1).c_str());
    }
    if (target_uid == 0) target_uid = api->get_me(ctx);

    std::string chat_nick = db_get_str(ctx, api, "nick:" + std::to_string(cid) + ":" + std::to_string(target_uid));
    std::string global_nick = db_get_str(ctx, api, "nick:0:" + std::to_string(target_uid));

    char name_buf[128] = {0};
    api->user_name(ctx, target_uid, name_buf, sizeof(name_buf));
    std::string dname = *name_buf ? name_buf : ("ID: " + std::to_string(target_uid));

    api->c_reset(ctx);
    put_static_emoji(ctx, api, "NICK");
    api->c_fmt(ctx, " RP-ники для ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, dname.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, ":\n", KOTO_ENT_BOLD);

    api->c_text(ctx, "• В этом чате: ");
    if (!chat_nick.empty() && chat_nick != "none") {
        api->c_fmt(ctx, chat_nick.c_str(), KOTO_ENT_CODE);
    } else {
        api->c_fmt(ctx, "не установлен", KOTO_ENT_ITALIC);
    }
    api->c_text(ctx, "\n• Глобальный: ");
    if (!global_nick.empty() && global_nick != "none") {
        api->c_fmt(ctx, global_nick.c_str(), KOTO_ENT_CODE);
    } else {
        api->c_fmt(ctx, "не установлен", KOTO_ENT_ITALIC);
    }

    api->c_edit(ctx, 0);
}

// ── 8. Команда .addrpcreator [@user] ──────────────────────────────────────────
static void cmd_addrpcreator(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";
    long long target_uid = api->reply_sender_id(ctx);
    if (target_uid == 0 && !args.empty() && args.front() == '@') {
        target_uid = api->resolve(ctx, args.substr(1).c_str());
    } else if (target_uid == 0 && !args.empty()) {
        try { target_uid = std::stoll(args); } catch (...) {}
    }

    if (target_uid == 0) {
        api->c_reset(ctx);
        put_static_emoji(ctx, api, "ERROR");
        api->c_fmt(ctx, " Укажите @user, ID или ответьте на сообщение.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    db_set_str(ctx, api, "creator:" + std::to_string(target_uid), "1");
    char name_buf[128] = {0};
    api->user_name(ctx, target_uid, name_buf, sizeof(name_buf));
    std::string dname = *name_buf ? name_buf : ("ID: " + std::to_string(target_uid));

    api->c_reset(ctx);
    put_static_emoji(ctx, api, "SUCCESS");
    api->c_fmt(ctx, " Пользователь ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, dname.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, " теперь является создателем RP-команд!", KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

// ── 9. Команда .delrpcreator [@user] ──────────────────────────────────────────
static void cmd_delrpcreator(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";
    long long target_uid = api->reply_sender_id(ctx);
    if (target_uid == 0 && !args.empty() && args.front() == '@') {
        target_uid = api->resolve(ctx, args.substr(1).c_str());
    } else if (target_uid == 0 && !args.empty()) {
        try { target_uid = std::stoll(args); } catch (...) {}
    }

    if (target_uid == 0) {
        api->c_reset(ctx);
        put_static_emoji(ctx, api, "ERROR");
        api->c_fmt(ctx, " Укажите @user, ID или ответьте на сообщение.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    db_del(ctx, api, "creator:" + std::to_string(target_uid));
    char name_buf[128] = {0};
    api->user_name(ctx, target_uid, name_buf, sizeof(name_buf));
    std::string dname = *name_buf ? name_buf : ("ID: " + std::to_string(target_uid));

    api->c_reset(ctx);
    put_static_emoji(ctx, api, "TRASH");
    api->c_fmt(ctx, " Пользователь ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, dname.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, " лишен прав создателя RP-команд.", KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

// ── 10. Команда .listrpcreators ──────────────────────────────────────────────
static void cmd_listrpcreators(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    api->c_reset(ctx);
    put_static_emoji(ctx, api, "CROWN");
    api->c_fmt(ctx, " Список создателей RP-команд:\n\n", KOTO_ENT_BOLD);

    char kbuf[4096] = {0};
    int kn = api->db_keys(ctx, kbuf, sizeof(kbuf) - 1);
    bool found = false;

    if (kn > 0) {
        auto keys = split(kbuf, '\n');
        for (const auto& k : keys) {
            if (k.rfind("creator:", 0) == 0) {
                found = true;
                std::string uid_str = k.substr(8);
                long long uid = 0;
                try { uid = std::stoll(uid_str); } catch (...) {}
                char name_buf[128] = {0};
                api->user_name(ctx, uid, name_buf, sizeof(name_buf));
                std::string dname = *name_buf ? name_buf : ("ID: " + uid_str);
                api->c_text(ctx, "• ");
                api->c_url(ctx, dname.c_str(), ("tg://user?id=" + uid_str).c_str());
                api->c_text(ctx, "\n");
            }
        }
    }

    if (!found) {
        api->c_fmt(ctx, "Список пуст. Только владелец и доверенные (TRUSTED) могут создавать команды.\n", KOTO_ENT_ITALIC);
    }
    api->c_edit(ctx, 0);
}

// ── 11. Команда .setrpemoji <KEY> <ID/эмодзи> [fallback] ─────────────────────
static void cmd_setrpemoji(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";
    auto parts = split(args, ' ');

    if (parts.size() < 2) {
        api->c_reset(ctx);
        put_static_emoji(ctx, api, "ERROR");
        api->c_fmt(ctx, " Использование: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".setrpemoji <КЛЮЧ> <ID или эмодзи> [fallback]", KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    std::string key_upper = parts[0];
    for (char& c : key_upper) c = (char)std::toupper((unsigned char)c);

    long long doc_id = 0;
    std::string fallback = (parts.size() > 2) ? parts[2] : "✨";

    // Проверяем entity премиум эмодзи
    int ent_cnt = api->msg_entity_count(ctx);
    for (int i = 0; i < ent_cnt; ++i) {
        int type = 0, off = 0, len = 0;
        long long did = 0;
        api->msg_entity(ctx, i, &type, &off, &len, &did, nullptr, 0);
        if (type == KOTO_ENT_CUSTOM_EMOJI && did != 0) {
            doc_id = did;
            break;
        }
    }

    if (doc_id == 0) {
        try { doc_id = std::stoll(parts[1]); } catch (...) { fallback = parts[1]; }
    }

    db_set_str(ctx, api, "se:" + key_upper, std::to_string(doc_id) + "|" + fallback);

    api->c_reset(ctx);
    put_static_emoji(ctx, api, "SUCCESS");
    api->c_fmt(ctx, " Эмодзи для ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, key_upper.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, " успешно обновлен!", KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

// ── 12. Команда .delrpemoji <KEY> ─────────────────────────────────────────────
static void cmd_delrpemoji(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    const char* raw_args = api->cmd_args(ctx);
    std::string key_upper = raw_args ? trim(raw_args) : "";
    for (char& c : key_upper) c = (char)std::toupper((unsigned char)c);

    if (key_upper.empty()) {
        api->c_reset(ctx);
        put_static_emoji(ctx, api, "ERROR");
        api->c_fmt(ctx, " Укажите ключ для сброса (например, ERROR, SUCCESS).", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    db_del(ctx, api, "se:" + key_upper);

    api->c_reset(ctx);
    put_static_emoji(ctx, api, "TRASH");
    api->c_fmt(ctx, " Эмодзи для ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, key_upper.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, " сброшен к дефолтному.", KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

// ── 13. Команда .rpemojis ─────────────────────────────────────────────────────
static void cmd_rpemojis(koto_ctx* ctx, const koto_api* api) {
    api->c_reset(ctx);
    put_static_emoji(ctx, api, "SETTINGS");
    api->c_fmt(ctx, " Статические RP-эмодзи:\n\n", KOTO_ENT_BOLD);

    for (const auto& item : DEFAULT_STATIC_EMOJIS) {
        api->c_text(ctx, "• ");
        put_static_emoji(ctx, api, item.key);
        api->c_fmt(ctx, (" " + std::string(item.key)).c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, " (дефолт: ");
        api->c_text(ctx, item.fallback);
        api->c_text(ctx, ")\n");
    }

    api->c_edit(ctx, 0);
}

// ── ГЛАВНЫЙ WATCHER: Обработка RP-действий ────────────────────────────────────
static int rp_watch(koto_ctx* ctx, const koto_api* api) {
    const char* raw_text = api->msg_text(ctx);
    if (!raw_text || !*raw_text) return 0;

    std::string text = trim(raw_text);
    if (text.empty()) return 0;

    long long cid = api->chat_id(ctx);
    long long sender = api->sender_id(ctx);

    // 1. Проверяем, включен ли RP в этом чате
    if (!check_rp_enabled_in_chat(ctx, api, cid)) {
        return 0;
    }

    ensure_default_commands(ctx, api);

    // Извлекаем первое слово
    size_t space_pos = text.find_first_of(" \t\n");
    std::string first_word = (space_pos != std::string::npos) ? text.substr(0, space_pos) : text;
    std::string rest = (space_pos != std::string::npos) ? trim(text.substr(space_pos)) : "";

    // Снимаем ведущий префикс '.', '!', '/' если есть
    std::string cmd = first_word;
    if (!cmd.empty() && (cmd[0] == '.' || cmd[0] == '!' || cmd[0] == '/')) {
        cmd = cmd.substr(1);
    }
    cmd = to_lower(cmd);

    // 2. Ищем команду в базе
    std::string val = db_get_str(ctx, api, "cmd:" + cmd);
    if (val.empty()) {
        return 0; // Не RP-команда
    }

    // 3. Проверка прав доступа в группе
    if (cid < 0) { // Группа или супергруппа
        if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) {
            bool is_public = db_has(ctx, api, "public:" + std::to_string(cid));
            bool has_access = db_has(ctx, api, "access:" + std::to_string(cid) + ":" + std::to_string(sender));
            if (!is_public && !has_access) {
                return 0; // Нет доступа в этом чате
            }
        }
    }

    // 4. Парсим данные действия: action|doc_id|fallback
    auto p = split(val, '|');
    if (p.size() < 3) return 0;
    std::string action = p[0];
    long long doc_id = 0;
    try { doc_id = std::stoll(p[1]); } catch (...) {}
    std::string fallback = p[2];

    // 5. Определение цели (target_uid) и комментария
    long long target_uid = api->reply_sender_id(ctx);
    std::string comment = rest;

    if (target_uid == 0 && !rest.empty()) {
        // Проверяем, начинается ли rest с @username или ID
        size_t r_space = rest.find_first_of(" \t\n");
        std::string first_arg = (r_space != std::string::npos) ? rest.substr(0, r_space) : rest;
        std::string potential_comment = (r_space != std::string::npos) ? trim(rest.substr(r_space)) : "";

        if (!first_arg.empty() && first_arg.front() == '@') {
            long long res_uid = api->resolve(ctx, first_arg.substr(1).c_str());
            if (res_uid != 0) {
                target_uid = res_uid;
                comment = potential_comment;
            }
        } else if (!first_arg.empty() && isdigit(first_arg.front())) {
            try {
                long long res_uid = std::stoll(first_arg);
                if (res_uid != 0) {
                    target_uid = res_uid;
                    comment = potential_comment;
                }
            } catch (...) {}
        }
    }

    // В ЛС с ботом или юзером без реплая цель = собеседник
    if (target_uid == 0 && cid > 0) {
        if (api->is_outgoing(ctx)) {
            target_uid = cid;
        } else {
            target_uid = api->get_me(ctx);
        }
    }

    // Имена участников
    std::string sender_name = get_rp_display_name(ctx, api, sender, cid);

    // 6. Формирование сообщения действия
    api->c_reset(ctx);

    if (doc_id != 0) {
        api->c_emoji(ctx, fallback.c_str(), doc_id);
    } else {
        api->c_text(ctx, fallback.c_str());
    }
    api->c_text(ctx, " | ");

    // Имя отправителя как ссылка
    api->c_url(ctx, sender_name.c_str(), ("tg://user?id=" + std::to_string(sender)).c_str());
    api->c_text(ctx, " ");

    // Действие (жирным)
    api->c_fmt(ctx, action.c_str(), KOTO_ENT_BOLD);

    if (target_uid == 0 || target_uid == sender) {
        api->c_fmt(ctx, " самого/саму себя", KOTO_ENT_BOLD);
    } else {
        std::string target_name = get_rp_display_name(ctx, api, target_uid, cid);
        api->c_text(ctx, " ");
        api->c_url(ctx, target_name.c_str(), ("tg://user?id=" + std::to_string(target_uid)).c_str());
    }

    // Комментарий (если есть)
    if (!comment.empty()) {
        api->c_text(ctx, "\n\n");
        put_static_emoji(ctx, api, "COMMENT");
        api->c_text(ctx, " ");
        api->c_fmt(ctx, comment.c_str(), KOTO_ENT_ITALIC);
    }

    // 7. Отправка
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, KOTO_NO_LINK_PREVIEW);
    } else {
        api->c_send(ctx, cid, KOTO_NO_LINK_PREVIEW);
        // Если входящее сообщение было от нашего же твинка:
        if (sender == api->get_me(ctx)) {
            api->delete_msg(ctx);
        }
    }

    return 1; // Команда успешно обработана watcher'ом
}

// ── Таблица команд ────────────────────────────────────────────────────────────
static const koto_command COMMANDS[] = {
    { "rp",             &cmd_rp,             KOTO_LEVEL_TRUSTED, "Управление RP-модулем в чате (on/off/access)", "[on|off|access]" },
    { "addrp",          &cmd_addrp,          KOTO_LEVEL_ALL,     "Добавить новую RP-команду", "<алиасы>|<действие>|<эмодзи>" },
    { "delrp",          &cmd_delrp,          KOTO_LEVEL_ALL,     "Удалить RP-команду", "<алиас|all|prem|simple>" },
    { "rplist",         &cmd_rplist,         KOTO_LEVEL_ALL,     "Список всех доступных RP-команд", NULL },
    { "setrpnick",      &cmd_setrpnick,      KOTO_LEVEL_TRUSTED, "Установить RP-ник пользователю", "[-g] [@user] <ник>" },
    { "delrpnick",      &cmd_delrpnick,      KOTO_LEVEL_TRUSTED, "Удалить или скрыть RP-ник", "[-g] [@user]" },
    { "rpnick",         &cmd_rpnick,         KOTO_LEVEL_ALL,     "Посмотреть текущие RP-ники", "[@user]" },
    { "addrpcreator",   &cmd_addrpcreator,   KOTO_LEVEL_TRUSTED, "Назначить создателя RP-команд", "[@user]" },
    { "delrpcreator",   &cmd_delrpcreator,   KOTO_LEVEL_TRUSTED, "Снять права создателя RP-команд", "[@user]" },
    { "listrpcreators", &cmd_listrpcreators, KOTO_LEVEL_TRUSTED, "Список создателей RP-команд", NULL },
    { "setrpemoji",     &cmd_setrpemoji,     KOTO_LEVEL_TRUSTED, "Настроить кастомный эмодзи для ключа", "<KEY> <ID/эмодзи> [fallback]" },
    { "delrpemoji",     &cmd_delrpemoji,     KOTO_LEVEL_TRUSTED, "Сбросить эмодзи ключа к дефолту", "<KEY>" },
    { "rpemojis",       &cmd_rpemojis,       KOTO_LEVEL_ALL,     "Список статических RP-эмодзи", NULL }
};

// ── Экспорт модуля ────────────────────────────────────────────────────────────
static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "rp",
    "Ролевые команды (Role-Play), РП-ники и кастомные действия (@Kote)",
    "2.8.0",
    12, // KoteLoader v0.2.1+
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    &rp_watch,
    KOTO_WATCH_ALL,
    NULL, // on_callback
    NULL, // settings
    0,
    "https://github.com/AresUser1/KoteModules"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
