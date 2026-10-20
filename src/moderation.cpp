// moderation.cpp — Мощный модуль для администрирования чатов для Kotogram / KoteLoader (C++ ABI 3).
// Портирован из Python-модуля moderation.py (v1.1.5) с сохранением 1:1 всех возможностей:
// • 6 кастомных премиум-эмодзи (молоток, мут, варн, история, успех, глобус)
// • Модерация: .ban, .unban, .kick, .mute, .unmute
// • Система предупреждений: .warn, .unwarn, .warns (кик при 3/3)
// • Игровой бан: .gameban (бан на 1 час с автоматическим разбаном на сервере Telegram)
// • История модерации: .history [user] (хранение до 1000 записей, показ 15 последних)
// • Глобальные наборы чатов: .gset (add/rm/list)
// • Глобальные действия: .gban, .ungban, .gkick по категориям
// Автор: Kote

#include "module_abi.h"
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <ctime>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

// ── Кастомные премиум-эмодзи ──────────────────────────────────────────────────
static constexpr long long HAMMER_EMOJI_ID  = 5253780051471642059LL; // 💥 / 👢
static constexpr long long MUTE_EMOJI_ID    = 5253690110561494560LL; // 🔇
static constexpr long long WARN_EMOJI_ID    = 5253864872780769235LL; // ⚠️
static constexpr long long HISTORY_EMOJI_ID = 5256230583717079814LL; // 📜
static constexpr long long SUCCESS_EMOJI_ID = 5118861066981344121LL; // ✅ / 🔊
static constexpr long long GLOBE_EMOJI_ID   = 4906943755644306322LL; // 🌐

static const int MAX_WARNS = 3;

static inline void c_text_s(koto_ctx* ctx, const koto_api* api, const std::string& s) {
    api->c_text(ctx, s.c_str());
}

// ── Вспомогательные функции для JSON и строк ──────────────────────────────────
static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

