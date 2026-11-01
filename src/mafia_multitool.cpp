// mafia_multitool.cpp — Мафия Мульти-Тул для Kotogram / KoteLoader (C++ ABI 3).
// Портирован из Python-модуля mafia_multitool.py (v1.2.6) с сохранением 1:1 логики:
// • Три модуля в одном: CombatMafia + TrueMafia + Baku + Koto + Mafioso
// • Поддержка мульти-аккаунтов (Main + Twins), заморозка твинков (.mfreeze)
// • Режим живых игроков (.mhuman) с имитацией пачек и задержек
// • Авто-голосование Combat (.mcombat, .mlast, .mprelast, .mvesh)
// • Регистрация (.mreg), скан ролей (.mrole), подтверждения линча (.mlike, .mdis)
// • Аналитика Baku (.mbaku) и KotoMafia (.mkgraf)
// Автор: Kote

#include "module_abi.h"
#include <string>
#include <vector>
#include <set>
#include <map>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <thread>
#include <random>

static const char* MODULE_NAME = "mafia_multitool";

static const char* COMBAT_BOT = "CombatMafiaSOBot";
static const char* TRUE_BOT   = "TrueMafiaBlackBot";
static const char* BAKU_BOT   = "MafiaBakuBlackBot";
static const char* BAKU_BOT2  = "MafiaBakuBlack1Bot";
static const char* KOTO_BOT   = "KoteMafiaBot";
static const char* OSO_BOT    = "MafiososBot";
static const char* OSO_BOT2   = "mafiozi";

// ── Структура аккаунта (Main + Twins) ─────────────────────────────────────────
struct AccountInfo {
    long long uid = 0;
    std::string name;
    bool is_main = false;
    bool online = false;
    bool frozen = false;
};

// ── Вспомогательные функции парсинга JSON ─────────────────────────────────────
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
            else { out += '\\'; out += next; }
        } else {
            out += s[i];
        }
    }
    return out;
}

static std::string json_get_string(const std::string& obj, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    size_t pos = obj.find(needle);
    if (pos == std::string::npos) return "";
    size_t val_start = pos + needle.size();
    size_t val_end = val_start;
    while (val_end < obj.size()) {
        if (obj[val_end] == '"' && obj[val_end - 1] != '\\') break;
        val_end++;
    }
    if (val_end < obj.size()) {
        return json_unescape(obj.substr(val_start, val_end - val_start));
    }
    return "";
}

static long long json_get_int64(const std::string& obj, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    size_t pos = obj.find(needle);
    if (pos == std::string::npos) return 0;
    size_t val_start = pos + needle.size();
    while (val_start < obj.size() && (obj[val_start] == ' ' || obj[val_start] == '\t')) val_start++;
    return std::atoll(obj.c_str() + val_start);
}

static bool json_get_bool(const std::string& obj, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    size_t pos = obj.find(needle);
    if (pos == std::string::npos) return false;
    size_t val_start = pos + needle.size();
    while (val_start < obj.size() && (obj[val_start] == ' ' || obj[val_start] == '\t')) val_start++;
    return obj.compare(val_start, 4, "true") == 0;
}

// ── Кнопка из истории или сообщения ───────────────────────────────────────────
struct MsgButton {
    std::string text;
    std::string data;
    std::string url;
    int row = 0;
    int col = 0;
};

// ── Сообщение из истории ──────────────────────────────────────────────────────
struct HistoryMessage {
    int id = 0;
    long long sender_id = 0;
    long long chat_id = 0;
    int date = 0;
    bool out = false;
    std::string text;
    std::vector<MsgButton> buttons;
};

static std::vector<MsgButton> parse_buttons_json(const std::string& bjson) {
    std::vector<MsgButton> res;
    size_t pos = 0;
    while ((pos = bjson.find('{', pos)) != std::string::npos) {
        size_t end_pos = bjson.find('}', pos);
        if (end_pos == std::string::npos) break;
        std::string obj = bjson.substr(pos, end_pos - pos + 1);
        MsgButton btn;
        btn.text = json_get_string(obj, "text");
        btn.data = json_get_string(obj, "data");
        btn.url = json_get_string(obj, "url");
        btn.row = (int)json_get_int64(obj, "row");
        btn.col = (int)json_get_int64(obj, "col");
        res.push_back(btn);
        pos = end_pos + 1;
    }
    return res;
}

static std::vector<HistoryMessage> parse_history_json(const std::string& hjson) {
    std::vector<HistoryMessage> res;
    size_t pos = 0;
    while ((pos = hjson.find("{\"id\":", pos)) != std::string::npos) {
        size_t end_pos = hjson.find("{\"id\":", pos + 6);
        std::string obj;
        if (end_pos != std::string::npos) {
            obj = hjson.substr(pos, end_pos - pos);
            pos = end_pos;
        } else {
            obj = hjson.substr(pos);
            pos = std::string::npos;
        }
        HistoryMessage m;
        m.id = (int)json_get_int64(obj, "id");
        m.sender_id = json_get_int64(obj, "sender_id");
        m.chat_id = json_get_int64(obj, "chat_id");
        m.date = (int)json_get_int64(obj, "date");
        m.out = json_get_bool(obj, "out");
        m.text = json_get_string(obj, "text");

        size_t bpos = obj.find("\"buttons\":[");
        if (bpos != std::string::npos) {
            size_t bend = obj.find(']', bpos);
            if (bend != std::string::npos) {
                m.buttons = parse_buttons_json(obj.substr(bpos + 10, bend - bpos - 9));
            }
        }
        res.push_back(m);
        if (pos == std::string::npos) break;
    }
    return res;
}

// ── Очистка и нормализация строк/ников ────────────────────────────────────────
static std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::string to_lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static char decode_math_cp(uint32_t cp) {
    static const struct { uint32_t start, end; char base; } r[] = {
        {0x1D400, 0x1D419, 'a'}, {0x1D41A, 0x1D433, 'a'}, // Bold
        {0x1D434, 0x1D44D, 'a'}, {0x1D44E, 0x1D467, 'a'}, // Italic
        {0x1D468, 0x1D481, 'a'}, {0x1D482, 0x1D49B, 'a'}, // Bold Italic
        {0x1D49C, 0x1D4B5, 'a'}, {0x1D4B6, 0x1D4CF, 'a'}, // Script
        {0x1D4D0, 0x1D4E9, 'a'}, {0x1D4EA, 0x1D503, 'a'}, // Bold Script
        {0x1D504, 0x1D51D, 'a'}, {0x1D51E, 0x1D537, 'a'}, // Fraktur
        {0x1D538, 0x1D551, 'a'}, {0x1D552, 0x1D56B, 'a'}, // Double-struck
        {0x1D56C, 0x1D585, 'a'}, {0x1D586, 0x1D59F, 'a'}, // Sans-serif
        {0x1D5A0, 0x1D5B9, 'a'}, {0x1D5BA, 0x1D5D3, 'a'}, // Sans-serif Bold
        {0x1D5D4, 0x1D5ED, 'a'}, {0x1D5EE, 0x1D607, 'a'}, // Sans-serif Italic
        {0x1D608, 0x1D621, 'a'}, {0x1D622, 0x1D63B, 'a'}, // Sans Bold Italic
        {0x1D63C, 0x1D655, 'a'}, {0x1D656, 0x1D66F, 'a'}, // Monospace
        {0x1D670, 0x1D689, 'a'}, {0x1D68A, 0x1D6A3, 'a'}, // Monospace
    };
    for (const auto& item : r) {
        if (cp >= item.start && cp <= item.end) {
            return (char)(item.base + (cp - item.start) % 26);
        }
    }
    static const struct { uint32_t start, end; } d[] = {
        {0x1D7CE, 0x1D7D7}, {0x1D7D8, 0x1D7E1}, {0x1D7E2, 0x1D7EB},
        {0x1D7EC, 0x1D7F5}, {0x1D7F6, 0x1D7FF}
    };
    for (const auto& item : d) {
        if (cp >= item.start && cp <= item.end) {
            return (char)('0' + (cp - item.start));
        }
    }
    return 0;
}

static std::string normalize_small_caps_and_unicode(const std::string& str) {
    std::string out;
    size_t i = 0;
    while (i < str.size()) {
        unsigned char c = (unsigned char)str[i];
        if (c < 0x80) {
            out += (char)std::tolower(c);
            i++;
            continue;
        }
        if (i + 3 < str.size() && (unsigned char)str[i] == 0xF0 && (unsigned char)str[i+1] == 0x9D) {
            uint32_t b3 = (unsigned char)str[i+2];
            uint32_t b4 = (unsigned char)str[i+3];
            uint32_t cp = ((0xF0 & 0x07) << 18) | ((0x9D & 0x3F) << 12) | ((b3 & 0x3F) << 6) | (b4 & 0x3F);
            char mch = decode_math_cp(cp);
            if (mch) {
                out += mch;
                i += 4;
                continue;
            }
        }
        if (i + 2 < str.size() && (unsigned char)str[i] == 0xE1 && (unsigned char)str[i+1] == 0xB4) {
            unsigned char b3 = (unsigned char)str[i+2];
            char repl = 0;
            switch (b3) {
                case 0x80: repl = 'a'; break;
                case 0x84: repl = 'c'; break;
                case 0x85: repl = 'd'; break;
                case 0x87: repl = 'e'; break;
                case 0x8A: repl = 'j'; break;
                case 0x8B: repl = 'k'; break;
                case 0x8D: repl = 'm'; break;
                case 0x8F: repl = 'o'; break;
                case 0x98: repl = 'p'; break;
                case 0x9B: repl = 't'; break;
                case 0x9C: repl = 'u'; break;
                case 0xA1: repl = 'w'; break;
                case 0xA2: repl = 'z'; break;
            }
            if (repl) { out += repl; i += 3; continue; }
        }
        if (i + 1 < str.size() && (unsigned char)str[i] == 0xCA) {
            unsigned char b2 = (unsigned char)str[i+1];
            char repl = 0;
            switch (b2) {
                case 0x99: repl = 'b'; break;
                case 0x9C: repl = 'h'; break;
                case 0x9F: repl = 'l'; break;
                case 0x80: repl = 'r'; break;
                case 0x93: repl = 's'; break;
                case 0x8F: repl = 'y'; break;
            }
            if (repl) { out += repl; i += 2; continue; }
        }
        if (i + 1 < str.size() && (unsigned char)str[i] == 0xC9) {
            unsigned char b2 = (unsigned char)str[i+1];
            char repl = 0;
            switch (b2) {
                case 0xA2: repl = 'g'; break;
                case 0xAA: repl = 'i'; break;
                case 0xB4: repl = 'n'; break;
            }
            if (repl) { out += repl; i += 2; continue; }
        }
        if (i + 1 < str.size() && (unsigned char)str[i] == 0xD0) {
            unsigned char b2 = (unsigned char)str[i+1];
            if (b2 >= 0x90 && b2 <= 0x9F) {
                out += (char)0xD0;
                out += (char)(b2 + 0x20);
                i += 2;
                continue;
            } else if (b2 >= 0xA0 && b2 <= 0xAF) {
                out += (char)0xD1;
                out += (char)(b2 - 0x20);
                i += 2;
                continue;
            } else if (b2 == 0x81) {
                out += (char)0xD1;
                out += (char)(b2);
                i += 2;
                continue;
            }
        }
        out += str[i];
        i++;
    }
    return out;
}

static std::vector<std::string> extract_core_tokens(const std::string& raw) {
    std::string s = normalize_small_caps_and_unicode(raw);
    std::vector<std::string> tokens;
    std::string cur;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        bool is_letter_or_digit = false;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            is_letter_or_digit = true;
            cur += (char)c;
        } else if (c >= 0xC0 && i + 1 < s.size()) {
            unsigned char c2 = (unsigned char)s[i+1];
            if ((c == 0xD0 && c2 >= 0xB0) || (c == 0xD1 && c2 <= 0x9F) || (c == 0xD1 && c2 == 0x91)) {
                is_letter_or_digit = true;
                cur += (char)c;
                cur += (char)c2;
                i++;
            }
        }
        if (!is_letter_or_digit) {
            if (!cur.empty()) {
                tokens.push_back(cur);
                cur.clear();
            }
        }
    }
    if (!cur.empty()) {
        tokens.push_back(cur);
    }
    std::vector<std::string> filtered;
    for (const auto& t : tokens) {
        if (t == "kt" || t == "кт" || t == "defbykote" || t == "bot" || t == "net" || t == "team") continue;
        if (t.size() < 2) continue;
        bool all_digits = true;
        for (char ch : t) if (ch < '0' || ch > '9') { all_digits = false; break; }
        if (all_digits) continue;
        filtered.push_back(t);
    }
    return filtered;
}

