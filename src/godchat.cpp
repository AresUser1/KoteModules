// godchat.cpp
// Комбайн-модуль модерации, администрирования, стражей чата, экспорта сообщений и цитат.
// Адаптированный порт GodChat.py (by @mofkomodules) на Kotogram C++ ABI 3 (Host 92+).
//
// Команды:
//   .ban [user] [время] [-d] [причина]   — бан пользователя (опц. на время)
//   .unban [user]                        — разбан пользователя
//   .kick [user] [причина]               — кик пользователя
//   .mute [user] [время] [причина]       — мут пользователя
//   .unmute [user]                       — снять мут
//   .dmute [user] [время]                — делит-мут (автоудаление всех сообщений юзера)
//   .gban [user] [причина]               — глобальный бан по доступным группам
//   .gunban [user]                       — глобальный разбан по доступным группам
//   .purge                               — удалить сообщения до реплая
//   .del [n]                             — удалить реплай или последние N сообщений
//   .pref [user] [ранг]                  — установить админ-префикс (ранг)
//   .aclear                              — снять админ-теги
//   .member [user/реплай]                — карточка пользователя
//   .rmember                             — выбрать случайного активного участника чата
//   .getmsg [user/реплай] [лимит]        — выгрузить все сообщения участника в .txt файл
//   .nochan                              — вкл/выкл автоудаление сообщений от каналов
//   .guest                               — вкл/выкл автокик гостевых ботов
//   .gq [n]                              — создать цитату (стикер) через Quotly API
//   .gfq <текст>                         — создать фейк-цитату
//   .gcfg                                — панель состояния и настроек GodChat

#include "module_abi.h"
#include "koto_services.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <set>
#include <ctime>
#include <cctype>
#include <random>
#include <sstream>
#include <algorithm>

using json = nlohmann::json;

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string to_lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

static std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

static void reply_or_edit(koto_ctx* ctx, const koto_api* api, int flags = 0) {
    if (api->is_outgoing && api->is_outgoing(ctx)) api->c_edit(ctx, flags);
    else if (api->c_reply) api->c_reply(ctx, flags);
    else api->c_edit(ctx, flags);
}

static std::string db_get_str(koto_ctx* ctx, const koto_api* api, const std::string& key) {
    if (!api->db_get) return "";
    std::vector<char> buf(64 * 1024);
    int n = api->db_get(ctx, key.c_str(), buf.data(), static_cast<int>(buf.size()) - 1);
    if (n <= 0) return "";
    if (n >= static_cast<int>(buf.size())) n = static_cast<int>(buf.size()) - 1;
    buf[n] = '\0';
    return std::string(buf.data(), static_cast<size_t>(n));
}

// Парсинг длительности: 30s / 10m / 2h / 3d / 1y (и русские: с, м, ч, д, г)
static bool parse_duration_sec(const std::string& raw, int& out_sec) {
    if (raw.empty()) return false;
    size_t i = 0;
    while (i < raw.size() && std::isdigit(static_cast<unsigned char>(raw[i]))) ++i;
    if (i == 0) return false;
    long long val = 0;
    try { val = std::stoll(raw.substr(0, i)); } catch (...) { return false; }
    if (val <= 0) return false;
    std::string unit = to_lower(raw.substr(i));
    long long mult = 60; // default minutes
    if (unit == "s" || unit == "sec" || unit == "с" || unit == "сек") mult = 1;
    else if (unit.empty() || unit == "m" || unit == "min" || unit == "м" || unit == "мин") mult = 60;
    else if (unit == "h" || unit == "hr" || unit == "ч" || unit == "час") mult = 3600;
    else if (unit == "d" || unit == "day" || unit == "д" || unit == "дн") mult = 86400;
    else if (unit == "w" || unit == "н" || unit == "нед") mult = 7 * 86400;
    else if (unit == "y" || unit == "г" || unit == "л") mult = 365 * 86400;
    else return false;

    long long total = val * mult;
    if (total > 366LL * 86400LL) total = 366LL * 86400LL;
    out_sec = static_cast<int>(total);
    return true;
}

static std::string fmt_duration(int sec) {
    if (sec <= 0) return "навсегда";
    if (sec % 86400 == 0) return std::to_string(sec / 86400) + " дн.";
    if (sec % 3600 == 0) return std::to_string(sec / 3600) + " ч.";
    if (sec % 60 == 0) return std::to_string(sec / 60) + " мин.";
    return std::to_string(sec) + " сек.";
}