static std::string json_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char next = s[++i];
            if (next == 'n') out += '\n';
            else if (next == 'r') out += '\r';
            else if (next == 't') out += '\t';
            else if (next == '"') out += '"';
            else if (next == '\\') out += '\\';
            else if (next == '/') out += '/';
            else {
                out += '\\';
                out += next;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

static std::string get_user_display_name(koto_ctx* ctx, const koto_api* api, long long uid) {
    char name_buf[256];
    if (api->user_first_name && api->user_first_name(ctx, uid, name_buf, sizeof(name_buf)) > 0) {
        if (name_buf[0]) return name_buf;
    }
    if (api->user_name && api->user_name(ctx, uid, name_buf, sizeof(name_buf)) > 0) {
        if (name_buf[0]) return name_buf;
    }
    return "ID " + std::to_string(uid);
}

static int parse_time(const std::string& time_str) {
    if (time_str.empty()) return 0;
    size_t i = 0;
    while (i < time_str.size() && time_str[i] >= '0' && time_str[i] <= '9') i++;
    if (i == 0 || i >= time_str.size()) return 0;
    int val = std::atoi(time_str.substr(0, i).c_str());
    char unit = (char)std::tolower((unsigned char)time_str[i]);
    if (unit == 'd') return val * 86400;
    if (unit == 'h') return val * 3600;
    if (unit == 'm') return val * 60;
    return 0;
}

// ── Определение цели (user_id) и оставшихся аргументов ────────────────────────
static long long get_target_user_and_args(koto_ctx* ctx, const koto_api* api, std::string& out_remaining_args) {
    long long reply_sender = api->reply_sender_id(ctx);
    const char* args_ptr = api->cmd_args(ctx);
    std::string args_str = args_ptr ? args_ptr : "";
    // trim
    size_t s = args_str.find_first_not_of(" \t\r\n");
    if (s != std::string::npos) args_str = args_str.substr(s);
    else args_str.clear();

    if (reply_sender != 0) {
        out_remaining_args = args_str;
        return reply_sender;
    }

    if (!args_str.empty()) {
        std::stringstream ss(args_str);
        std::string first_token;
        ss >> first_token;
        std::string rest;
        std::getline(ss, rest);
        size_t rs = rest.find_first_not_of(" \t\r\n");
        if (rs != std::string::npos) rest = rest.substr(rs);
        else rest.clear();

        // 1. Попытка resolve через username (@user или user)
        long long resolved = api->resolve(ctx, first_token.c_str());
        if (resolved != 0) {
            out_remaining_args = rest;
            return resolved;
        }

        // 2. Попытка распарсить числовой ID
        bool all_digits = true;
        for (char c : first_token) {
            if (c < '0' || c > '9') { all_digits = false; break; }
        }
        if (all_digits && !first_token.empty()) {
            long long uid = std::atoll(first_token.c_str());
            if (uid > 0) {
                out_remaining_args = rest;
                return uid;
            }
        }
    }

    out_remaining_args = args_str;
    return 0;
}

// ── Запись в историю модерации (mod_history) ───────────────────────────────────
struct HistoryEntry {
    long long moderator_id = 0;
    long long chat_id = 0;
    long long user_id = 0;
    std::string action;
    std::string reason;
    long long timestamp = 0;
};

static void log_action(koto_ctx* ctx, const koto_api* api, long long mod_id, long long chat_id,
                       long long user_id, const std::string& action, const std::string& reason) {
    std::vector<char> buf(65536, 0);
    int len = api->db_get(ctx, "mod_history", buf.data(), (int)buf.size() - 1);
    std::string hist_raw = (len > 0) ? std::string(buf.data(), len) : "[]";

    std::string new_entry = "{\"m\":" + std::to_string(mod_id) +
                            ",\"c\":" + std::to_string(chat_id) +
                            ",\"u\":" + std::to_string(user_id) +
                            ",\"a\":\"" + json_escape(action) + "\"" +
                            ",\"r\":\"" + json_escape(reason) + "\"" +
                            ",\"t\":" + std::to_string(std::time(nullptr)) + "}";

    if (hist_raw.size() <= 2) {
        hist_raw = "[" + new_entry + "]";
    } else {
        if (hist_raw.back() == ']') hist_raw.pop_back();
        hist_raw += "," + new_entry + "]";
    }

    api->db_set(ctx, "mod_history", hist_raw.c_str());
}

static std::vector<HistoryEntry> load_history(koto_ctx* ctx, const koto_api* api) {
    std::vector<char> buf(65536, 0);
    int len = api->db_get(ctx, "mod_history", buf.data(), (int)buf.size() - 1);
    std::vector<HistoryEntry> list;
    if (len <= 0) return list;

    std::string raw(buf.data(), len);
    size_t pos = 0;
    while ((pos = raw.find("{\"m\":", pos)) != std::string::npos) {
        size_t end_pos = raw.find('}', pos);
        if (end_pos == std::string::npos) break;
        std::string obj = raw.substr(pos, end_pos - pos + 1);

        HistoryEntry e;
        size_t mp = obj.find("\"m\":");
        if (mp != std::string::npos) e.moderator_id = std::atoll(obj.c_str() + mp + 4);
        size_t cp = obj.find("\"c\":");
        if (cp != std::string::npos) e.chat_id = std::atoll(obj.c_str() + cp + 4);
        size_t up = obj.find("\"u\":");
        if (up != std::string::npos) e.user_id = std::atoll(obj.c_str() + up + 4);
        size_t tp = obj.find("\"t\":");
        if (tp != std::string::npos) e.timestamp = std::atoll(obj.c_str() + tp + 4);

        size_t ap = obj.find("\"a\":\"");
        if (ap != std::string::npos) {
            size_t val_start = ap + 5;
            size_t val_end = obj.find('"', val_start);
            if (val_end != std::string::npos) e.action = json_unescape(obj.substr(val_start, val_end - val_start));
        }

        size_t rp = obj.find("\"r\":\"");
        if (rp != std::string::npos) {
            size_t val_start = rp + 5;
            size_t val_end = obj.find('"', val_start);
            if (val_end != std::string::npos) e.reason = json_unescape(obj.substr(val_start, val_end - val_start));
        }

        list.push_back(std::move(e));
        pos = end_pos + 1;
    }
    return list;
}

// ── Глобальные наборы чатов (global_sets) ──────────────────────────────────────
static std::map<std::string, std::vector<long long>> load_global_sets(koto_ctx* ctx, const koto_api* api) {
    std::map<std::string, std::vector<long long>> res;
    std::vector<char> buf(32768, 0);
    int len = api->db_get(ctx, "global_sets", buf.data(), (int)buf.size() - 1);
    if (len <= 0) return res;

    std::string raw(buf.data(), len);
    size_t pos = 0;
    while ((pos = raw.find('"', pos)) != std::string::npos) {
        size_t key_end = raw.find('"', pos + 1);
        if (key_end == std::string::npos) break;
        std::string key = raw.substr(pos + 1, key_end - pos - 1);

        size_t arr_start = raw.find('[', key_end);
        if (arr_start == std::string::npos) break;
        size_t arr_end = raw.find(']', arr_start);
        if (arr_end == std::string::npos) break;

        std::string arr_str = raw.substr(arr_start + 1, arr_end - arr_start - 1);
        std::stringstream ss(arr_str);
        std::string item;
        std::vector<long long> ids;
        while (std::getline(ss, item, ',')) {
            size_t s = item.find_first_not_of(" \t\r\n");
            if (s != std::string::npos) {
                long long cid = std::atoll(item.c_str() + s);
                if (cid != 0) ids.push_back(cid);
            }
        }
        res[key] = std::move(ids);
        pos = arr_end + 1;
    }
    return res;
}

static void save_global_sets(koto_ctx* ctx, const koto_api* api,
                             const std::map<std::string, std::vector<long long>>& sets) {
    std::string json = "{";
    bool first = true;
    for (const auto& [cat, ids] : sets) {
        if (!first) json += ",";
        first = false;
        json += "\"" + json_escape(cat) + "\":[";
        for (size_t i = 0; i < ids.size(); ++i) {
            if (i > 0) json += ",";
            json += std::to_string(ids[i]);
        }
        json += "]";
    }
    json += "}";
    api->db_set(ctx, "global_sets", json.c_str());
}

// ── Проверка доступа и типа чата ──────────────────────────────────────────────
static bool is_group_chat(koto_ctx* ctx, const koto_api* api, long long cid) {
    (void)cid;
    int type = api->chat_type(ctx);
    return type == KOTO_CHAT_GROUP || type == KOTO_CHAT_CHANNEL;
}

static bool ensure_group_and_permission(koto_ctx* ctx, const koto_api* api, int min_level) {
    if (api->check_permission && !api->check_permission(ctx, min_level)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "🚫 ", KOTO_ENT_BOLD);
        api->c_text(ctx, "Доступ запрещен.");
        api->c_edit(ctx, 0);
        return false;
    }
    long long cid = api->chat_id(ctx);
    if (!is_group_chat(ctx, api, cid)) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Эта команда предназначена только для групп.");
        api->c_edit(ctx, 0);
        return false;
    }
    return true;
}