static bool is_same_player(const std::string& name1, const std::string& name2) {
    auto t1 = extract_core_tokens(name1);
    auto t2 = extract_core_tokens(name2);
    if (t1.empty() || t2.empty()) return false;
    for (const auto& w1 : t1) {
        for (const auto& w2 : t2) {
            if (w1 == w2) return true;
            if (w1.size() >= 4 && w2.find(w1) != std::string::npos) return true;
            if (w2.size() >= 4 && w1.find(w2) != std::string::npos) return true;
        }
    }
    return false;
}

static std::string normalize_nick(std::string nick) {
    std::string norm = normalize_small_caps_and_unicode(nick);
    std::string out;
    for (size_t i = 0; i < norm.size(); ++i) {
        unsigned char c = (unsigned char)norm[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ') {
            out += (char)c;
        } else if (c >= 0xC0) {
            out += (char)c;
        } else if (c >= 0x80) {
            out += (char)c;
        }
    }
    std::string res;
    bool sp = false;
    for (char c : out) {
        if (c == ' ') {
            if (!sp && !res.empty()) { res += ' '; sp = true; }
        } else {
            res += c;
            sp = false;
        }
    }
    return trim(res);
}

static std::string clean_nick_baku(std::string nick) {
    size_t link_pos = nick.find("](tg://user?id=");
    if (link_pos != std::string::npos) {
        size_t close_pos = nick.find(')', link_pos);
        if (close_pos != std::string::npos) nick.erase(link_pos, close_pos - link_pos + 1);
    }
    while (!nick.empty() && (nick.front() == '[' || nick.front() == ']')) nick.erase(nick.begin());
    while (!nick.empty() && (nick.back() == '[' || nick.back() == ']')) nick.pop_back();

    const std::string heart = "\xF0\x9F\xAB\x80";
    size_t hp = 0;
    while ((hp = nick.find(heart, hp)) != std::string::npos) nick.erase(hp, heart.size());

    bool changed = true;
    while (changed) {
        changed = false;
        size_t s = nick.find_first_not_of(" \t\r\n");
        if (s != std::string::npos && s > 0) { nick.erase(0, s); changed = true; }
        const std::string blue = "\xF0\x9F\x94\xB5";
        const std::string red  = "\xF0\x9F\x94\xB4";
        if (nick.rfind(blue, 0) == 0) { nick.erase(0, blue.size()); changed = true; }
        else if (nick.rfind(red, 0) == 0) { nick.erase(0, red.size()); changed = true; }
    }

    static const std::vector<std::string> u_spaces = {
        "\xC2\xA0", "\xE2\x80\x80", "\xE2\x80\x81", "\xE2\x80\x82", "\xE2\x80\x83",
        "\xE2\x80\x84", "\xE2\x80\x85", "\xE2\x80\x86", "\xE2\x80\x87", "\xE2\x80\x88",
        "\xE2\x80\x89", "\xE2\x80\x8A", "\xE2\x80\xAF", "\xE2\x81\x9F", "\xE3\x80\x80"
    };
    for (const auto& sp : u_spaces) {
        size_t pos = 0;
        while ((pos = nick.find(sp, pos)) != std::string::npos) {
            nick.replace(pos, sp.size(), " ");
            pos += 1;
        }
    }

    std::string result;
    bool in_space = false;
    for (char c : nick) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!in_space && !result.empty()) { result += ' '; in_space = true; }
        } else {
            result += c;
            in_space = false;
        }
    }
    if (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}

static std::string normalize_nick_true(std::string nick) {
    static const std::vector<std::string> emojis = {
        "🕵️‍♂️","🕵️‍♀️","🕵️","👮‍♂️","👮‍♀️","👮","🤵🏻","🤵‍♂️","🤵‍♀️","🤵","👿","🐧","🤐","🚑","💌","💣","🔫"
    };
    for (const auto& e : emojis) {
        size_t pos = 0;
        while ((pos = nick.find(e, pos)) != std::string::npos) nick.erase(pos, e.size());
    }
    size_t p = 0;
    while ((p = nick.find("||")) != std::string::npos) nick.erase(p, 2);
    while ((p = nick.find('|')) != std::string::npos) nick.erase(p, 1);
    return normalize_nick(nick);
}

static std::string normalize_baku_btn(std::string text) {
    text = clean_nick_baku(text);
    return to_lower(text);
}

static int baku_match_score(const std::string& target_norm, const std::string& btn_norm) {
    if (target_norm.empty() || btn_norm.empty()) return 0;
    std::stringstream ss(target_norm);
    std::string w;
    int score = 0;
    while (ss >> w) {
        if (btn_norm.find(w) != std::string::npos) score++;
    }
    return score;
}

// ── Загрузка настроек модуля ──────────────────────────────────────────────────
struct Settings {
    long long tourney_chat_id = 0;
    long long arena_id = 0;
    bool enabled_combat = false;
    bool enabled_true = false;
    int vote_delay_ms = 0;
    std::string combat_mode = "last"; // "last", "prelast", "name"
    std::string combat_target_name;
    std::string combat_direction;

    bool human_mode = false;
    int human_instant = 0;
    int human_window_sec = 10;
    int human_batch_min = 2;
    int human_batch_max = 4;

    std::set<std::string> frozen_twins;
};

static Settings load_settings(koto_ctx* ctx, const koto_api* api) {
    Settings s;
    s.tourney_chat_id = api->db_get_int(ctx, "tourney_chat_id", 0);
    s.arena_id        = api->db_get_int(ctx, "arena_id", 0);
    s.enabled_combat  = api->db_get_int(ctx, "enabled_combat", 0) != 0;
    s.enabled_true    = api->db_get_int(ctx, "enabled_true", 0) != 0;
    s.vote_delay_ms   = (int)api->db_get_int(ctx, "vote_delay_ms", 0);

    char buf[512];
    if (api->db_get(ctx, "combat_mode", buf, sizeof(buf)) > 0) s.combat_mode = buf;
    if (api->db_get(ctx, "combat_target_name", buf, sizeof(buf)) > 0) s.combat_target_name = buf;
    if (api->db_get(ctx, "combat_direction", buf, sizeof(buf)) > 0) s.combat_direction = buf;

    s.human_mode       = api->db_get_int(ctx, "human_mode", 0) != 0;
    s.human_instant    = (int)api->db_get_int(ctx, "human_instant", 0);
    s.human_window_sec = (int)api->db_get_int(ctx, "human_window_sec", 10);
    s.human_batch_min  = (int)api->db_get_int(ctx, "human_batch_min", 2);
    s.human_batch_max  = (int)api->db_get_int(ctx, "human_batch_max", 4);

    char fbuf[4096];
    int flen = api->db_get(ctx, "frozen_twins", fbuf, sizeof(fbuf));
    if (flen > 0) {
        std::stringstream ss(std::string(fbuf, flen));
        std::string token;
        while (std::getline(ss, token, ',')) {
            token = trim(token);
            if (!token.empty()) s.frozen_twins.insert(token);
        }
    }
    return s;
}

static void save_settings(koto_ctx* ctx, const koto_api* api, const Settings& s) {
    api->db_set_int(ctx, "tourney_chat_id", s.tourney_chat_id);
    api->db_set_int(ctx, "arena_id", s.arena_id);
    api->db_set_int(ctx, "enabled_combat", s.enabled_combat ? 1 : 0);
    api->db_set_int(ctx, "enabled_true", s.enabled_true ? 1 : 0);
    api->db_set_int(ctx, "vote_delay_ms", s.vote_delay_ms);

    api->db_set(ctx, "combat_mode", s.combat_mode.c_str());
    api->db_set(ctx, "combat_target_name", s.combat_target_name.c_str());
    api->db_set(ctx, "combat_direction", s.combat_direction.c_str());

    api->db_set_int(ctx, "human_mode", s.human_mode ? 1 : 0);
    api->db_set_int(ctx, "human_instant", s.human_instant);
    api->db_set_int(ctx, "human_window_sec", s.human_window_sec);
    api->db_set_int(ctx, "human_batch_min", s.human_batch_min);
    api->db_set_int(ctx, "human_batch_max", s.human_batch_max);

    std::string fstr;
    for (const auto& name : s.frozen_twins) {
        if (!fstr.empty()) fstr += ",";
        fstr += name;
    }
    api->db_set(ctx, "frozen_twins", fstr.c_str());
}

// ── Получение списка активных аккаунтов ───────────────────────────────────────
static std::vector<AccountInfo> get_all_accounts(koto_ctx* ctx, const koto_api* api, const Settings& s) {
    std::vector<AccountInfo> res;
    char tbuf[16384];
    int tlen = api->get_twins_json ? api->get_twins_json(ctx, tbuf, sizeof(tbuf)) : 0;
    if (tlen > 0) {
        std::string json(tbuf, tlen);
        size_t pos = 0;
        while ((pos = json.find('{', pos)) != std::string::npos) {
            size_t end_pos = json.find('}', pos);
            if (end_pos == std::string::npos) break;
            std::string obj = json.substr(pos, end_pos - pos + 1);
            AccountInfo acc;
            acc.uid = json_get_int64(obj, "uid");
            acc.name = json_get_string(obj, "name");
            acc.is_main = json_get_bool(obj, "is_main");
            acc.online = json_get_bool(obj, "online");
            acc.frozen = (s.frozen_twins.count(acc.name) > 0 || s.frozen_twins.count(std::to_string(acc.uid)) > 0);
            res.push_back(acc);
            pos = end_pos + 1;
        }
    }
    if (res.empty()) {
        AccountInfo main_acc;
        main_acc.uid = api->get_me(ctx);
        char me_name[256];
        if (api->get_me_name(ctx, me_name, sizeof(me_name)) > 0) main_acc.name = me_name;
        else main_acc.name = "Main";
        main_acc.is_main = true;
        main_acc.online = true;
        main_acc.frozen = (s.frozen_twins.count(main_acc.name) > 0);
        res.push_back(main_acc);
    }
    return res;
}

static std::vector<AccountInfo> get_active_accounts(koto_ctx* ctx, const koto_api* api, const Settings& s) {
    auto all = get_all_accounts(ctx, api, s);
    std::vector<AccountInfo> active;
    for (const auto& a : all) {
        if (!a.frozen && a.online) active.push_back(a);
    }
    return active;
}

// ── Проверка нахождения в лог-чате ───────────────────────────────────────────
static bool check_tourney(koto_ctx* ctx, const koto_api* api, const Settings& s) {
    long long cur = api->chat_id(ctx);
    // Если команда из терминала KoteLoader (cur == 0) или лог-чат не задан — разрешаем
    if (cur == 0 || s.tourney_chat_id == 0) return true;
    // Если лог-чат задан — разрешаем команды в лог-чате и в арене
    if (cur == s.tourney_chat_id || (s.arena_id != 0 && cur == s.arena_id)) return true;

    api->c_reset(ctx);
    api->c_text(ctx, "❌ Команда разрешена только в турнирном лог-чате или арене.");
    api->c_edit(ctx, 0);
    return false;
}

// ── Получение названия чата ──────────────────────────────────────────────────
static std::string get_chat_title(koto_ctx* ctx, const koto_api* api, long long cid) {
    if (cid == 0) return "(не задан)";
    char buf[4096];
    int len = api->chat_full_json ? api->chat_full_json(ctx, cid, buf, sizeof(buf)) : 0;
    if (len > 0) {
        std::string j(buf, len);
        std::string t = json_get_string(j, "title");
        if (!t.empty()) return t;
    }
    return "ID " + std::to_string(cid);
}

