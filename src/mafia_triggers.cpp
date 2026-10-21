// mafia_triggers.cpp — Триггеры для Мафии (Warn/Mute за AFK и выход из игры) (C++ ABI 3).
// Портирован из Python-модуля mafia_triggers.py (v2.2.0) с полным сохранением логики:
// контекстная сегментация упоминаний (_entity_segments), наказание каждого нарушителя,
// поддержка @username и tg://user?id=, авто-распознавание всех ботов Мафии (включая Mafia Baku Black),
// кастомизация длительности мута (.mtrtime / cfg_mute_time) и текста/пункта правил за AFK и выход.
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
#include <cstdint>

namespace {

// ID известных ботов Мафии (True Mafia, Mafia Baku, Mafioso, KoteMafia и др.)
static const long long MAFIA_BOTS[] = {
    606933972LL, 1044037207LL, 1050428643LL, 761250017LL, 468253535LL,
    1520369962LL, 6981158759LL, 1634167847LL, 5424831786LL, 7199004377LL
};

// Триггеры "Сна" (AFK)
static const char* TRIGGERS_AFK[] = {
    "бу-у-у-у-ду", "пообещал", "сон", "просыпаться", "qichqirganini",
    "умер от скуки", "afk"
};

// Триггеры "Выхода" (Leave)
static const char* TRIGGERS_LEAVE[] = {
    "гнетущей", "убежал", "прыгнул", "повесился", "не выдержал", "вышел"
};

static const int CONTEXT_PREFIX = 60;
static const int CONTEXT_SUFFIX = 160;

// ── Преобразования UTF-8 <-> UTF-16 ─────────────────────────────────────────

static std::vector<uint16_t> utf8_to_utf16(const std::string& s) {
    std::vector<uint16_t> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp; int len;
        if (c < 0x80)             { cp = c;        len = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; len = 2; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; len = 3; }
        else if ((c >> 3) == 0x1E){ cp = c & 0x07; len = 4; }
        else                      { cp = 0xFFFD;   len = 1; }
        for (int k = 1; k < len && i + k < n; ++k)
            cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        i += len;
        if (cp <= 0xFFFF) out.push_back((uint16_t)cp);
        else {
            cp -= 0x10000;
            out.push_back((uint16_t)(0xD800 + (cp >> 10)));
            out.push_back((uint16_t)(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

static void append_cp_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out.push_back((char)cp);
    else if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

static std::string utf16_slice_to_utf8(const std::vector<uint16_t>& u, int off, int len) {
    std::string out;
    int end = off + len, sz = (int)u.size();
    for (int i = off; i < end && i < sz; ) {
        uint32_t cp = u[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < end && i + 1 < sz) {
            uint32_t lo = u[i + 1];
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            i += 2;
        } else i += 1;
        append_cp_utf8(out, cp);
    }
    return out;
}

static std::string utf8_to_lower(const std::string& str) {
    std::string res;
    res.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        if (c < 0x80) {
            res += static_cast<char>(tolower(c));
        } else if (c == 0xD0 && i + 1 < str.size()) {
            unsigned char c2 = static_cast<unsigned char>(str[i + 1]);
            if (c2 >= 0x90 && c2 <= 0x9F) {
                res += static_cast<char>(0xD0);
                res += static_cast<char>(c2 + 0x20);
                i++;
            } else if (c2 >= 0xA0 && c2 <= 0xAF) {
                res += static_cast<char>(0xD1);
                res += static_cast<char>(c2 - 0x20);
                i++;
            } else if (c2 == 0x81) {
                res += static_cast<char>(0xD1);
                res += static_cast<char>(0x91);
                i++;
            } else {
                res += str[i];
            }
        } else {
            res += str[i];
        }
    }
    return res;
}

// ── Определение бота Мафии ───────────────────────────────────────────────────

static bool is_mafia_bot(koto_ctx* ctx, const koto_api* api, long long uid, const std::string& text_lower) {
    if (uid <= 0) return false;
    long long me_id = api->get_me ? api->get_me(ctx) : 0;
    if (uid == me_id) return false;

    // 1. Статический список ID (True Mafia, Mafia Baku Black, Mafioso, KoteMafia и др.)
    for (long long b : MAFIA_BOTS) {
        if (b == uid) return true;
    }

    // 2. Юзернейм отправителя
    char ubuf[128] = {0};
    if (api->user_username && api->user_username(ctx, uid, ubuf, sizeof(ubuf)) > 0) {
        std::string uname = koto_utils::trim(ubuf);
        for (char& c : uname) c = (char)tolower((unsigned char)c);
        if (uname == "mafiabakublackbot" || uname == "mafiabakublack1bot" ||
            uname == "combatmafiasobot" || uname == "truemafiablackbot" ||
            uname == "kotemafiabot" || uname == "mafiososbot" || uname == "mafiozi") {
            return true;
        }
        if ((uname.find("mafia") != std::string::npos || uname.find("baku") != std::string::npos) &&
            uname.size() >= 3 && uname.substr(uname.size() - 3) == "bot") {
            return true;
        }
    }

    // 3. Динамический резолв известных имен ботов Мафии
    if (api->resolve) {
        const char* known_usernames[] = {
            "MafiaBakuBlackBot", "MafiaBakuBlack1Bot", "TrueMafiaBlackBot",
            "CombatMafiaSOBot", "KoteMafiaBot", "MafiososBot"
        };
        for (const char* ku : known_usernames) {
            long long resolved = api->resolve(ctx, ku);
            if (resolved > 0 && resolved == uid) return true;
        }
    }

    // 4. Проверка имени бота: если имя содержит "Mafia" или "Мафия" и в юзернейме/данных это бот
    char fbuf[256] = {0};
    if (api->user_first_name && api->user_first_name(ctx, uid, fbuf, sizeof(fbuf)) > 0) {
        std::string fname = utf8_to_lower(fbuf);
        if (fname.find("mafia baku") != std::string::npos ||
            fname.find("true mafia") != std::string::npos ||
            fname.find("combat mafia") != std::string::npos) {
            return true;
        }
    }

    return false;
}

// ── База данных и настройки ──────────────────────────────────────────────────

static std::string get_cfg_str(const koto_api* api, koto_ctx* ctx, const char* key, const char* def) {
    char buf[512] = {0};
    if (api->db_get && api->db_get(ctx, key, buf, sizeof(buf) - 1) > 0) {
        std::string val = koto_utils::trim(buf);
        if (!val.empty()) return val;
    }
    return def ? def : "";
}

static void set_cfg_str(const koto_api* api, koto_ctx* ctx, const char* key, const std::string& val) {
    if (api->db_set) {
        api->db_set(ctx, key, val.c_str());
    }
}

static bool chat_in_set(const std::set<long long>& s, long long cid) {
    if (s.empty() || cid == 0) return false;
    if (s.find(cid) != s.end()) return true;
    long long abs_id = (cid < 0) ? -cid : cid;
    if (s.find(abs_id) != s.end()) return true;
    if (s.find(-abs_id) != s.end()) return true;

    std::string str_id = std::to_string(cid);
    if (str_id.rfind("-100", 0) == 0 && str_id.size() > 4) {
        try {
            long long bare = std::stoll(str_id.substr(4));
            if (s.find(bare) != s.end() || s.find(-bare) != s.end()) return true;
        } catch (...) {}
    } else if (cid > 0) {
        try {
            long long full = -std::stoll("100" + str_id);
            if (s.find(full) != s.end()) return true;
        } catch (...) {}
    }
    return false;
}

static std::set<long long> load_chat_set(const koto_api* api, koto_ctx* ctx, const char* key) {
    std::set<long long> s;
    char buf[16384] = {0};
    int n = api->db_get ? api->db_get(ctx, key, buf, sizeof(buf) - 1) : -1;
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
    if (!api->db_set) return;
    std::string out;
    for (long long id : s) {
        out += std::to_string(id) + "\n";
    }
    api->db_set(ctx, key, out.c_str());
}

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

// ── Отрисовка интерактивной панели настроек ──────────────────────────────────

static void render_settings_panel(koto_ctx* ctx, const koto_api* api, long long cid) {
    std::set<long long> warn_chats = load_chat_set(api, ctx, "warn_chats");
    std::set<long long> mute_chats = load_chat_set(api, ctx, "mute_chats");
    bool has_warn = chat_in_set(warn_chats, cid);
    bool has_mute = chat_in_set(mute_chats, cid);

    std::string mute_time = get_cfg_str(api, ctx, "cfg_mute_time", "2h");
    std::string afk_reason = get_cfg_str(api, ctx, "cfg_afk_reason", "сон");
    std::string leave_reason = get_cfg_str(api, ctx, "cfg_leave_reason", "выход");

    api->c_reset(ctx);
    api->c_fmt(ctx, "⚙️ Панель настроек MafiaTriggers v2.2.0\n\n", KOTO_ENT_BOLD);

    api->c_text(ctx, "• Мут в текущем чате: ");
    api->c_fmt(ctx, has_mute ? "⚪️ Включен\n" : "⚫️ Выключен\n", KOTO_ENT_BOLD);

    api->c_text(ctx, "• Варн в текущем чате: ");
    api->c_fmt(ctx, has_warn ? "⚪️ Включен\n" : "⚫️ Выключен\n", KOTO_ENT_BOLD);

    api->c_text(ctx, "• Длительность мута: ");
    api->c_fmt(ctx, (mute_time + "\n").c_str(), KOTO_ENT_CODE);

    api->c_text(ctx, "• Причина за AFK: ");
    api->c_fmt(ctx, (afk_reason + "\n").c_str(), KOTO_ENT_CODE);

    api->c_text(ctx, "• Причина за Выход: ");
    api->c_fmt(ctx, (leave_reason + "\n\n").c_str(), KOTO_ENT_CODE);

    api->c_fmt(ctx, "💡 Выберите настройку кнопками ниже:", KOTO_ENT_ITALIC);

    if (api->c_button) {
        // Ряд 1: Вкл/выкл триггеров
        std::string mtr_btn = has_mute ? "⚫️ Выкл Mute" : "⚪️ Вкл Mute";
        std::string wtr_btn = has_warn ? "⚫️ Выкл Warn" : "⚪️ Вкл Warn";
        api->c_button(ctx, mtr_btn.c_str(), "mtr:togglemute", 1);
        api->c_button(ctx, wtr_btn.c_str(), "mtr:togglewarn", 0);

        // Ряд 2: Быстрый выбор длительности мута
        api->c_button(ctx, (mute_time == "30m" ? "✅ 30м" : "⏱ 30м"), "mtr:time:30m", 1);
        api->c_button(ctx, (mute_time == "1h"  ? "✅ 1ч"  : "⏱ 1ч"),  "mtr:time:1h",  0);
        api->c_button(ctx, (mute_time == "2h"  ? "✅ 2ч"  : "⏱ 2ч"),  "mtr:time:2h",  0);
        api->c_button(ctx, (mute_time == "4h"  ? "✅ 4ч"  : "⏱ 4ч"),  "mtr:time:4h",  0);
        api->c_button(ctx, (mute_time == "1d"  ? "✅ 1д"  : "⏱ 1д"),  "mtr:time:1d",  0);

        // Ряд 3: Быстрый выбор причины AFK
        api->c_button(ctx, (afk_reason == "сон"     ? "✅ 'сон'"     : "💤 'сон'"),     "mtr:afk:сон",     1);
        api->c_button(ctx, (afk_reason == "1.2 АФК" ? "✅ '1.2 АФК'" : "💤 '1.2 АФК'"), "mtr:afk:1.2 АФК", 0);
        api->c_button(ctx, (afk_reason == "AFK"     ? "✅ 'AFK'"     : "💤 'AFK'"),     "mtr:afk:AFK",     0);

        // Ряд 4: Причина выхода и Сброс
        api->c_button(ctx, (leave_reason == "выход"   ? "✅ 'выход'"   : "🚪 'выход'"),   "mtr:leave:выход",   1);
        api->c_button(ctx, (leave_reason == "1.3 Лив" ? "✅ '1.3 Лив'" : "🚪 '1.3 Лив'"), "mtr:leave:1.3 Лив", 0);
        api->c_button(ctx, "🔄 Сброс", "mtr:reset", 0);

        // Ряд 5: Закрыть
        api->c_button(ctx, "❌ Закрыть", "mtr:close", 1);
    }
}

// ── .wtr (Warn Trigger) ───────────────────────────────────────────────────────

static void cmd_wtr(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    if (cid == 0) return;

    std::set<long long> chats = load_chat_set(api, ctx, "warn_chats");
    bool active = false;
    if (chat_in_set(chats, cid)) {
        chats.erase(cid);
        chats.erase(-cid);
        long long abs_id = (cid < 0) ? -cid : cid;
        chats.erase(abs_id);
        std::string s = std::to_string(cid);
        if (s.rfind("-100", 0) == 0 && s.size() > 4) {
            try { chats.erase(std::stoll(s.substr(4))); } catch (...) {}
        }
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
    if (chat_in_set(chats, cid)) {
        chats.erase(cid);
        chats.erase(-cid);
        long long abs_id = (cid < 0) ? -cid : cid;
        chats.erase(abs_id);
        std::string s = std::to_string(cid);
        if (s.rfind("-100", 0) == 0 && s.size() > 4) {
            try { chats.erase(std::stoll(s.substr(4))); } catch (...) {}
        }
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

// ── .mtrcfg / .mtrset (Настройки модуля) ──────────────────────────────────────

static void cmd_mtrcfg(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::string args;
    if (api->cmd_args) {
        const char* raw = api->cmd_args(ctx);
        if (raw) args = koto_utils::trim(raw);
    }
    if (args.empty() && api->msg_text) {
        std::string full = api->msg_text(ctx);
        size_t sp = full.find(' ');
        if (sp != std::string::npos) args = koto_utils::trim(full.substr(sp + 1));
    }

    long long cid = api->chat_id(ctx);

    if (args.empty()) {
        std::set<long long> warn_chats = load_chat_set(api, ctx, "warn_chats");
        std::set<long long> mute_chats = load_chat_set(api, ctx, "mute_chats");
        bool has_warn = chat_in_set(warn_chats, cid);
        bool has_mute = chat_in_set(mute_chats, cid);

        std::string mute_time = get_cfg_str(api, ctx, "cfg_mute_time", "2h");
        std::string afk_reason = get_cfg_str(api, ctx, "cfg_afk_reason", "сон");
        std::string leave_reason = get_cfg_str(api, ctx, "cfg_leave_reason", "выход");

        api->c_reset(ctx);
        api->c_fmt(ctx, "⚙️ Настройки MafiaTriggers v2.2.0\n\n", KOTO_ENT_BOLD);

        api->c_text(ctx, "• Мут в текущем чате: ");
        api->c_fmt(ctx, has_mute ? "⚪️ Включен\n" : "⚫️ Выключен\n", KOTO_ENT_BOLD);

        api->c_text(ctx, "• Варн в текущем чате: ");
        api->c_fmt(ctx, has_warn ? "⚪️ Включен\n" : "⚫️ Выключен\n", KOTO_ENT_BOLD);

        api->c_text(ctx, "• Длительность мута: ");
        api->c_fmt(ctx, (mute_time + "\n").c_str(), KOTO_ENT_CODE);

        api->c_text(ctx, "• Причина за AFK: ");
        api->c_fmt(ctx, (afk_reason + "\n").c_str(), KOTO_ENT_CODE);

        api->c_text(ctx, "• Причина за Выход: ");
        api->c_fmt(ctx, (leave_reason + "\n\n").c_str(), KOTO_ENT_CODE);

        api->c_fmt(ctx, "💡 Команды управления:\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".mtrcfg time <30m|1h|2h|4h|1d>", KOTO_ENT_CODE);
        api->c_text(ctx, " — время мута\n");
        api->c_fmt(ctx, ".mtrcfg reason <причина>", KOTO_ENT_CODE);
        api->c_text(ctx, " — причина/пункт правил за AFK (напр. 1.2 АФК)\n");
        api->c_fmt(ctx, ".mtrcfg leave <причина>", KOTO_ENT_CODE);
        api->c_text(ctx, " — причина за лив из игры (напр. 1.3 Лив)\n");
        api->c_fmt(ctx, ".mtrcfg reset", KOTO_ENT_CODE);
        api->c_text(ctx, " — сброс на умолчания (2h, сон, выход)\n");
        api->c_fmt(ctx, ".mtrsettings", KOTO_ENT_CODE);
        api->c_text(ctx, " — открыть интерактивную панель с кнопками");

        send_result(ctx, api);
        return;
    }

    std::string lower = utf8_to_lower(args);

    if (lower == "reset") {
        set_cfg_str(api, ctx, "cfg_mute_time", "2h");
        set_cfg_str(api, ctx, "cfg_afk_reason", "сон");
        set_cfg_str(api, ctx, "cfg_leave_reason", "выход");
        api->c_reset(ctx);
        api->c_text(ctx, "✅ ");
        api->c_fmt(ctx, "Настройки сброшены: мут 2h, AFK 'сон', выход 'выход'", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    if (lower.rfind("time ", 0) == 0 || lower.rfind("t ", 0) == 0) {
        size_t sp = args.find(' ');
        std::string val = koto_utils::trim(args.substr(sp + 1));
        if (val.empty()) val = "2h";
        set_cfg_str(api, ctx, "cfg_mute_time", val);
        api->c_reset(ctx);
        api->c_text(ctx, "✅ ");
        api->c_fmt(ctx, "Длительность мута установлена: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, val.c_str(), KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

    if (lower.rfind("reason ", 0) == 0 || lower.rfind("afk ", 0) == 0 || lower.rfind("r ", 0) == 0) {
        size_t sp = args.find(' ');
        std::string val = koto_utils::trim(args.substr(sp + 1));
        if (val.empty()) val = "сон";
        set_cfg_str(api, ctx, "cfg_afk_reason", val);
        api->c_reset(ctx);
        api->c_text(ctx, "✅ ");
        api->c_fmt(ctx, "Причина за AFK установлена: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, val.c_str(), KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

    if (lower.rfind("leave ", 0) == 0 || lower.rfind("l ", 0) == 0) {
        size_t sp = args.find(' ');
        std::string val = koto_utils::trim(args.substr(sp + 1));
        if (val.empty()) val = "выход";
        set_cfg_str(api, ctx, "cfg_leave_reason", val);
        api->c_reset(ctx);
        api->c_text(ctx, "✅ ");
        api->c_fmt(ctx, "Причина за Выход установлена: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, val.c_str(), KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

    api->c_reset(ctx);
    api->c_text(ctx, "ℹ️ ");
    api->c_fmt(ctx, "Использование: .mtrcfg [time <время> | reason <причина> | leave <причина> | reset]", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// ── .mtrtime <время> ─────────────────────────────────────────────────────────

static void cmd_mtrtime(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::string args;
    if (api->cmd_args) {
        const char* raw = api->cmd_args(ctx);
        if (raw) args = koto_utils::trim(raw);
    }
    if (args.empty() && api->msg_text) {
        std::string full = api->msg_text(ctx);
        size_t sp = full.find(' ');
        if (sp != std::string::npos) args = koto_utils::trim(full.substr(sp + 1));
    }

    if (args.empty()) {
        std::string cur = get_cfg_str(api, ctx, "cfg_mute_time", "2h");
        api->c_reset(ctx);
        api->c_fmt(ctx, "⏱ Текущая длительность мута: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, cur.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\nДля изменения: .mtrtime 30m / 1h / 2h / 4h / 1d");
        send_result(ctx, api);
        return;
    }

    set_cfg_str(api, ctx, "cfg_mute_time", args);
    api->c_reset(ctx);
    api->c_text(ctx, "✅ ");
    api->c_fmt(ctx, "Длительность мута установлена: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, args.c_str(), KOTO_ENT_CODE);
    send_result(ctx, api);
}

// ── .mtrreason <причина> ─────────────────────────────────────────────────────

static void cmd_mtrreason(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::string args;
    if (api->cmd_args) {
        const char* raw = api->cmd_args(ctx);
        if (raw) args = koto_utils::trim(raw);
    }
    if (args.empty() && api->msg_text) {
        std::string full = api->msg_text(ctx);
        size_t sp = full.find(' ');
        if (sp != std::string::npos) args = koto_utils::trim(full.substr(sp + 1));
    }

    if (args.empty()) {
        std::string cur = get_cfg_str(api, ctx, "cfg_afk_reason", "сон");
        api->c_reset(ctx);
        api->c_fmt(ctx, "💤 Текущая причина за AFK: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, cur.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\nДля изменения: .mtrreason 1.2 АФК или сон");
        send_result(ctx, api);
        return;
    }

    set_cfg_str(api, ctx, "cfg_afk_reason", args);
    api->c_reset(ctx);
    api->c_text(ctx, "✅ ");
    api->c_fmt(ctx, "Причина за AFK установлена: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, args.c_str(), KOTO_ENT_CODE);
    send_result(ctx, api);
}

// ── .mtrleave <причина> ──────────────────────────────────────────────────────

static void cmd_mtrleave(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::string args;
    if (api->cmd_args) {
        const char* raw = api->cmd_args(ctx);
        if (raw) args = koto_utils::trim(raw);
    }
    if (args.empty() && api->msg_text) {
        std::string full = api->msg_text(ctx);
        size_t sp = full.find(' ');
        if (sp != std::string::npos) args = koto_utils::trim(full.substr(sp + 1));
    }

    if (args.empty()) {
        std::string cur = get_cfg_str(api, ctx, "cfg_leave_reason", "выход");
        api->c_reset(ctx);
        api->c_fmt(ctx, "🚪 Текущая причина за Выход: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, cur.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\nДля изменения: .mtrleave 1.3 Лив или выход");
        send_result(ctx, api);
        return;
    }

    set_cfg_str(api, ctx, "cfg_leave_reason", args);
    api->c_reset(ctx);
    api->c_text(ctx, "✅ ");
    api->c_fmt(ctx, "Причина за Выход установлена: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, args.c_str(), KOTO_ENT_CODE);
    send_result(ctx, api);
}

// ── .mtrsettings (Интерактивная панель) ──────────────────────────────────────

static void cmd_mtrsettings(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    long long cid = api->chat_id(ctx);
    render_settings_panel(ctx, api, cid);
    send_result(ctx, api);
}

// ── Inline Callback обработчик (on_callback) ─────────────────────────────────

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    const char* data_raw = api->cb_data ? api->cb_data(ctx) : nullptr;
    if (!data_raw) return;
    std::string data = data_raw;
    if (data.rfind("mtr:", 0) != 0) return;

    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) {
        if (api->cb_answer) api->cb_answer(ctx, "Недостаточно прав", 1);
        return;
    }

    long long cid = api->chat_id(ctx);

    if (data == "mtr:close") {
        if (api->cb_answer) api->cb_answer(ctx, "Закрыто", 0);
        if (api->delete_msg) api->delete_msg(ctx);
        return;
    }

    if (data == "mtr:togglemute") {
        std::set<long long> chats = load_chat_set(api, ctx, "mute_chats");
        if (chat_in_set(chats, cid)) {
            chats.erase(cid);
            chats.erase(-cid);
            long long abs_id = (cid < 0) ? -cid : cid;
            chats.erase(abs_id);
            std::string s = std::to_string(cid);
            if (s.rfind("-100", 0) == 0 && s.size() > 4) {
                try { chats.erase(std::stoll(s.substr(4))); } catch (...) {}
            }
            if (api->cb_answer) api->cb_answer(ctx, "Mute выключен", 0);
        } else {
            chats.insert(cid);
            if (api->cb_answer) api->cb_answer(ctx, "Mute включен", 0);
        }
        save_chat_set(api, ctx, "mute_chats", chats);
    } else if (data == "mtr:togglewarn") {
        std::set<long long> chats = load_chat_set(api, ctx, "warn_chats");
        if (chat_in_set(chats, cid)) {
            chats.erase(cid);
            chats.erase(-cid);
            long long abs_id = (cid < 0) ? -cid : cid;
            chats.erase(abs_id);
            std::string s = std::to_string(cid);
            if (s.rfind("-100", 0) == 0 && s.size() > 4) {
                try { chats.erase(std::stoll(s.substr(4))); } catch (...) {}
            }
            if (api->cb_answer) api->cb_answer(ctx, "Warn выключен", 0);
        } else {
            chats.insert(cid);
            if (api->cb_answer) api->cb_answer(ctx, "Warn включен", 0);
        }
        save_chat_set(api, ctx, "warn_chats", chats);
    } else if (data.rfind("mtr:time:", 0) == 0) {
        std::string val = data.substr(9);
        set_cfg_str(api, ctx, "cfg_mute_time", val);
        if (api->cb_answer) api->cb_answer(ctx, ("Мут: " + val).c_str(), 0);
    } else if (data.rfind("mtr:afk:", 0) == 0) {
        std::string val = data.substr(8);
        set_cfg_str(api, ctx, "cfg_afk_reason", val);
        if (api->cb_answer) api->cb_answer(ctx, ("AFK: " + val).c_str(), 0);
    } else if (data.rfind("mtr:leave:", 0) == 0) {
        std::string val = data.substr(10);
        set_cfg_str(api, ctx, "cfg_leave_reason", val);
        if (api->cb_answer) api->cb_answer(ctx, ("Выход: " + val).c_str(), 0);
    } else if (data == "mtr:reset") {
        set_cfg_str(api, ctx, "cfg_mute_time", "2h");
        set_cfg_str(api, ctx, "cfg_afk_reason", "сон");
        set_cfg_str(api, ctx, "cfg_leave_reason", "выход");
        if (api->cb_answer) api->cb_answer(ctx, "Настройки сброшены", 0);
    }

    render_settings_panel(ctx, api, cid);
    if (api->c_edit) api->c_edit(ctx, 0);
}

// ── Вспомогательные структуры упоминаний ─────────────────────────────────────

struct MentionEnt {
    int type;              // KOTO_ENT_URL или KOTO_ENT_MENTION
    int off;               // Смещение в UTF-16
    int len;               // Длина в UTF-16
    long long target_id;   // Численный ID (если tg://user?id=)
    std::string username;  // @username (если KOTO_ENT_MENTION)
};

// ── Watcher: слежка за ботами Мафии ──────────────────────────────────────────

static int on_watch(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) return 0;

    const char* text_raw = api->msg_text(ctx);
    if (!text_raw || text_raw[0] == '\0') return 0;
    std::string text = text_raw;
    std::string text_lower = utf8_to_lower(text);

    // 1. Проверяем, от бота ли сообщение
    long long sender = api->sender_id(ctx);
    if (!is_mafia_bot(ctx, api, sender, text_lower)) return 0;

    // 2. Проверяем, включен ли модуль в текущем чате
    long long cid = api->chat_id(ctx);
    if (cid == 0) return 0;

    std::set<long long> warn_chats = load_chat_set(api, ctx, "warn_chats");
    std::set<long long> mute_chats = load_chat_set(api, ctx, "mute_chats");

    bool has_warn = chat_in_set(warn_chats, cid);
    bool has_mute = chat_in_set(mute_chats, cid);
    if (!has_warn && !has_mute) return 0;

    // 3. Быстрый фильтр триггерных слов
    bool has_any_trigger = false;
    for (const char* w : TRIGGERS_AFK) {
        if (text_lower.find(w) != std::string::npos) { has_any_trigger = true; break; }
    }
    if (!has_any_trigger) {
        for (const char* w : TRIGGERS_LEAVE) {
            if (text_lower.find(w) != std::string::npos) { has_any_trigger = true; break; }
        }
    }
    if (!has_any_trigger) return 0;

    // Считываем пользовательские настройки длительности и причин наказания
    std::string mute_time = get_cfg_str(api, ctx, "cfg_mute_time", "2h");
    std::string afk_reason = get_cfg_str(api, ctx, "cfg_afk_reason", "сон");
    std::string leave_reason = get_cfg_str(api, ctx, "cfg_leave_reason", "выход");

    // 4. Сбор сущностей упоминаний сообщения
    std::vector<uint16_t> u16 = utf8_to_utf16(text);
    std::vector<MentionEnt> mentions;

    if (api->msg_entity_count && api->msg_entity) {
        int cnt = api->msg_entity_count(ctx);
        for (int i = 0; i < cnt; ++i) {
            int type = 0, off = 0, len = 0;
            long long doc_id = 0;
            char url_buf[512] = {0};
            if (api->msg_entity(ctx, i, &type, &off, &len, &doc_id, url_buf, sizeof(url_buf)) > 0) {
                if (type == KOTO_ENT_URL) {
                    long long tid = 0;
                    if (strncmp(url_buf, "tg://user?id=", 13) == 0) {
                        tid = strtoll(url_buf + 13, nullptr, 10);
                    } else {
                        const char* p = strstr(url_buf, "user?id=");
                        if (p) tid = strtoll(p + 8, nullptr, 10);
                    }
                    if (tid > 0) {
                        mentions.push_back({type, off, len, tid, ""});
                    }
                } else if (type == KOTO_ENT_MENTION) {
                    std::string uname = utf16_slice_to_utf8(u16, off, len);
                    if (!uname.empty() && uname.front() == '@') uname = uname.substr(1);
                    long long tid = 0;
                    if (api->resolve && !uname.empty()) {
                        tid = api->resolve(ctx, uname.c_str());
                    }
                    mentions.push_back({type, off, len, tid, uname});
                }
            }
        }
    }

    // Если entity не обнаружены, резервный парсер tg://user?id= прямо из текста
    if (mentions.empty()) {
        size_t pos = 0;
        while ((pos = text.find("tg://user?id=", pos)) != std::string::npos) {
            size_t id_start = pos + 13;
            size_t id_end = id_start;
            while (id_end < text.size() && isdigit((unsigned char)text[id_end])) {
                id_end++;
            }
            if (id_end > id_start) {
                long long tid = strtoll(text.substr(id_start, id_end - id_start).c_str(), nullptr, 10);
                if (tid > 0) {
                    int u16_off = (int)utf8_to_utf16(text.substr(0, pos)).size();
                    int u16_len = (int)utf8_to_utf16(text.substr(pos, id_end - pos)).size();
                    mentions.push_back({KOTO_ENT_URL, u16_off, u16_len, tid, ""});
                }
            }
            pos = id_end;
        }
    }

    if (mentions.empty()) return 0;

    // Сортируем упоминания по смещению для изоляции локальных контекстов
    std::sort(mentions.begin(), mentions.end(), [](const MentionEnt& a, const MentionEnt& b) {
        return a.off < b.off;
    });

    // 5. Контекстная сегментация (аналог _entity_segments из Python)
    // Находим нарушение для КАЖДОГО упомянутого игрока в его собственном абзаце
    std::vector<std::pair<long long, std::string>> to_punish;

    for (size_t i = 0; i < mentions.size(); ++i) {
        int prev_end = (i > 0) ? (mentions[i - 1].off + mentions[i - 1].len) : 0;
        int next_start = (i + 1 < mentions.size()) ? mentions[i + 1].off : (int)u16.size();

        int seg_start = std::max(prev_end, mentions[i].off - CONTEXT_PREFIX);
        int seg_end = std::min(next_start, mentions[i].off + mentions[i].len + CONTEXT_SUFFIX);

        if (seg_start < 0) seg_start = 0;
        if (seg_end > (int)u16.size()) seg_end = (int)u16.size();
        if (seg_start > seg_end) seg_start = seg_end;

        std::string segment = utf16_slice_to_utf8(u16, seg_start, seg_end - seg_start);
        std::string seg_lower = utf8_to_lower(segment);

        std::string reason;
        bool matched = false;

        for (const char* w : TRIGGERS_AFK) {
            if (seg_lower.find(w) != std::string::npos) {
                matched = true;
                reason = afk_reason;
                break;
            }
        }
        if (!matched) {
            for (const char* w : TRIGGERS_LEAVE) {
                if (seg_lower.find(w) != std::string::npos) {
                    matched = true;
                    reason = leave_reason;
                    break;
                }
            }
        }

        if (!matched) continue;

        long long tid = mentions[i].target_id;
        if (tid == 0 && !mentions[i].username.empty() && api->resolve) {
            tid = api->resolve(ctx, mentions[i].username.c_str());
        }

        if (tid > 0) {
            to_punish.push_back({tid, reason});
        }
    }

    if (to_punish.empty()) return 0;

    // 6. Дедупликация и отправка команд в чат отдельными сообщениями (как в .py через event.respond)
    std::set<std::pair<long long, std::string>> seen;

    for (const auto& item : to_punish) {
        if (seen.find(item) != seen.end()) continue;
        seen.insert(item);

        long long tid = item.first;
        const std::string& reason = item.second;

        // Если включен Warn — отправляем !warn
        if (has_warn) {
            std::string cmd = "!warn " + std::to_string(tid) + " " + reason;
            api->c_reset(ctx);
            api->c_text(ctx, cmd.c_str());
            api->c_send(ctx, cid, 0);
        }

        // Если включен Mute — отправляем !mute
        if (has_mute) {
            std::string cmd = "!mute " + std::to_string(tid) + " " + mute_time + " " + reason;
            api->c_reset(ctx);
            api->c_text(ctx, cmd.c_str());
            api->c_send(ctx, cid, 0);
        }
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
    },
    {
        "mtrcfg",
        &cmd_mtrcfg,
        KOTO_LEVEL_TRUSTED,
        "Настройки MafiaTriggers: время мута, причины AFK и лива.",
        "[time <время> | reason <причина> | leave <причина> | reset]"
    },
    {
        "mtrtime",
        &cmd_mtrtime,
        KOTO_LEVEL_TRUSTED,
        "Установить длительность мута (напр. .mtrtime 30m / 1h / 2h / 1d).",
        "<время>"
    },
    {
        "mtrreason",
        &cmd_mtrreason,
        KOTO_LEVEL_TRUSTED,
        "Установить причину/пункт правил за AFK (напр. .mtrreason 1.2 АФК).",
        "<причина>"
    },
    {
        "mtrleave",
        &cmd_mtrleave,
        KOTO_LEVEL_TRUSTED,
        "Установить причину/пункт правил за выход (напр. .mtrleave 1.3 Лив).",
        "<причина>"
    },
    {
        "mtrsettings",
        &cmd_mtrsettings,
        KOTO_LEVEL_TRUSTED,
        "Интерактивная панель настроек MafiaTriggers с кнопками.",
        nullptr
    }
};

static const koto_setting_field SETTINGS[] = {
    {"cfg_mute_time", "Длительность мута (напр. 30m, 1h, 2h, 1d)", KOTO_FIELD_STR, "2h", nullptr},
    {"cfg_afk_reason", "Причина AFK (текст или пункт правил, напр. 1.2 АФК)", KOTO_FIELD_STR, "сон", nullptr},
    {"cfg_leave_reason", "Причина Выхода (текст или пункт правил, напр. 1.3 Лив)", KOTO_FIELD_STR, "выход", nullptr}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "mafia_triggers",
    "Триггеры для Мафии: авто-наказание (Warn/Mute) за AFK и лив с выбором времени и правил (C++ ABI 3).",
    "2.2.0",
    26,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    &on_watch,
    KOTO_WATCH_INCOMING,
    &on_callback,
    SETTINGS,
    sizeof(SETTINGS) / sizeof(SETTINGS[0]),
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/mafia_triggers.so"
};

} // namespace

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
