// rp.cpp
// Полнофункциональный Role-Play модуль для Kotogram (C/C++ ABI 3).
// Автор: Kote
// Паритет с оригиналом rpmod.py 2.7.7: премиум-кастом-эмодзи, формат RP-экшна
// {эмодзи} | {отправитель} {действие} {цель}, addrp с эмодзи, управление иконками.

#include <string>
#include <vector>
#include <sstream>
#include <map>
#include <set>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
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

// Разбить строку по разделителю БЕЗ отбрасывания пустых и БЕЗ trim (для хранения).
static inline std::vector<std::string> split_raw(const std::string& s, char delim) {
    std::vector<std::string> res;
    std::string cur;
    for (char c : s) {
        if (c == delim) { res.push_back(cur); cur.clear(); }
        else cur += c;
    }
    res.push_back(cur);
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

// ── Статические премиум-эмодзи (как DEFAULTS в питоне) ────────────────────────
struct StaticEmoji { const char* key; long long id; const char* fallback; };
static const StaticEmoji STATIC_EMOJIS[] = {
    { "ERROR",    5195105446679039811LL, "\xE2\x9D\x8C" },             // ❌
    { "SUCCESS",  5195127277997806826LL, "\xE2\x9C\x85" },             // ✅
    { "INFO",     6028435952299413210LL, "\xE2\x84\xB9\xEF\xB8\x8F" }, // ℹ️
    { "TRASH",    6039522349517115015LL, "\xF0\x9F\x97\x91\xEF\xB8\x8F" }, // 🗑️
    { "LOCK",     5778570255555105942LL, "\xF0\x9F\x94\x92" },         // 🔒
    { "CROWN",    5807868868886009920LL, "\xF0\x9F\x91\x91" },         // 👑
    { "NICK",     6008104758236156926LL, "\xF0\x9F\x93\x9B" },         // 📛
    { "COMMENT",  6034831751308644168LL, "\xF0\x9F\x92\xAC" },         // 💬
    { "LOAD",     5891211339170326418LL, "\xE2\x8F\xB3" },             // ⏳
    { "IN_OUT",   5877307202888273539LL, "\xF0\x9F\x93\xA5" },         // 📥
    { "UPDATE",   5877410604225924969LL, "\xF0\x9F\x94\x84" },         // 🔄
    { "SKIP",     5253868738251335638LL, "\xE2\x8F\xAD\xEF\xB8\x8F" }, // ⏭️
    { "LIST",     5956561916573782596LL, "\xF0\x9F\x93\x8B" },         // 📋
    { "SETTINGS", 6032742198179532882LL, "\xE2\x9A\x99\xEF\xB8\x8F" }, // ⚙️
};
static const int STATIC_EMOJI_COUNT = sizeof(STATIC_EMOJIS) / sizeof(STATIC_EMOJIS[0]);

// Данные статического эмодзи по ключу: дефолт + override из БД (semoji:<KEY> = id|fallback).
static void get_static_emoji(koto_ctx* ctx, const koto_api* api, const std::string& key,
                             long long& out_id, std::string& out_fb) {
    std::string K = key;
    for (char& c : K) c = (char)std::toupper((unsigned char)c);
    out_id = 0; out_fb = "\xE2\x9D\x94"; // ❔
    for (int i = 0; i < STATIC_EMOJI_COUNT; ++i) {
        if (K == STATIC_EMOJIS[i].key) { out_id = STATIC_EMOJIS[i].id; out_fb = STATIC_EMOJIS[i].fallback; break; }
    }
    std::string ov = db_get_str(ctx, api, "semoji:" + K);
    if (!ov.empty()) {
        size_t p = ov.find('|');
        if (p != std::string::npos) {
            try { out_id = std::stoll(ov.substr(0, p)); } catch (...) {}
            std::string f = ov.substr(p + 1);
            if (!f.empty()) out_fb = f;
        }
    }
}

// Выдать премиум-эмодзи (или фоллбэк-текст, если id==0) в композер.
static void emit_emoji_part(koto_ctx* ctx, const koto_api* api, long long id, const std::string& fallback) {
    std::string fb = fallback.empty() ? std::string("\xE2\x9D\x94") : fallback; // ❔
    if (id != 0) api->c_emoji(ctx, fb.c_str(), id);
    else api->c_text(ctx, fb.c_str());
}

// Выдать статический эмодзи по ключу (ERROR/SUCCESS/...).
static void emit_static(koto_ctx* ctx, const koto_api* api, const std::string& key) {
    long long id; std::string fb;
    get_static_emoji(ctx, api, key, id, fb);
    emit_emoji_part(ctx, api, id, fb);
}

// ── Формат хранения RP-команды: cmd:<alias> = "action|emoji_id|fallback" ───────
struct RpCmd { std::string action; long long emoji_id; std::string fallback; };

static RpCmd parse_rp_cmd(const std::string& stored) {
    RpCmd r; r.emoji_id = 0; r.fallback = "";
    size_t p1 = stored.find('|');
    if (p1 == std::string::npos) { r.action = stored; return r; }
    r.action = stored.substr(0, p1);
    std::string rest = stored.substr(p1 + 1);
    size_t p2 = rest.find('|');
    if (p2 == std::string::npos) {
        try { r.emoji_id = std::stoll(rest); } catch (...) {}
        return r;
    }
    try { r.emoji_id = std::stoll(rest.substr(0, p2)); } catch (...) {}
    r.fallback = rest.substr(p2 + 1);
    return r;
}

static std::string serialize_rp_cmd(const std::string& action, long long emoji_id, const std::string& fallback) {
    return action + "|" + std::to_string(emoji_id) + "|" + fallback;
}

// Найти в сообщении кастом-эмодзи-entity с НАИБОЛЬШИМ офсетом (эмодзи-часть идёт в конце).
static long long find_last_custom_emoji(koto_ctx* ctx, const koto_api* api) {
    int cnt = api->msg_entity_count(ctx);
    long long best_doc = 0; int best_off = -1;
    for (int i = 0; i < cnt; ++i) {
        int type = 0, off = 0, len = 0; long long doc = 0; char url[8] = {0};
        if (api->msg_entity(ctx, i, &type, &off, &len, &doc, url, sizeof(url)) <= 0) continue;
        if (type == KOTO_ENT_CUSTOM_EMOJI && doc != 0 && off > best_off) {
            best_off = off; best_doc = doc;
        }
    }
    return best_doc;
}

// ── Очистка старых предустановленных дефолтов при обновлении ───────────────────
static void cleanup_old_defaults_if_needed(koto_ctx* ctx, const koto_api* api) {
    std::string v = db_get_str(ctx, api, "v_cleaned");
    if (v == "3.0.0") return;

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
        if (is_old) db_del(ctx, api, "cmd:" + c);
        else remaining.push_back(c);
    }
    std::string new_list;
    for (const auto& c : remaining) {
        if (!new_list.empty()) new_list += ",";
        new_list += c;
    }
    db_set_str(ctx, api, "cmd_list", new_list);
    db_set_str(ctx, api, "v_cleaned", "3.0.0");
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
    if (uid == 0) return "\xD0\x9F\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8C"; // Пользователь

    std::string cn = db_get_str(ctx, api, "nick:" + std::to_string(cid) + ":" + std::to_string(uid));
    if (!cn.empty() && cn != "none") return cn;

    std::string gn = db_get_str(ctx, api, "nick:0:" + std::to_string(uid));
    if (!gn.empty() && gn != "none") return gn;

    if (uid == api->get_me(ctx)) {
        char mbuf[128] = {0};
        if (api->get_me_name(ctx, mbuf, sizeof(mbuf)) > 0 && *mbuf) return std::string(mbuf);
    }

    char buf[128] = {0};
    if (api->user_name(ctx, uid, buf, sizeof(buf)) > 0 && *buf) return std::string(buf);

    return (uid == api->get_me(ctx)) ? "\xD0\xAF" : "\xD0\x9F\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8C";
}

// PLACEHOLDER_CMDS

// ── 1. Команда .rp [on/off/access] ───────────────────────────────────────────
static void cmd_rp_access(koto_ctx* ctx, const koto_api* api, long long cid, const std::vector<std::string>& sub);