// ── Команда: .ban <user> [reason] ─────────────────────────────────────────────
static void cmd_ban(koto_ctx* ctx, const koto_api* api) {
    if (!ensure_group_and_permission(ctx, api, KOTO_LEVEL_TRUSTED)) return;

    std::string reason;
    long long target_uid = get_target_user_and_args(ctx, api, reason);
    if (target_uid == 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Не удалось найти пользователя.");
        api->c_edit(ctx, 0);
        return;
    }
    if (reason.empty()) reason = "Причина не указана";

    long long cid = api->chat_id(ctx);
    long long me = api->get_me(ctx);
    std::string display_name = get_user_display_name(ctx, api, target_uid);

    api->ban_user(ctx, cid, target_uid, 0);
    log_action(ctx, api, me, cid, target_uid, "ban", reason);

    api->c_reset(ctx);
    api->c_emoji(ctx, "💥", HAMMER_EMOJI_ID);
    c_text_s(ctx, api, " Пользователь " + display_name + " забанен. Причина: ");
    api->c_fmt(ctx, reason.c_str(), KOTO_ENT_CODE);
    api->c_edit(ctx, 0);
}

// ── Команда: .unban <user> [reason] ───────────────────────────────────────────
static void cmd_unban(koto_ctx* ctx, const koto_api* api) {
    if (!ensure_group_and_permission(ctx, api, KOTO_LEVEL_TRUSTED)) return;

    std::string reason;
    long long target_uid = get_target_user_and_args(ctx, api, reason);
    if (target_uid == 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Не удалось найти пользователя.");
        api->c_edit(ctx, 0);
        return;
    }
    if (reason.empty()) reason = "Амнистия";

    long long cid = api->chat_id(ctx);
    long long me = api->get_me(ctx);
    std::string display_name = get_user_display_name(ctx, api, target_uid);

    api->unban_user(ctx, cid, target_uid);
    log_action(ctx, api, me, cid, target_uid, "unban", reason);

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", SUCCESS_EMOJI_ID);
    c_text_s(ctx, api, " Пользователь " + display_name + " разбанен.");
    api->c_edit(ctx, 0);
}

// ── Команда: .kick <user> [reason] ────────────────────────────────────────────
static void cmd_kick(koto_ctx* ctx, const koto_api* api) {
    if (!ensure_group_and_permission(ctx, api, KOTO_LEVEL_TRUSTED)) return;

    std::string reason;
    long long target_uid = get_target_user_and_args(ctx, api, reason);
    if (target_uid == 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Не удалось найти пользователя.");
        api->c_edit(ctx, 0);
        return;
    }
    if (reason.empty()) reason = "Причина не указана";

    long long cid = api->chat_id(ctx);
    long long me = api->get_me(ctx);
    std::string display_name = get_user_display_name(ctx, api, target_uid);

    api->kick_user(ctx, cid, target_uid);
    log_action(ctx, api, me, cid, target_uid, "kick", reason);

    api->c_reset(ctx);
    api->c_emoji(ctx, "👢", HAMMER_EMOJI_ID);
    c_text_s(ctx, api, " Пользователь " + display_name + " кикнут. Причина: ");
    api->c_fmt(ctx, reason.c_str(), KOTO_ENT_CODE);
    api->c_edit(ctx, 0);
}