static bool is_int_str(const std::string& s) {
    if (s.empty()) return false;
    size_t i = (s[0] == '-') ? 1 : 0;
    if (i == s.size()) return false;
    for (; i < s.size(); ++i) if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

static std::string resolve_name(koto_ctx* ctx, const koto_api* api, long long uid) {
    char buf[256] = {0};
    if (api->user_username) {
        int n = api->user_username(ctx, uid, buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = '\0'; std::string u = trim(buf); if (!u.empty()) return "@" + u; }
    }
    if (api->user_first_name) {
        int n = api->user_first_name(ctx, uid, buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = '\0'; std::string f = trim(buf); if (!f.empty()) return f; }
    }
    if (api->user_name) {
        int n = api->user_name(ctx, uid, buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = '\0'; std::string nm = trim(buf); if (!nm.empty()) return nm; }
    }
    return "ID:" + std::to_string(uid);
}

// Извлечь target user_id из реплая или первого токена аргументов
static long long resolve_target(koto_ctx* ctx, const koto_api* api, std::vector<std::string>& toks) {
    long long rep_uid = api->reply_sender_id ? api->reply_sender_id(ctx) : 0;
    if (!toks.empty()) {
        const std::string& t = toks[0];
        if (!t.empty() && t[0] == '@' && api->resolve) {
            long long r = api->resolve(ctx, t.c_str());
            if (r != 0) { toks.erase(toks.begin()); return r; }
        } else if (is_int_str(t) && t.size() >= 5) {
            try {
                long long r = std::stoll(t);
                toks.erase(toks.begin());
                return r;
            } catch (...) {}
        }
    }
    return rep_uid;
}

// ── Баны / Кики / Муты ───────────────────────────────────────────────────────
static void cmd_ban(koto_ctx* ctx, const koto_api* api) {
    auto toks = split_ws(trim(api->cmd_args ? api->cmd_args(ctx) : ""));
    long long uid = resolve_target(ctx, api, toks);
    if (uid == 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Укажите пользователя (@username / ID) или ответьте на сообщение.", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }

    int dur_sec = 0;
    bool del_reply = false;
    std::string reason;
    for (const auto& t : toks) {
        if (t == "-d") { del_reply = true; continue; }
        if (dur_sec == 0 && parse_duration_sec(t, dur_sec)) continue;
        if (!reason.empty()) reason += " ";
        reason += t;
    }

    long long cid = api->chat_id(ctx);
    int until = dur_sec > 0 ? static_cast<int>(std::time(nullptr)) + dur_sec : 0;
    if (api->ban_user) api->ban_user(ctx, cid, uid, until);
    if (del_reply && api->reply_msg_id && api->delete_message) {
        long long rid = api->reply_msg_id(ctx);
        if (rid > 0) api->delete_message(ctx, cid, rid);
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "\xF0\x9F\x9A\xAB", 5278578973595427038LL);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, "Забанен: ", KOTO_ENT_BOLD);
    api->c_text(ctx, resolve_name(ctx, api, uid).c_str());
    api->c_text(ctx, ("\n\xE2\x8F\xB1 Срок: " + fmt_duration(dur_sec)).c_str());
    if (!reason.empty()) api->c_text(ctx, ("\n\xE2\x9C\x8F\xEF\xB8\x8F Причина: " + reason).c_str());
    reply_or_edit(ctx, api);
}

static void cmd_unban(koto_ctx* ctx, const koto_api* api) {
    auto toks = split_ws(trim(api->cmd_args ? api->cmd_args(ctx) : ""));
    long long uid = resolve_target(ctx, api, toks);
    if (uid == 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Укажите пользователя для разбана.", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    if (api->unban_user) api->unban_user(ctx, api->chat_id(ctx), uid);
    api->c_reset(ctx);
    api->c_emoji(ctx, "\xE2\x9C\x85", 5206607081334906820LL);
    api->c_text(ctx, (" Разбанен: " + resolve_name(ctx, api, uid)).c_str());
    reply_or_edit(ctx, api);
}

static void cmd_kick(koto_ctx* ctx, const koto_api* api) {
    auto toks = split_ws(trim(api->cmd_args ? api->cmd_args(ctx) : ""));
    long long uid = resolve_target(ctx, api, toks);
    if (uid == 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Укажите пользователя для кика.", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    std::string reason;
    for (const auto& t : toks) { if (!reason.empty()) reason += " "; reason += t; }
    if (api->kick_user) api->kick_user(ctx, api->chat_id(ctx), uid);
    api->c_reset(ctx);
    api->c_emoji(ctx, "\xF0\x9F\x93\xA4", 5873225338984599714LL);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, "Кикнут: ", KOTO_ENT_BOLD);
    api->c_text(ctx, resolve_name(ctx, api, uid).c_str());
    if (!reason.empty()) api->c_text(ctx, ("\n\xE2\x9C\x8F\xEF\xB8\x8F Причина: " + reason).c_str());
    reply_or_edit(ctx, api);
}

static void cmd_mute(koto_ctx* ctx, const koto_api* api) {
    auto toks = split_ws(trim(api->cmd_args ? api->cmd_args(ctx) : ""));
    long long uid = resolve_target(ctx, api, toks);
    if (uid == 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Укажите пользователя для мута.", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    int dur_sec = 0;
    std::string reason;
    for (const auto& t : toks) {
        if (dur_sec == 0 && parse_duration_sec(t, dur_sec)) continue;
        if (!reason.empty()) reason += " ";
        reason += t;
    }
    int until = dur_sec > 0 ? static_cast<int>(std::time(nullptr)) + dur_sec : 0;
    if (api->mute_user) api->mute_user(ctx, api->chat_id(ctx), uid, until);
    api->c_reset(ctx);
    api->c_emoji(ctx, "\xF0\x9F\xA4\x90", 5407001145740631266LL);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, "Замучен: ", KOTO_ENT_BOLD);
    api->c_text(ctx, resolve_name(ctx, api, uid).c_str());
    api->c_text(ctx, ("\n\xE2\x8F\xB1 Срок: " + fmt_duration(dur_sec)).c_str());
    if (!reason.empty()) api->c_text(ctx, ("\n\xE2\x9C\x8F\xEF\xB8\x8F Причина: " + reason).c_str());
    reply_or_edit(ctx, api);
}

static void cmd_unmute(koto_ctx* ctx, const koto_api* api) {
    auto toks = split_ws(trim(api->cmd_args ? api->cmd_args(ctx) : ""));
    long long uid = resolve_target(ctx, api, toks);
    if (uid == 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Укажите пользователя для снятия мута.", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    if (api->unmute_user) api->unmute_user(ctx, api->chat_id(ctx), uid);
    api->c_reset(ctx);
    api->c_emoji(ctx, "\xE2\x9C\x85", 5206607081334906820LL);
    api->c_text(ctx, (" Мут снят: " + resolve_name(ctx, api, uid)).c_str());
    reply_or_edit(ctx, api);
}

// ── Делит-мут (.dmute) ───────────────────────────────────────────────────────
static void cmd_dmute(koto_ctx* ctx, const koto_api* api) {
    auto toks = split_ws(trim(api->cmd_args ? api->cmd_args(ctx) : ""));
    long long uid = resolve_target(ctx, api, toks);
    if (uid == 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Ответьте на сообщение или укажите @username/ID для делит-мута.", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    int dur_sec = 0;
    if (!toks.empty()) parse_duration_sec(toks[0], dur_sec);

    long long cid = api->chat_id(ctx);
    std::string key = "dmute_" + std::to_string(cid);
    json dm = json::parse(db_get_str(ctx, api, key), nullptr, false);
    if (!dm.is_object()) dm = json::object();

    std::string suid = std::to_string(uid);
    if (dm.contains(suid)) {
        dm.erase(suid);
        api->db_set(ctx, key.c_str(), dm.dump().c_str());
        api->c_reset(ctx);
        api->c_emoji(ctx, "\xE2\x9C\x85", 5206607081334906820LL);
        api->c_text(ctx, (" Делит-мут снят: " + resolve_name(ctx, api, uid)).c_str());
        reply_or_edit(ctx, api);
        return;
    }

    long long until = dur_sec > 0 ? static_cast<long long>(std::time(nullptr)) + dur_sec : 0LL;
    dm[suid] = until;
    api->db_set(ctx, key.c_str(), dm.dump().c_str());

    api->c_reset(ctx);
    api->c_emoji(ctx, "\xF0\x9F\xA4\x90", 5407001145740631266LL);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, "Делит-мут включён: ", KOTO_ENT_BOLD);
    api->c_text(ctx, resolve_name(ctx, api, uid).c_str());
    api->c_text(ctx, ("\n\xE2\x8F\xB1 Срок: " + fmt_duration(dur_sec)).c_str());
    reply_or_edit(ctx, api);
}

// ── Глобальный бан/разбан (.gban / .gunban) ──────────────────────────────────
static void do_global_ban(koto_ctx* ctx, const koto_api* api, bool is_ban) {
    auto toks = split_ws(trim(api->cmd_args ? api->cmd_args(ctx) : ""));
    long long uid = resolve_target(ctx, api, toks);
    if (uid == 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Укажите пользователя (@username / ID / реплай).", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }

    api->c_reset(ctx);
    api->c_fmt(ctx, is_ban ? "\xE2\x8F\xB3 Выполняю глобальный бан..." : "\xE2\x8F\xB3 Выполняю глобальный разбан...", KOTO_ENT_ITALIC);
    reply_or_edit(ctx, api);

    int count = 0;
    if (koto::services::available(ctx, api, 74)) {
        std::string dres = koto::services::call(ctx, api, "telegram.dialogs.v1", "{\"limit\":200}");
        json dj = json::parse(dres, nullptr, false);
        if (dj.is_object() && dj.value("ok", false) && dj.contains("dialogs") && dj["dialogs"].is_array()) {
            for (const auto& d : dj["dialogs"]) {
                if (!d.is_object()) continue;
                std::string ptype = d.value("type", "");
                if (ptype != "chat" && ptype != "channel" && ptype != "group") continue;
                long long dcid = d.value("chat_id", d.value("id", 0LL));
                if (dcid == 0) continue;
                if (is_ban && api->ban_user) { api->ban_user(ctx, dcid, uid, 0); count++; }
                else if (!is_ban && api->unban_user) { api->unban_user(ctx, dcid, uid); count++; }
            }
        }
    }
    if (count == 0) {
        long long cid = api->chat_id(ctx);
        if (is_ban && api->ban_user) { api->ban_user(ctx, cid, uid, 0); count = 1; }
        else if (!is_ban && api->unban_user) { api->unban_user(ctx, cid, uid); count = 1; }
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, is_ban ? "\xF0\x9F\x9A\xAB" : "\xE2\x9C\x85", is_ban ? 5278578973595427038LL : 5206607081334906820LL);
    api->c_text(ctx, (" " + std::string(is_ban ? "Глобальный бан" : "Глобальный разбан") + ": " +
                      resolve_name(ctx, api, uid) + " (чатов: " + std::to_string(count) + ")").c_str());
    reply_or_edit(ctx, api);
}

static void cmd_gban(koto_ctx* ctx, const koto_api* api) { do_global_ban(ctx, api, true); }
static void cmd_gunban(koto_ctx* ctx, const koto_api* api) { do_global_ban(ctx, api, false); }

// ── Purge / Del ──────────────────────────────────────────────────────────────
static void cmd_purge(koto_ctx* ctx, const koto_api* api) {
    long long rid = api->reply_msg_id ? api->reply_msg_id(ctx) : 0;
    long long mid = api->msg_id ? api->msg_id(ctx) : 0;
    long long cid = api->chat_id(ctx);
    if (rid == 0 || !api->delete_message) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Ответьте на сообщение, до которого нужно удалить историю.", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }

    int deleted = 0;
    if (api->get_history_json) {
        std::vector<char> buf(256 * 1024);
        int n = api->get_history_json(ctx, cid, 0, 200, 0, buf.data(), static_cast<int>(buf.size()) - 1);
        if (n > 0) {
            buf[std::min(n, static_cast<int>(buf.size()) - 1)] = '\0';
            json arr = json::parse(buf.data(), nullptr, false);
            if (arr.is_array()) {
                for (const auto& m : arr) {
                    long long id = m.value("id", 0LL);
                    if (id >= rid && id <= mid) {
                        api->delete_message(ctx, cid, id);
                        deleted++;
                    }
                }
            }
        }
    }
    if (deleted == 0) {
        for (long long id = rid; id <= mid && deleted < 200; ++id) {
            api->delete_message(ctx, cid, id);
            deleted++;
        }
    }
    api->delete_msg(ctx);
}

static void cmd_del(koto_ctx* ctx, const koto_api* api) {
    std::string args = trim(api->cmd_args ? api->cmd_args(ctx) : "");
    long long cid = api->chat_id(ctx);
    long long rid = api->reply_msg_id ? api->reply_msg_id(ctx) : 0;

    if (args.empty()) {
        if (rid > 0 && api->delete_message) api->delete_message(ctx, cid, rid);
        api->delete_msg(ctx);
        return;
    }

    int count = 0;
    try { count = std::stoi(args); } catch (...) { count = 0; }
    if (count < 1) count = 1;
    if (count > 200) count = 200;

    if (api->get_history_json && api->delete_message) {
        std::vector<char> buf(256 * 1024);
        int n = api->get_history_json(ctx, cid, 0, count + 1, 0, buf.data(), static_cast<int>(buf.size()) - 1);
        if (n > 0) {
            buf[std::min(n, static_cast<int>(buf.size()) - 1)] = '\0';
            json arr = json::parse(buf.data(), nullptr, false);
            if (arr.is_array()) {
                for (const auto& m : arr) {
                    long long id = m.value("id", 0LL);
                    if (id > 0) api->delete_message(ctx, cid, id);
                }
            }
        }
    }
    api->delete_msg(ctx);
}

// ── Префиксы и администрирование (.pref, .aclear) ───────────────────────────
static void cmd_pref(koto_ctx* ctx, const koto_api* api) {
    if (!api->admin_action) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Требуется Host >= 60 (admin_action).", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    auto toks = split_ws(trim(api->cmd_args ? api->cmd_args(ctx) : ""));
    long long uid = resolve_target(ctx, api, toks);
    if (uid == 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Ответьте на сообщение или укажите пользователя.", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    std::string rank;
    for (const auto& t : toks) {
        if (t == "-a") continue;
        if (!rank.empty()) rank += " ";
        rank += t;
    }
    api->admin_action(ctx, KOTO_ADMIN_RANK, api->chat_id(ctx), uid, "", rank.c_str());
    api->c_reset(ctx);
    api->c_emoji(ctx, "\xE2\x9C\x8F\xEF\xB8\x8F", 5879841310902324730LL);
    api->c_text(ctx, (" Префикс для " + resolve_name(ctx, api, uid) + " обновлён: ").c_str());
    api->c_fmt(ctx, rank.empty() ? "(снят)" : rank.c_str(), KOTO_ENT_CODE);
    reply_or_edit(ctx, api);
}

static void cmd_aclear(koto_ctx* ctx, const koto_api* api) {
    if (!api->admin_action) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Требуется Host >= 60 (admin_action).", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    api->admin_action(ctx, KOTO_ADMIN_CLEAR_TAGS, api->chat_id(ctx), 0, "", "");
    api->c_reset(ctx);
    api->c_emoji(ctx, "\xE2\x9C\x85", 5206607081334906820LL);
    api->c_text(ctx, " Запрос на очистку админ-тегов отправлен.");
    reply_or_edit(ctx, api);
}

// ── Карточка участника (.member) & Случайный участник (.rmember) ────────────
static void cmd_member(koto_ctx* ctx, const koto_api* api) {
    auto toks = split_ws(trim(api->cmd_args ? api->cmd_args(ctx) : ""));
    long long uid = resolve_target(ctx, api, toks);
    if (uid == 0 && api->get_me) uid = api->get_me(ctx);

    api->c_reset(ctx);
    api->c_fmt(ctx, "\xF0\x9F\x91\xA4 Карточка участника\n\n", KOTO_ENT_BOLD);
    api->c_fmt(ctx, "ID: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, std::to_string(uid).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n");
    api->c_fmt(ctx, "Имя: ", KOTO_ENT_BOLD);
    api->c_text(ctx, resolve_name(ctx, api, uid).c_str());

    if (api->user_full_json) {
        std::vector<char> buf(16 * 1024);
        int n = api->user_full_json(ctx, uid, buf.data(), static_cast<int>(buf.size()) - 1);
        if (n > 0) {
            buf[std::min(n, static_cast<int>(buf.size()) - 1)] = '\0';
            json uj = json::parse(buf.data(), nullptr, false);
            if (uj.is_object()) {
                if (uj.contains("username") && uj["username"].is_string() && !uj["username"].get<std::string>().empty()) {
                    api->c_text(ctx, "\n");
                    api->c_fmt(ctx, "Username: ", KOTO_ENT_BOLD);
                    api->c_text(ctx, ("@" + uj["username"].get<std::string>()).c_str());
                }
                if (uj.contains("about") && uj["about"].is_string() && !uj["about"].get<std::string>().empty()) {
                    api->c_text(ctx, "\n");
                    api->c_fmt(ctx, "Био: ", KOTO_ENT_BOLD);
                    api->c_text(ctx, uj["about"].get<std::string>().c_str());
                }
            }
        }
    }
    reply_or_edit(ctx, api);
}

static void cmd_rmember(koto_ctx* ctx, const koto_api* api) {
    if (!api->get_history_json) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Требуется get_history_json.", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    std::vector<char> buf(256 * 1024);
    int n = api->get_history_json(ctx, api->chat_id(ctx), 0, 200, 0, buf.data(), static_cast<int>(buf.size()) - 1);
    std::set<long long> uids;
    if (n > 0) {
        buf[std::min(n, static_cast<int>(buf.size()) - 1)] = '\0';
        json arr = json::parse(buf.data(), nullptr, false);
        if (arr.is_array()) {
            for (const auto& m : arr) {
                long long s = m.value("sender_id", 0LL);
                if (s > 0) uids.insert(s);
            }
        }
    }
    if (uids.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Не найдено активных участников в последних сообщениях.", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    std::vector<long long> vec(uids.begin(), uids.end());
    std::random_device rd;
    std::mt19937_64 gen(rd());
    long long chosen = vec[gen() % vec.size()];

    api->c_reset(ctx);
    api->c_emoji(ctx, "\xF0\x9F\x8E\xB2", 5444931419270839381LL);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, "Случайный участник: ", KOTO_ENT_BOLD);
    api->c_text(ctx, resolve_name(ctx, api, chosen).c_str());
    api->c_text(ctx, " (");
    api->c_fmt(ctx, std::to_string(chosen).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, ")");
    reply_or_edit(ctx, api);
}

// ── Экспорт сообщений участника в .txt (.getmsg) ─────────────────────────────
static void cmd_getmsg(koto_ctx* ctx, const koto_api* api) {
    if (!api->get_history_json) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Требуется get_history_json.", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    auto toks = split_ws(trim(api->cmd_args ? api->cmd_args(ctx) : ""));
    long long uid = resolve_target(ctx, api, toks);
    if (uid == 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Укажите участника (@username / ID / реплай).", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    int scan_limit = 3000;
    if (!toks.empty() && is_int_str(toks[0])) {
        try { scan_limit = std::clamp(std::stoi(toks[0]), 100, 10000); } catch (...) {}
    }

    api->c_reset(ctx);
    api->c_fmt(ctx, "\xE2\x8F\xB3 Собираю сообщения участника...", KOTO_ENT_ITALIC);
    reply_or_edit(ctx, api);

    long long cid = api->chat_id(ctx);
    std::string uname = resolve_name(ctx, api, uid);
    std::ostringstream out;
    out << "=== Сообщения пользователя " << uname << " (ID: " << uid << ") ===\n\n";

    int found = 0, fetched = 0, offset_id = 0;
    std::vector<char> buf(512 * 1024);

    while (fetched < scan_limit) {
        int lim = std::min(100, scan_limit - fetched);
        int n = api->get_history_json(ctx, cid, offset_id, lim, 0, buf.data(), static_cast<int>(buf.size()) - 1);
        if (n <= 0) break;
        buf[std::min(n, static_cast<int>(buf.size()) - 1)] = '\0';
        json arr = json::parse(buf.data(), nullptr, false);
        if (!arr.is_array() || arr.empty()) break;

        int min_id = offset_id;
        for (const auto& m : arr) {
            int mid = m.value("id", 0);
            if (mid > 0 && (min_id == 0 || mid < min_id)) min_id = mid;
            if (m.value("sender_id", 0LL) == uid) {
                std::string txt = m.value("text", "");
                if (!txt.empty()) {
                    long long dt = m.value("date", 0LL);
                    std::time_t tt = static_cast<std::time_t>(dt);
                    char tbuf[32] = {0};
                    std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", std::gmtime(&tt));
                    out << "[" << tbuf << " UTC] #" << mid << ": " << txt << "\n";
                    found++;
                }
            }
        }
        fetched += static_cast<int>(arr.size());
        if (min_id == 0 || min_id == offset_id) break;
        offset_id = min_id;
    }

    if (found == 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, ("Сообщений от " + uname + " за последние " + std::to_string(fetched) + " сообщений не найдено.").c_str(), KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }

    std::string content = out.str();
    std::string fname = "messages_" + std::to_string(uid) + ".txt";
    api->c_reset(ctx);
    api->c_emoji(ctx, "\xF0\x9F\x92\xAC", 5253590213917158323LL);
    api->c_text(ctx, (" Собрано " + std::to_string(found) + " сообщений от " + uname).c_str());
    api->c_send_bytes(ctx, cid, fname.c_str(),
                      reinterpret_cast<const unsigned char*>(content.data()),
                      static_cast<int>(content.size()), KOTO_SEND_AS_FILE);
    api->delete_msg(ctx);
}

// ── Стражи чата (.nochan, .guest) ────────────────────────────────────────────
static void toggle_guard(koto_ctx* ctx, const koto_api* api, const char* prefix_key, const char* label) {
    long long cid = api->chat_id(ctx);
    std::string key = std::string(prefix_key) + "_" + std::to_string(cid);
    long long cur = api->db_get_int(ctx, key.c_str(), 0);
    long long nxt = cur ? 0 : 1;
    api->db_set_int(ctx, key.c_str(), nxt);
    api->c_reset(ctx);
    api->c_emoji(ctx, nxt ? "\xE2\x9C\x85" : "\xE2\xAC\x9C", nxt ? 5206607081334906820LL : 5985346521103604145LL);
    api->c_text(ctx, (" " + std::string(label) + ": " + (nxt ? "ВКЛЮЧЕНО" : "ВЫКЛЮЧЕНО")).c_str());
    reply_or_edit(ctx, api);
}

static void cmd_nochan(koto_ctx* ctx, const koto_api* api) { toggle_guard(ctx, api, "nochan", "Запрет сообщений от каналов"); }
static void cmd_guest(koto_ctx* ctx, const koto_api* api)  { toggle_guard(ctx, api, "guest",  "Блокировка гостевых ботов"); }

// ── Цитаты (.gq / .gfq через Quotly API) ────────────────────────────────────
static bool b64_decode(const std::string& in, std::vector<unsigned char>& out) {
    static const int T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    out.clear();
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) continue;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<unsigned char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return !out.empty();
}

static void generate_quote(koto_ctx* ctx, const koto_api* api, long long uid, const std::string& name, const std::string& text) {
    if (!api->http_post) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Требуется Host >= 92 (http_post).", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    api->c_reset(ctx);
    api->c_fmt(ctx, "\xE2\x8F\xB3 Рисую цитату...", KOTO_ENT_ITALIC);
    reply_or_edit(ctx, api);

    json payload = {
        {"type", "quote"},
        {"format", "webp"},
        {"backgroundColor", "#1b1429"},
        {"width", 512},
        {"height", 768},
        {"scale", 2},
        {"messages", json::array({
            {
                {"entities", json::array()},
                {"avatar", true},
                {"from", {{"id", uid}, {"name", name}}},
                {"text", text}
            }
        })}
    };

    std::vector<unsigned char> resp(512 * 1024);
    long long n = api->http_post(ctx, "https://bot.lyo.su/quote/generate",
                                 payload.dump().c_str(),
                                 "Content-Type: application/json\r\n",
                                 resp.data(), static_cast<int>(resp.size()));
    if (n <= 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Сервис генерации цитат недоступен.", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }

    json j = json::parse(reinterpret_cast<const char*>(resp.data()),
                         reinterpret_cast<const char*>(resp.data()) + n, nullptr, false);
    std::string b64;
    if (j.is_object() && j.contains("result") && j["result"].is_object() && j["result"].contains("image")) {
        b64 = j["result"]["image"].get<std::string>();
    }
    std::vector<unsigned char> webp;
    if (b64.empty() || !b64_decode(b64, webp)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Не удалось декодировать изображение цитаты.", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }

    api->c_reset(ctx);
    api->c_send_bytes(ctx, api->chat_id(ctx), "quote.webp", webp.data(), static_cast<int>(webp.size()), 0);
    api->delete_msg(ctx);
}

static void cmd_gq(koto_ctx* ctx, const koto_api* api) {
    const char* rep = api->reply_text ? api->reply_text(ctx) : nullptr;
    if (!rep || !rep[0]) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Ответьте на сообщение для создания цитаты.", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    long long uid = api->reply_sender_id ? api->reply_sender_id(ctx) : 0;
    generate_quote(ctx, api, uid, resolve_name(ctx, api, uid), rep);
}

static void cmd_gfq(koto_ctx* ctx, const koto_api* api) {
    std::string text = trim(api->cmd_args ? api->cmd_args(ctx) : "");
    if (text.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Использование: .gfq <текст фейк-цитаты> (в ответ на пользователя).", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    long long uid = api->reply_sender_id ? api->reply_sender_id(ctx) : (api->get_me ? api->get_me(ctx) : 1);
    generate_quote(ctx, api, uid, resolve_name(ctx, api, uid), text);
}

// ── Закрыть/открыть чат (.banchat) и Паника (.panic) ────────────────────────
static void cmd_banchat(koto_ctx* ctx, const koto_api* api) {
    if (!koto::services::available(ctx, api, 112)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Требуется Kotogram Host >= 112.", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    long long cid = api->chat_id(ctx);
    std::string key = "banchat_" + std::to_string(cid);
    std::string args = to_lower(trim(api->cmd_args ? api->cmd_args(ctx) : ""));

    bool currently_closed = api->db_get_int(ctx, key.c_str(), 0) != 0;
    bool target_close = !currently_closed;
    int dur_sec = 0;

    if (args == "off" || args == "open" || args == "0") {
        target_close = false;
    } else if (args == "on" || args == "close") {
        target_close = true;
    } else if (!args.empty() && parse_duration_sec(args, dur_sec)) {
        target_close = true;
    }

    int until = (target_close && dur_sec > 0) ? static_cast<int>(std::time(nullptr)) + dur_sec : 0;
    std::string res = koto::services::chat_set_closed(ctx, api, cid, target_close, until);
    json j = json::parse(res, nullptr, false);
    if (!j.is_object() || !j.value("ok", false)) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "\xF0\x9F\x91\x8E", 5121063440311386962LL);
        api->c_text(ctx, (" Ошибка изменения прав чата: " + (j.is_object() ? j.value("error", "неизвестно") : "нет ответа")).c_str());
        reply_or_edit(ctx, api);
        return;
    }

    api->db_set_int(ctx, key.c_str(), target_close ? 1 : 0);
    api->c_reset(ctx);
    api->c_emoji(ctx, target_close ? "\xF0\x9F\x9A\xAB" : "\xE2\x9C\x85", target_close ? 5278578973595427038LL : 5206607081334906820LL);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, target_close ? "Чат закрыт" : "Чат открыт", KOTO_ENT_BOLD);
    if (target_close && dur_sec > 0) {
        api->c_text(ctx, (" (\xE2\x8F\xB1 " + fmt_duration(dur_sec) + ")").c_str());
    }
    reply_or_edit(ctx, api);
}

static void cmd_panic(koto_ctx* ctx, const koto_api* api) {
    if (!koto::services::available(ctx, api, 112)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Требуется Kotogram Host >= 112.", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    long long cid = api->chat_id(ctx);
    std::string key = "panic_" + std::to_string(cid);
    std::string args = to_lower(trim(api->cmd_args ? api->cmd_args(ctx) : ""));

    bool cur = api->db_get_int(ctx, key.c_str(), 0) != 0;
    bool enable = !cur;
    if (args == "on" || args == "1") enable = true;
    else if (args == "off" || args == "0") enable = false;

    api->c_reset(ctx);
    api->c_fmt(ctx, enable ? "\xE2\x8F\xB3 Активирую режим паники..." : "\xE2\x8F\xB3 Выключаю режим паники...", KOTO_ENT_ITALIC);
    reply_or_edit(ctx, api);

    auto r_chat = json::parse(koto::services::chat_set_closed(ctx, api, cid, enable, 0), nullptr, false);
    auto r_slow = json::parse(koto::services::chat_slowmode(ctx, api, cid, enable ? 3600 : 0), nullptr, false);
    json r_req;
    if (enable) {
        r_req = json::parse(koto::services::join_requests_resolve_all(ctx, api, cid, false), nullptr, false);
    }

    api->db_set_int(ctx, key.c_str(), enable ? 1 : 0);
    api->db_set_int(ctx, ("banchat_" + std::to_string(cid)).c_str(), enable ? 1 : 0);

    api->c_reset(ctx);
    api->c_emoji(ctx, enable ? "\xE2\x9A\xA0\xEF\xB8\x8F" : "\xE2\x9C\x85", enable ? 4904936030232117798LL : 5206607081334906820LL);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, enable ? "РЕЖИМ ПАНИКИ ВКЛЮЧЁН\n\n" : "Режим паники выключен\n\n", KOTO_ENT_BOLD);
    api->c_text(ctx, ("• Чат " + std::string(enable ? "закрыт" : "открыт") + ": " +
                      (r_chat.is_object() && r_chat.value("ok", false) ? "\xE2\x9C\x85" : "\xE2\x9D\x8C") + "\n").c_str());
    api->c_text(ctx, ("• Slow Mode (" + std::string(enable ? "1ч" : "выкл") + "): " +
                      (r_slow.is_object() && r_slow.value("ok", false) ? "\xE2\x9C\x85" : "\xE2\x9D\x8C")).c_str());
    if (enable) {
        api->c_text(ctx, ("\n• Отклонение заявок: " +
                          std::string(r_req.is_object() && r_req.value("ok", false) ? "\xE2\x9C\x85" : "\xE2\x80\x94")).c_str());
    }
    reply_or_edit(ctx, api);
}

// ── Заявки на вступление (.req) ──────────────────────────────────────────────
static void cmd_req(koto_ctx* ctx, const koto_api* api) {
    if (!koto::services::available(ctx, api, 112)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Требуется Kotogram Host >= 112.", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    long long cid = api->chat_id(ctx);
    std::string arg = to_lower(trim(api->cmd_args ? api->cmd_args(ctx) : ""));

    if (arg == "approve" || arg == "accept" || arg == "yes" || arg == "all") {
        auto r = json::parse(koto::services::join_requests_resolve_all(ctx, api, cid, true), nullptr, false);
        api->c_reset(ctx);
        if (r.is_object() && r.value("ok", false)) {
            api->c_emoji(ctx, "\xE2\x9C\x85", 5206607081334906820LL);
            api->c_text(ctx, " Все заявки на вступление одобрены.");
        } else {
            api->c_fmt(ctx, ("\xE2\x9D\x8C Ошибка: " + (r.is_object() ? r.value("error", "не удалось") : "нет ответа")).c_str(), KOTO_ENT_BOLD);
        }
        reply_or_edit(ctx, api);
        return;
    }
    if (arg == "decline" || arg == "reject" || arg == "deny" || arg == "no") {
        auto r = json::parse(koto::services::join_requests_resolve_all(ctx, api, cid, false), nullptr, false);
        api->c_reset(ctx);
        if (r.is_object() && r.value("ok", false)) {
            api->c_emoji(ctx, "\xF0\x9F\x9A\xAB", 5278578973595427038LL);
            api->c_text(ctx, " Все заявки на вступление отклонены.");
        } else {
            api->c_fmt(ctx, ("\xE2\x9D\x8C Ошибка: " + (r.is_object() ? r.value("error", "не удалось") : "нет ответа")).c_str(), KOTO_ENT_BOLD);
        }
        reply_or_edit(ctx, api);
        return;
    }

    auto r = json::parse(koto::services::join_requests_list(ctx, api, cid, 50), nullptr, false);
    if (!r.is_object() || !r.value("ok", false)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, ("\xE2\x9D\x8C Не удалось получить заявки: " + (r.is_object() ? r.value("error", "ошибка") : "нет ответа")).c_str(), KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    int count = r.value("count", 0);
    api->c_reset(ctx);
    api->c_fmt(ctx, ("\xF0\x9F\x93\xA9 Заявки на вступление (" + std::to_string(count) + ")\n\n").c_str(), KOTO_ENT_BOLD);
    if (count == 0 || !r.contains("requests") || !r["requests"].is_array() || r["requests"].empty()) {
        api->c_text(ctx, "Активных заявок нет.");
        reply_or_edit(ctx, api);
        return;
    }

    const auto& reqs = r["requests"];
    size_t show = std::min<size_t>(15, reqs.size());
    for (size_t i = 0; i < show; ++i) {
        std::string nm = reqs[i].value("name", "");
        std::string un = reqs[i].value("username", "");
        long long uid = reqs[i].value("user_id", 0LL);
        std::string label = !nm.empty() ? nm : ("ID:" + std::to_string(uid));
        if (!un.empty()) label += " (@" + un + ")";
        api->c_text(ctx, (std::to_string(i + 1) + ". " + label + "\n").c_str());
    }
    if (count > static_cast<int>(show)) {
        api->c_text(ctx, ("... и ещё " + std::to_string(count - static_cast<int>(show)) + "\n").c_str());
    }
    if (api->c_button_ex) {
        std::string scid = std::to_string(cid);
        api->c_button_ex(ctx, "Одобрить все", ("gc_req_ok:" + scid).c_str(), 1, KOTO_BTN_SUCCESS, 0);
        api->c_button_ex(ctx, "Отклонить все", ("gc_req_no:" + scid).c_str(), 0, KOTO_BTN_DANGER, 0);
    } else if (api->c_button) {
        std::string scid = std::to_string(cid);
        api->c_button(ctx, "Одобрить все", ("gc_req_ok:" + scid).c_str(), 1);
        api->c_button(ctx, "Отклонить все", ("gc_req_no:" + scid).c_str(), 0);
    }
    reply_or_edit(ctx, api);
}

// ── Кик участников по времени вступления (.tkick <время | -d>) ───────────────
static void cmd_tkick(koto_ctx* ctx, const koto_api* api) {
    if (!koto::services::available(ctx, api, 112)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Требуется Kotogram Host >= 112.", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }
    std::string args = to_lower(trim(api->cmd_args ? api->cmd_args(ctx) : ""));
    bool kick_deleted = (args == "-d" || args == "deleted");
    int dur_sec = 0;
    if (!kick_deleted && !parse_duration_sec(args, dur_sec)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "Использование:\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".tkick 30m", KOTO_ENT_CODE);
        api->c_text(ctx, " — кикнуть вступивших за последние 30 минут\n");
        api->c_fmt(ctx, ".tkick 2h", KOTO_ENT_CODE);
        api->c_text(ctx, " — кикнуть вступивших за последние 2 часа\n");
        api->c_fmt(ctx, ".tkick -d", KOTO_ENT_CODE);
        api->c_text(ctx, " — кикнуть удалённые аккаунты");
        reply_or_edit(ctx, api);
        return;
    }

    api->c_reset(ctx);
    api->c_fmt(ctx, "\xE2\x8F\xB3 Сканирую участников чата...", KOTO_ENT_ITALIC);
    reply_or_edit(ctx, api);

    long long cid = api->chat_id(ctx);
    long long me_id = api->get_me ? api->get_me(ctx) : 0;
    long long cutoff = static_cast<long long>(std::time(nullptr)) - dur_sec;

    std::vector<long long> to_kick;
    int scanned = 0;
    for (int offset = 0; offset < 2000; offset += 200) {
        auto r = json::parse(koto::services::chat_participants(ctx, api, cid, offset, 200), nullptr, false);
        if (!r.is_object() || !r.value("ok", false) || !r.contains("participants") || !r["participants"].is_array()) {
            if (offset == 0) {
                api->c_reset(ctx);
                api->c_fmt(ctx, ("\xE2\x9D\x8C Не удалось получить участников: " + (r.is_object() ? r.value("error", "ошибка") : "нет ответа")).c_str(), KOTO_ENT_BOLD);
                reply_or_edit(ctx, api);
                return;
            }
            break;
        }
        const auto& arr = r["participants"];
        if (arr.empty()) break;
        scanned += static_cast<int>(arr.size());

        for (const auto& p : arr) {
            std::string role = p.value("role", "member");
            long long uid = p.value("user_id", 0LL);
            if (uid == 0 || uid == me_id || role == "admin" || role == "creator" || role == "self") continue;

            if (kick_deleted) {
                if (p.value("deleted", false)) to_kick.push_back(uid);
            } else {
                long long jdate = p.value("date", 0LL);
                if (jdate >= cutoff) to_kick.push_back(uid);
            }
        }
        if (arr.size() < 200) break;
    }

    if (to_kick.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "\xE2\x9C\x85", 5206607081334906820LL);
        api->c_text(ctx, (" Подходящих участников для кика не найдено (проверено: " + std::to_string(scanned) + ").").c_str());
        reply_or_edit(ctx, api);
        return;
    }

    int kicked = 0;
    for (long long uid : to_kick) {
        if (api->kick_user) {
            api->kick_user(ctx, cid, uid);
            kicked++;
        }
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "\xF0\x9F\x93\xA4", 5873225338984599714LL);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, (kick_deleted ? "Кик удалённых аккаунтов: " : "Кик по времени вступления (" + fmt_duration(dur_sec) + "): ").c_str(), KOTO_ENT_BOLD);
    api->c_text(ctx, (std::to_string(kicked) + " чел. (проверено: " + std::to_string(scanned) + ")").c_str());
    reply_or_edit(ctx, api);
}

// ── Настройки и статус (.gcfg) ───────────────────────────────────────────────
static void render_gcfg(koto_ctx* ctx, const koto_api* api, long long cid) {
    bool nochan = api->db_get_int(ctx, ("nochan_" + std::to_string(cid)).c_str(), 0) != 0;
    bool guest = api->db_get_int(ctx, ("guest_" + std::to_string(cid)).c_str(), 0) != 0;
    json dm = json::parse(db_get_str(ctx, api, "dmute_" + std::to_string(cid)), nullptr, false);
    int dm_cnt = dm.is_object() ? static_cast<int>(dm.size()) : 0;

    api->c_reset(ctx);
    api->c_fmt(ctx, "\xE2\x9A\x99\xEF\xB8\x8F Панель GodChat\n\n", KOTO_ENT_BOLD);
    api->c_text(ctx, ("• Запрет каналов (.nochan): " + std::string(nochan ? "\xE2\x9C\x85 ВКЛ" : "\xE2\xAC\x9C ВЫКЛ") + "\n").c_str());
    api->c_text(ctx, ("• Анти-гостевые боты (.guest): " + std::string(guest ? "\xE2\x9C\x85 ВКЛ" : "\xE2\xAC\x9C ВЫКЛ") + "\n").c_str());
    api->c_text(ctx, ("• Активных делит-мутов (.dmute): " + std::to_string(dm_cnt)).c_str());

    if (api->c_button) {
        std::string scid = std::to_string(cid);
        api->c_button(ctx, nochan ? "Каналы: ВКЛ" : "Каналы: ВЫКЛ", ("gc_nc:" + scid).c_str(), 1);
        api->c_button(ctx, guest ? "Гость-боты: ВКЛ" : "Гость-боты: ВЫКЛ", ("gc_gb:" + scid).c_str(), 0);
        if (dm_cnt > 0) api->c_button(ctx, "Очистить dmute", ("gc_dmclr:" + scid).c_str(), 1);
    }
}

static void cmd_gcfg(koto_ctx* ctx, const koto_api* api) {
    render_gcfg(ctx, api, api->chat_id(ctx));
    reply_or_edit(ctx, api);
}

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    std::string data = api->cb_data ? api->cb_data(ctx) : "";
    auto colon = data.find(':');
    if (colon == std::string::npos) return;
    std::string act = data.substr(0, colon);
    long long cid = 0;
    try { cid = std::stoll(data.substr(colon + 1)); } catch (...) { return; }

    if (act == "gc_req_ok" || act == "gc_req_no") {
        bool approve = (act == "gc_req_ok");
        auto r = json::parse(koto::services::join_requests_resolve_all(ctx, api, cid, approve), nullptr, false);
        bool ok = r.is_object() && r.value("ok", false);
        if (api->cb_answer) api->cb_answer(ctx, ok ? (approve ? "Заявки одобрены" : "Заявки отклонены") : "Ошибка", ok ? 0 : 1);
        api->c_reset(ctx);
        if (ok) {
            api->c_emoji(ctx, approve ? "\xE2\x9C\x85" : "\xF0\x9F\x9A\xAB", approve ? 5206607081334906820LL : 5278578973595427038LL);
            api->c_text(ctx, approve ? " Все заявки на вступление одобрены." : " Все заявки на вступление отклонены.");
        } else {
            api->c_fmt(ctx, "\xE2\x9D\x8C Не удалось обработать заявки.", KOTO_ENT_BOLD);
        }
        api->c_edit_message(ctx, cid, api->cb_message_id(ctx), 0);
        return;
    }

    if (act == "gc_nc") {
        std::string k = "nochan_" + std::to_string(cid);
        api->db_set_int(ctx, k.c_str(), api->db_get_int(ctx, k.c_str(), 0) ? 0 : 1);
    } else if (act == "gc_gb") {
        std::string k = "guest_" + std::to_string(cid);
        api->db_set_int(ctx, k.c_str(), api->db_get_int(ctx, k.c_str(), 0) ? 0 : 1);
    } else if (act == "gc_dmclr") {
        api->db_del(ctx, ("dmute_" + std::to_string(cid)).c_str());
    } else {
        return;
    }

    if (api->cb_answer) api->cb_answer(ctx, "Обновлено", 0);
    render_gcfg(ctx, api, cid);
    api->c_edit_message(ctx, cid, api->cb_message_id(ctx), 0);
}

// ── Watcher: делит-мут, запрет каналов, анти-гостевые боты ───────────────────
static int on_watch(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing && api->is_outgoing(ctx)) return 0;
    if (!api->delete_message) return 0;

    long long cid = api->chat_id(ctx);
    long long sid = api->sender_id ? api->sender_id(ctx) : 0;
    long long mid = api->msg_id ? api->msg_id(ctx) : 0;
    if (cid == 0 || mid == 0) return 0;

    // 1. Запрет сообщений от имени канала (отрицательный sender_id)
    if (sid < 0 && api->db_get_int(ctx, ("nochan_" + std::to_string(cid)).c_str(), 0) != 0) {
        api->delete_message(ctx, cid, mid);
        return 0;
    }

    // 2. Делит-мут
    if (sid != 0) {
        std::string dm_key = "dmute_" + std::to_string(cid);
        std::string raw_dm = db_get_str(ctx, api, dm_key);
        if (!raw_dm.empty()) {
            json dm = json::parse(raw_dm, nullptr, false);
            std::string suid = std::to_string(sid);
            if (dm.is_object() && dm.contains(suid)) {
                long long until = dm[suid].get<long long>();
                long long now = static_cast<long long>(std::time(nullptr));
                if (until == 0 || now < until) {
                    api->delete_message(ctx, cid, mid);
                    return 0;
                } else {
                    dm.erase(suid);
                    api->db_set(ctx, dm_key.c_str(), dm.dump().c_str());
                }
            }
        }
    }

    // 3. Анти-гостевые боты
    if (sid > 0 && api->db_get_int(ctx, ("guest_" + std::to_string(cid)).c_str(), 0) != 0 && api->user_username) {
        char ubuf[128] = {0};
        int n = api->user_username(ctx, sid, ubuf, sizeof(ubuf) - 1);
        if (n >= 3) {
            ubuf[n] = '\0';
            std::string un = to_lower(ubuf);
            if (un.size() >= 3 && un.substr(un.size() - 3) == "bot") {
                api->delete_message(ctx, cid, mid);
                if (api->kick_user) api->kick_user(ctx, cid, sid);
                return 0;
            }
        }
    }

    return 0;
}

static const koto_command COMMANDS[] = {
    { "ban",     &cmd_ban,     KOTO_LEVEL_TRUSTED, "[ru] Забанить пользователя [время] [-d] [причина]. [en] Ban user.", "[user] [time] [-d] [reason]" },
    { "unban",   &cmd_unban,   KOTO_LEVEL_TRUSTED, "[ru] Разбанить пользователя. [en] Unban user.", "[user]" },
    { "kick",    &cmd_kick,    KOTO_LEVEL_TRUSTED, "[ru] Кикнуть пользователя. [en] Kick user.", "[user] [reason]" },
    { "mute",    &cmd_mute,    KOTO_LEVEL_TRUSTED, "[ru] Замутить пользователя [время] [причина]. [en] Mute user.", "[user] [time] [reason]" },
    { "unmute",  &cmd_unmute,  KOTO_LEVEL_TRUSTED, "[ru] Снять мут с пользователя. [en] Unmute user.", "[user]" },
    { "dmute",   &cmd_dmute,   KOTO_LEVEL_TRUSTED, "[ru] Делит-мут (автоудаление сообщений пользователя). [en] Delete-mute toggle.", "[user] [time]" },
    { "gban",    &cmd_gban,    KOTO_LEVEL_OWNER,   "[ru] Глобальный бан во всех доступных чатах. [en] Global ban.", "[user] [reason]" },
    { "gunban",  &cmd_gunban,  KOTO_LEVEL_OWNER,   "[ru] Глобальный разбан во всех чатах. [en] Global unban.", "[user]" },
    { "purge",   &cmd_purge,   KOTO_LEVEL_TRUSTED, "[ru] Удалить все сообщения до реплая. [en] Delete messages up to reply.", nullptr },
    { "del",     &cmd_del,     KOTO_LEVEL_TRUSTED, "[ru] Удалить реплай или последние N сообщений. [en] Delete reply or last N messages.", "[N]" },
    { "pref",    &cmd_pref,    KOTO_LEVEL_TRUSTED, "[ru] Установить админ-префикс (ранг). [en] Set admin tag.", "[user] [rank]" },
    { "aclear",  &cmd_aclear,  KOTO_LEVEL_OWNER,   "[ru] Очистить админ-теги чата. [en] Clear admin tags.", nullptr },
    { "member",  &cmd_member,  KOTO_LEVEL_TRUSTED, "[ru] Карточка участника (ID, юзернейм, био). [en] Member info card.", "[user/reply]" },
    { "rmember", &cmd_rmember, KOTO_LEVEL_TRUSTED, "[ru] Выбрать случайного активного участника чата. [en] Random active chat member.", nullptr },
    { "getmsg",  &cmd_getmsg,  KOTO_LEVEL_OWNER,   "[ru] Выгрузить сообщения участника в TXT-файл. [en] Export user messages to TXT.", "[user/reply] [limit]" },
    { "nochan",  &cmd_nochan,  KOTO_LEVEL_TRUSTED, "[ru] Вкл/выкл автоудаление сообщений от каналов. [en] Toggle channel block.", nullptr },
    { "guest",   &cmd_guest,   KOTO_LEVEL_TRUSTED, "[ru] Вкл/выкл автокик гостевых ботов. [en] Toggle guest bot blocker.", nullptr },
    { "banchat", &cmd_banchat, KOTO_LEVEL_TRUSTED, "[ru] Закрыть/открыть чат [время | on | off]. [en] Close/open chat.", "[time | on | off]" },
    { "panic",   &cmd_panic,   KOTO_LEVEL_TRUSTED, "[ru] Режим паники (закрыть чат + slowmode 1ч + отклонить заявки). [en] Toggle panic mode.", "[on | off]" },
    { "req",     &cmd_req,     KOTO_LEVEL_TRUSTED, "[ru] Управление заявками на вступление [approve | decline]. [en] Manage join requests.", "[approve | decline]" },
    { "tkick",   &cmd_tkick,   KOTO_LEVEL_TRUSTED, "[ru] Кик участников по времени вступления (30m/2h) или удалённых (-d). [en] Kick recent joiners or deleted accounts.", "<время | -d>" },
    { "gq",      &cmd_gq,      KOTO_LEVEL_TRUSTED, "[ru] Цитата из реплая (стикер через Quotly). [en] Create quote sticker.", nullptr },
    { "gfq",     &cmd_gfq,     KOTO_LEVEL_TRUSTED, "[ru] Фейк-цитата с произвольным текстом. [en] Create fake quote.", "<текст>" },
    { "gcfg",    &cmd_gcfg,    KOTO_LEVEL_OWNER,   "[ru] Интерактивная панель настроек GodChat. [en] GodChat settings panel.", nullptr }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "godchat",
    "[ru] Модерация, banchat/panic, заявки (req), tkick, делит-мут, гбан, экспорт сообщений в TXT, стражи чата и цитаты (порт GodChat.py). Host 112+, C++ ABI 3. [en] All-in-one chat moderation & utilities suite. C++ ABI 3.",
    "1.1.1",
    112,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    &on_watch,
    KOTO_WATCH_INCOMING,
    &on_callback,
    nullptr, 0,
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/godchat.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