// ── Human scatter ─────────────────────────────────────────────────────────────
template<typename F>
static void human_scatter(const std::vector<AccountInfo>& accounts, const Settings& s, F&& task_fn) {
    if (accounts.empty()) return;
    if (!s.human_mode) {
        for (const auto& acc : accounts) task_fn(acc);
        return;
    }

    int instant = s.human_instant;
    size_t imm_count = std::min((size_t)std::max(0, instant), accounts.size());
    for (size_t i = 0; i < imm_count; ++i) {
        task_fn(accounts[i]);
    }
    if (imm_count >= accounts.size()) return;

    std::vector<AccountInfo> deferred(accounts.begin() + imm_count, accounts.end());
    int bmin = std::max(1, s.human_batch_min);
    int bmax = std::max(bmin, s.human_batch_max);
    double window = std::max(1.0, (double)s.human_window_sec);

    std::vector<std::vector<AccountInfo>> batches;
    std::mt19937 rng((unsigned)std::chrono::system_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<int> bdist(bmin, bmax);

    size_t idx = 0;
    while (idx < deferred.size()) {
        int sz = bdist(rng);
        size_t cur_end = std::min(idx + sz, deferred.size());
        batches.emplace_back(deferred.begin() + idx, deferred.begin() + cur_end);
        idx = cur_end;
    }

    size_t n = batches.size();
    double step = window / (double)n;
    for (size_t i = 0; i < n; ++i) {
        std::uniform_real_distribution<double> sdist(0.1, 0.9);
        double delay_sec = step * sdist(rng);
        if (delay_sec > 0.05) {
            std::this_thread::sleep_for(std::chrono::milliseconds((int)(delay_sec * 1000)));
        }
        for (const auto& acc : batches[i]) {
            task_fn(acc);
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
// КОМАНДЫ МОДУЛЯ
// ════════════════════════════════════════════════════════════════════════════

// ── .mset <tourney|arena> [id] ───────────────────────────────────────────────
static void cmd_mset(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    const char* args_ptr = api->cmd_args(ctx);
    std::string args = args_ptr ? trim(args_ptr) : "";
    std::stringstream ss(args);
    std::string param, id_str;
    ss >> param >> id_str;
    param = to_lower(param);

    if (param.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "Usage: .mset <параметр> [id]\n\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "• ");
        api->c_fmt(ctx, "tourney", KOTO_ENT_CODE);
        api->c_text(ctx, " — лог-чат (команды управления только отсюда)\n• ");
        api->c_fmt(ctx, "arena", KOTO_ENT_CODE);
        api->c_text(ctx, " — игровая арена для ботов (combat, baku, true, koto)");
        api->c_edit(ctx, 0);
        return;
    }

    long long target_chat = id_str.empty() ? api->chat_id(ctx) : std::atoll(id_str.c_str());
    if (param == "tourney") {
        s.tourney_chat_id = target_chat;
        save_settings(ctx, api, s);
        std::string title = get_chat_title(ctx, api, target_chat);
        api->c_reset(ctx);
        api->c_text(ctx, "✅ Лог-чат tourney установлен: ");
        api->c_fmt(ctx, title.c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, " (");
        api->c_fmt(ctx, std::to_string(target_chat).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, ")");
        api->c_edit(ctx, 0);
    } else if (param == "arena") {
        s.arena_id = target_chat;
        save_settings(ctx, api, s);
        std::string title = get_chat_title(ctx, api, target_chat);
        api->c_reset(ctx);
        api->c_text(ctx, "✅ Арена arena установлена: ");
        api->c_fmt(ctx, title.c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, " (");
        api->c_fmt(ctx, std::to_string(target_chat).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, ")");
        api->c_edit(ctx, 0);
    } else {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Неизвестный параметр. Используйте tourney или arena.");
        api->c_edit(ctx, 0);
    }
}

// ── .mdelay [мс] ─────────────────────────────────────────────────────────────
static void cmd_mdelay(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    const char* args_ptr = api->cmd_args(ctx);
    std::string args = args_ptr ? trim(args_ptr) : "";
    if (args.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "⏱ Текущая задержка голосования: ");
        api->c_fmt(ctx, (std::to_string(s.vote_delay_ms) + " мс").c_str(), KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }
    int val = std::atoi(args.c_str());
    if (val < 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Укажите число ≥ 0.");
        api->c_edit(ctx, 0);
        return;
    }
    s.vote_delay_ms = val;
    save_settings(ctx, api, s);
    api->c_reset(ctx);
    api->c_text(ctx, "✅ Задержка голосования установлена: ");
    api->c_fmt(ctx, (std::to_string(val) + " мс").c_str(), KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

// ── .mhuman [on|off|status|set ...] ──────────────────────────────────────────
static void cmd_mhuman(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    const char* args_ptr = api->cmd_args(ctx);
    std::string args = args_ptr ? trim(args_ptr) : "";
    std::stringstream ss(args);
    std::string sub;
    ss >> sub;
    sub = to_lower(sub);

    if (sub.empty() || sub == "status") {
        api->c_reset(ctx);
        api->c_fmt(ctx, "🎭 Режим живых игроков\n\n", KOTO_ENT_BOLD);
        api->c_text(ctx, s.human_mode ? "Статус:   ✅ ВКЛЮЧЁН\n" : "Статус:   ❌ ВЫКЛЮЧЕН\n");
        api->c_text(ctx, ("Мгновенно: " + std::to_string(s.human_instant) + " акк.\n").c_str());
        api->c_text(ctx, ("Окно:      " + std::to_string(s.human_window_sec) + " сек\n").c_str());
        api->c_text(ctx, ("Пачка:     " + std::to_string(s.human_batch_min) + "–" +
                          std::to_string(s.human_batch_max) + " акк.\n\n").c_str());
        api->c_text(ctx, "Команды рег/веш/лайк/диз автоматически имитируют живых игроков, когда режим ВКЛ.");
        api->c_edit(ctx, 0);
        return;
    }

    if (sub == "on") {
        s.human_mode = true;
        save_settings(ctx, api, s);
        api->c_reset(ctx);
        api->c_text(ctx, "✅ Режим живых игроков ");
        api->c_fmt(ctx, "ВКЛЮЧЁН", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }
    if (sub == "off") {
        s.human_mode = false;
        save_settings(ctx, api, s);
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Режим живых игроков ");
        api->c_fmt(ctx, "ВЫКЛЮЧЕН", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }
    if (sub == "set") {
        std::string p1, p2, p3;
        ss >> p1 >> p2 >> p3;
        p1 = to_lower(p1);
        if (p1 == "instant") {
            s.human_instant = std::max(0, std::atoi(p2.c_str()));
            save_settings(ctx, api, s);
            api->c_reset(ctx);
            api->c_text(ctx, ("✅ Мгновенно: " + std::to_string(s.human_instant) + " акк.").c_str());
            api->c_edit(ctx, 0);
            return;
        }
        if (p1 == "window") {
            s.human_window_sec = std::max(1, std::atoi(p2.c_str()));
            save_settings(ctx, api, s);
            api->c_reset(ctx);
            api->c_text(ctx, ("✅ Окно: " + std::to_string(s.human_window_sec) + " сек.").c_str());
            api->c_edit(ctx, 0);
            return;
        }
        if (p1 == "batch") {
            int bmin = std::max(1, std::atoi(p2.c_str()));
            int bmax = std::max(bmin, std::atoi(p3.c_str()));
            s.human_batch_min = bmin;
            s.human_batch_max = bmax;
            save_settings(ctx, api, s);
            api->c_reset(ctx);
            api->c_text(ctx, ("✅ Пачка: " + std::to_string(bmin) + "–" + std::to_string(bmax) + " акк.").c_str());
            api->c_edit(ctx, 0);
            return;
        }
    }

    api->c_reset(ctx);
    api->c_fmt(ctx, "Использование:\n", KOTO_ENT_BOLD);
    api->c_fmt(ctx, ".mhuman on/off\n.mhuman status\n.mhuman set instant <N>\n.mhuman set window <сек>\n.mhuman set batch <min> <max>", KOTO_ENT_CODE);
    api->c_edit(ctx, 0);
}

// ── .msettings ───────────────────────────────────────────────────────────────
static void cmd_msettings(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    auto accounts = get_all_accounts(ctx, api, s);

    std::string tourney_name = get_chat_title(ctx, api, s.tourney_chat_id);
    std::string arena_name   = get_chat_title(ctx, api, s.arena_id);

    std::string strat = s.combat_mode;
    if (s.combat_mode == "name") strat += " → " + s.combat_target_name;

    api->c_reset(ctx);
    api->c_fmt(ctx, "⚙️ Мафия Мульти-Тул\n\n", KOTO_ENT_BOLD);
    api->c_text(ctx, ("Combat:    " + std::string(s.enabled_combat ? "✅ ВКЛ" : "❌ ВЫКЛ") + "\n").c_str());
    api->c_text(ctx, ("TrueMafia: " + std::string(s.enabled_true ? "✅ ВКЛ" : "❌ ВЫКЛ") + "\n\n").c_str());
    api->c_text(ctx, ("Стратегия: " + strat + "\n").c_str());
    api->c_text(ctx, ("Задержка:  " + std::to_string(s.vote_delay_ms) + " мс\n\n").c_str());
    api->c_text(ctx, ("🎭 Живые игроки: " + std::string(s.human_mode ? "✅ ВКЛ" : "❌ ВЫКЛ") + "\n").c_str());
    api->c_text(ctx, ("   Мгновенно: " + std::to_string(s.human_instant) + " акк. | Окно: " +
                      std::to_string(s.human_window_sec) + " сек\n").c_str());
    api->c_text(ctx, ("   Пачка: " + std::to_string(s.human_batch_min) + "–" +
                      std::to_string(s.human_batch_max) + " акк.\n\n").c_str());
    api->c_text(ctx, ("Лог-чат:   " + tourney_name + "\n").c_str());
    api->c_text(ctx, ("Арена:     " + arena_name + "\n\n").c_str());
    api->c_fmt(ctx, "Аккаунты:\n", KOTO_ENT_BOLD);
    for (const auto& a : accounts) {
        std::string icon = a.frozen ? "❄️" : (a.online ? "🟢" : "⚪");
        std::string line = "  " + icon + " " + a.name + (a.is_main ? " (Main)" : "") +
                           (a.frozen ? " [заморожен]" : "") + "\n";
        api->c_text(ctx, line.c_str());
    }
    api->c_edit(ctx, 0);
}

static void render_mfreeze(koto_ctx* ctx, const koto_api* api, const Settings& s) {
    auto accounts = get_all_accounts(ctx, api, s);
    api->c_reset(ctx);
    api->c_fmt(ctx, "❄️ Управление заморозкой твинков\n\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "Использование: ");
    api->c_fmt(ctx, ".mfreeze <имя/ник>", KOTO_ENT_CODE);
    api->c_text(ctx, "\n\nТекущий статус:\n");
    for (const auto& a : accounts) {
        std::string icon = a.frozen ? "❄️" : "🟢";
        std::string st = a.frozen ? "заморожен" : "активен";
        api->c_text(ctx, ("• " + icon + " " + a.name + " — " + st + "\n").c_str());
    }

    // Инлайн-кнопки управления заморозкой для каждого аккаунта
    for (const auto& a : accounts) {
        std::string btn_label = (a.frozen ? "🟢 Разморозить: " : "❄️ Заморозить: ") + a.name;
        std::string cb = "mfz:toggle:" + a.name;
        int color = a.frozen ? KOTO_BTN_SUCCESS : KOTO_BTN_PRIMARY;
        if (api->c_button_ex) {
            api->c_button_ex(ctx, btn_label.c_str(), cb.c_str(), 1, color, 0);
        } else {
            api->c_button(ctx, btn_label.c_str(), cb.c_str(), 1);
        }
    }

    // Кнопка закрытия
    if (api->c_button_ex) {
        api->c_button_ex(ctx, "❌ Закрыть", "mfz:close", 1, KOTO_BTN_DANGER, 0);
    } else {
        api->c_button(ctx, "❌ Закрыть", "mfz:close", 1);
    }
}

// ── .mfreeze [имя/ник] ───────────────────────────────────────────────────────
static void cmd_mfreeze(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    const char* args_ptr = api->cmd_args(ctx);
    std::string target = args_ptr ? trim(args_ptr) : "";

    if (target.empty()) {
        render_mfreeze(ctx, api, s);
        api->c_edit(ctx, 0);
        return;
    }

    if (s.frozen_twins.count(target) > 0) {
        s.frozen_twins.erase(target);
        save_settings(ctx, api, s);
        api->c_reset(ctx);
        api->c_text(ctx, "🟢 Аккаунт ");
        api->c_fmt(ctx, target.c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, " разморожен.");
        api->c_edit(ctx, 0);
    } else {
        s.frozen_twins.insert(target);
        save_settings(ctx, api, s);
        api->c_reset(ctx);
        api->c_text(ctx, "❄️ Аккаунт ");
        api->c_fmt(ctx, target.c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, " заморожен (игнорирует команды модуля).");
        api->c_edit(ctx, 0);
    }
}

// ── .mcombat <on/off> ────────────────────────────────────────────────────────
static void cmd_mcombat(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    const char* args_ptr = api->cmd_args(ctx);
    std::string arg = args_ptr ? to_lower(trim(args_ptr)) : "";

    if (arg == "on") {
        s.enabled_combat = true;
        save_settings(ctx, api, s);
        auto active = get_active_accounts(ctx, api, s);
        api->c_reset(ctx);
        api->c_text(ctx, ("✅ Combat авто-голосование ВКЛЮЧЕНО. Активных аккаунтов: " +
                          std::to_string(active.size())).c_str());
        api->c_edit(ctx, 0);
    } else if (arg == "off") {
        s.enabled_combat = false;
        save_settings(ctx, api, s);
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Combat авто-голосование ВЫКЛЮЧЕНО.");
        api->c_edit(ctx, 0);
    } else {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Укажите on или off: .mcombat on/off");
        api->c_edit(ctx, 0);
    }
}

// ── .mlast ───────────────────────────────────────────────────────────────────
static void cmd_mlast(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;
    s.combat_mode = "last";
    s.combat_target_name.clear();
    save_settings(ctx, api, s);
    api->c_reset(ctx);
    api->c_text(ctx, "🎯 Combat стратегия: ");
    api->c_fmt(ctx, "Последний враг", KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

// ── .mprelast ────────────────────────────────────────────────────────────────
static void cmd_mprelast(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;
    s.combat_mode = "prelast";
    s.combat_target_name.clear();
    save_settings(ctx, api, s);
    api->c_reset(ctx);
    api->c_text(ctx, "🎯 Combat стратегия: ");
    api->c_fmt(ctx, "Предпоследний враг", KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

// ── .mvesh <имя> ─────────────────────────────────────────────────────────────
static void cmd_mvesh(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;
    const char* args_ptr = api->cmd_args(ctx);
    std::string target = args_ptr ? trim(args_ptr) : "";
    if (target.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Укажите имя: .mvesh <имя>");
        api->c_edit(ctx, 0);
        return;
    }
    s.combat_mode = "name";
    s.combat_target_name = target;
    save_settings(ctx, api, s);
    api->c_reset(ctx);
    api->c_text(ctx, "🎯 Combat стратегия: ");
    api->c_fmt(ctx, ("Голос в " + target).c_str(), KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

// ── Безопасный резолв бота с повторными попытками ────────────────────────────
static long long safe_resolve_bot(koto_ctx* ctx, const koto_api* api, const std::string& username) {
    if (username.empty()) return 0;
    std::string u = to_lower(username);
    if (u.front() == '@') u = u.substr(1);

    // 1. Сначала пробуем динамический резолв через Telegram MTProto (кеширует access_hash)
    if (api && api->resolve) {
        long long id = api->resolve(ctx, u.c_str());
        if (id > 0) return id;
        id = api->resolve(ctx, username.c_str());
        if (id > 0) return id;
    }

    // 2. Известные fallback ID ботов
    if (u == "combatmafiasobot") return 1634167847LL;
    if (u == "mafiabakublackbot" || u == "mafiabakublack1bot") return 850550410LL;
    if (u == "truemafiablackbot" || u == "truemafiabot") return 731804554LL;
    if (u == "mafiososbot" || u == "mafiozi" || u == "mafioso" || u == "oso") return 5424831786LL;

    // 3. Повторные попытки резолва
    if (api && api->resolve) {
        for (int r = 0; r < 3; ++r) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            long long id = api->resolve(ctx, u.c_str());
            if (id > 0) return id;
        }
    }
    return 0;
}

// ── .mreg <ссылка или параметр> ──────────────────────────────────────────────
static void cmd_mreg(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;

    const char* args_ptr = api->cmd_args(ctx);
    std::string args = args_ptr ? trim(args_ptr) : "";
    if (args.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Укажите ссылку или старт-параметр.\n\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "Примеры:\n"
                         "  .mreg https://t.me/KoteMafiaBot?start=jointeam_-100123_orange\n"
                         "  .mreg https://t.me/CombatMafiaSOBot?start=vsrandom_-100123\n"
                         "  .mreg -1002404766570_1  (→ Baku)");
        api->c_edit(ctx, 0);
        return;
    }

    std::string bot_username = BAKU_BOT;
    std::string start_param = args;

    // Парсим бот и параметр
    size_t qpos = args.find("?start=");
    if (qpos == std::string::npos) qpos = args.find("&start=");
    if (qpos != std::string::npos) {
        start_param = args.substr(qpos + 7);
        size_t bstart = args.rfind('/', qpos);
        if (bstart != std::string::npos) {
            bot_username = args.substr(bstart + 1, qpos - bstart - 1);
        } else {
            size_t astart = args.rfind('@', qpos);
            if (astart != std::string::npos) {
                bot_username = args.substr(astart + 1, qpos - astart - 1);
            } else {
                bot_username = args.substr(0, qpos);
            }
        }
    } else {
        if (args.rfind("jointeam", 0) == 0 || args.rfind("join_", 0) == 0) {
            bot_username = KOTO_BOT;
        } else if (args.find("combat") != std::string::npos || args.find("vsrandom") != std::string::npos) {
            bot_username = COMBAT_BOT;
        }
    }

    // Очищаем bot_username от мусора (слеши, @, пробелы, ?)
    while (!bot_username.empty() && (bot_username.front() == '@' || bot_username.front() == '/' || bot_username.front() == ' ')) {
        bot_username.erase(bot_username.begin());
    }
    while (!bot_username.empty() && (bot_username.back() == '/' || bot_username.back() == '?' || bot_username.back() == ' ')) {
        bot_username.pop_back();
    }
    if (bot_username.empty()) bot_username = BAKU_BOT;

    auto accounts = get_active_accounts(ctx, api, s);
    if (accounts.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Нет активных аккаунтов (твинов) для регистрации.");
        api->c_edit(ctx, 0);
        return;
    }

    if (api->db_set) {
        api->db_set(ctx, "last_registered_bot", bot_username.c_str());
    }
    api->c_reset(ctx);
    api->c_text(ctx, ("⏳ Регистрирую " + std::to_string(accounts.size()) + " аккаунтов в @" +
                      bot_username + " (" + start_param + ")...").c_str());
    api->c_edit(ctx, 0);

    long long bot_peer_id = safe_resolve_bot(ctx, api, bot_username);
    if (bot_peer_id <= 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Ошибка регистрации:\n", KOTO_ENT_BOLD);
        api->c_text(ctx, ("Не удалось определить ID бота @" + bot_username + " (ID = 0).\n"
                          "⚠️ Отправка отменена, чтобы не спамить /start в турнирный чат!\n"
                          "💡 Напишите боту @" + bot_username + " любое сообщение в ЛС и повторите .mreg.").c_str());
        api->c_edit(ctx, 0);
        return;
    }

    int success_count = 0;
    human_scatter(accounts, s, [&](const AccountInfo& acc) {
        std::string cmd = "/start " + start_param;
        api->twin_send(ctx, acc.uid, bot_peer_id, cmd.c_str());
        success_count++;
    });

    api->c_reset(ctx);
    api->c_text(ctx, ("✅ /start отправлен с " + std::to_string(success_count) + " аккаунтов → @" + bot_username).c_str());
    api->c_edit(ctx, 0);
}

// ── .mbvesh <ник или номер> ──────────────────────────────────────────────────
static void cmd_mbvesh(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;

    const char* args_ptr = api->cmd_args(ctx);
    std::string target_raw = args_ptr ? trim(args_ptr) : "";
    if (target_raw.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Укажите ник или номер: .mbvesh <ник/номер/UID>");
        api->c_edit(ctx, 0);
        return;
    }

    auto accounts = get_active_accounts(ctx, api, s);
    if (accounts.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Нет активных аккаунтов.");
        api->c_edit(ctx, 0);
        return;
    }

    // Определяем целевой UID игрока, если указан прямой ID или имя одного из наших аккаунтов
    long long target_uid = 0;
    if (isdigit((unsigned char)target_raw[0])) {
        char* end_p = nullptr;
        long long parsed_id = std::strtoll(target_raw.c_str(), &end_p, 10);
        if (parsed_id > 1000000LL) target_uid = parsed_id;
    }
    if (target_uid == 0) {
        for (const auto& a : accounts) {
            if (is_same_player(a.name, target_raw)) {
                target_uid = a.uid;
                break;
            }
        }
    }
    std::string target_uid_str = (target_uid > 0) ? std::to_string(target_uid) : "";

    api->c_reset(ctx);
    api->c_text(ctx, ("🗳 [MultiTool v1.3.11] Baku: голос за " + target_raw + " (" + std::to_string(accounts.size()) + " акк.)...").c_str());
    api->c_edit(ctx, 0);

    long long bot_id = safe_resolve_bot(ctx, api, BAKU_BOT);
    if (bot_id <= 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Ошибка: ", KOTO_ENT_BOLD);
        api->c_text(ctx, ("Не удалось определить ID бота @" + std::string(BAKU_BOT) + ".").c_str());
        api->c_edit(ctx, 0);
        return;
    }

    int voted_count = 0;
    std::string matched_target_name;

    for (const auto& acc : accounts) {
        char hbuf[65536];
        int hlen = api->twin_get_history_json(ctx, acc.uid, bot_id, 0, 30, 0, hbuf, sizeof(hbuf));
        if (hlen <= 0) continue;

        auto msgs = parse_history_json(std::string(hbuf, hlen));
        int target_msg_id = 0;
        std::string target_btn_data;

        for (const auto& m : msgs) {
            if (m.buttons.empty()) continue;

            for (const auto& btn : m.buttons) {
                // Защита: нельзя голосовать за самого себя
                if (!btn.data.empty() && btn.data.find(std::to_string(acc.uid)) != std::string::npos) {
                    continue;
                }
                if (is_same_player(acc.name, btn.text)) {
                    continue;
                }

                bool match = false;

                // 1. Точное совпадение по целевому UID в callback (a:game_id:target_uid:vote:1)
                if (!target_uid_str.empty() && btn.data.find(target_uid_str) != std::string::npos) {
                    match = true;
                }

                // 2. Совпадение по имени игрока (is_same_player нормализует шрифты, значки 🔴/🔵/🌝/🌚 и клантеги)
                if (!match && is_same_player(target_raw, btn.text)) {
                    match = true;
                }

                // 3. Совпадение по тексту кнопки
                if (!match) {
                    std::string btn_norm = normalize_baku_btn(btn.text);
                    std::string t_norm = normalize_baku_btn(target_raw);
                    if (!t_norm.empty() && btn_norm.find(t_norm) != std::string::npos) {
                        match = true;
                    }
                }

                // 4. Если аргумент — номер позиции в списке (напр. "4" или "4.")
                if (!match && isdigit((unsigned char)target_raw[0]) && target_raw.size() <= 2) {
                    std::string pfx1 = target_raw + ".";
                    std::string pfx2 = target_raw + " ";
                    if (btn.text.rfind(pfx1, 0) == 0 || btn.text.rfind(pfx2, 0) == 0 ||
                        btn.text.find(" " + pfx1) != std::string::npos) {
                        match = true;
                    }
                }

                if (match) {
                    target_msg_id = m.id;
                    target_btn_data = btn.data;
                    if (matched_target_name.empty()) matched_target_name = btn.text;
                    break;
                }
            }

            if (target_msg_id != 0) break;
        }

        if (target_msg_id != 0 && !target_btn_data.empty()) {
            api->twin_click_button(ctx, acc.uid, bot_id, target_msg_id, target_btn_data.c_str());
            voted_count++;
        }
    }

    api->c_reset(ctx);
    std::string res_msg = "✅ [MultiTool v1.3.11] Baku vesh: проголосовало " + std::to_string(voted_count) + " акк.";
    if (!matched_target_name.empty()) {
        res_msg += "\n🎯 Цель: " + matched_target_name;
    }
    api->c_text(ctx, res_msg.c_str());
    api->c_edit(ctx, 0);
}

// ── .mblike / .mbdis ─────────────────────────────────────────────────────────
static void baku_confirm(koto_ctx* ctx, const koto_api* api, const Settings& s, const std::string& action) {
    long long arena_id = s.arena_id ? s.arena_id : api->chat_id(ctx);
    auto accounts = get_active_accounts(ctx, api, s);

    api->c_reset(ctx);
    api->c_text(ctx, ("⏳ Baku " + (action == "like" ? std::string("👍") : std::string("👎")) + "...").c_str());
    api->c_edit(ctx, 0);

    int target_msg_id = 0;
    std::string target_btn_data;
    long long rep_id = api->reply_msg_id ? api->reply_msg_id(ctx) : 0;

    char hbuf[65536];
    int hlen = api->get_history_json ? api->get_history_json(ctx, arena_id, 0, 15, 0, hbuf, sizeof(hbuf)) : 0;
    if (hlen > 0) {
        auto msgs = parse_history_json(std::string(hbuf, hlen));
        if (rep_id != 0) {
            for (const auto& m : msgs) {
                if (m.id == rep_id) {
                    for (const auto& btn : m.buttons) {
                        if (btn.data.rfind(action + ":", 0) == 0 ||
                            (action == "like" && btn.text.find("👍") != std::string::npos) ||
                            (action != "like" && btn.text.find("👎") != std::string::npos)) {
                            target_msg_id = m.id;
                            target_btn_data = btn.data;
                            break;
                        }
                    }
                    break;
                }
            }
        }

        if (target_msg_id == 0) {
            for (const auto& m : msgs) {
                for (const auto& btn : m.buttons) {
                    if (btn.data.rfind(action + ":", 0) == 0 ||
                        btn.data.find(action) != std::string::npos ||
                        (action == "like" && btn.text.find("👍") != std::string::npos) ||
                        (action != "like" && btn.text.find("👎") != std::string::npos)) {
                        target_msg_id = m.id;
                        target_btn_data = btn.data;
                        break;
                    }
                }
                if (target_msg_id != 0) break;
            }
        }
    }

    if (target_msg_id == 0 || target_btn_data.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Кнопка Baku-голосования не найдена в арене.");
        api->c_edit(ctx, 0);
        return;
    }

    api->twin_click_button(ctx, 0, arena_id, target_msg_id, target_btn_data.c_str());
    int count = (int)accounts.size();

    api->c_reset(ctx);
    api->c_text(ctx, ("✅ [MultiTool v1.3.11] Baku " + (action == "like" ? std::string("линч 👍") : std::string("отмена 👎")) +
                      ": нажато с " + std::to_string(count) + " акк.").c_str());
    api->c_edit(ctx, 0);
}

static void cmd_mblike(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;
    baku_confirm(ctx, api, s, "like");
}

static void cmd_mbdis(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;
    baku_confirm(ctx, api, s, "dislike");
}

struct RoleBotConfig {
    std::string key;
    std::string username;
};

static bool is_allies_text(const std::string& text) {
    if (text.empty()) return false;
    if (text.find("союзник") != std::string::npos ||
        text.find("Союзник") != std::string::npos ||
        text.find("напарник") != std::string::npos ||
        text.find("Напарник") != std::string::npos ||
        text.find("Запомни своих") != std::string::npos ||
        text.find("запомни своих") != std::string::npos) {
        return true;
    }
    return false;
}

bool is_role_text(const std::string& text, const std::string& bot_key) {
    if (text.empty()) return false;

    // Исключаем игровые и системные сообщения, которые бот шлёт в ЛС, но которые НЕ являются ролью
    if (text.find("присоединился") != std::string::npos) return false;
    if (text.find("Голосование") != std::string::npos) return false;
    if (text.find("голосование") != std::string::npos) return false;
    if (text.find("проголосовал за") != std::string::npos) return false;
    if (text.find("принесла в жертву") != std::string::npos) return false;
    if (text.find("изменены права") != std::string::npos) return false;
    if (text.find("Кого бы хотел убить?") != std::string::npos) return false;
    if (text.find("Живые игроки:") != std::string::npos) return false;
    if (text.find("Итоги голосования") != std::string::npos) return false;
    if (text.find("В игре начался") != std::string::npos) return false;
    if (text.find("Игра началась") != std::string::npos) return false;
    if (text.find("разошлись, не повесив") != std::string::npos) return false;

    if (bot_key == "combat" || bot_key == "true") {
        return text.find("Твоя роль в игре:") != std::string::npos;
    }
    if (bot_key == "baku") {
        return (text.find("Твоя роль") != std::string::npos ||
                text.find("твоя роль") != std::string::npos);
    }
    if (bot_key == "koto") {
        return text.find("Твоя роль:") != std::string::npos;
    }
    if (bot_key == "oso") {
        return (text.rfind("Ты —", 0) == 0 || text.rfind("Ты -", 0) == 0 || text.find("\nТы —") != std::string::npos);
    }
    return false;
}

// ── .mrole [combat|true|baku|koto|oso] ───────────────────────────────────────
static void cmd_mrole(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;

    const char* args_ptr = api->cmd_args(ctx);
    std::string bot_choice = args_ptr ? to_lower(trim(args_ptr)) : "";

    std::string target_bot_username;
    std::string target_bot_key;
    if (bot_choice == "combat") {
        target_bot_username = COMBAT_BOT;
        target_bot_key = "combat";
    } else if (bot_choice == "true") {
        target_bot_username = TRUE_BOT;
        target_bot_key = "true";
    } else if (bot_choice == "baku") {
        target_bot_username = BAKU_BOT;
        target_bot_key = "baku";
    } else if (bot_choice == "koto") {
        target_bot_username = KOTO_BOT;
        target_bot_key = "koto";
    } else if (bot_choice == "oso") {
        target_bot_username = OSO_BOT;
        target_bot_key = "oso";
    } else if (bot_choice.empty()) {
        char reg_bot_buf[128] = {0};
        int rlen = api->db_get ? api->db_get(ctx, "last_registered_bot", reg_bot_buf, sizeof(reg_bot_buf)) : 0;
        std::string reg_bot = (rlen > 0) ? to_lower(trim(reg_bot_buf)) : "";
        if (!reg_bot.empty()) {
            if (reg_bot.find("baku") != std::string::npos) {
                target_bot_username = BAKU_BOT;
                target_bot_key = "baku";
            } else if (reg_bot.find("true") != std::string::npos) {
                target_bot_username = TRUE_BOT;
                target_bot_key = "true";
            } else if (reg_bot.find("koto") != std::string::npos) {
                target_bot_username = KOTO_BOT;
                target_bot_key = "koto";
            } else if (reg_bot.find("oso") != std::string::npos || reg_bot.find("mafioso") != std::string::npos) {
                target_bot_username = OSO_BOT;
                target_bot_key = "oso";
            } else {
                target_bot_username = COMBAT_BOT;
                target_bot_key = "combat";
            }
        } else {
            if (s.enabled_true) {
                target_bot_username = TRUE_BOT;
                target_bot_key = "true";
            } else {
                target_bot_username = COMBAT_BOT;
                target_bot_key = "combat";
            }
        }
    } else {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Укажите бота: .mrole [combat|true|baku|koto|oso]");
        api->c_edit(ctx, 0);
        return;
    }

    long long cur_chat = api->chat_id(ctx);
    long long dest_chat = s.tourney_chat_id ? s.tourney_chat_id : cur_chat;
    if (dest_chat == 0) dest_chat = cur_chat;

    auto accounts = get_active_accounts(ctx, api, s);
    api->c_reset(ctx);
    std::string scan_msg = "🕵️ [MultiTool v1.3.11] Сканирую роли из ЛС @" + target_bot_username + "...";
    api->c_text(ctx, scan_msg.c_str());
    api->c_edit(ctx, 0);

    long long bot_id = safe_resolve_bot(ctx, api, target_bot_username);
    if (bot_id <= 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Ошибка: ", KOTO_ENT_BOLD);
        api->c_text(ctx, ("Не удалось определить ID бота @" + target_bot_username).c_str());
        api->c_edit(ctx, 0);
        return;
    }

    int forwarded_count = 0;
    time_t now_ts = std::time(nullptr);

    for (const auto& acc : accounts) {
        char hbuf[65536];
        int hlen = api->twin_get_history_json(ctx, acc.uid, bot_id, 0, 50, 0, hbuf, sizeof(hbuf));
        if (hlen <= 0) continue;

        auto msgs = parse_history_json(std::string(hbuf, hlen));
        int role_mid = 0;
        int allies_mid = 0;

        for (const auto& m : msgs) {
            if (m.out) continue;
            // В ЛС с ботом все входящие сообщения гарантированно от этого бота
            if (m.sender_id != 0 && bot_id == 0) {
                bot_id = m.sender_id;
            }
            if (m.date > 0 && (now_ts - m.date > 7200)) continue;

            if (role_mid == 0 && is_role_text(m.text, target_bot_key)) {
                role_mid = m.id;
            }
            if (allies_mid == 0 && is_allies_text(m.text)) {
                allies_mid = m.id;
            }
            if (role_mid != 0 && allies_mid != 0) break;
        }

        if (role_mid != 0 || allies_mid != 0) {
            std::vector<int> fwd_ids;
            // Пересылаем в хронологическом порядке: сначала роль, затем союзников
            if (role_mid != 0 && allies_mid != 0) {
                if (allies_mid < role_mid) {
                    fwd_ids.push_back(allies_mid);
                    fwd_ids.push_back(role_mid);
                } else {
                    fwd_ids.push_back(role_mid);
                    fwd_ids.push_back(allies_mid);
                }
            } else if (role_mid != 0) {
                fwd_ids.push_back(role_mid);
            } else {
                fwd_ids.push_back(allies_mid);
            }
            api->twin_forward_messages(ctx, acc.uid, bot_id, fwd_ids.data(), (int)fwd_ids.size(), dest_chat);
            forwarded_count++;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    api->c_reset(ctx);
    if (forwarded_count > 0) {
        std::string res_text = "✅ [MultiTool v1.3.11] Роли пересланы с " + std::to_string(forwarded_count) + " акк. (из @" + target_bot_username + ")";
        if (dest_chat != cur_chat) {
            res_text += "\n📍 Отправлены в лог-чат: " + get_chat_title(ctx, api, dest_chat);
        }
        api->c_text(ctx, res_text.c_str());
    } else {
        api->c_text(ctx, ("❌ Свежие роли в ЛС @" + target_bot_username + " не найдены (проверьте, началась ли игра и выдана ли роль).").c_str());
    }
    api->c_edit(ctx, 0);
}

// ── .mlike / .mdis ───────────────────────────────────────────────────────────
static void combat_or_bot_confirm(koto_ctx* ctx, const koto_api* api, const Settings& s, bool is_like, const std::string& target_bot) {
    long long arena = s.arena_id ? s.arena_id : api->chat_id(ctx);
    if (arena == 0) arena = api->chat_id(ctx);
    auto accounts = get_active_accounts(ctx, api, s);

    api->c_reset(ctx);
    api->c_text(ctx, ("⏳ Нажимаю " + (is_like ? std::string("👍") : std::string("👎")) + "...").c_str());
    api->c_edit(ctx, 0);

    int target_msg_id = 0;
    std::string target_btn_data;
    long long rep_id = api->reply_msg_id ? api->reply_msg_id(ctx) : 0;

    // Сканируем последние сообщения арены ОДИН РАЗ через историю основного клиента
    char hbuf[65536];
    int hlen = api->get_history_json ? api->get_history_json(ctx, arena, 0, 15, 0, hbuf, sizeof(hbuf)) : 0;
    if (hlen > 0) {
        auto msgs = parse_history_json(std::string(hbuf, hlen));

        // Если команда отправлена в ответ на сообщение бота, ищем сначала в нём
        if (rep_id != 0) {
            for (const auto& m : msgs) {
                if (m.id == rep_id) {
                    for (const auto& btn : m.buttons) {
                        bool match = false;
                        if (target_bot == "oso") {
                            std::string pfx = is_like ? "like_" : "dislike_";
                            if (btn.data.rfind(pfx, 0) == 0 || (is_like && btn.text.find("👍") != std::string::npos) || (!is_like && btn.text.find("👎") != std::string::npos)) match = true;
                        } else if (target_bot == "true") {
                            std::string sub = is_like ? "yes" : "no";
                            if ((btn.data.find("lynch_confirm") != std::string::npos && btn.data.find(sub) != std::string::npos) ||
                                (is_like && btn.text.find("👍") != std::string::npos) || (!is_like && btn.text.find("👎") != std::string::npos)) match = true;
                        } else if (target_bot == "koto") {
                            std::string suf = is_like ? ":like" : ":dislike";
                            if ((btn.data.size() >= suf.size() && btn.data.compare(btn.data.size() - suf.size(), suf.size(), suf) == 0) ||
                                (is_like && btn.text.find("👍") != std::string::npos) || (!is_like && btn.text.find("👎") != std::string::npos)) match = true;
                        } else if (target_bot == "baku") {
                            std::string pfx = is_like ? "like:" : "dislike:";
                            if (btn.data.rfind(pfx, 0) == 0 || (is_like && btn.text.find("👍") != std::string::npos) || (!is_like && btn.text.find("👎") != std::string::npos)) match = true;
                        } else {
                            std::string key = is_like ? "like_" : "diss_";
                            if (btn.data.find(key) != std::string::npos || (is_like && btn.text.find("👍") != std::string::npos) || (!is_like && btn.text.find("👎") != std::string::npos)) match = true;
                        }
                        if (match) {
                            target_msg_id = m.id;
                            target_btn_data = btn.data;
                            break;
                        }
                    }
                    break;
                }
            }
        }

        // Если не реплай или в реплае кнопки не нашлись, ищем в последних сообщениях
        if (target_msg_id == 0) {
            for (const auto& m : msgs) {
                for (const auto& btn : m.buttons) {
                    bool match = false;
                    if (target_bot == "oso") {
                        std::string pfx = is_like ? "like_" : "dislike_";
                        if (btn.data.rfind(pfx, 0) == 0 || (is_like && btn.text.find("👍") != std::string::npos) || (!is_like && btn.text.find("👎") != std::string::npos)) match = true;
                    } else if (target_bot == "true") {
                        std::string sub = is_like ? "yes" : "no";
                        if ((btn.data.find("lynch_confirm") != std::string::npos && btn.data.find(sub) != std::string::npos) ||
                            (is_like && btn.text.find("👍") != std::string::npos) || (!is_like && btn.text.find("👎") != std::string::npos)) match = true;
                    } else if (target_bot == "koto") {
                        std::string suf = is_like ? ":like" : ":dislike";
                        if ((btn.data.size() >= suf.size() && btn.data.compare(btn.data.size() - suf.size(), suf.size(), suf) == 0) ||
                            (is_like && btn.text.find("👍") != std::string::npos) || (!is_like && btn.text.find("👎") != std::string::npos)) match = true;
                    } else {
                        // Combat default
                        std::string key = is_like ? "like_" : "diss_";
                        if (btn.data.find(key) != std::string::npos || (is_like && btn.text.find("👍") != std::string::npos) || (!is_like && btn.text.find("👎") != std::string::npos)) match = true;
                    }
                    if (match) {
                        target_msg_id = m.id;
                        target_btn_data = btn.data;
                        break;
                    }
                }
                if (target_msg_id != 0) break;
            }
        }
    }

    if (target_msg_id == 0 || target_btn_data.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Кнопка голосования не найдена в арене.\n(Используйте команду в ответ на сообщение бота или повторите).");
        api->c_edit(ctx, 0);
        return;
    }

    api->twin_click_button(ctx, 0, arena, target_msg_id, target_btn_data.c_str());
    int count = (int)accounts.size();

    api->c_reset(ctx);
    api->c_text(ctx, ("✅ [MultiTool v1.3.11] Нажато " + (is_like ? std::string("👍") : std::string("👎")) + " с " + std::to_string(count) + " акк.").c_str());
    api->c_edit(ctx, 0);
}

static std::string detect_target_bot(koto_ctx* ctx, const koto_api* api, const std::string& explicit_arg) {
    if (!explicit_arg.empty()) return explicit_arg;
    char reg_bot_buf[128] = {0};
    int rlen = api->db_get ? api->db_get(ctx, "last_registered_bot", reg_bot_buf, sizeof(reg_bot_buf)) : 0;
    std::string reg_bot = (rlen > 0) ? to_lower(trim(reg_bot_buf)) : "";
    if (!reg_bot.empty()) {
        if (reg_bot.find("baku") != std::string::npos) return "baku";
        if (reg_bot.find("true") != std::string::npos) return "true";
        if (reg_bot.find("koto") != std::string::npos) return "koto";
        if (reg_bot.find("oso") != std::string::npos || reg_bot.find("mafioso") != std::string::npos) return "oso";
        return "combat";
    }
    return "combat";
}

static void cmd_mlike(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;
    const char* args_ptr = api->cmd_args(ctx);
    std::string arg = detect_target_bot(ctx, api, args_ptr ? to_lower(trim(args_ptr)) : "");
    combat_or_bot_confirm(ctx, api, s, true, arg);
}

static void cmd_mdis(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;
    const char* args_ptr = api->cmd_args(ctx);
    std::string arg = detect_target_bot(ctx, api, args_ptr ? to_lower(trim(args_ptr)) : "");
    combat_or_bot_confirm(ctx, api, s, false, arg);
}

// ── .mprofile ────────────────────────────────────────────────────────────────
static void cmd_mprofile(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;
    auto accounts = get_active_accounts(ctx, api, s);

    long long combat_id = safe_resolve_bot(ctx, api, COMBAT_BOT);
    if (combat_id <= 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Ошибка: ", KOTO_ENT_BOLD);
        api->c_text(ctx, ("Не удалось определить ID бота @" + std::string(COMBAT_BOT) + ".").c_str());
        api->c_edit(ctx, 0);
        return;
    }
    int count = 0;
    for (const auto& a : accounts) {
        api->twin_send(ctx, a.uid, combat_id, "/profile");
        count++;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    api->c_reset(ctx);
    api->c_text(ctx, ("✅ /profile отправлено с " + std::to_string(count) + " акк.").c_str());
    api->c_edit(ctx, 0);
}

// ── .mtrue <on/off> ──────────────────────────────────────────────────────────
static void cmd_mtrue(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    const char* args_ptr = api->cmd_args(ctx);
    std::string arg = args_ptr ? to_lower(trim(args_ptr)) : "";
    if (arg == "on") {
        s.enabled_true = true;
        save_settings(ctx, api, s);
        api->c_reset(ctx);
        api->c_text(ctx, "✅ TrueMafia режим ВКЛЮЧЁН.");
        api->c_edit(ctx, 0);
    } else if (arg == "off") {
        s.enabled_true = false;
        save_settings(ctx, api, s);
        api->c_reset(ctx);
        api->c_text(ctx, "❌ TrueMafia режим ВЫКЛЮЧЕН.");
        api->c_edit(ctx, 0);
    } else {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Укажите on или off: .mtrue on/off");
        api->c_edit(ctx, 0);
    }
}

// ── .mtvesh <ник> ────────────────────────────────────────────────────────────
static void cmd_mtvesh(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;
    const char* args_ptr = api->cmd_args(ctx);
    std::string target_raw = args_ptr ? trim(args_ptr) : "";
    if (target_raw.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Укажите ник: .mtvesh <ник>");
        api->c_edit(ctx, 0);
        return;
    }
    std::string target_norm = normalize_nick_true(target_raw);
    auto accounts = get_active_accounts(ctx, api, s);

    api->c_reset(ctx);
    api->c_text(ctx, ("🔫 TrueMafia: Голосую за " + target_raw + "...").c_str());
    api->c_edit(ctx, 0);

    long long bot_id = safe_resolve_bot(ctx, api, TRUE_BOT);
    if (bot_id <= 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Ошибка: ", KOTO_ENT_BOLD);
        api->c_text(ctx, ("Не удалось определить ID бота @" + std::string(TRUE_BOT) + ".").c_str());
        api->c_edit(ctx, 0);
        return;
    }
    int count = 0;
    human_scatter(accounts, s, [&](const AccountInfo& acc) {
        char hbuf[65536];
        int hlen = api->twin_get_history_json(ctx, acc.uid, bot_id, 0, 30, 0, hbuf, sizeof(hbuf));
        if (hlen <= 0) return;
        auto msgs = parse_history_json(std::string(hbuf, hlen));
        for (const auto& m : msgs) {
            for (const auto& btn : m.buttons) {
                if (normalize_nick_true(btn.text).find(target_norm) != std::string::npos) {
                    api->twin_click_button(ctx, acc.uid, bot_id, m.id, btn.data.c_str());
                    count++;
                    return;
                }
            }
        }
    });

    api->c_reset(ctx);
    api->c_text(ctx, ("✅ TrueMafia: Проголосовало " + std::to_string(count) + " акк.").c_str());
    api->c_edit(ctx, 0);
}

// ── .mkvesh <ник> ────────────────────────────────────────────────────────────
static void cmd_mkvesh(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;
    const char* args_ptr = api->cmd_args(ctx);
    std::string target_raw = args_ptr ? trim(args_ptr) : "";
    if (target_raw.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Укажите ник: .mkvesh <ник>");
        api->c_edit(ctx, 0);
        return;
    }
    std::string target_norm = normalize_nick(target_raw);
    auto accounts = get_active_accounts(ctx, api, s);

    api->c_reset(ctx);
    api->c_text(ctx, ("🗳 KotoMafia: голос за " + target_raw + "...").c_str());
    api->c_edit(ctx, 0);

    long long bot_id = safe_resolve_bot(ctx, api, KOTO_BOT);
    if (bot_id <= 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Ошибка: ", KOTO_ENT_BOLD);
        api->c_text(ctx, ("Не удалось определить ID бота @" + std::string(KOTO_BOT) + ".").c_str());
        api->c_edit(ctx, 0);
        return;
    }
    int count = 0;
    human_scatter(accounts, s, [&](const AccountInfo& acc) {
        char hbuf[65536];
        int hlen = api->twin_get_history_json(ctx, acc.uid, bot_id, 0, 30, 0, hbuf, sizeof(hbuf));
        if (hlen <= 0) return;
        auto msgs = parse_history_json(std::string(hbuf, hlen));
        for (const auto& m : msgs) {
            std::string ltxt = to_lower(m.text);
            if (ltxt.find("выгнать") == std::string::npos && ltxt.find("голосов") == std::string::npos) continue;
            for (const auto& btn : m.buttons) {
                if (normalize_nick(btn.text).find(target_norm) != std::string::npos) {
                    api->twin_click_button(ctx, acc.uid, bot_id, m.id, btn.data.c_str());
                    count++;
                    return;
                }
            }
        }
    });

    api->c_reset(ctx);
    api->c_text(ctx, ("✅ KotoMafia vesh: проголосовало " + std::to_string(count) + " акк.").c_str());
    api->c_edit(ctx, 0);
}

// ── .movesh <ник/номер/UID/callback> ─────────────────────────────────────────
static void cmd_movesh(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;

    const char* args_ptr = api->cmd_args(ctx);
    std::string target_raw = args_ptr ? trim(args_ptr) : "";

    long long target_uid = 0;
    int target_num = 0;
    std::string callback_needle;

    // Поддержка ответа на сообщение (reply)
    if (target_raw.empty()) {
        long long rep_sender = api->reply_sender_id ? api->reply_sender_id(ctx) : 0;
        if (rep_sender > 0) {
            target_uid = rep_sender;
            char nbuf[128] = {0};
            if (api->user_name && api->user_name(ctx, rep_sender, nbuf, sizeof(nbuf)) > 0) {
                target_raw = nbuf;
            } else {
                target_raw = std::to_string(rep_sender);
            }
        }
    }

    if (target_raw.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Укажите цель для голосования в Mafioso:\n\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "Форматы:\n"
                         "  .movesh <ник>       — по имени/нику (например, .movesh Nexus)\n"
                         "  .movesh <номер>     — по номеру кнопки (например, .movesh 4)\n"
                         "  .movesh <UID>       — по ID игрока (например, .movesh 7165828293)\n"
                         "  .movesh <callback>  — по callback (например, .movesh bpvote_-1002404766570_7165828293)\n"
                         "  .movesh (в reply)   — ответ на сообщение игрока в чате");
        api->c_edit(ctx, 0);
        return;
    }

    auto accounts = get_active_accounts(ctx, api, s);
    if (accounts.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Нет активных аккаунтов (твинов).");
        api->c_edit(ctx, 0);
        return;
    }

    // Если передан полный или частичный callback: bpvote_-1002404766570_7165828293
    if (target_raw.find("bpvote_") != std::string::npos) {
        callback_needle = target_raw;
        size_t last_underscore = target_raw.rfind('_');
        if (last_underscore != std::string::npos && last_underscore + 1 < target_raw.size()) {
            char* end_p = nullptr;
            long long parsed_id = std::strtoll(target_raw.c_str() + last_underscore + 1, &end_p, 10);
            if (parsed_id > 1000000LL) target_uid = parsed_id;
        }
    } else if (isdigit((unsigned char)target_raw[0])) {
        char* end_p = nullptr;
        long long parsed_num = std::strtoll(target_raw.c_str(), &end_p, 10);
        if (parsed_num > 1000000LL) {
            target_uid = parsed_num;
        } else if (parsed_num >= 1 && parsed_num <= 50 && target_raw.size() <= 2) {
            target_num = (int)parsed_num;
        }
    }

    // Если указано имя одного из наших аккаунтов
    if (target_uid == 0 && target_num == 0) {
        for (const auto& a : accounts) {
            if (is_same_player(a.name, target_raw)) {
                target_uid = a.uid;
                break;
            }
        }
    }

    std::string target_uid_str = (target_uid > 0) ? std::to_string(target_uid) : "";
    std::string target_norm = normalize_nick(target_raw);
    std::string target_raw_lower = to_lower(target_raw);

    api->c_reset(ctx);
    api->c_text(ctx, ("🗳 Mafioso: голос за " + target_raw + " (" + std::to_string(accounts.size()) + " акк.)...").c_str());
    api->c_edit(ctx, 0);

    long long bot_id = safe_resolve_bot(ctx, api, OSO_BOT);
    if (bot_id <= 0) bot_id = 5424831786LL; // Официальный ID бота Mafioso

    int count = 0;
    std::string matched_target_name;

    human_scatter(accounts, s, [&](const AccountInfo& acc) {
        char hbuf[65536];
        int hlen = api->twin_get_history_json(ctx, acc.uid, bot_id, 0, 30, 0, hbuf, sizeof(hbuf));
        if (hlen <= 0) return;
        auto msgs = parse_history_json(std::string(hbuf, hlen));

        int target_msg_id = 0;
        std::string target_btn_data;
        std::string cur_btn_name;

        for (const auto& m : msgs) {
            if (m.buttons.empty()) continue;

            bool is_vote_msg = false;
            std::string ltxt = to_lower(m.text);
            if (ltxt.find("повесить") != std::string::npos ||
                ltxt.find("виноватых") != std::string::npos ||
                ltxt.find("голосован") != std::string::npos ||
                ltxt.find("казн") != std::string::npos) {
                is_vote_msg = true;
            }
            if (!is_vote_msg) {
                for (const auto& btn : m.buttons) {
                    if (btn.data.rfind("bpvote_", 0) == 0 || btn.data.find("bpvote") != std::string::npos) {
                        is_vote_msg = true;
                        break;
                    }
                }
            }
            if (!is_vote_msg) continue;

            int btn_idx = 0;
            for (const auto& btn : m.buttons) {
                if (btn.data.empty()) continue;
                btn_idx++;

                // Защита: нельзя голосовать за себя
                if (!btn.data.empty() && btn.data.find(std::to_string(acc.uid)) != std::string::npos) {
                    continue;
                }
                if (is_same_player(acc.name, btn.text)) {
                    continue;
                }

                bool match = false;

                // 1. Точное совпадение по callback
                if (!callback_needle.empty() && btn.data.find(callback_needle) != std::string::npos) {
                    match = true;
                }

                // 2. Совпадение по целевому UID в callback
                if (!match && !target_uid_str.empty() && btn.data.find(target_uid_str) != std::string::npos) {
                    match = true;
                }

                // 3. Совпадение по порядковому номеру кнопки (например, .movesh 4)
                if (!match && target_num > 0) {
                    if (btn_idx == target_num) {
                        match = true;
                    }
                    std::string pfx1 = std::to_string(target_num) + ".";
                    std::string pfx2 = std::to_string(target_num) + " ";
                    if (btn.text.rfind(pfx1, 0) == 0 || btn.text.rfind(pfx2, 0) == 0 ||
                        btn.text.find(" " + pfx1) != std::string::npos) {
                        match = true;
                    }
                }

                // 4. Нечеткое совпадение по имени игрока (фильтрация клантегов/эмодзи/шрифтов)
                if (!match && is_same_player(target_raw, btn.text)) {
                    match = true;
                }

                // 5. Совпадение по нормализованному нику
                if (!match && !target_norm.empty()) {
                    std::string bnorm = normalize_nick(btn.text);
                    if (bnorm.find(target_norm) != std::string::npos) {
                        match = true;
                    }
                }

                // 6. Подстрока в нижнем регистре
                if (!match && !target_raw_lower.empty()) {
                    std::string bl = to_lower(btn.text);
                    if (bl.find(target_raw_lower) != std::string::npos) {
                        match = true;
                    }
                }

                if (match) {
                    target_msg_id = m.id;
                    target_btn_data = btn.data;
                    cur_btn_name = btn.text;
                    break;
                }
            }

            if (target_msg_id != 0) break;
        }

        if (target_msg_id != 0 && !target_btn_data.empty()) {
            api->twin_click_button(ctx, acc.uid, bot_id, target_msg_id, target_btn_data.c_str());
            count++;
            if (matched_target_name.empty()) {
                matched_target_name = cur_btn_name;
            }
        }
    });

    api->c_reset(ctx);
    std::string res_msg = "✅ [MultiTool v1.3.11] Mafioso vesh: проголосовало " + std::to_string(count) + " акк.";
    if (!matched_target_name.empty()) {
        res_msg += "\n🎯 Цель: " + matched_target_name;
    }
    api->c_text(ctx, res_msg.c_str());
    api->c_edit(ctx, 0);
}

// Сбор сообщений вниз от reply_id до текущего момента
static std::vector<HistoryMessage> fetch_history_after(koto_ctx* ctx, const koto_api* api, long long chat_id, long long reply_id, long long trigger_id, int& out_scanned) {
    std::vector<HistoryMessage> result;
    out_scanned = 0;
    if (!api->get_history_json) return result;

    int current_offset_id = 0;
    int total_fetched = 0;
    const int max_to_fetch = 300;
    bool reached_reply = false;
    std::vector<char> hbuf(131072, 0);

    while (total_fetched < max_to_fetch && !reached_reply) {
        int hlen = api->get_history_json(ctx, chat_id, current_offset_id, 100, 0, hbuf.data(), (int)hbuf.size() - 1);
        if (hlen <= 2) break;

        auto chunk = parse_history_json(std::string(hbuf.data(), hlen));
        if (chunk.empty()) break;

        long long oldest_id = 0;
        for (const auto& m : chunk) {
            if (oldest_id == 0 || m.id < oldest_id) {
                oldest_id = m.id;
            }
            if (reply_id > 0 && m.id <= reply_id) {
                reached_reply = true;
                break;
            }
            if (m.id != trigger_id) {
                out_scanned++;
                result.push_back(m);
            }
        }

        if (oldest_id == 0 || oldest_id == current_offset_id) break;
        total_fetched += (int)chunk.size();
        current_offset_id = (int)oldest_id;
    }
    return result;
}

// ── .mkgraf (KotoMafia аналитика не проголосовавших) ──────────────────────────
static void cmd_mkgraf(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    api->delete_msg(ctx);
    long long me_id = api->get_me(ctx);

    long long reply_id = api->reply_msg_id ? api->reply_msg_id(ctx) : 0;
    const char* rtext_ptr = api->reply_text(ctx);
    if (reply_id == 0 || !rtext_ptr || !*rtext_ptr) {
        api->c_reset(ctx);
        api->c_text(ctx, "⚠️ ");
        api->c_fmt(ctx, "mkgraf:", KOTO_ENT_BOLD);
        api->c_text(ctx, " Используйте в ответ на «Живые котики».");
        api->c_send(ctx, me_id, 0);
        return;
    }

    std::string reply_text = rtext_ptr;
    std::vector<std::string> player_lines;
    std::map<std::string, std::string> players; // norm_name -> orig_line

    std::stringstream pss(reply_text);
    std::string pline;
    while (std::getline(pss, pline)) {
        pline = trim(pline);
        if (pline.empty()) continue;
        size_t dot_p = pline.find('.');
        if (dot_p != std::string::npos && dot_p < 5 && isdigit(pline[0])) {
            std::string name_part = trim(pline.substr(dot_p + 1));
            std::string norm = normalize_nick(name_part);
            if (!norm.empty()) {
                player_lines.push_back(pline);
                players[norm] = pline;
            }
        }
    }

    if (players.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "⚠️ ");
        api->c_fmt(ctx, "mkgraf:", KOTO_ENT_BOLD);
        api->c_text(ctx, " Строки игроков не найдены.");
        api->c_send(ctx, me_id, 0);
        return;
    }

    long long chat_id = api->chat_id(ctx);
    long long trigger_id = api->msg_id(ctx);
    int scanned = 0;
    auto msgs = fetch_history_after(ctx, api, chat_id, reply_id, trigger_id, scanned);
    if (msgs.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "⚠️ ");
        api->c_fmt(ctx, "mkgraf:", KOTO_ENT_BOLD);
        std::string err = " Голоса не найдены в истории чата (проверено " + std::to_string(scanned) + " сообщ. от списка игроков вниз).";
        api->c_text(ctx, err.c_str());
        api->c_send(ctx, me_id, 0);
        return;
    }

    std::set<std::string> voted;
    for (const auto& m : msgs) {
        if (m.text.find("проголосовал за") != std::string::npos) {
            if (m.text.find("Котик проголосовал за: Котика") != std::string::npos) continue; // аноним
            size_t ppos = m.text.find("проголосовал за");
            std::string voter = m.text.substr(0, ppos);
            voted.insert(normalize_nick(voter));
        }
    }

    std::vector<std::string> not_voted;
    for (const auto& [norm_p, orig] : players) {
        bool did_vote = false;
        for (const auto& vn : voted) {
            if (vn.find(norm_p) != std::string::npos || norm_p.find(vn) != std::string::npos) {
                did_vote = true;
                break;
            }
        }
        if (!did_vote) not_voted.push_back(orig);
    }

    api->c_reset(ctx);
    if (!not_voted.empty()) {
        std::string title = get_chat_title(ctx, api, chat_id);
        api->c_fmt(ctx, ("КотоМафия — Отчёт \"" + title + "\"\n\n").c_str(), KOTO_ENT_BOLD);
        api->c_fmt(ctx, ("Не проголосовали (" + std::to_string(not_voted.size()) + "):\n").c_str(), KOTO_ENT_BOLD);
        for (const auto& l : not_voted) api->c_text(ctx, (l + "\n").c_str());
    } else {
        api->c_text(ctx, "✅ ");
        api->c_fmt(ctx, "mkgraf: Все проголосовали!", KOTO_ENT_BOLD);
    }
    api->c_send(ctx, me_id, 0);
}

// ── .mbaku (Baku аналитика не проголосовавших) ───────────────────────────────
static void cmd_mbaku(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    api->delete_msg(ctx);
    long long me_id = api->get_me(ctx);

    long long reply_id = api->reply_msg_id ? api->reply_msg_id(ctx) : 0;
    const char* rtext_ptr = api->reply_text(ctx);
    if (reply_id == 0 || !rtext_ptr || !*rtext_ptr) {
        api->c_reset(ctx);
        api->c_text(ctx, "⚠️ ");
        api->c_fmt(ctx, "BakuSearch Ошибка:\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "Эту команду нужно использовать в ответ на список живых игроков.");
        api->c_send(ctx, me_id, 0);
        return;
    }

    std::string reply_text = rtext_ptr;
    std::vector<std::string> player_lines;
    std::vector<std::string> player_identifiers;

    std::stringstream pss(reply_text);
    std::string pline;
    while (std::getline(pss, pline)) {
        pline = trim(pline);
        if (pline.empty()) continue;
        size_t dot_p = pline.find('.');
        if (dot_p != std::string::npos && dot_p < 5 && isdigit(pline[0])) {
            player_lines.push_back(pline);
            player_identifiers.push_back(trim(pline.substr(dot_p + 1)));
        }
    }

    if (player_lines.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "⚠️ ");
        api->c_fmt(ctx, "BakuSearch Ошибка:\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "В отвеченном сообщении не найдены строки игроков.");
        api->c_send(ctx, me_id, 0);
        return;
    }

    const char* args_ptr = api->cmd_args(ctx);
    std::string mode = args_ptr ? to_lower(trim(args_ptr)) : "";

    long long chat_id = api->chat_id(ctx);
    long long trigger_id = api->msg_id(ctx);
    int scanned = 0;
    auto msgs = fetch_history_after(ctx, api, chat_id, reply_id, trigger_id, scanned);
    if (msgs.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "⚠️ ");
        api->c_fmt(ctx, "BakuSearch Ошибка:\n", KOTO_ENT_BOLD);
        std::string err = "Голоса не найдены в истории чата (проверено " + std::to_string(scanned) + " сообщ. от списка игроков вниз).";
        api->c_text(ctx, err.c_str());
        api->c_send(ctx, me_id, 0);
        return;
    }

    std::set<std::string> voted;
    for (const auto& m : msgs) {
        if (m.text.find("голосовал") != std::string::npos) {
            std::stringstream ss(m.text);
            std::string line;
            while (std::getline(ss, line)) {
                line = trim(line);
                if (line.find("голосовал") == std::string::npos) continue;
                if (line.find("Мэр голосовал") != std::string::npos || line.find("Мэр проголосовал") != std::string::npos) {
                    voted.insert(clean_nick_baku("🎖 Мэр"));
                    voted.insert("Мэр");
                    voted.insert("🎖 Мэр");
                    continue;
                }
                size_t prog_pos = line.find("проголосовал");
                size_t gol_pos = line.find("голосовал");
                size_t gpos = (prog_pos != std::string::npos) ? prog_pos : gol_pos;

                std::string nick = line.substr(0, gpos);
                size_t s = nick.find_first_not_of(" \t\r\n");
                if (s != std::string::npos) nick.erase(0, s);
                const std::string ballot_box = "\xF0\x9F\x97\xB3"; // 🗳
                if (nick.rfind(ballot_box, 0) == 0) {
                    nick.erase(0, ballot_box.size());
                }
                std::string cl = clean_nick_baku(nick);
                if (!cl.empty()) voted.insert(cl);
            }
        }
    }

    std::vector<std::string> not_voted;
    for (size_t i = 0; i < player_lines.size(); ++i) {
        std::string cl = clean_nick_baku(player_identifiers[i]);
        bool found = false;
        if (voted.count(cl) > 0) {
            found = true;
        } else {
            std::string lower_id = to_lower(cl);
            for (const auto& vn : voted) {
                if (lower_id == to_lower(vn)) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) not_voted.push_back(player_lines[i]);
    }

    if (mode == "red") {
        std::vector<std::string> fil;
        for (const auto& p : not_voted) if (p.find("🔴") != std::string::npos) fil.push_back(p);
        not_voted = std::move(fil);
    } else if (mode == "blue") {
        std::vector<std::string> fil;
        for (const auto& p : not_voted) if (p.find("🔵") != std::string::npos) fil.push_back(p);
        not_voted = std::move(fil);
    }

    api->c_reset(ctx);
    if (!not_voted.empty()) {
        std::string title = get_chat_title(ctx, api, chat_id);
        api->c_fmt(ctx, ("Отчёт из чата \"" + title + "\"\n\n").c_str(), KOTO_ENT_BOLD);
        api->c_fmt(ctx, "Не голосовали:\n", KOTO_ENT_BOLD);
        for (const auto& l : not_voted) api->c_text(ctx, (l + "\n").c_str());
    } else {
        api->c_text(ctx, "✅ ");
        api->c_fmt(ctx, "BakuSearch: Все проголосовали", KOTO_ENT_BOLD);
    }
    api->c_send(ctx, me_id, 0);
}

// ════════════════════════════════════════════════════════════════════════════
// WATCHER (COMBAT ARENA WATCHER)
// ════════════════════════════════════════════════════════════════════════════

static int on_watch(koto_ctx* ctx, const koto_api* api) {
    Settings s = load_settings(ctx, api);
    long long cur_chat = api->chat_id(ctx);
    if (!s.enabled_combat) return 0;
    if (s.arena_id != 0 && cur_chat != s.arena_id) return 0;

    const char* txt_ptr = api->msg_text(ctx);
    if (!txt_ptr || !*txt_ptr) return 0;
    std::string text = txt_ptr;

    // 1. Определение своего цвета ("Живые игроки:")
    if (text.find("Живые игроки:") != std::string::npos) {
        auto accounts = get_active_accounts(ctx, api, s);
        size_t start = text.find("Живые игроки:");
        size_t end = text.find("Из них:");
        if (start != std::string::npos) {
            std::string section = (end != std::string::npos && end > start)
                                  ? text.substr(start, end - start)
                                  : text.substr(start);
            std::stringstream ss(section);
            std::string line;
            while (std::getline(ss, line)) {
                line = trim(line);
                std::string color;
                if (line.find("🌝") != std::string::npos) color = "🌝";
                else if (line.find("🌚") != std::string::npos) color = "🌚";
                if (color.empty()) continue;

                for (const auto& acc : accounts) {
                    if (is_same_player(acc.name, line)) {
                        std::string k = "combat_color_" + std::to_string(acc.uid);
                        api->db_set(ctx, k.c_str(), color.c_str());
                        if (acc.is_main) {
                            api->db_set(ctx, "my_combat_color", color.c_str());
                        }
                    }
                }
            }
        }
    }
    // 2. Голосование ("Кого бы хотел убить?")
    else if (text.find("Кого бы хотел убить?") != std::string::npos) {
        char bbuf[16384];
        int blen = api->msg_buttons_json ? api->msg_buttons_json(ctx, bbuf, sizeof(bbuf)) : 0;
        if (blen <= 0) return 0;
        auto buttons = parse_buttons_json(std::string(bbuf, blen));
        if (buttons.empty()) return 0;

        auto accounts = get_active_accounts(ctx, api, s);
        if (accounts.empty()) return 0;

        // Разделяем кнопки на желтые и черные
        std::vector<const MsgButton*> yellow_btns; // кнопки с 🌝
        std::vector<const MsgButton*> black_btns;  // кнопки с 🌚
        for (const auto& btn : buttons) {
            if (btn.text.find("🌝") != std::string::npos) yellow_btns.push_back(&btn);
            else if (btn.text.find("🌚") != std::string::npos) black_btns.push_back(&btn);
        }

        // Определяем команду каждого аккаунта
        std::map<long long, std::string> acc_colors;
        for (const auto& acc : accounts) {
            std::string c;
            std::string uid_str = std::to_string(acc.uid);
            for (const auto& btn : buttons) {
                // Если callback_data содержит UID аккаунта или ник совпадает
                if ((!btn.data.empty() && btn.data.find(uid_str) != std::string::npos) ||
                    is_same_player(acc.name, btn.text)) {
                    if (btn.text.find("🌝") != std::string::npos) c = "🌝";
                    else if (btn.text.find("🌚") != std::string::npos) c = "🌚";
                    break;
                }
            }
            if (c.empty()) {
                char cbuf[32] = {0};
                std::string k = "combat_color_" + uid_str;
                int clen = api->db_get(ctx, k.c_str(), cbuf, sizeof(cbuf));
                if (clen > 0) c = std::string(cbuf, clen);
            }
            if (c.empty() && acc.is_main) {
                char cbuf[32] = {0};
                int clen = api->db_get(ctx, "my_combat_color", cbuf, sizeof(cbuf));
                if (clen > 0) c = std::string(cbuf, clen);
            }
            if (c.empty()) c = "🌚"; // по умолчанию
            acc_colors[acc.uid] = c;
        }

        if (s.vote_delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(s.vote_delay_ms));
        }

        int voted_count = 0;
        for (const auto& acc : accounts) {
            if (acc.uid <= 0) continue;
            std::string my_c = acc_colors[acc.uid];
            // Жёлтые бьют по Чёрным, Чёрные бьют по Жёлтым!
            const auto& candidate_pool = (my_c == "🌝") ? black_btns : yellow_btns;

            // Фильтруем: исключаем себя (сам в себя голосовать нельзя!)
            std::vector<const MsgButton*> valid_targets;
            std::string uid_str = std::to_string(acc.uid);
            for (const auto* b : candidate_pool) {
                if (!b->data.empty() && b->data.find(uid_str) != std::string::npos) continue;
                if (is_same_player(acc.name, b->text)) continue;
                valid_targets.push_back(b);
            }

            // Если в целевом пуле никого не осталось — берём любые кнопки кроме себя И кроме союзников по цвету!
            if (valid_targets.empty()) {
                for (const auto& btn : buttons) {
                    if (!btn.data.empty() && btn.data.find(uid_str) != std::string::npos) continue;
                    if (is_same_player(acc.name, btn.text)) continue;
                    if (!my_c.empty() && btn.text.find(my_c) != std::string::npos) continue;
                    valid_targets.push_back(&btn);
                }
            }

            if (valid_targets.empty()) continue;

            const MsgButton* target_btn = nullptr;
            if (s.combat_mode == "name" && !s.combat_target_name.empty()) {
                for (const auto* b : valid_targets) {
                    if (is_same_player(s.combat_target_name, b->text)) {
                        target_btn = b;
                        break;
                    }
                }
            }
            if (!target_btn) {
                if (s.combat_mode == "prelast" && valid_targets.size() >= 2) {
                    target_btn = valid_targets[valid_targets.size() - 2];
                } else {
                    target_btn = valid_targets.back();
                }
            }

            if (target_btn && !target_btn->data.empty()) {
                api->twin_click_button(ctx, acc.uid, cur_chat, api->msg_id(ctx), target_btn->data.c_str());
                voted_count++;
            }
        }

        return voted_count > 0 ? 1 : 0;
    }

    return 0;
}

// ── Реестр команд модуля ──────────────────────────────────────────────────────
static const koto_command MAFIA_COMMANDS[] = {
    {"mset",      cmd_mset,      KOTO_LEVEL_TRUSTED, "Установить лог-чат или арену", "<tourney|arena> [id]"},
    {"mdelay",    cmd_mdelay,    KOTO_LEVEL_TRUSTED, "Задержка голосования (мс)", "[мс]"},
    {"mhuman",    cmd_mhuman,    KOTO_LEVEL_TRUSTED, "Режим живых игроков", "[on|off|status|set...]"},
    {"msettings", cmd_msettings, KOTO_LEVEL_TRUSTED, "Текущие настройки и статус твинков", NULL},
    {"mfreeze",   cmd_mfreeze,   KOTO_LEVEL_TRUSTED, "Заморозить/разморозить твинка", "[имя]"},
    {"mcombat",   cmd_mcombat,   KOTO_LEVEL_TRUSTED, "Вкл/выкл Combat авто-голосование", "<on/off>"},
    {"mlast",     cmd_mlast,     KOTO_LEVEL_TRUSTED, "Combat: голос в последнего врага", NULL},
    {"mprelast",  cmd_mprelast,  KOTO_LEVEL_TRUSTED, "Combat: голос в предпоследнего врага", NULL},
    {"mvesh",     cmd_mvesh,     KOTO_LEVEL_TRUSTED, "Combat: голос в конкретного игрока", "<имя>"},
    {"mreg",      cmd_mreg,      KOTO_LEVEL_TRUSTED, "Регистрация всех аккаунтов в игре", "<ссылка/старт-параметр>"},
    {"mbvesh",    cmd_mbvesh,    KOTO_LEVEL_TRUSTED, "Baku: голос за игрока через ЛС", "<ник>"},
    {"mblike",    cmd_mblike,    KOTO_LEVEL_TRUSTED, "Baku: подтвердить линч (лайк)", NULL},
    {"mbdis",     cmd_mbdis,     KOTO_LEVEL_TRUSTED, "Baku: оправдать (дизлайк)", NULL},
    {"mrole",     cmd_mrole,     KOTO_LEVEL_TRUSTED, "Скан ролей из ЛС в лог-чат", "[combat|true|baku|koto|oso]"},
    {"mlike",     cmd_mlike,     KOTO_LEVEL_TRUSTED, "Combat/Oso/True/Koto: подтвердить линч", "[oso|true|koto]"},
    {"mdis",      cmd_mdis,      KOTO_LEVEL_TRUSTED, "Combat/Oso/True/Koto: оправдать", "[oso|true|koto]"},
    {"mprofile",  cmd_mprofile,  KOTO_LEVEL_TRUSTED, "Отправить /profile в Combat со всех акк.", NULL},
    {"mtrue",     cmd_mtrue,     KOTO_LEVEL_TRUSTED, "Вкл/выкл TrueMafia режим", "<on/off>"},
    {"mtvesh",    cmd_mtvesh,    KOTO_LEVEL_TRUSTED, "TrueMafia: голос за игрока", "<ник>"},
    {"mkvesh",    cmd_mkvesh,    KOTO_LEVEL_TRUSTED, "KotoMafia: голос за игрока через ЛС", "<ник>"},
    {"mkgraf",    cmd_mkgraf,    KOTO_LEVEL_TRUSTED, "KotoMafia: поиск не проголосовавших", NULL},
    {"mbaku",     cmd_mbaku,     KOTO_LEVEL_TRUSTED, "Baku: поиск не проголосовавших", "[red/blue]"},
    {"movesh",    cmd_movesh,    KOTO_LEVEL_TRUSTED, "Mafioso: голос за игрока через ЛС", "<ник/номер/UID/callback>"},
    {"mosh",      cmd_movesh,    KOTO_LEVEL_TRUSTED, "Mafioso: голос за игрока (алиас .movesh)", "<ник/номер/UID/callback>"}
};

// ── Обработчик нажатий inline-кнопок (on_callback) ───────────────────────────
static void on_callback(koto_ctx* ctx, const koto_api* api) {
    const char* data_ptr = api->cb_data(ctx);
    if (!data_ptr || strncmp(data_ptr, "mfz:", 4) != 0) return;

    long long sender = api->sender_id ? api->sender_id(ctx) : 0;
    long long me = api->get_me ? api->get_me(ctx) : 0;

    bool allowed = (me != 0 && sender == me);
    if (!allowed && api->check_permission) {
        allowed = (api->check_permission(ctx, KOTO_LEVEL_TRUSTED) != 0);
    }

    if (!allowed) {
        api->cb_answer(ctx, "⛔ Эта панель только для владельца!", 1);
        return;
    }
    std::string data = data_ptr;

    if (data == "mfz:close") {
        api->cb_answer(ctx, "Закрыто", 0);
        api->delete_msg(ctx);
        return;
    }

    if (data.rfind("mfz:toggle:", 0) == 0) {
        std::string target = data.substr(11);
        Settings s = load_settings(ctx, api);
        bool now_frozen = false;
        if (s.frozen_twins.count(target) > 0) {
            s.frozen_twins.erase(target);
            now_frozen = false;
        } else {
            s.frozen_twins.insert(target);
            now_frozen = true;
        }
        save_settings(ctx, api, s);

        std::string toast = target + (now_frozen ? " заморожен ❄️" : " разморожен 🟢");
        api->cb_answer(ctx, toast.c_str(), 0);

        render_mfreeze(ctx, api, s);
        api->c_edit(ctx, 0);
        return;
    }
}

static const koto_module MAFIA_MODULE = {
    KOTO_MODULE_ABI,
    MODULE_NAME,
    "Мафия Мульти-Тул — боевой комбайн для мафиозных ботов (Combat, TrueMafia, Baku, Koto, Mafioso).",
    "1.3.11",
    28, // min_host_version 28 (0.3.8+)
    0,
    MAFIA_COMMANDS,
    sizeof(MAFIA_COMMANDS) / sizeof(MAFIA_COMMANDS[0]),
    on_watch,
    KOTO_WATCH_ALL,
    on_callback, // on_callback
    nullptr, // settings
    0,
    "https://t.me/KoteModulesMint"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MAFIA_MODULE;
}