// ── Команда: .mute <user> [time] [reason] ─────────────────────────────────────
static void cmd_mute(koto_ctx* ctx, const koto_api* api) {
    if (!ensure_group_and_permission(ctx, api, KOTO_LEVEL_TRUSTED)) return;

    std::string remaining;
    long long target_uid = get_target_user_and_args(ctx, api, remaining);
    if (target_uid == 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Не удалось найти пользователя.");
        api->c_edit(ctx, 0);
        return;
    }

    std::stringstream ss(remaining);
    std::string first_part;
    ss >> first_part;
    int duration = parse_time(first_part);
    std::string reason;
    std::string duration_text = " навсегда";
    if (duration > 0) {
        duration_text = " на " + first_part;
        std::getline(ss, reason);
    } else {
        reason = remaining;
    }
    size_t s = reason.find_first_not_of(" \t\r\n");
    if (s != std::string::npos) reason = reason.substr(s);
    else reason.clear();
    if (reason.empty()) reason = "Причина не указана";

    long long cid = api->chat_id(ctx);
    long long me = api->get_me(ctx);
    std::string display_name = get_user_display_name(ctx, api, target_uid);

    api->mute_user(ctx, cid, target_uid, duration);
    log_action(ctx, api, me, cid, target_uid, "mute", reason);

    api->c_reset(ctx);
    api->c_emoji(ctx, "🔇", MUTE_EMOJI_ID);
    c_text_s(ctx, api, " Пользователь " + display_name + " замучен" + duration_text + ". Причина: ");
    api->c_fmt(ctx, reason.c_str(), KOTO_ENT_CODE);
    api->c_edit(ctx, 0);
}

// ── Команда: .unmute <user> ───────────────────────────────────────────────────
static void cmd_unmute(koto_ctx* ctx, const koto_api* api) {
    if (!ensure_group_and_permission(ctx, api, KOTO_LEVEL_TRUSTED)) return;

    std::string reason;
    long long target_uid = get_target_user_and_args(ctx, api, reason);
    if (target_uid == 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Не удалось найти пользователя.");
        api->c_edit(ctx, 0);
        return;
    }
    if (reason.empty()) reason = "Амнистия";

    long long cid = api->chat_id(ctx);
    long long me = api->get_me(ctx);
    std::string display_name = get_user_display_name(ctx, api, target_uid);

    api->unmute_user(ctx, cid, target_uid);
    log_action(ctx, api, me, cid, target_uid, "unmute", reason);

    api->c_reset(ctx);
    api->c_emoji(ctx, "🔊", SUCCESS_EMOJI_ID);
    c_text_s(ctx, api, " Пользователь " + display_name + " размучен.");
    api->c_edit(ctx, 0);
}

// ── Команда: .warn <user> [reason] ────────────────────────────────────────────
static void cmd_warn(koto_ctx* ctx, const koto_api* api) {
    if (!ensure_group_and_permission(ctx, api, KOTO_LEVEL_TRUSTED)) return;

    std::string reason;
    long long target_uid = get_target_user_and_args(ctx, api, reason);
    if (target_uid == 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Не удалось найти пользователя.");
        api->c_edit(ctx, 0);
        return;
    }
    if (reason.empty()) reason = "Причина не указана";

    long long cid = api->chat_id(ctx);
    long long me = api->get_me(ctx);
    std::string display_name = get_user_display_name(ctx, api, target_uid);

    std::string key = "warns_" + std::to_string(cid) + "_" + std::to_string(target_uid);
    long long new_warns = api->db_get_int(ctx, key.c_str(), 0) + 1;
    api->db_set_int(ctx, key.c_str(), new_warns);

    log_action(ctx, api, me, cid, target_uid, "warn", reason);

    api->c_reset(ctx);
    api->c_emoji(ctx, "⚠️", WARN_EMOJI_ID);
    c_text_s(ctx, api, " " + display_name + " получил предупреждение ");
    std::string warn_stat = "(" + std::to_string(new_warns) + "/" + std::to_string(MAX_WARNS) + ")";
    api->c_fmt(ctx, warn_stat.c_str(), KOTO_ENT_BOLD);
    api->c_text(ctx, ". Причина: ");
    api->c_fmt(ctx, reason.c_str(), KOTO_ENT_CODE);

    if (new_warns >= MAX_WARNS) {
        api->kick_user(ctx, cid, target_uid);
        api->db_set_int(ctx, key.c_str(), 0);
        log_action(ctx, api, me, cid, target_uid, "kick", "Превышен лимит варнов (" + std::to_string(MAX_WARNS) + ")");
        api->c_text(ctx, "\n\n💥 ");
        api->c_fmt(ctx, "Пользователь кикнут за превышение лимита предупреждений.", KOTO_ENT_BOLD);
    }

    api->c_edit(ctx, 0);
}