static void cmd_rp(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    cleanup_old_defaults_if_needed(ctx, api);
    long long cid = api->chat_id(ctx);
    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";

    if (args.empty()) {
        api->c_reset(ctx);
        emit_static(ctx, api, "INFO");
        api->c_fmt(ctx, " \xD0\xA3\xD0\xBF\xD1\x80\xD0\xB0\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5 RP-\xD0\xBC\xD0\xBE\xD0\xB4\xD1\x83\xD0\xBB\xD0\xB5\xD0\xBC:", KOTO_ENT_BOLD);
        api->c_text(ctx, "\n\n\xE2\x80\xA2 ");
        api->c_fmt(ctx, ".rp on", KOTO_ENT_CODE);
        api->c_text(ctx, " \xE2\x80\x94 \xD0\xB2\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87\xD0\xB8\xD1\x82\xD1\x8C RP \xD0\xB2 \xD1\x8D\xD1\x82\xD0\xBE\xD0\xBC \xD1\x87\xD0\xB0\xD1\x82\xD0\xB5\n\xE2\x80\xA2 ");
        api->c_fmt(ctx, ".rp off", KOTO_ENT_CODE);
        api->c_text(ctx, " \xE2\x80\x94 \xD0\xB2\xD1\x8B\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87\xD0\xB8\xD1\x82\xD1\x8C RP \xD0\xB2 \xD1\x8D\xD1\x82\xD0\xBE\xD0\xBC \xD1\x87\xD0\xB0\xD1\x82\xD0\xB5\n\xE2\x80\xA2 ");
        api->c_fmt(ctx, ".rp access list", KOTO_ENT_CODE);
        api->c_text(ctx, " \xE2\x80\x94 \xD1\x81\xD0\xBF\xD0\xB8\xD1\x81\xD0\xBE\xD0\xBA \xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF\xD0\xB0\n\xE2\x80\xA2 ");
        api->c_fmt(ctx, ".rp access add <@user/all>", KOTO_ENT_CODE);
        api->c_text(ctx, " \xE2\x80\x94 \xD0\xB4\xD0\xB0\xD1\x82\xD1\x8C \xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF\n\xE2\x80\xA2 ");
        api->c_fmt(ctx, ".rp access remove <@user/all>", KOTO_ENT_CODE);
        api->c_text(ctx, " \xE2\x80\x94 \xD0\xB7\xD0\xB0\xD0\xB1\xD1\x80\xD0\xB0\xD1\x82\xD1\x8C \xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF");
        api->c_edit(ctx, 0);
        return;
    }

    auto sub = split(args, ' ');
    std::string subcmd = to_lower(sub[0]);

    if (subcmd == "on") {
        db_set_str(ctx, api, "enabled:" + std::to_string(cid), "1");
        api->c_reset(ctx);
        emit_static(ctx, api, "SUCCESS");
        api->c_fmt(ctx, " RP-\xD0\xBC\xD0\xBE\xD0\xB4\xD1\x83\xD0\xBB\xD1\x8C \xD1\x83\xD1\x81\xD0\xBF\xD0\xB5\xD1\x88\xD0\xBD\xD0\xBE \xD0\x92\xD0\x9A\xD0\x9B\xD0\xAE\xD0\xA7\xD0\x95\xD0\x9D \xD0\xB2 \xD1\x8D\xD1\x82\xD0\xBE\xD0\xBC \xD1\x87\xD0\xB0\xD1\x82\xD0\xB5!", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
    } else if (subcmd == "off") {
        db_del(ctx, api, "enabled:" + std::to_string(cid));
        api->c_reset(ctx);
        emit_static(ctx, api, "TRASH");
        api->c_fmt(ctx, " RP-\xD0\xBC\xD0\xBE\xD0\xB4\xD1\x83\xD0\xBB\xD1\x8C \xD0\x92\xD0\xAB\xD0\x9A\xD0\x9B\xD0\xAE\xD0\xA7\xD0\x95\xD0\x9D \xD0\xB2 \xD1\x8D\xD1\x82\xD0\xBE\xD0\xBC \xD1\x87\xD0\xB0\xD1\x82\xD0\xB5.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
    } else if (subcmd == "access") {
        cmd_rp_access(ctx, api, cid, sub);
    } else {
        api->c_reset(ctx);
        emit_static(ctx, api, "ERROR");
        api->c_fmt(ctx, " \xD0\x9D\xD0\xB5\xD0\xB8\xD0\xB7\xD0\xB2\xD0\xB5\xD1\x81\xD1\x82\xD0\xBD\xD0\xB0\xD1\x8F \xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD0\xB0. \xD0\x9E\xD0\xB6\xD0\xB8\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xBE\xD1\x81\xD1\x8C: on, off \xD0\xB8\xD0\xBB\xD0\xB8 access.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
    }
}

// PLACEHOLDER_AFTER_RP

// ── 1b. .rp access <list|add|remove> [@user/all] ──────────────────────────────
static void cmd_rp_access(koto_ctx* ctx, const koto_api* api, long long cid, const std::vector<std::string>& sub) {
    // sub[0] == "access"
    if (sub.size() < 2) {
        api->c_reset(ctx);
        emit_static(ctx, api, "ERROR");
        api->c_fmt(ctx, " \xD0\xA3\xD0\xBA\xD0\xB0\xD0\xB6\xD0\xB8\xD1\x82\xD0\xB5 \xD0\xB4\xD0\xB5\xD0\xB9\xD1\x81\xD1\x82\xD0\xB2\xD0\xB8\xD0\xB5: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".rp access <add/remove/list> [@user/all]", KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    std::string action = to_lower(sub[1]);
    std::string target = "";
    for (size_t i = 2; i < sub.size(); ++i) { if (i > 2) target += " "; target += sub[i]; }
    target = trim(target);

    if (action == "list") {
        bool pub = db_has(ctx, api, "public:" + std::to_string(cid));
        api->c_reset(ctx);
        if (pub) {
            emit_static(ctx, api, "SUCCESS");
            api->c_fmt(ctx, " \xD0\x94\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF \xD0\xBA RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD0\xB0\xD0\xBC \xD0\xB2 \xD1\x8D\xD1\x82\xD0\xBE\xD0\xBC \xD1\x87\xD0\xB0\xD1\x82\xD0\xB5 \xD0\xBE\xD1\x82\xD0\xBA\xD1\x80\xD1\x8B\xD1\x82 \xD0\xB4\xD0\xBB\xD1\x8F \xD0\xB2\xD1\x81\xD0\xB5\xD1\x85.", KOTO_ENT_BOLD);
            api->c_edit(ctx, 0);
            return;
        }
        emit_static(ctx, api, "LOCK");
        api->c_fmt(ctx, " \xD0\x9F\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD0\xB8 \xD1\x81 \xD0\xB8\xD0\xBD\xD0\xB4\xD0\xB8\xD0\xB2\xD0\xB8\xD0\xB4\xD1\x83\xD0\xB0\xD0\xBB\xD1\x8C\xD0\xBD\xD1\x8B\xD0\xBC \xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF\xD0\xBE\xD0\xBC:", KOTO_ENT_BOLD);
        api->c_text(ctx, "\n\n");
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
                    long long uid = 0; try { uid = std::stoll(uid_str); } catch (...) {}
                    std::string nm = get_rp_display_name(ctx, api, uid, cid);
                    api->c_text(ctx, "\xE2\x80\xA2 ");
                    api->c_url(ctx, nm.c_str(), ("tg://user?id=" + uid_str).c_str());
                    api->c_text(ctx, "\n");
                    count++;
                }
            }
        }
        if (count == 0) api->c_text(ctx, "\xD0\x9D\xD0\xB8\xD0\xBA\xD0\xBE\xD0\xBC\xD1\x83 \xD0\xBD\xD0\xB5 \xD0\xB2\xD1\x8B\xD0\xB4\xD0\xB0\xD0\xBD \xD0\xB8\xD0\xBD\xD0\xB4\xD0\xB8\xD0\xB2\xD0\xB8\xD0\xB4\xD1\x83\xD0\xB0\xD0\xBB\xD1\x8C\xD0\xBD\xD1\x8B\xD0\xB9 \xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF.");
        api->c_text(ctx, "\n\n");
        api->c_fmt(ctx, "\xD0\x92\xD0\xBB\xD0\xB0\xD0\xB4\xD0\xB5\xD0\xBB\xD0\xB5\xD1\x86 \xD0\xB8 \xD0\xB4\xD0\xBE\xD0\xB2\xD0\xB5\xD1\x80\xD0\xB5\xD0\xBD\xD0\xBD\xD1\x8B\xD0\xB5 (TRUSTED) \xD0\xB2\xD1\x81\xD0\xB5\xD0\xB3\xD0\xB4\xD0\xB0 \xD0\xB8\xD0\xBC\xD0\xB5\xD1\x8E\xD1\x82 \xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    if (action != "add" && action != "remove" && action != "del") {
        api->c_reset(ctx);
        emit_static(ctx, api, "ERROR");
        api->c_fmt(ctx, " \xD0\x9D\xD0\xB5\xD0\xB8\xD0\xB7\xD0\xB2\xD0\xB5\xD1\x81\xD1\x82\xD0\xBD\xD0\xBE\xD0\xB5 \xD0\xB4\xD0\xB5\xD0\xB9\xD1\x81\xD1\x82\xD0\xB2\xD0\xB8\xD0\xB5. \xD0\x9E\xD0\xB6\xD0\xB8\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xBE\xD1\x81\xD1\x8C: add, remove \xD0\xB8\xD0\xBB\xD0\xB8 list.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }
    bool is_add = (action == "add");

    if (to_lower(target) == "all") {
        api->c_reset(ctx);
        if (is_add) {
            db_set_str(ctx, api, "public:" + std::to_string(cid), "1");
            emit_static(ctx, api, "SUCCESS");
            api->c_fmt(ctx, " \xD0\x94\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF \xD0\xBA RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD0\xB0\xD0\xBC \xD0\xBE\xD1\x82\xD0\xBA\xD1\x80\xD1\x8B\xD1\x82 \xD0\xB4\xD0\xBB\xD1\x8F \xD0\x92\xD0\xA1\xD0\x95\xD0\xA5!", KOTO_ENT_BOLD);
        } else {
            db_del(ctx, api, "public:" + std::to_string(cid));
            emit_static(ctx, api, "TRASH");
            api->c_fmt(ctx, " \xD0\x9F\xD1\x83\xD0\xB1\xD0\xBB\xD0\xB8\xD1\x87\xD0\xBD\xD1\x8B\xD0\xB9 \xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF \xD0\xBA RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD0\xB0\xD0\xBC \xD0\x97\xD0\x90\xD0\x9A\xD0\xA0\xD0\xAB\xD0\xA2!", KOTO_ENT_BOLD);
        }
        api->c_edit(ctx, 0);
        return;
    }

    long long target_uid = api->reply_sender_id(ctx);
    if (!target.empty()) {
        if (target.front() == '@') target_uid = api->resolve(ctx, target.substr(1).c_str());
        else if (isdigit((unsigned char)target.front())) { try { target_uid = std::stoll(target); } catch (...) {} }
    }
    if (target_uid == 0) {
        api->c_reset(ctx);
        emit_static(ctx, api, "ERROR");
        api->c_fmt(ctx, " \xD0\xA3\xD0\xBA\xD0\xB0\xD0\xB6\xD0\xB8\xD1\x82\xD0\xB5 \xD1\x86\xD0\xB5\xD0\xBB\xD1\x8C (@user, ID, all \xD0\xB8\xD0\xBB\xD0\xB8 \xD1\x80\xD0\xB5\xD0\xBF\xD0\xBB\xD0\xB0\xD0\xB9).", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    api->c_reset(ctx);
    std::string nm = get_rp_display_name(ctx, api, target_uid, cid);
    if (is_add) {
        db_set_str(ctx, api, "access:" + std::to_string(cid) + ":" + std::to_string(target_uid), "1");
        emit_static(ctx, api, "SUCCESS");
        api->c_fmt(ctx, " \xD0\x9F\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8C ", KOTO_ENT_BOLD);
        api->c_url(ctx, nm.c_str(), ("tg://user?id=" + std::to_string(target_uid)).c_str());
        api->c_fmt(ctx, " \xD0\xBF\xD0\xBE\xD0\xBB\xD1\x83\xD1\x87\xD0\xB8\xD0\xBB \xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF \xD0\xBA RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD0\xB0\xD0\xBC.", KOTO_ENT_BOLD);
    } else {
        db_del(ctx, api, "access:" + std::to_string(cid) + ":" + std::to_string(target_uid));
        emit_static(ctx, api, "TRASH");
        api->c_fmt(ctx, " \xD0\x9F\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8C ", KOTO_ENT_BOLD);
        api->c_url(ctx, nm.c_str(), ("tg://user?id=" + std::to_string(target_uid)).c_str());
        api->c_fmt(ctx, " \xD0\xBB\xD0\xB8\xD1\x88\xD0\xB5\xD0\xBD \xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF\xD0\xB0 \xD0\xBA RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD0\xB0\xD0\xBC.", KOTO_ENT_BOLD);
    }
    api->c_edit(ctx, 0);
}

// PLACEHOLDER_ADDRP

// ── 2. Команда .addrp <алиасы>|<действие>[|<эмодзи/ID>] ───────────────────────
static void cmd_addrp(koto_ctx* ctx, const koto_api* api) {
    long long sender = api->sender_id(ctx);
    if (!is_rp_creator(ctx, api, sender)) {
        api->c_reset(ctx);
        emit_static(ctx, api, "ERROR");
        api->c_fmt(ctx, " \xD0\xA3 \xD0\xB2\xD0\xB0\xD1\x81 \xD0\xBD\xD0\xB5\xD1\x82 \xD0\xBF\xD1\x80\xD0\xB0\xD0\xB2 \xD1\x81\xD0\xBE\xD0\xB7\xD0\xB4\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8F RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";
    if (args.empty()) {
        api->c_reset(ctx);
        emit_static(ctx, api, "INFO");
        api->c_fmt(ctx, " \xD0\x98\xD1\x81\xD0\xBF\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB5:", KOTO_ENT_BOLD);
        api->c_text(ctx, "\n");
        api->c_fmt(ctx, ".addrp <\xD0\xB0\xD0\xBB\xD0\xB8\xD0\xB0\xD1\x81\xD1\x8B>|<\xD0\xB4\xD0\xB5\xD0\xB9\xD1\x81\xD1\x82\xD0\xB2\xD0\xB8\xD0\xB5>|<\xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8/ID>", KOTO_ENT_CODE);
        api->c_text(ctx, "\n\n\xD0\x9F\xD1\x80\xD0\xB8\xD0\xBC\xD0\xB5\xD1\x80\xD1\x8B:\n");
        api->c_fmt(ctx, ".addrp \xD0\xBE\xD0\xB1\xD0\xBD\xD1\x8F\xD1\x82\xD1\x8C|\xD0\xBE\xD0\xB1\xD0\xBD\xD1\x8F\xD0\xBB(\xD0\xB0)|\xF0\x9F\xA4\x97", KOTO_ENT_CODE);
        api->c_text(ctx, "\n");
        api->c_fmt(ctx, ".addrp \xD0\xBA\xD1\x83\xD1\x81\xD1\x8C/\xD0\xBA\xD1\x83\xD1\x81\xD0\xB8\xD1\x82\xD1\x8C|\xD1\x81\xD0\xB4\xD0\xB5\xD0\xBB\xD0\xB0\xD0\xBB(\xD0\xB0) \xD0\xBA\xD1\x83\xD1\x81\xD1\x8C|\xF0\x9F\x98\xBA", KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    auto parts = split_raw(args, '|');
    if (parts.size() < 2) {
        api->c_reset(ctx);
        emit_static(ctx, api, "ERROR");
        api->c_fmt(ctx, " \xD0\x9D\xD0\xB5\xD0\xB2\xD0\xB5\xD1\x80\xD0\xBD\xD1\x8B\xD0\xB9 \xD1\x84\xD0\xBE\xD1\x80\xD0\xBC\xD0\xB0\xD1\x82! \xD0\xA0\xD0\xB0\xD0\xB7\xD0\xB4\xD0\xB5\xD0\xBB\xD0\xB8\xD1\x82\xD0\xB5: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".addrp \xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD0\xB0|\xD0\xB4\xD0\xB5\xD0\xB9\xD1\x81\xD1\x82\xD0\xB2\xD0\xB8\xD0\xB5|\xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8/ID", KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    std::string aliases_str = trim(parts[0]);
    std::string action = trim(parts[1]);
    std::string emoji_input = "";
    for (size_t i = 2; i < parts.size(); ++i) { if (i > 2) emoji_input += "|"; emoji_input += parts[i]; }
    emoji_input = trim(emoji_input);

    std::vector<std::string> aliases;
    for (const auto& a : split(aliases_str, '/')) {
        std::string t = to_lower(trim(a));
        if (!t.empty()) { if (t.front() == '.') t = t.substr(1); aliases.push_back(t); }
    }

    if (aliases.empty() || action.empty()) {
        api->c_reset(ctx);
        emit_static(ctx, api, "ERROR");
        api->c_fmt(ctx, " \xD0\x90\xD0\xBB\xD0\xB8\xD0\xB0\xD1\x81\xD1\x8B \xD0\xB8\xD0\xBB\xD0\xB8 \xD0\xB4\xD0\xB5\xD0\xB9\xD1\x81\xD1\x82\xD0\xB2\xD0\xB8\xD0\xB5 \xD0\xBD\xD0\xB5 \xD0\xBC\xD0\xBE\xD0\xB3\xD1\x83\xD1\x82 \xD0\xB1\xD1\x8B\xD1\x82\xD1\x8C \xD0\xBF\xD1\x83\xD1\x81\xD1\x82\xD1\x8B\xD0\xBC\xD0\xB8.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    // Эмодзи: 1) custom-emoji entity из сообщения; 2) числовой ID; 3) обычный символ.
    long long emoji_id = find_last_custom_emoji(ctx, api);
    std::string emoji_fb;
    if (emoji_id != 0) {
        emoji_fb = emoji_input.empty() ? std::string("\xE2\x9C\xA8") : emoji_input; // ✨
    } else if (!emoji_input.empty()) {
        bool all_digit = true;
        for (char c : emoji_input) if (!isdigit((unsigned char)c)) { all_digit = false; break; }
        if (all_digit) { try { emoji_id = std::stoll(emoji_input); } catch (...) {} emoji_fb = "\xE2\x9C\xA8"; }
        else emoji_fb = emoji_input;
    }

    std::string cur_list = db_get_str(ctx, api, "cmd_list");
    auto cur_cmds = split(cur_list, ',');
    std::set<std::string> cmd_set(cur_cmds.begin(), cur_cmds.end());

    std::string stored = serialize_rp_cmd(action, emoji_id, emoji_fb);
    for (const auto& a : aliases) { db_set_str(ctx, api, "cmd:" + a, stored); cmd_set.insert(a); }

    std::string new_list;
    for (const auto& c : cmd_set) { if (!new_list.empty()) new_list += ","; new_list += c; }
    db_set_str(ctx, api, "cmd_list", new_list);

    api->c_reset(ctx);
    emit_static(ctx, api, "SUCCESS");
    api->c_fmt(ctx, " RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD0\xB0(\xD1\x8B) ", KOTO_ENT_BOLD);
    std::string al_str;
    for (size_t i = 0; i < aliases.size(); ++i) { if (i > 0) al_str += ", "; al_str += aliases[i]; }
    api->c_fmt(ctx, al_str.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, " \xD1\x83\xD1\x81\xD0\xBF\xD0\xB5\xD1\x88\xD0\xBD\xD0\xBE \xD0\xB4\xD0\xBE\xD0\xB1\xD0\xB0\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xB0(\xD1\x8B)! ", KOTO_ENT_BOLD);
    emit_emoji_part(ctx, api, emoji_id, emoji_fb);
    api->c_edit(ctx, 0);
}

// PLACEHOLDER_DELRP

// ── 3. Команда .delrp <алиас|all|prem|simple> ─────────────────────────────────
static void cmd_delrp(koto_ctx* ctx, const koto_api* api) {
    long long sender = api->sender_id(ctx);
    if (!is_rp_creator(ctx, api, sender)) {
        api->c_reset(ctx);
        emit_static(ctx, api, "ERROR");
        api->c_fmt(ctx, " \xD0\xA3 \xD0\xB2\xD0\xB0\xD1\x81 \xD0\xBD\xD0\xB5\xD1\x82 \xD0\xBF\xD1\x80\xD0\xB0\xD0\xB2 \xD1\x81\xD0\xBE\xD0\xB7\xD0\xB4\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8F RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    const char* raw_args = api->cmd_args(ctx);
    std::string arg = raw_args ? to_lower(trim(raw_args)) : "";
    if (arg.empty()) {
        api->c_reset(ctx);
        emit_static(ctx, api, "ERROR");
        api->c_fmt(ctx, " \xD0\xA3\xD0\xBA\xD0\xB0\xD0\xB6\xD0\xB8\xD1\x82\xD0\xB5, \xD1\x87\xD1\x82\xD0\xBE \xD1\x83\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xB8\xD1\x82\xD1\x8C: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".delrp <\xD0\xB0\xD0\xBB\xD0\xB8\xD0\xB0\xD1\x81|all|prem|simple>", KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    std::string cmd_list = db_get_str(ctx, api, "cmd_list");
    auto cur_cmds = split(cmd_list, ',');

    if (arg == "all" || arg == "prem" || arg == "simple") {
        std::vector<std::string> to_del, remaining;
        for (const auto& c : cur_cmds) {
            bool match;
            if (arg == "all") match = true;
            else {
                RpCmd rc = parse_rp_cmd(db_get_str(ctx, api, "cmd:" + c));
                match = (arg == "prem") ? (rc.emoji_id != 0) : (rc.emoji_id == 0);
            }
            if (match) to_del.push_back(c); else remaining.push_back(c);
        }

        api->c_reset(ctx);
        if (to_del.empty()) {
            emit_static(ctx, api, "INFO");
            if (arg == "all") api->c_fmt(ctx, " \xD0\xA1\xD0\xBF\xD0\xB8\xD1\x81\xD0\xBE\xD0\xBA RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4 \xD0\xB8 \xD1\x82\xD0\xB0\xD0\xBA \xD0\xBF\xD1\x83\xD1\x81\xD1\x82.", KOTO_ENT_BOLD);
            else if (arg == "prem") api->c_fmt(ctx, " \xD0\x9D\xD0\xB5\xD1\x82 \xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4 \xD1\x81 \xD0\xBF\xD1\x80\xD0\xB5\xD0\xBC\xD0\xB8\xD1\x83\xD0\xBC-\xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8.", KOTO_ENT_BOLD);
            else api->c_fmt(ctx, " \xD0\x9D\xD0\xB5\xD1\x82 \xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4 \xD0\xB1\xD0\xB5\xD0\xB7 \xD0\xBF\xD1\x80\xD0\xB5\xD0\xBC\xD0\xB8\xD1\x83\xD0\xBC-\xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8.", KOTO_ENT_BOLD);
            api->c_edit(ctx, 0);
            return;
        }
        for (const auto& c : to_del) db_del(ctx, api, "cmd:" + c);
        std::string new_list;
        for (const auto& c : remaining) { if (!new_list.empty()) new_list += ","; new_list += c; }
        if (new_list.empty()) db_del(ctx, api, "cmd_list"); else db_set_str(ctx, api, "cmd_list", new_list);

        emit_static(ctx, api, "TRASH");
        std::string msg = " \xD0\xA3\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xBE " + std::to_string(to_del.size()) + " RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4!";
        api->c_fmt(ctx, msg.c_str(), KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    if (arg.front() == '.') arg = arg.substr(1);
    bool found = false;
    std::vector<std::string> remaining;
    for (const auto& c : cur_cmds) {
        if (c == arg) { db_del(ctx, api, "cmd:" + c); found = true; }
        else remaining.push_back(c);
    }
    std::string new_list;
    for (const auto& c : remaining) { if (!new_list.empty()) new_list += ","; new_list += c; }
    if (new_list.empty()) db_del(ctx, api, "cmd_list"); else db_set_str(ctx, api, "cmd_list", new_list);

    api->c_reset(ctx);
    if (found) {
        emit_static(ctx, api, "TRASH");
        api->c_fmt(ctx, " RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD0\xB0 ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, arg.c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, " \xD1\x83\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xB0!", KOTO_ENT_BOLD);
    } else {
        emit_static(ctx, api, "ERROR");
        api->c_fmt(ctx, " \xD0\x9A\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD0\xB0 \xD0\xBD\xD0\xB5 \xD0\xBD\xD0\xB0\xD0\xB9\xD0\xB4\xD0\xB5\xD0\xBD\xD0\xB0.", KOTO_ENT_BOLD);
    }
    api->c_edit(ctx, 0);
}

// PLACEHOLDER_RPLIST

// ── 4. Команда .rplist ────────────────────────────────────────────────────────
static void cmd_rplist(koto_ctx* ctx, const koto_api* api) {
    if (!is_rp_creator(ctx, api, api->sender_id(ctx))) return;
    cleanup_old_defaults_if_needed(ctx, api);
    std::string cmd_list = db_get_str(ctx, api, "cmd_list");
    auto cur_cmds = split(cmd_list, ',');

    if (cur_cmds.empty()) {
        api->c_reset(ctx);
        emit_static(ctx, api, "INFO");
        api->c_fmt(ctx, " \xD0\xA1\xD0\xBF\xD0\xB8\xD1\x81\xD0\xBE\xD0\xBA RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4 \xD0\xBF\xD1\x83\xD1\x81\xD1\x82!", KOTO_ENT_BOLD);
        api->c_text(ctx, "\n\xD0\x94\xD0\xBE\xD0\xB1\xD0\xB0\xD0\xB2\xD1\x8C\xD1\x82\xD0\xB5 \xD1\x87\xD0\xB5\xD1\x80\xD0\xB5\xD0\xB7: ");
        api->c_fmt(ctx, ".addrp <\xD0\xB0\xD0\xBB\xD0\xB8\xD0\xB0\xD1\x81\xD1\x8B>|<\xD0\xB4\xD0\xB5\xD0\xB9\xD1\x81\xD1\x82\xD0\xB2\xD0\xB8\xD0\xB5>|<\xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8>", KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    // Группировка по действию (с эмодзи первого алиаса группы).
    struct Grp { std::vector<std::string> aliases; long long emoji_id; std::string fallback; bool has_emoji; };
    std::map<std::string, Grp> groups;
    for (const auto& c : cur_cmds) {
        RpCmd rc = parse_rp_cmd(db_get_str(ctx, api, "cmd:" + c));
        if (rc.action.empty()) continue;
        auto& g = groups[rc.action];
        g.aliases.push_back(c);
        if (!g.has_emoji) { g.emoji_id = rc.emoji_id; g.fallback = rc.fallback; g.has_emoji = true; }
    }

    api->c_reset(ctx);
    emit_static(ctx, api, "LIST");
    api->c_fmt(ctx, " \xD0\x94\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF\xD0\xBD\xD1\x8B\xD0\xB5 RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD1\x8B:", KOTO_ENT_BOLD);
    api->c_text(ctx, "\n\n");

    for (auto& [act, g] : groups) {
        api->c_text(ctx, "\xE2\x80\xA2 ");
        std::sort(g.aliases.begin(), g.aliases.end());
        std::string al_str;
        for (size_t i = 0; i < g.aliases.size(); ++i) { if (i > 0) al_str += ", "; al_str += g.aliases[i]; }
        api->c_fmt(ctx, al_str.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, " \xE2\x80\x94 ");
        api->c_fmt(ctx, act.c_str(), KOTO_ENT_BOLD);
        if (g.emoji_id != 0 || !g.fallback.empty()) {
            api->c_text(ctx, " ");
            emit_emoji_part(ctx, api, g.emoji_id, g.fallback);
        }
        api->c_text(ctx, "\n");
    }
    api->c_edit(ctx, 0);
}

// PLACEHOLDER_NICKS

// ── 5. Команда .setrpnick [-g] [@user] <ник> ──────────────────────────────────
static void cmd_setrpnick(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";
    if (args.empty()) {
        api->c_reset(ctx);
        emit_static(ctx, api, "INFO");
        api->c_fmt(ctx, " \xD0\x98\xD1\x81\xD0\xBF\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB5:", KOTO_ENT_BOLD);
        api->c_text(ctx, "\n");
        api->c_fmt(ctx, ".setrpnick [-g] [@user] <\xD0\xBD\xD0\xB8\xD0\xBA>", KOTO_ENT_CODE);
        api->c_text(ctx, "\n\xE2\x80\xA2 -g \xE2\x80\x94 \xD0\xB3\xD0\xBB\xD0\xBE\xD0\xB1\xD0\xB0\xD0\xBB\xD1\x8C\xD0\xBD\xD0\xBE (\xD0\xB4\xD0\xBB\xD1\x8F \xD0\xB2\xD1\x81\xD0\xB5\xD1\x85 \xD1\x87\xD0\xB0\xD1\x82\xD0\xBE\xD0\xB2)");
        api->c_edit(ctx, 0);
        return;
    }

    auto tokens = split(args, ' ');
    bool is_global = false;
    long long target_uid = api->reply_sender_id(ctx);
    std::vector<std::string> rest_tokens;

    for (const auto& t : tokens) {
        if (t == "-g" || t == "-global" || t == "--g") is_global = true;
        else if (!t.empty() && t.front() == '@') { long long r = api->resolve(ctx, t.substr(1).c_str()); if (r != 0) target_uid = r; }
        else if (!t.empty() && isdigit((unsigned char)t.front()) && target_uid == 0 && t.size() > 4) {
            try { target_uid = std::stoll(t); } catch (...) { rest_tokens.push_back(t); }
        } else rest_tokens.push_back(t);
    }

    if (target_uid == 0) target_uid = api->sender_id(ctx);

    std::string new_nick;
    for (size_t i = 0; i < rest_tokens.size(); ++i) { if (i > 0) new_nick += " "; new_nick += rest_tokens[i]; }
    new_nick = trim(new_nick);

    if (new_nick.empty()) {
        api->c_reset(ctx);
        emit_static(ctx, api, "ERROR");
        api->c_fmt(ctx, " \xD0\x92\xD1\x8B \xD0\xBD\xD0\xB5 \xD1\x83\xD0\xBA\xD0\xB0\xD0\xB7\xD0\xB0\xD0\xBB\xD0\xB8 \xD0\xBD\xD0\xB8\xD0\xBA\xD0\xBD\xD0\xB5\xD0\xB9\xD0\xBC.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    long long cid = is_global ? 0 : api->chat_id(ctx);
    db_set_str(ctx, api, "nick:" + std::to_string(cid) + ":" + std::to_string(target_uid), new_nick);

    api->c_reset(ctx);
    emit_static(ctx, api, "SUCCESS");
    api->c_fmt(ctx, is_global ? " \xD0\x93\xD0\xBB\xD0\xBE\xD0\xB1\xD0\xB0\xD0\xBB\xD1\x8C\xD0\xBD\xD1\x8B\xD0\xB9 RP-\xD0\xBD\xD0\xB8\xD0\xBA \xD1\x83\xD1\x81\xD1\x82\xD0\xB0\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD: "
                                : " RP-\xD0\xBD\xD0\xB8\xD0\xBA \xD0\xB4\xD0\xBB\xD1\x8F \xD1\x8D\xD1\x82\xD0\xBE\xD0\xB3\xD0\xBE \xD1\x87\xD0\xB0\xD1\x82\xD0\xB0 \xD1\x83\xD1\x81\xD1\x82\xD0\xB0\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, new_nick.c_str(), KOTO_ENT_CODE);
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
        if (t == "-g" || t == "-global" || t == "--g") is_global = true;
        else if (!t.empty() && t.front() == '@') { long long r = api->resolve(ctx, t.substr(1).c_str()); if (r != 0) target_uid = r; }
    }

    if (target_uid == 0) target_uid = api->sender_id(ctx);
    long long cid = is_global ? 0 : api->chat_id(ctx);

    api->c_reset(ctx);
    if (is_global) {
        db_del(ctx, api, "nick:0:" + std::to_string(target_uid));
        emit_static(ctx, api, "TRASH");
        api->c_fmt(ctx, " \xD0\x93\xD0\xBB\xD0\xBE\xD0\xB1\xD0\xB0\xD0\xBB\xD1\x8C\xD0\xBD\xD1\x8B\xD0\xB9 RP-\xD0\xBD\xD0\xB8\xD0\xBA \xD1\x83\xD0\xB4\xD0\xB0\xD0\xBB\xD1\x91\xD0\xBD.", KOTO_ENT_BOLD);
    } else {
        // 'none' отключает действие глобального ника в этом чате (как в питоне).
        db_set_str(ctx, api, "nick:" + std::to_string(cid) + ":" + std::to_string(target_uid), "none");
        emit_static(ctx, api, "SUCCESS");
        api->c_fmt(ctx, " \xD0\x9E\xD1\x82\xD0\xBE\xD0\xB1\xD1\x80\xD0\xB0\xD0\xB6\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5 RP-\xD0\xBD\xD0\xB8\xD0\xBA\xD0\xB0 \xD0\xB2 \xD1\x8D\xD1\x82\xD0\xBE\xD0\xBC \xD1\x87\xD0\xB0\xD1\x82\xD0\xB5 \xD0\xBE\xD1\x82\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87\xD0\xB5\xD0\xBD\xD0\xBE.", KOTO_ENT_BOLD);
    }
    api->c_edit(ctx, 0);
}

// ── 7. Команда .rpnick [@user] ────────────────────────────────────────────────
static void cmd_rpnick(koto_ctx* ctx, const koto_api* api) {
    long long target_uid = api->reply_sender_id(ctx);
    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";

    if (!args.empty() && args.front() == '@') { long long r = api->resolve(ctx, args.substr(1).c_str()); if (r != 0) target_uid = r; }
    if (target_uid == 0) target_uid = api->sender_id(ctx);

    long long cid = api->chat_id(ctx);
    std::string chat_nick = db_get_str(ctx, api, "nick:" + std::to_string(cid) + ":" + std::to_string(target_uid));
    std::string global_nick = db_get_str(ctx, api, "nick:0:" + std::to_string(target_uid));

    api->c_reset(ctx);
    emit_static(ctx, api, "NICK");
    api->c_fmt(ctx, " RP-\xD0\xBD\xD0\xB8\xD0\xBA\xD0\xB8:", KOTO_ENT_BOLD);
    api->c_text(ctx, "\n\n\xE2\x80\xA2 \xD0\x92 \xD1\x8D\xD1\x82\xD0\xBE\xD0\xBC \xD1\x87\xD0\xB0\xD1\x82\xD0\xB5: ");
    api->c_fmt(ctx, (!chat_nick.empty() && chat_nick != "none") ? chat_nick.c_str() : "\xD0\xBD\xD0\xB5 \xD1\x83\xD1\x81\xD1\x82\xD0\xB0\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD", KOTO_ENT_CODE);
    api->c_text(ctx, "\n\xE2\x80\xA2 \xD0\x93\xD0\xBB\xD0\xBE\xD0\xB1\xD0\xB0\xD0\xBB\xD1\x8C\xD0\xBD\xD1\x8B\xD0\xB9: ");
    api->c_fmt(ctx, (!global_nick.empty() && global_nick != "none") ? global_nick.c_str() : "\xD0\xBD\xD0\xB5 \xD1\x83\xD1\x81\xD1\x82\xD0\xB0\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD", KOTO_ENT_CODE);
    api->c_edit(ctx, 0);
}

// PLACEHOLDER_CREATORS

// ── 8. Команды создателей RP ──────────────────────────────────────────────────
static void cmd_addrpcreator(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    long long target_uid = api->reply_sender_id(ctx);
    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";
    if (!args.empty() && args.front() == '@') { long long r = api->resolve(ctx, args.substr(1).c_str()); if (r != 0) target_uid = r; }
    else if (!args.empty() && isdigit((unsigned char)args.front())) { try { target_uid = std::stoll(args); } catch (...) {} }

    if (target_uid == 0) {
        api->c_reset(ctx);
        emit_static(ctx, api, "ERROR");
        api->c_fmt(ctx, " \xD0\xA3\xD0\xBA\xD0\xB0\xD0\xB6\xD0\xB8\xD1\x82\xD0\xB5 \xD0\xBF\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8F (@username, ID \xD0\xB8\xD0\xBB\xD0\xB8 \xD1\x80\xD0\xB5\xD0\xBF\xD0\xBB\xD0\xB0\xD0\xB9).", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    db_set_str(ctx, api, "creator:" + std::to_string(target_uid), "1");
    std::string nm = get_rp_display_name(ctx, api, target_uid, api->chat_id(ctx));
    api->c_reset(ctx);
    emit_static(ctx, api, "CROWN");
    api->c_fmt(ctx, " \xD0\x9F\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8C ", KOTO_ENT_BOLD);
    api->c_url(ctx, nm.c_str(), ("tg://user?id=" + std::to_string(target_uid)).c_str());
    api->c_fmt(ctx, " \xD1\x82\xD0\xB5\xD0\xBF\xD0\xB5\xD1\x80\xD1\x8C \xD0\xBC\xD0\xBE\xD0\xB6\xD0\xB5\xD1\x82 \xD1\x81\xD0\xBE\xD0\xB7\xD0\xB4\xD0\xB0\xD0\xB2\xD0\xB0\xD1\x82\xD1\x8C RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD1\x8B!", KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

static void cmd_delrpcreator(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    long long target_uid = api->reply_sender_id(ctx);
    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";
    if (!args.empty() && args.front() == '@') { long long r = api->resolve(ctx, args.substr(1).c_str()); if (r != 0) target_uid = r; }
    else if (!args.empty() && isdigit((unsigned char)args.front())) { try { target_uid = std::stoll(args); } catch (...) {} }

    if (target_uid == 0) {
        api->c_reset(ctx);
        emit_static(ctx, api, "ERROR");
        api->c_fmt(ctx, " \xD0\xA3\xD0\xBA\xD0\xB0\xD0\xB6\xD0\xB8\xD1\x82\xD0\xB5 \xD0\xBF\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8F (@username, ID \xD0\xB8\xD0\xBB\xD0\xB8 \xD1\x80\xD0\xB5\xD0\xBF\xD0\xBB\xD0\xB0\xD0\xB9).", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    db_del(ctx, api, "creator:" + std::to_string(target_uid));
    std::string nm = get_rp_display_name(ctx, api, target_uid, api->chat_id(ctx));
    api->c_reset(ctx);
    emit_static(ctx, api, "TRASH");
    api->c_fmt(ctx, " \xD0\x9F\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8C ", KOTO_ENT_BOLD);
    api->c_url(ctx, nm.c_str(), ("tg://user?id=" + std::to_string(target_uid)).c_str());
    api->c_fmt(ctx, " \xD0\xB1\xD0\xBE\xD0\xBB\xD1\x8C\xD1\x88\xD0\xB5 \xD0\xBD\xD0\xB5 \xD0\xBC\xD0\xBE\xD0\xB6\xD0\xB5\xD1\x82 \xD1\x81\xD0\xBE\xD0\xB7\xD0\xB4\xD0\xB0\xD0\xB2\xD0\xB0\xD1\x82\xD1\x8C RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD1\x8B.", KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

static void cmd_listrpcreators(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    char keys_buf[4096] = {0};
    int n = api->db_keys(ctx, keys_buf, sizeof(keys_buf) - 1);

    api->c_reset(ctx);
    emit_static(ctx, api, "CROWN");
    api->c_fmt(ctx, " \xD0\xA1\xD0\xBE\xD0\xB7\xD0\xB4\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD0\xB8 RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4:", KOTO_ENT_BOLD);
    api->c_text(ctx, "\n\n");

    int count = 0;
    if (n > 0) {
        std::stringstream ss(keys_buf);
        std::string k;
        while (std::getline(ss, k, '\n')) {
            if (k.rfind("creator:", 0) == 0) {
                std::string uid_str = k.substr(8);
                long long uid = 0; try { uid = std::stoll(uid_str); } catch (...) {}
                std::string nm = get_rp_display_name(ctx, api, uid, api->chat_id(ctx));
                api->c_text(ctx, "\xE2\x80\xA2 ");
                api->c_url(ctx, nm.c_str(), ("tg://user?id=" + uid_str).c_str());
                api->c_text(ctx, "\n");
                count++;
            }
        }
    }
    if (count == 0) api->c_text(ctx, "\xD0\xA1\xD0\xBF\xD0\xB8\xD1\x81\xD0\xBE\xD0\xBA \xD0\xBF\xD1\x83\xD1\x81\xD1\x82. \xD0\xA2\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xBA\xD0\xBE \xD0\xB2\xD0\xBB\xD0\xB0\xD0\xB4\xD0\xB5\xD0\xBB\xD0\xB5\xD1\x86 \xD0\xB8 \xD0\xB4\xD0\xBE\xD0\xB2\xD0\xB5\xD1\x80\xD0\xB5\xD0\xBD\xD0\xBD\xD1\x8B\xD0\xB5 (TRUSTED).");
    api->c_edit(ctx, 0);
}

// PLACEHOLDER_EMOJI_CMDS

// ── 9. .setrpemoji <ключ> <эмодзи/ID> [| fallback] ────────────────────────────
static void cmd_setrpemoji(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";

    std::string before_pipe = args, fallback_char = "";
    size_t pp = args.find('|');
    if (pp != std::string::npos) {
        before_pipe = trim(args.substr(0, pp));
        std::string fb = trim(args.substr(pp + 1));
        if (!fb.empty()) fallback_char = fb; // первый «символ» — берём как есть (может быть мультибайт)
    }

    auto tok = split(before_pipe, ' ');
    if (tok.empty()) {
        api->c_reset(ctx);
        emit_static(ctx, api, "ERROR");
        api->c_fmt(ctx, " \xD0\x9D\xD0\xB5\xD0\xB2\xD0\xB5\xD1\x80\xD0\xBD\xD1\x8B\xD0\xB9 \xD1\x84\xD0\xBE\xD1\x80\xD0\xBC\xD0\xB0\xD1\x82. \xD0\x9F\xD1\x80\xD0\xB8\xD0\xBC\xD0\xB5\xD1\x80: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".setrpemoji SUCCESS <\xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8/ID> | \xE2\x9C\x85", KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }
    std::string key = tok[0];
    std::string K = key; for (char& c : K) c = (char)std::toupper((unsigned char)c);

    bool known = false;
    for (int i = 0; i < STATIC_EMOJI_COUNT; ++i) if (K == STATIC_EMOJIS[i].key) { known = true; break; }
    if (!known) {
        api->c_reset(ctx);
        emit_static(ctx, api, "ERROR");
        api->c_fmt(ctx, " \xD0\x9D\xD0\xB5\xD0\xB8\xD0\xB7\xD0\xB2\xD0\xB5\xD1\x81\xD1\x82\xD0\xBD\xD1\x8B\xD0\xB9 \xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87. \xD0\x94\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF\xD0\xBD\xD1\x8B\xD0\xB5: ", KOTO_ENT_BOLD);
        std::string all; for (int i = 0; i < STATIC_EMOJI_COUNT; ++i) { if (i) all += ", "; all += STATIC_EMOJIS[i].key; }
        api->c_fmt(ctx, all.c_str(), KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    long long emoji_id = find_last_custom_emoji(ctx, api);
    std::string value_tok = (tok.size() > 1) ? tok[1] : "";
    if (emoji_id == 0 && !value_tok.empty()) {
        bool all_digit = true;
        for (char c : value_tok) if (!isdigit((unsigned char)c)) { all_digit = false; break; }
        if (all_digit) { try { emoji_id = std::stoll(value_tok); } catch (...) {} }
        else if (fallback_char.empty()) fallback_char = value_tok;
    }
    if (fallback_char.empty()) { long long d; get_static_emoji(ctx, api, K, d, fallback_char); }

    db_set_str(ctx, api, "semoji:" + K, std::to_string(emoji_id) + "|" + fallback_char);

    api->c_reset(ctx);
    emit_static(ctx, api, "SUCCESS");
    api->c_fmt(ctx, " \xD0\xA1\xD1\x82\xD0\xB0\xD1\x82\xD0\xB8\xD1\x87\xD0\xBD\xD1\x8B\xD0\xB9 RP-\xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8 \xD0\xB4\xD0\xBB\xD1\x8F ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, K.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, " \xD1\x83\xD1\x81\xD1\x82\xD0\xB0\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD! ", KOTO_ENT_BOLD);
    emit_emoji_part(ctx, api, emoji_id, fallback_char);
    api->c_edit(ctx, 0);
}

// ── 10. .delrpemoji <ключ> ────────────────────────────────────────────────────
static void cmd_delrpemoji(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    const char* raw_args = api->cmd_args(ctx);
    std::string key = raw_args ? trim(raw_args) : "";
    std::string K = key; for (char& c : K) c = (char)std::toupper((unsigned char)c);

    api->c_reset(ctx);
    if (K.empty()) {
        emit_static(ctx, api, "ERROR");
        api->c_fmt(ctx, " \xD0\xA3\xD0\xBA\xD0\xB0\xD0\xB6\xD0\xB8\xD1\x82\xD0\xB5 \xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87 \xD0\xB4\xD0\xBB\xD1\x8F \xD1\x81\xD0\xB1\xD1\x80\xD0\xBE\xD1\x81\xD0\xB0 (\xD0\xBD\xD0\xB0\xD0\xBF\xD1\x80. ERROR).", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }
    if (db_has(ctx, api, "semoji:" + K)) {
        db_del(ctx, api, "semoji:" + K);
        emit_static(ctx, api, "TRASH");
        api->c_fmt(ctx, " \xD0\x9A\xD0\xB0\xD1\x81\xD1\x82\xD0\xBE\xD0\xBC\xD0\xBD\xD1\x8B\xD0\xB9 RP-\xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8 \xD0\xB4\xD0\xBB\xD1\x8F ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, K.c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, " \xD1\x81\xD0\xB1\xD1\x80\xD0\xBE\xD1\x88\xD0\xB5\xD0\xBD \xD0\xB4\xD0\xBE \xD0\xB4\xD0\xB5\xD1\x84\xD0\xBE\xD0\xBB\xD1\x82\xD0\xBD\xD0\xBE\xD0\xB3\xD0\xBE.", KOTO_ENT_BOLD);
    } else {
        emit_static(ctx, api, "INFO");
        api->c_fmt(ctx, " RP-\xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8 \xD0\xB4\xD0\xBB\xD1\x8F ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, K.c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, " \xD0\xBD\xD0\xB5 \xD0\xB1\xD1\x8B\xD0\xBB \xD0\xBA\xD0\xB0\xD1\x81\xD1\x82\xD0\xBE\xD0\xBC\xD0\xB8\xD0\xB7\xD0\xB8\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBD.", KOTO_ENT_BOLD);
    }
    api->c_edit(ctx, 0);
}

// ── 11. .rpemojis ─────────────────────────────────────────────────────────────
static void cmd_rpemojis(koto_ctx* ctx, const koto_api* api) {
    if (api->user_level(ctx) < KOTO_LEVEL_TRUSTED) return;

    api->c_reset(ctx);
    emit_static(ctx, api, "SETTINGS");
    api->c_fmt(ctx, " \xD0\xA1\xD1\x82\xD0\xB0\xD1\x82\xD0\xB8\xD1\x87\xD0\xB5\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB5 RP-\xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8", KOTO_ENT_BOLD);
    api->c_text(ctx, "\n(\xD0\xBA\xD0\xB0\xD1\x81\xD1\x82\xD0\xBE\xD0\xBC\xD0\xBD\xD1\x8B\xD0\xB5 \xD0\xB8\xD0\xB7 \xD0\x91\xD0\x94 \xD0\xBF\xD0\xB5\xD1\x80\xD0\xB5\xD0\xBF\xD0\xB8\xD1\x81\xD1\x8B\xD0\xB2\xD0\xB0\xD1\x8E\xD1\x82 \xD0\xB4\xD0\xB5\xD1\x84\xD0\xBE\xD0\xBB\xD1\x82)\n\n");

    for (int i = 0; i < STATIC_EMOJI_COUNT; ++i) {
        const char* key = STATIC_EMOJIS[i].key;
        long long id; std::string fb; get_static_emoji(ctx, api, key, id, fb);
        bool custom = db_has(ctx, api, std::string("semoji:") + key);
        api->c_text(ctx, "\xE2\x80\xA2 ");
        emit_emoji_part(ctx, api, id, fb);
        std::string label = std::string(" ") + key + (custom ? " (\xD0\xBA\xD0\xB0\xD1\x81\xD1\x82\xD0\xBE\xD0\xBC\xD0\xBD\xD1\x8B\xD0\xB9)" : "");
        api->c_fmt(ctx, label.c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, ": ");
        if (id != 0) api->c_fmt(ctx, std::to_string(id).c_str(), KOTO_ENT_CODE);
        else api->c_fmt(ctx, "ID \xD0\xBD\xD0\xB5 \xD0\xB7\xD0\xB0\xD0\xB4\xD0\xB0\xD0\xBD", KOTO_ENT_ITALIC);
        api->c_text(ctx, "\n");
    }
    api->c_edit(ctx, 0);
}

// ── Главный watcher: исполнение RP-экшна по динамическому алиасу ──────────────
// Формат (как generic_rp_handler): {эмодзи} | {отправитель} {действие} {цель}
//                                   [\n\n {COMMENT-эмодзи} {комментарий}]
static int rp_watch(koto_ctx* ctx, const koto_api* api) {
    const char* raw = api->msg_text(ctx);
    if (!raw || !*raw) return 0;
    std::string text = trim(raw);
    if (text.empty()) return 0;

    // Префикс опционален (как в питоне: is_prefixed ? strip : whole).
    char pbuf[32] = {0};
    int pn = api->setting_get(ctx, "prefix", pbuf, sizeof(pbuf) - 1);
    std::string prefix = (pn > 0) ? std::string(pbuf) : std::string(".");
    std::string body = text;
    if (!prefix.empty() && body.rfind(prefix, 0) == 0) body = body.substr(prefix.size());
    body = trim(body);
    if (body.empty()) return 0;

    // Первое слово — команда, остаток — аргументы.
    std::string command, rest;
    {
        size_t sp = body.find_first_of(" \t\n");
        if (sp == std::string::npos) { command = body; }
        else { command = body.substr(0, sp); rest = trim(body.substr(sp + 1)); }
    }
    command = to_lower(command);
    if (command.empty()) return 0;

    // Это RP-алиас? (быстрая проверка по cmd_list)
    std::string cmd_list = db_get_str(ctx, api, "cmd_list");
    bool is_rp = false;
    for (const auto& c : split(cmd_list, ',')) if (c == command) { is_rp = true; break; }
    if (!is_rp) return 0;

    std::string stored = db_get_str(ctx, api, "cmd:" + command);
    if (stored.empty()) return 0;
    RpCmd rc = parse_rp_cmd(stored);
    if (rc.action.empty()) return 0;

    long long cid = api->chat_id(ctx);
    long long sender = api->sender_id(ctx);
    long long me = api->get_me(ctx);
    bool out = api->is_outgoing(ctx) != 0;
    bool is_private = (cid > 0);

    // RP должно быть включено в этом чате.
    if (!check_rp_enabled_in_chat(ctx, api, cid)) return 0;

    // Доступ: в группах не-TRUSTED нужен публичный доступ или индивидуальный.
    if (!is_private && api->user_level(ctx) < KOTO_LEVEL_TRUSTED) {
        bool pub = db_has(ctx, api, "public:" + std::to_string(cid));
        bool personal = db_has(ctx, api, "access:" + std::to_string(cid) + ":" + std::to_string(sender));
        if (!pub && !personal) return 0;
    }

    // Определение цели и комментария.
    long long target_uid = 0;
    std::string comment;
    long long reply_uid = api->reply_sender_id(ctx);
    if (reply_uid != 0) {
        target_uid = reply_uid;
        comment = rest;
    } else if (!rest.empty()) {
        std::string first = rest, remainder;
        size_t sp = rest.find_first_of(" \t\n");
        if (sp != std::string::npos) { first = rest.substr(0, sp); remainder = trim(rest.substr(sp + 1)); }
        if (!first.empty() && first.front() == '@') {
            long long r = api->resolve(ctx, first.substr(1).c_str());
            if (r != 0) { target_uid = r; comment = remainder; }
            else comment = rest;
        } else {
            bool all_digit = !first.empty();
            for (char c : first) if (!isdigit((unsigned char)c)) { all_digit = false; break; }
            if (all_digit && first.size() > 4) { try { target_uid = std::stoll(first); } catch (...) {} comment = remainder; }
            else comment = rest;
        }
    }
    // В ЛС без явной цели: исходящее → собеседник (cid), входящее → я.
    if (target_uid == 0 && is_private) target_uid = out ? cid : me;

    // Сборка сообщения.
    api->c_reset(ctx);
    emit_emoji_part(ctx, api, rc.emoji_id, rc.fallback);
    api->c_text(ctx, " | ");
    std::string sender_name = get_rp_display_name(ctx, api, sender, cid);
    api->c_url(ctx, sender_name.c_str(), ("tg://user?id=" + std::to_string(sender)).c_str());
    api->c_text(ctx, " ");
    api->c_fmt(ctx, rc.action.c_str(), KOTO_ENT_BOLD);

    if (target_uid == 0) {
        api->c_fmt(ctx, " \xD1\x81\xD0\xB0\xD0\xBC\xD0\xBE\xD0\xB3\xD0\xBE/\xD1\x81\xD0\xB0\xD0\xBC\xD1\x83 \xD1\x81\xD0\xB5\xD0\xB1\xD1\x8F", KOTO_ENT_BOLD); // самого/саму себя
    } else {
        api->c_text(ctx, " ");
        std::string target_name = get_rp_display_name(ctx, api, target_uid, cid);
        api->c_url(ctx, target_name.c_str(), ("tg://user?id=" + std::to_string(target_uid)).c_str());
    }

    if (!comment.empty()) {
        api->c_text(ctx, "\n\n");
        emit_static(ctx, api, "COMMENT");
        api->c_text(ctx, " ");
        api->c_fmt(ctx, comment.c_str(), KOTO_ENT_ITALIC);
    }

    if (out) {
        api->c_edit(ctx, KOTO_NO_LINK_PREVIEW);
    } else {
        api->c_send(ctx, cid, KOTO_NO_LINK_PREVIEW);
        if (sender == me) api->delete_msg(ctx);
    }
    return 1;
}

// ── Декларация команд ─────────────────────────────────────────────────────────
static const koto_command COMMANDS[] = {
    { "rp",             &cmd_rp,             KOTO_LEVEL_TRUSTED, "\xD0\xA3\xD0\xBF\xD1\x80\xD0\xB0\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5 RP (on/off/access).", "[on|off|access ...]" },
    { "addrp",          &cmd_addrp,          KOTO_LEVEL_ALL,     "\xD0\x94\xD0\xBE\xD0\xB1\xD0\xB0\xD0\xB2\xD0\xB8\xD1\x82\xD1\x8C RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD1\x83.", "<\xD0\xB0\xD0\xBB\xD0\xB8\xD0\xB0\xD1\x81\xD1\x8B>|<\xD0\xB4\xD0\xB5\xD0\xB9\xD1\x81\xD1\x82\xD0\xB2\xD0\xB8\xD0\xB5>|<\xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8>" },
    { "delrp",          &cmd_delrp,          KOTO_LEVEL_ALL,     "\xD0\xA3\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xB8\xD1\x82\xD1\x8C RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD1\x83.", "<\xD0\xB0\xD0\xBB\xD0\xB8\xD0\xB0\xD1\x81|all|prem|simple>" },
    { "rplist",         &cmd_rplist,         KOTO_LEVEL_ALL,     "\xD0\xA1\xD0\xBF\xD0\xB8\xD1\x81\xD0\xBE\xD0\xBA RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4.", NULL },
    { "setrpnick",      &cmd_setrpnick,      KOTO_LEVEL_TRUSTED, "\xD0\xA3\xD1\x81\xD1\x82\xD0\xB0\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xB8\xD1\x82\xD1\x8C RP-\xD0\xBD\xD0\xB8\xD0\xBA.", "[-g] [@user] <\xD0\xBD\xD0\xB8\xD0\xBA>" },
    { "delrpnick",      &cmd_delrpnick,      KOTO_LEVEL_TRUSTED, "\xD0\xA3\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xB8\xD1\x82\xD1\x8C RP-\xD0\xBD\xD0\xB8\xD0\xBA.", "[-g] [@user]" },
    { "rpnick",         &cmd_rpnick,         KOTO_LEVEL_TRUSTED, "\xD0\x9F\xD0\xBE\xD0\xBA\xD0\xB0\xD0\xB7\xD0\xB0\xD1\x82\xD1\x8C RP-\xD0\xBD\xD0\xB8\xD0\xBA\xD0\xB8.", "[@user]" },
    { "addrpcreator",   &cmd_addrpcreator,   KOTO_LEVEL_TRUSTED, "\xD0\x94\xD0\xB0\xD1\x82\xD1\x8C \xD0\xBF\xD1\x80\xD0\xB0\xD0\xB2\xD0\xBE \xD1\x81\xD0\xBE\xD0\xB7\xD0\xB4\xD0\xB0\xD0\xB2\xD0\xB0\xD1\x82\xD1\x8C RP.", "[@user]" },
    { "delrpcreator",   &cmd_delrpcreator,   KOTO_LEVEL_TRUSTED, "\xD0\x97\xD0\xB0\xD0\xB1\xD1\x80\xD0\xB0\xD1\x82\xD1\x8C \xD0\xBF\xD1\x80\xD0\xB0\xD0\xB2\xD0\xBE \xD1\x81\xD0\xBE\xD0\xB7\xD0\xB4\xD0\xB0\xD0\xB2\xD0\xB0\xD1\x82\xD1\x8C RP.", "[@user]" },
    { "listrpcreators", &cmd_listrpcreators, KOTO_LEVEL_TRUSTED, "\xD0\xA1\xD0\xBF\xD0\xB8\xD1\x81\xD0\xBE\xD0\xBA \xD1\x81\xD0\xBE\xD0\xB7\xD0\xB4\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD0\xB5\xD0\xB9 RP.", NULL },
    { "setrpemoji",     &cmd_setrpemoji,     KOTO_LEVEL_TRUSTED, "\xD0\x9A\xD0\xB0\xD1\x81\xD1\x82\xD0\xBE\xD0\xBC\xD0\xBD\xD1\x8B\xD0\xB9 \xD1\x81\xD1\x82\xD0\xB0\xD1\x82\xD0\xB8\xD1\x87\xD0\xBD\xD1\x8B\xD0\xB9 RP-\xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8.", "<\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87> <\xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8/ID>" },
    { "delrpemoji",     &cmd_delrpemoji,     KOTO_LEVEL_TRUSTED, "\xD0\xA1\xD0\xB1\xD1\x80\xD0\xBE\xD1\x81\xD0\xB8\xD1\x82\xD1\x8C \xD1\x81\xD1\x82\xD0\xB0\xD1\x82\xD0\xB8\xD1\x87\xD0\xBD\xD1\x8B\xD0\xB9 RP-\xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8.", "<\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87>" },
    { "rpemojis",       &cmd_rpemojis,       KOTO_LEVEL_TRUSTED, "\xD0\xA1\xD0\xBF\xD0\xB8\xD1\x81\xD0\xBE\xD0\xBA \xD1\x81\xD1\x82\xD0\xB0\xD1\x82\xD0\xB8\xD1\x87\xD0\xBD\xD1\x8B\xD1\x85 RP-\xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8.", NULL },
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "rp",
    "Role-Play: \xD0\xBA\xD0\xB0\xD1\x81\xD1\x82\xD0\xBE\xD0\xBC\xD0\xBD\xD1\x8B\xD0\xB5 RP-\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD1\x8B \xD1\x81 \xD0\xBF\xD1\x80\xD0\xB5\xD0\xBC\xD0\xB8\xD1\x83\xD0\xBC-\xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8, RP-\xD0\xBD\xD0\xB8\xD0\xBA\xD0\xB8, \xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF \xD0\xB8 \xD1\x81\xD0\xBE\xD0\xB7\xD0\xB4\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD0\xB8 (C++ ABI 3).",
    "3.0.0",
    12,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    &rp_watch, KOTO_WATCH_ALL,
    nullptr,
    nullptr, 0,
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/rp.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}






