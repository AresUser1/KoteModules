// rp.cpp
// Полнофункциональный Role-Play модуль для Kotogram (C/C++ ABI 3).
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

// ── Очистка старых предустановленных дефолтов при обновлении ───────────────────

static void cleanup_old_defaults_if_needed(koto_ctx* ctx, const koto_api* api) {
    std::string v = db_get_str(ctx, api, "v_cleaned");
    if (v == "2.9.0") return;

    static const char* OLD_DEFAULTS[] = {
        "обнять", "hug", "поцеловать", "kiss", "кусь", "bite", "ударить", "slap",
        "погладить", "pat", "убить", "kill", "чай", "tea", "кофе", "coffee",
        "спать", "sleep", "прижать", "cuddle", "ущипнуть", "pinch", "покормить", "feed",
        "лизнуть", "lick", "пнуть", "kick", "выпить", "drink", "расстрелять", "shoot",
        "связать", "tie", "укрыть", "blanket", "подарить", "gift", "похвалить", "praise",
        "испугать", "scare", "утешить", "comfort"
    };

    std::string cur_list = db_get_str(ctx, api, "cmd_list");
    auto cmds = split(cur_list, ',');
    std::vector<std::string> remaining;
    for (const auto& c : cmds) {
        bool is_old = false;
        for (const char* old_cmd : OLD_DEFAULTS) {
            if (c == old_cmd) { is_old = true; break; }
        }
        if (is_old) {
            db_del(ctx, api, "cmd:" + c);
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
    db_set_str(ctx, api, "v_cleaned", "2.9.0");
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
    if (uid == 0) return "Пользователь";

    // 1. Ник для данного чата
    std::string cn = db_get_str(ctx, api, "nick:" + std::to_string(cid) + ":" + std::to_string(uid));
    if (!cn.empty() && cn != "none") return cn;

    // 2. Глобальный ник
    std::string gn = db_get_str(ctx, api, "nick:0:" + std::to_string(uid));
    if (!gn.empty() && gn != "none") return gn;

    // 3. Если это наш собственный аккаунт - берем имя аккаунта из хоста:
    if (uid == api->get_me(ctx)) {
        char mbuf[128] = {0};
        if (api->get_me_name(ctx, mbuf, sizeof(mbuf)) > 0 && *mbuf) {
            return std::string(mbuf);
        }
    }

    // 4. Имя Telegram из хоста (если известно)
    char buf[128] = {0};
    if (api->user_name(ctx, uid, buf, sizeof(buf)) > 0 && *buf) {
        return std::string(buf);
    }

    return (uid == api->get_me(ctx)) ? "Я" : "Пользователь";
}

// ── 1. Команда .rp [on/off/access] ───────────────────────────────────────────
static void cmd_rp(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    cleanup_old_defaults_if_needed(ctx, api);
    long long cid = api->chat_id(ctx);
    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";

    if (args.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "ℹ️ Управление RP-модулем:\n\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, "• .rp on", KOTO_ENT_CODE);
        api->c_text(ctx, " — включить RP в этом чате\n");
        api->c_fmt(ctx, "• .rp off", KOTO_ENT_CODE);
        api->c_text(ctx, " — выключить RP в этом чате\n");
        api->c_fmt(ctx, "• .rp access [all|trusted|creators]", KOTO_ENT_CODE);
        api->c_text(ctx, " — уровень доступа к командам в чате\n");
        api->c_fmt(ctx, "• .rp access add/del [@user]", KOTO_ENT_CODE);
        api->c_text(ctx, " — персональный доступ пользователю\n");
        api->c_fmt(ctx, "• .rp access list", KOTO_ENT_CODE);
        api->c_text(ctx, " — список пользователей с доступом");
        api->c_edit(ctx, 0);
        return;
    }

    auto sub = split(args, ' ');
    std::string subcmd = to_lower(sub[0]);

    if (subcmd == "on") {
        db_set_str(ctx, api, "enabled:" + std::to_string(cid), "1");
        api->c_reset(ctx);
        api->c_fmt(ctx, "✅ RP-модуль успешно ВКЛЮЧЕН в этом чате!", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
    } else if (subcmd == "off") {
        db_del(ctx, api, "enabled:" + std::to_string(cid));
        api->c_reset(ctx);
        api->c_fmt(ctx, "🔒 RP-модуль ВЫКЛЮЧЕН в этом чате.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
    } else if (subcmd == "access") {
        if (sub.size() < 2) {
            bool pub = db_has(ctx, api, "public:" + std::to_string(cid));
            api->c_reset(ctx);
            api->c_fmt(ctx, "ℹ️ Текущий режим доступа в чате: ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, pub ? "all (все участники)" : "restricted (только с доступом)", KOTO_ENT_CODE);
            api->c_edit(ctx, 0);
            return;
        }

        std::string mode = to_lower(sub[1]);
        if (mode == "all") {
            db_set_str(ctx, api, "public:" + std::to_string(cid), "1");
            api->c_reset(ctx);
            api->c_fmt(ctx, "✅ Доступ к RP-командам открыт для ВСЕХ участников чата.", KOTO_ENT_BOLD);
            api->c_edit(ctx, 0);
        } else if (mode == "trusted" || mode == "creators" || mode == "restricted") {
            db_del(ctx, api, "public:" + std::to_string(cid));
            api->c_reset(ctx);
            api->c_fmt(ctx, "🔒 Доступ к RP-командам ограничен (только доверенные и создатели).", KOTO_ENT_BOLD);
            api->c_edit(ctx, 0);
        } else if (mode == "add" || mode == "del") {
            long long target_uid = api->reply_sender_id(ctx);
            if (sub.size() >= 3) {
                std::string uarg = sub[2];
                if (uarg.front() == '@') {
                    target_uid = api->resolve(ctx, uarg.substr(1).c_str());
                } else if (isdigit(uarg.front())) {
                    try { target_uid = std::stoll(uarg); } catch (...) {}
                }
            }
            if (target_uid == 0) {
                api->c_reset(ctx);
                api->c_fmt(ctx, "❌ Укажите пользователя (@username, ID или реплай).", KOTO_ENT_BOLD);
                api->c_edit(ctx, 0);
                return;
            }

            if (mode == "add") {
                db_set_str(ctx, api, "access:" + std::to_string(cid) + ":" + std::to_string(target_uid), "1");
                api->c_reset(ctx);
                api->c_fmt(ctx, "✅ Пользователю предоставлен доступ к RP в этом чате.", KOTO_ENT_BOLD);
                api->c_edit(ctx, 0);
            } else {
                db_del(ctx, api, "access:" + std::to_string(cid) + ":" + std::to_string(target_uid));
                api->c_reset(ctx);
                api->c_fmt(ctx, "🗑️ Пользователь лишен доступа к RP в этом чате.", KOTO_ENT_BOLD);
                api->c_edit(ctx, 0);
            }
        } else if (mode == "list") {
            api->c_reset(ctx);
            api->c_fmt(ctx, "📋 Пользователи с доступом к RP в этом чате:\n", KOTO_ENT_BOLD);
            char keys_buf[4096] = {0};
            int n = api->db_keys(ctx, keys_buf, sizeof(keys_buf) - 1);
            int count = 0;
            if (n > 0) {
                std::string pfx = "access:" + std::to_string(cid) + ":";
                std::stringstream ss(keys_buf);
                std::string k;
                while (std::getline(ss, k, '\n')) {
                    if (k.rfind(pfx, 0) == 0) {
                        std::string uid_str = k.substr(pfx.size());
                        api->c_text(ctx, "• ID ");
                        api->c_fmt(ctx, uid_str.c_str(), KOTO_ENT_CODE);
                        api->c_text(ctx, "\n");
                        count++;
                    }
                }
            }
            if (count == 0) {
                api->c_text(ctx, "(список пуст)");
            }
            api->c_edit(ctx, 0);
        }
    }
}

// ── 2. Команда .addrp <алиасы>|<действие> ─────────────────────────────────────
static void cmd_addrp(koto_ctx* ctx, const koto_api* api) {
    long long sender = api->sender_id(ctx);
    if (!is_rp_creator(ctx, api, sender)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ У вас нет прав создателя RP-команд.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";
    if (args.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "ℹ️ Использование:\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".addrp <алиасы>|<действие>", KOTO_ENT_CODE);
        api->c_text(ctx, "\n\nПримеры:\n");
        api->c_fmt(ctx, ".addrp обнять|обнял(а)", KOTO_ENT_CODE);
        api->c_text(ctx, "\n");
        api->c_fmt(ctx, ".addrp кусь/кусить/сделать кусь|сделал(а) кусь", KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    auto parts = split(args, '|');
    if (parts.size() < 2) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Неверный формат! Разделите алиасы и действие символом '|'.\nПример: .addrp обнять|обнял(а)", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    std::string action = parts.back();
    std::vector<std::string> aliases;
    for (size_t i = 0; i < parts.size() - 1; ++i) {
        auto sub = split(parts[i], '/');
        for (const auto& a : sub) {
            std::string t = to_lower(trim(a));
            if (!t.empty()) {
                if (t.front() == '.') t = t.substr(1);
                aliases.push_back(t);
            }
        }
    }

    if (aliases.empty() || action.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Алиасы или действие не могут быть пустыми.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    std::string cur_list = db_get_str(ctx, api, "cmd_list");
    auto cur_cmds = split(cur_list, ',');
    std::set<std::string> cmd_set(cur_cmds.begin(), cur_cmds.end());

    for (const auto& a : aliases) {
        db_set_str(ctx, api, "cmd:" + a, action);
        cmd_set.insert(a);
    }

    std::string new_list;
    for (const auto& c : cmd_set) {
        if (!new_list.empty()) new_list += ",";
        new_list += c;
    }
    db_set_str(ctx, api, "cmd_list", new_list);

    api->c_reset(ctx);
    api->c_fmt(ctx, "✅ RP-команда(ы) ", KOTO_ENT_BOLD);
    std::string al_str;
    for (size_t i = 0; i < aliases.size(); ++i) {
        if (i > 0) al_str += ", ";
        al_str += "." + aliases[i];
    }
    api->c_fmt(ctx, al_str.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, " успешно добавлена(ы)!", KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

// ── 3. Команда .delrp <алиас|all> ─────────────────────────────────────────────
static void cmd_delrp(koto_ctx* ctx, const koto_api* api) {
    long long sender = api->sender_id(ctx);
    if (!is_rp_creator(ctx, api, sender)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ У вас нет прав создателя RP-команд.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    const char* raw_args = api->cmd_args(ctx);
    std::string arg = raw_args ? to_lower(trim(raw_args)) : "";
    if (arg.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "ℹ️ Укажите алиас для удаления (или 'all'):\n.delrp <алиас|all>", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    std::string cmd_list = db_get_str(ctx, api, "cmd_list");
    auto cur_cmds = split(cmd_list, ',');

    if (arg == "all") {
        for (const auto& c : cur_cmds) {
            db_del(ctx, api, "cmd:" + c);
        }
        db_del(ctx, api, "cmd_list");
        api->c_reset(ctx);
        api->c_fmt(ctx, "🗑️ Все RP-команды успешно удалены!", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    if (arg.front() == '.') arg = arg.substr(1);

    bool found = false;
    std::vector<std::string> remaining;
    for (const auto& c : cur_cmds) {
        if (c == arg) {
            db_del(ctx, api, "cmd:" + c);
            found = true;
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
    if (found) {
        api->c_fmt(ctx, "🗑️ Команда .", KOTO_ENT_BOLD);
        api->c_fmt(ctx, arg.c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, " успешно удалена!", KOTO_ENT_BOLD);
    } else {
        api->c_fmt(ctx, "❌ Команда не найдена.", KOTO_ENT_BOLD);
    }
    api->c_edit(ctx, 0);
}

// ── 4. Команда .rplist ────────────────────────────────────────────────────────
static void cmd_rplist(koto_ctx* ctx, const koto_api* api) {
    cleanup_old_defaults_if_needed(ctx, api);
    std::string cmd_list = db_get_str(ctx, api, "cmd_list");
    auto cur_cmds = split(cmd_list, ',');

    if (cur_cmds.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "ℹ️ Список RP-команд пуст!\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "Добавьте команду через: ");
        api->c_fmt(ctx, ".addrp <алиасы>|<действие>", KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    std::map<std::string, std::vector<std::string>> groups;
    for (const auto& c : cur_cmds) {
        std::string act = db_get_str(ctx, api, "cmd:" + c);
        size_t pipe = act.find('|');
        if (pipe != std::string::npos) act = act.substr(0, pipe);
        if (!act.empty()) {
            groups[act].push_back(c);
        }
    }

    api->c_reset(ctx);
    api->c_fmt(ctx, "📋 Доступные RP-команды:\n\n", KOTO_ENT_BOLD);

    for (const auto& [act, aliases] : groups) {
        api->c_text(ctx, "• ");
        std::string al_str;
        for (size_t i = 0; i < aliases.size(); ++i) {
            if (i > 0) al_str += ", ";
            al_str += "." + aliases[i];
        }
        api->c_fmt(ctx, al_str.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, " — ");
        api->c_fmt(ctx, act.c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, "\n");
    }

    api->c_edit(ctx, 0);
}

// ── 5. Команда .setrpnick [-g] [@user] <ник> ──────────────────────────────────
static void cmd_setrpnick(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";
    if (args.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "ℹ️ Использование:\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".setrpnick [-g] [@user] <ник>", KOTO_ENT_CODE);
        api->c_text(ctx, "\n• -g — установить ник глобально (для всех чатов)");
        api->c_edit(ctx, 0);
        return;
    }

    auto tokens = split(args, ' ');
    bool is_global = false;
    long long target_uid = api->reply_sender_id(ctx);
    std::vector<std::string> rest_tokens;

    for (const auto& t : tokens) {
        if (t == "-g" || t == "-global") {
            is_global = true;
        } else if (!t.empty() && t.front() == '@') {
            long long r = api->resolve(ctx, t.substr(1).c_str());
            if (r != 0) target_uid = r;
        } else if (!t.empty() && isdigit(t.front()) && target_uid == 0 && t.size() > 4) {
            try { target_uid = std::stoll(t); } catch (...) { rest_tokens.push_back(t); }
        } else {
            rest_tokens.push_back(t);
        }
    }

    if (target_uid == 0) {
        target_uid = api->sender_id(ctx);
    }

    std::string new_nick;
    for (size_t i = 0; i < rest_tokens.size(); ++i) {
        if (i > 0) new_nick += " ";
        new_nick += rest_tokens[i];
    }
    new_nick = trim(new_nick);

    if (new_nick.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Укажите желаемый RP-ник.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    long long cid = is_global ? 0 : api->chat_id(ctx);
    db_set_str(ctx, api, "nick:" + std::to_string(cid) + ":" + std::to_string(target_uid), new_nick);

    api->c_reset(ctx);
    api->c_fmt(ctx, "✅ RP-ник успешно установлен:\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "• Ник: ");
    api->c_fmt(ctx, new_nick.c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n• Область: ");
    api->c_fmt(ctx, is_global ? "Глобально" : "Текущий чат", KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

// ── 6. Команда .delrpnick [-g] [@user] ────────────────────────────────────────
static void cmd_delrpnick(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";
    auto tokens = split(args, ' ');
    bool is_global = false;
    long long target_uid = api->reply_sender_id(ctx);

    for (const auto& t : tokens) {
        if (t == "-g" || t == "-global") is_global = true;
        else if (!t.empty() && t.front() == '@') {
            long long r = api->resolve(ctx, t.substr(1).c_str());
            if (r != 0) target_uid = r;
        }
    }

    if (target_uid == 0) target_uid = api->sender_id(ctx);

    long long cid = is_global ? 0 : api->chat_id(ctx);
    db_del(ctx, api, "nick:" + std::to_string(cid) + ":" + std::to_string(target_uid));

    api->c_reset(ctx);
    api->c_fmt(ctx, "🗑️ RP-ник успешно удален.", KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

// ── 7. Команда .rpnick [@user] ────────────────────────────────────────────────
static void cmd_rpnick(koto_ctx* ctx, const koto_api* api) {
    long long target_uid = api->reply_sender_id(ctx);
    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";

    if (!args.empty() && args.front() == '@') {
        long long r = api->resolve(ctx, args.substr(1).c_str());
        if (r != 0) target_uid = r;
    }
    if (target_uid == 0) target_uid = api->sender_id(ctx);

    long long cid = api->chat_id(ctx);
    std::string chat_nick = db_get_str(ctx, api, "nick:" + std::to_string(cid) + ":" + std::to_string(target_uid));
    std::string global_nick = db_get_str(ctx, api, "nick:0:" + std::to_string(target_uid));

    api->c_reset(ctx);
    api->c_fmt(ctx, "📛 Информация об RP-никах:\n\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "• В этом чате: ");
    api->c_fmt(ctx, !chat_nick.empty() ? chat_nick.c_str() : "(не установлен)", KOTO_ENT_CODE);
    api->c_text(ctx, "\n• Глобальный: ");
    api->c_fmt(ctx, !global_nick.empty() ? global_nick.c_str() : "(не установлен)", KOTO_ENT_CODE);
    api->c_edit(ctx, 0);
}

// ── 8. Команды создателей RP: .addrpcreator / .delrpcreator / .listrpcreators ───
static void cmd_addrpcreator(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    long long target_uid = api->reply_sender_id(ctx);
    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";
    if (!args.empty() && args.front() == '@') {
        long long r = api->resolve(ctx, args.substr(1).c_str());
        if (r != 0) target_uid = r;
    }

    if (target_uid == 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Укажите пользователя (@username или реплай).", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    db_set_str(ctx, api, "creator:" + std::to_string(target_uid), "1");
    api->c_reset(ctx);
    api->c_fmt(ctx, "👑 Пользователю выданы права создателя RP-команд!", KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

static void cmd_delrpcreator(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    long long target_uid = api->reply_sender_id(ctx);
    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";
    if (!args.empty() && args.front() == '@') {
        long long r = api->resolve(ctx, args.substr(1).c_str());
        if (r != 0) target_uid = r;
    }

    if (target_uid == 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Укажите пользователя (@username или реплай).", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    db_del(ctx, api, "creator:" + std::to_string(target_uid));
    api->c_reset(ctx);
    api->c_fmt(ctx, "🗑️ Права создателя RP-команд сняты.", KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

static void cmd_listrpcreators(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    char keys_buf[4096] = {0};
    int n = api->db_keys(ctx, keys_buf, sizeof(keys_buf) - 1);

    api->c_reset(ctx);
    api->c_fmt(ctx, "👑 Создатели RP-команд:\n\n", KOTO_ENT_BOLD);

    int count = 0;
    if (n > 0) {
        std::stringstream ss(keys_buf);
        std::string k;
        while (std::getline(ss, k, '\n')) {
            if (k.rfind("creator:", 0) == 0) {
                std::string uid = k.substr(8);
                api->c_text(ctx, "• ID ");
                api->c_fmt(ctx, uid.c_str(), KOTO_ENT_CODE);
                api->c_text(ctx, "\n");
                count++;
            }
        }
    }

    if (count == 0) {
        api->c_text(ctx, "(только владельцы и доверенные лица)");
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

    cleanup_old_defaults_if_needed(ctx, api);

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
    std::string action = db_get_str(ctx, api, "cmd:" + cmd);
    if (action.empty()) {
        return 0; // Не RP-команда
    }

    // Если в базе был старый формат action|doc_id|fallback
    size_t pipe = action.find('|');
    if (pipe != std::string::npos) {
        action = action.substr(0, pipe);
    }

    // 3. Проверка прав доступа в группе
    if (cid < 0) {
        if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) {
            bool is_public = db_has(ctx, api, "public:" + std::to_string(cid));
            bool has_access = db_has(ctx, api, "access:" + std::to_string(cid) + ":" + std::to_string(sender));
            if (!is_public && !has_access) {
                return 0; // Нет доступа в этом чате
            }
        }
    }

    // 4. Определение цели (target_uid) и комментария
    long long target_uid = api->reply_sender_id(ctx);
    std::string comment = rest;
    std::string explicit_username_target;

    if (target_uid == 0 && !rest.empty()) {
        size_t r_space = rest.find_first_of(" \t\n");
        std::string first_arg = (r_space != std::string::npos) ? rest.substr(0, r_space) : rest;
        std::string potential_comment = (r_space != std::string::npos) ? trim(rest.substr(r_space)) : "";

        if (!first_arg.empty() && first_arg.front() == '@') {
            long long res_uid = api->resolve(ctx, first_arg.substr(1).c_str());
            if (res_uid != 0) {
                target_uid = res_uid;
                comment = potential_comment;
                explicit_username_target = first_arg;
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

    // 5. Формирование сообщения действия БЕЗ значков и лишних разделителей
    api->c_reset(ctx);

    // Имя отправителя как кликабельная ссылка
    api->c_url(ctx, sender_name.c_str(), ("tg://user?id=" + std::to_string(sender)).c_str());
    api->c_text(ctx, " ");

    // Действие (жирным шрифтом)
    api->c_fmt(ctx, action.c_str(), KOTO_ENT_BOLD);

    // Цель
    if (target_uid == 0 || target_uid == sender) {
        api->c_fmt(ctx, " самого/саму себя", KOTO_ENT_BOLD);
    } else {
        std::string target_name = get_rp_display_name(ctx, api, target_uid, cid);
        if (target_name == "Пользователь" && !explicit_username_target.empty()) {
            target_name = explicit_username_target;
        }
        api->c_text(ctx, " ");
        api->c_url(ctx, target_name.c_str(), ("tg://user?id=" + std::to_string(target_uid)).c_str());
    }

    // Комментарий (если есть)
    if (!comment.empty()) {
        api->c_text(ctx, "\n\n💬 ");
        api->c_fmt(ctx, comment.c_str(), KOTO_ENT_ITALIC);
    }

    // 6. Отправка
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, KOTO_NO_LINK_PREVIEW);
    } else {
        api->c_send(ctx, cid, KOTO_NO_LINK_PREVIEW);
        if (sender == api->get_me(ctx)) {
            api->delete_msg(ctx);
        }
    }

    return 1;
}

// ── Таблица команд ────────────────────────────────────────────────────────────
static const koto_command COMMANDS[] = {
    { "rp",             &cmd_rp,             KOTO_LEVEL_TRUSTED, "Управление RP-модулем в чате (on/off/access)", "[on|off|access]" },
    { "addrp",          &cmd_addrp,          KOTO_LEVEL_ALL,     "Добавить новую RP-команду", "<алиасы>|<действие>" },
    { "delrp",          &cmd_delrp,          KOTO_LEVEL_ALL,     "Удалить RP-команду", "<алиас|all>" },
    { "rplist",         &cmd_rplist,         KOTO_LEVEL_ALL,     "Список всех доступных RP-команд", NULL },
    { "setrpnick",      &cmd_setrpnick,      KOTO_LEVEL_TRUSTED, "Установить RP-ник пользователю", "[-g] [@user] <ник>" },
    { "delrpnick",      &cmd_delrpnick,      KOTO_LEVEL_TRUSTED, "Удалить или скрыть RP-ник", "[-g] [@user]" },
    { "rpnick",         &cmd_rpnick,         KOTO_LEVEL_ALL,     "Посмотреть текущие RP-ники", "[@user]" },
    { "addrpcreator",   &cmd_addrpcreator,   KOTO_LEVEL_TRUSTED, "Назначить создателя RP-команд", "[@user]" },
    { "delrpcreator",   &cmd_delrpcreator,   KOTO_LEVEL_TRUSTED, "Снять права создателя RP-команд", "[@user]" },
    { "listrpcreators", &cmd_listrpcreators, KOTO_LEVEL_TRUSTED, "Список создателей RP-команд", NULL }
};

// ── Экспорт модуля ────────────────────────────────────────────────────────────
static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "rp",
    "Ролевые команды (Role-Play), РП-ники и кастомные действия (@Kote)",
    "2.9.0",
    12, // KoteLoader v0.2.1+
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    &rp_watch,
    KOTO_WATCH_ALL,
    NULL, // on_callback
    NULL, // settings
    0,
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/rp.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