// ── Команда: .unwarn <user> ───────────────────────────────────────────────────
static void cmd_unwarn(koto_ctx* ctx, const koto_api* api) {
    if (!ensure_group_and_permission(ctx, api, KOTO_LEVEL_TRUSTED)) return;

    std::string reason;
    long long target_uid = get_target_user_and_args(ctx, api, reason);
    if (target_uid == 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Не удалось найти пользователя.");
        api->c_edit(ctx, 0);
        return;
    }
    if (reason.empty()) reason = "Амнистия";

    long long cid = api->chat_id(ctx);
    long long me = api->get_me(ctx);
    std::string display_name = get_user_display_name(ctx, api, target_uid);

    std::string key = "warns_" + std::to_string(cid) + "_" + std::to_string(target_uid);
    long long current_warns = api->db_get_int(ctx, key.c_str(), 0);

    api->c_reset(ctx);
    if (current_warns <= 0) {
        c_text_s(ctx, api, "ℹ️ У пользователя " + display_name + " нет предупреждений.");
    } else {
        long long rem = current_warns - 1;
        api->db_set_int(ctx, key.c_str(), rem);
        log_action(ctx, api, me, cid, target_uid, "unwarn", reason);

        api->c_emoji(ctx, "✅", SUCCESS_EMOJI_ID);
        c_text_s(ctx, api, " Одно предупреждение снято с " + display_name + ". ");
        api->c_fmt(ctx, ("Осталось: " + std::to_string(rem)).c_str(), KOTO_ENT_BOLD);
    }
    api->c_edit(ctx, 0);
}

// ── Команда: .warns [user] ────────────────────────────────────────────────────
static void cmd_warns(koto_ctx* ctx, const koto_api* api) {
    if (!ensure_group_and_permission(ctx, api, KOTO_LEVEL_TRUSTED)) return;

    std::string dummy;
    long long target_uid = get_target_user_and_args(ctx, api, dummy);
    if (target_uid == 0) target_uid = api->sender_id(ctx);

    long long cid = api->chat_id(ctx);
    std::string display_name = get_user_display_name(ctx, api, target_uid);
    std::string key = "warns_" + std::to_string(cid) + "_" + std::to_string(target_uid);
    long long current_warns = api->db_get_int(ctx, key.c_str(), 0);

    api->c_reset(ctx);
    api->c_text(ctx, "ℹ️ У пользователя ");
    api->c_fmt(ctx, display_name.c_str(), KOTO_ENT_CODE);
    std::string msg = " " + std::to_string(current_warns) + "/" + std::to_string(MAX_WARNS) +
                      " предупреждений в этом чате.";
    api->c_text(ctx, msg.c_str());
    api->c_edit(ctx, 0);
}

// ── Команда: .gameban <user> ──────────────────────────────────────────────────
static void cmd_gameban(koto_ctx* ctx, const koto_api* api) {
    if (!ensure_group_and_permission(ctx, api, KOTO_LEVEL_TRUSTED)) return;

    std::string dummy;
    long long target_uid = get_target_user_and_args(ctx, api, dummy);
    if (target_uid == 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Не удалось найти пользователя.");
        api->c_edit(ctx, 0);
        return;
    }

    long long cid = api->chat_id(ctx);
    long long me = api->get_me(ctx);
    std::string display_name = get_user_display_name(ctx, api, target_uid);
    std::string reason = "бан на игру";

    // 3600 секунд (1 час) — MTProto сервер Telegram снимет бан автоматически!
    api->ban_user(ctx, cid, target_uid, 3600);
    log_action(ctx, api, me, cid, target_uid, "GAMEBAN", reason);

    api->c_reset(ctx);
    api->c_emoji(ctx, "💥", HAMMER_EMOJI_ID);
    c_text_s(ctx, api, " Пользователь " + display_name + " забанен на 1 час. Причина: ");
    api->c_fmt(ctx, reason.c_str(), KOTO_ENT_CODE);
    api->c_edit(ctx, 0);
}

// ── Команда: .history [user] ──────────────────────────────────────────────────
static void cmd_history(koto_ctx* ctx, const koto_api* api) {
    if (api->check_permission && !api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "🚫 ", KOTO_ENT_BOLD);
        api->c_text(ctx, "Доступ запрещен.");
        api->c_edit(ctx, 0);
        return;
    }

    std::string dummy;
    long long target_uid = get_target_user_and_args(ctx, api, dummy);

    auto all_entries = load_history(ctx, api);
    if (all_entries.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "📋 История модерации пуста.");
        api->c_edit(ctx, 0);
        return;
    }

    std::vector<HistoryEntry> filtered;
    for (const auto& e : all_entries) {
        if (target_uid == 0 || e.user_id == target_uid) {
            filtered.push_back(e);
        }
    }

    if (filtered.empty()) {
        std::string display_name = get_user_display_name(ctx, api, target_uid);
        api->c_reset(ctx);
        c_text_s(ctx, api, "📋 История модерации для " + display_name + " пуста.");
        api->c_edit(ctx, 0);
        return;
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "📜", HISTORY_EMOJI_ID);
    std::string header = " История модерации" +
                         (target_uid != 0 ? " для " + get_user_display_name(ctx, api, target_uid) : "") + ":\n\n";
    api->c_fmt(ctx, header.c_str(), KOTO_ENT_BOLD);

    // До 15 последних записей в обратном порядке
    int count = 0;
    for (int i = (int)filtered.size() - 1; i >= 0 && count < 15; --i, ++count) {
        const auto& e = filtered[i];
        char date_buf[64];
        std::time_t tt = (std::time_t)e.timestamp;
        std::tm tm_struct{};
        localtime_r(&tt, &tm_struct);
        std::strftime(date_buf, sizeof(date_buf), "%d.%m %H:%M", &tm_struct);

        api->c_text(ctx, ("[" + std::string(date_buf) + "] [").c_str());
        api->c_fmt(ctx, e.action.c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, "] → ");

        std::string uname = get_user_display_name(ctx, api, e.user_id);
        std::string user_url = "tg://user?id=" + std::to_string(e.user_id);
        api->c_url(ctx, uname.c_str(), user_url.c_str());

        if (e.chat_id != 0) {
            api->c_text(ctx, (" в чате ID " + std::to_string(e.chat_id)).c_str());
        }

        api->c_text(ctx, "\n└ Причина: ");
        api->c_fmt(ctx, e.reason.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n\n");
    }

    api->c_edit(ctx, 0);
}

// ── Команда: .gset add/rm/list <category> [chat_id] ───────────────────────────
static void cmd_gset(koto_ctx* ctx, const koto_api* api) {
    if (api->check_permission && !api->check_permission(ctx, KOTO_LEVEL_OWNER)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "🚫 ", KOTO_ENT_BOLD);
        api->c_text(ctx, "Доступ запрещен (только OWNER).");
        api->c_edit(ctx, 0);
        return;
    }

    const char* args_ptr = api->cmd_args(ctx);
    std::string args_str = args_ptr ? args_ptr : "";
    std::stringstream ss(args_str);
    std::string action, category, chat_id_str;
    ss >> action >> category >> chat_id_str;

    if (action.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Ошибка использования ");
        api->c_fmt(ctx, ".gset", KOTO_ENT_CODE);
        api->c_text(ctx, ":\n\n");
        api->c_fmt(ctx, "Описание:", KOTO_ENT_BOLD);
        api->c_text(ctx, " Управление глобальными наборами чатов (только OWNER).\n");
        api->c_fmt(ctx, "Использование:", KOTO_ENT_BOLD);
        api->c_fmt(ctx, " .gset add/rm/list <category> [chat_id]", KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    for (char& c : action) c = (char)std::tolower((unsigned char)c);
    auto all_sets = load_global_sets(ctx, api);

    if (action == "list") {
        api->c_reset(ctx);
        if (all_sets.empty()) {
            api->c_emoji(ctx, "🌐", GLOBE_EMOJI_ID);
            api->c_text(ctx, " Наборы глобальных чатов пусты.");
        } else if (!category.empty()) {
            if (all_sets.find(category) == all_sets.end()) {
                api->c_text(ctx, ("❌ Категория '" + category + "' не найдена.").c_str());
            } else {
                api->c_emoji(ctx, "🌐", GLOBE_EMOJI_ID);
                api->c_fmt(ctx, (" 🗂️ Категория: " + category + "\n\n").c_str(), KOTO_ENT_BOLD);
                for (long long id : all_sets[category]) {
                    api->c_text(ctx, "  • ");
                    api->c_fmt(ctx, std::to_string(id).c_str(), KOTO_ENT_CODE);
                    api->c_text(ctx, "\n");
                }
            }
        } else {
            api->c_emoji(ctx, "🌐", GLOBE_EMOJI_ID);
            api->c_fmt(ctx, " 🗂️ Наборы глобальных чатов:\n\n", KOTO_ENT_BOLD);
            for (const auto& [cat, ids] : all_sets) {
                std::string line = "Категория: " + cat + " (" + std::to_string(ids.size()) + " шт.)\n";
                api->c_fmt(ctx, line.c_str(), KOTO_ENT_BOLD);
            }
        }
        api->c_edit(ctx, 0);
        return;
    }

    if (action != "add" && action != "rm") {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Неизвестное действие (используйте add, rm или list).");
        api->c_edit(ctx, 0);
        return;
    }

    if (category.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Укажите <категорию>.");
        api->c_edit(ctx, 0);
        return;
    }

    long long target_chat_id = api->chat_id(ctx);
    if (!chat_id_str.empty()) {
        target_chat_id = std::atoll(chat_id_str.c_str());
        if (target_chat_id == 0) {
            api->c_reset(ctx);
            api->c_text(ctx, "❌ ID чата должен быть числом.");
            api->c_edit(ctx, 0);
            return;
        }
    } else if (!is_group_chat(ctx, api, target_chat_id)) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ В ЛС нужно указать <chat_id>.");
        api->c_edit(ctx, 0);
        return;
    }

    api->c_reset(ctx);
    if (action == "add") {
        auto& ids = all_sets[category];
        if (std::find(ids.begin(), ids.end(), target_chat_id) != ids.end()) {
            api->c_text(ctx, ("ℹ️ Чат " + std::to_string(target_chat_id) + " уже в категории '" + category + "'.").c_str());
        } else {
            ids.push_back(target_chat_id);
            save_global_sets(ctx, api, all_sets);
            api->c_emoji(ctx, "✅", SUCCESS_EMOJI_ID);
            api->c_text(ctx, (" Чат " + std::to_string(target_chat_id) + " добавлен в категорию '" + category + "'.").c_str());
        }
    } else if (action == "rm") {
        auto it = all_sets.find(category);
        if (it == all_sets.end() || std::find(it->second.begin(), it->second.end(), target_chat_id) == it->second.end()) {
            api->c_text(ctx, ("ℹ️ Чата " + std::to_string(target_chat_id) + " нет в категории '" + category + "'.").c_str());
        } else {
            auto& ids = it->second;
            ids.erase(std::remove(ids.begin(), ids.end(), target_chat_id), ids.end());
            if (ids.empty()) all_sets.erase(it);
            save_global_sets(ctx, api, all_sets);
            api->c_emoji(ctx, "✅", SUCCESS_EMOJI_ID);
            api->c_text(ctx, (" Чат " + std::to_string(target_chat_id) + " удален из категории '" + category + "'.").c_str());
        }
    }
    api->c_edit(ctx, 0);
}

// ── Глобальные действия: .gban, .ungban, .gkick ──────────────────────────────
static void do_global_action(koto_ctx* ctx, const koto_api* api, const std::string& action_type) {
    if (api->check_permission && !api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "🚫 ", KOTO_ENT_BOLD);
        api->c_text(ctx, "Доступ запрещен.");
        api->c_edit(ctx, 0);
        return;
    }

    const char* args_ptr = api->cmd_args(ctx);
    std::string args_str = args_ptr ? args_ptr : "";
    std::stringstream ss(args_str);
    std::string category;
    ss >> category;

    if (category.empty()) {
        std::string desc = (action_type == "gban") ? "Глобальный бан в чатах категории."
                         : (action_type == "ungban") ? "Снять глобальный бан в чатах категории."
                         : "Глобальный кик из чатов категории.";
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Ошибка использования ");
        api->c_fmt(ctx, ("." + action_type).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, ":\n\n");
        api->c_fmt(ctx, "Описание:", KOTO_ENT_BOLD);
        api->c_text(ctx, (" " + desc + "\n").c_str());
        api->c_fmt(ctx, "Использование:", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (" ." + action_type + " <category> <user> [reason]").c_str(), KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    std::string rest;
    std::getline(ss, rest);
    size_t rs = rest.find_first_not_of(" \t\r\n");
    if (rs != std::string::npos) rest = rest.substr(rs);
    else rest.clear();

    long long target_uid = api->reply_sender_id(ctx);
    std::string reason;

    if (target_uid == 0 && !rest.empty()) {
        std::stringstream rss(rest);
        std::string user_token;
        rss >> user_token;
        std::string r_rest;
        std::getline(rss, r_rest);
        size_t rrs = r_rest.find_first_not_of(" \t\r\n");
        if (rrs != std::string::npos) r_rest = r_rest.substr(rrs);
        else r_rest.clear();

        long long resolved = api->resolve(ctx, user_token.c_str());
        if (resolved != 0) {
            target_uid = resolved;
            reason = r_rest;
        } else {
            bool all_digits = true;
            for (char c : user_token) if (c < '0' || c > '9') { all_digits = false; break; }
            if (all_digits && !user_token.empty()) {
                target_uid = std::atoll(user_token.c_str());
                reason = r_rest;
            }
        }
    } else {
        reason = rest;
    }

    if (reason.empty()) reason = "Глобальное действие";

    if (target_uid == 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Не удалось найти пользователя.");
        api->c_edit(ctx, 0);
        return;
    }

    auto all_sets = load_global_sets(ctx, api);
    auto it = all_sets.find(category);
    if (it == all_sets.end() || it->second.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, ("❌ Категория '" + category + "' пуста.").c_str());
        api->c_edit(ctx, 0);
        return;
    }

    const auto& chat_ids = it->second;
    long long me = api->get_me(ctx);
    std::string display_name = get_user_display_name(ctx, api, target_uid);

    // Применяем действие по всем чатам категории
    for (long long cid : chat_ids) {
        if (action_type == "gban") {
            api->ban_user(ctx, cid, target_uid, 0);
        } else if (action_type == "ungban") {
            api->unban_user(ctx, cid, target_uid);
        } else if (action_type == "gkick") {
            api->kick_user(ctx, cid, target_uid);
        }
    }

    log_action(ctx, api, me, 0, target_uid, action_type, reason + " (" + category + ")");

    std::string action_verb = (action_type == "gban") ? "забанен"
                            : (action_type == "ungban") ? "разбанен"
                            : "кикнут";

    api->c_reset(ctx);
    api->c_emoji(ctx, "🌐", GLOBE_EMOJI_ID);
    std::string done_text = " Готово! " + display_name + " был " + action_verb + " в " +
                            std::to_string(chat_ids.size()) + " чатах.\n\nПодробности:\n";
    api->c_fmt(ctx, done_text.c_str(), KOTO_ENT_BOLD);

    std::string quote_text;
    for (size_t i = 0; i < chat_ids.size(); ++i) {
        quote_text += "▪️ Чат ID " + std::to_string(chat_ids[i]) + (i + 1 < chat_ids.size() ? "\n" : "");
    }
    api->c_fmt(ctx, quote_text.c_str(), KOTO_ENT_BLOCKQUOTE);
    api->c_edit(ctx, 0);
}

static void cmd_gban(koto_ctx* ctx, const koto_api* api)  { do_global_action(ctx, api, "gban"); }
static void cmd_ungban(koto_ctx* ctx, const koto_api* api) { do_global_action(ctx, api, "ungban"); }
static void cmd_gkick(koto_ctx* ctx, const koto_api* api)  { do_global_action(ctx, api, "gkick"); }

// ── Декларация команд модуля ──────────────────────────────────────────────────
static const koto_command MODERATION_COMMANDS[] = {
    { "ban",      cmd_ban,      KOTO_LEVEL_TRUSTED, "Забанить пользователя в чате",           "<user> [reason]" },
    { "unban",    cmd_unban,    KOTO_LEVEL_TRUSTED, "Разбанить пользователя",                 "<user> [reason]" },
    { "kick",     cmd_kick,     KOTO_LEVEL_TRUSTED, "Кикнуть пользователя из чата",           "<user> [reason]" },
    { "mute",     cmd_mute,     KOTO_LEVEL_TRUSTED, "Замутить пользователя (1d/2h/30m)",      "<user> [time] [reason]" },
    { "unmute",   cmd_unmute,   KOTO_LEVEL_TRUSTED, "Снять мут с пользователя",               "<user>" },
    { "warn",     cmd_warn,     KOTO_LEVEL_TRUSTED, "Выдать варн (кик при 3/3)",              "<user> [reason]" },
    { "unwarn",   cmd_unwarn,   KOTO_LEVEL_TRUSTED, "Снять одно предупреждение",              "<user>" },
    { "warns",    cmd_warns,    KOTO_LEVEL_TRUSTED, "Показать количество варнов пользователя", "[user]" },
    { "history",  cmd_history,  KOTO_LEVEL_TRUSTED, "История модерации (последние 15 действий)","[user]" },
    { "gameban",  cmd_gameban,  KOTO_LEVEL_TRUSTED, "Игровой бан на 1 час с авто-разбаном",   "<user>" },
    { "gset",     cmd_gset,     KOTO_LEVEL_OWNER,   "Настройка категорий глобальных чатов",    "add/rm/list <cat> [id]" },
    { "gban",     cmd_gban,     KOTO_LEVEL_TRUSTED, "Глобальный бан по категории чатов",       "<cat> <user> [reason]" },
    { "ungban",   cmd_ungban,   KOTO_LEVEL_TRUSTED, "Снять глобальный бан по категории",       "<cat> <user> [reason]" },
    { "gkick",    cmd_gkick,    KOTO_LEVEL_TRUSTED, "Глобальный кик по категории чатов",       "<cat> <user> [reason]" }
};

static const koto_module MODERATION_MODULE = {
    KOTO_MODULE_ABI,
    "moderation",
    "Мощный модуль для администрирования чатов (бан, мут, варны, gameban, gban/gkick).",
    "1.1.7", 60, // min_host_version: resolved peer API
    0,  // flags
    MODERATION_COMMANDS,
    sizeof(MODERATION_COMMANDS) / sizeof(MODERATION_COMMANDS[0]),
    nullptr, // on_watch
    0,       // watch_flags
    nullptr, // on_callback
    nullptr, // settings
    0,       // settings_count
    "https://t.me/KoteModulesMint"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODERATION_MODULE;
}
