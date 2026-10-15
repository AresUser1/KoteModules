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

static std::string normalize_nick(std::string nick) {
    std::string out;
    for (size_t i = 0; i < nick.size(); ++i) {
        unsigned char c = (unsigned char)nick[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ') {
            out += (char)std::tolower(c);
        } else if (c >= 0xC0) {
            out += (char)c;
        } else if (c >= 0x80) {
            out += (char)c;
        }
    }
    // Схлопываем пробелы
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
    if (s.tourney_chat_id == 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, "Лог-чат не задан.", KOTO_ENT_BOLD);
        api->c_text(ctx, " Установите: ");
        api->c_fmt(ctx, ".mset tourney", KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return false;
    }
    if (api->chat_id(ctx) != s.tourney_chat_id) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Команда разрешена только в лог-чате.");
        api->c_edit(ctx, 0);
        return false;
    }
    return true;
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
        api->c_text(ctx, "Примеры:\n  .mreg https://t.me/KoteMafiaBot?start=jointeam_-100123_orange\n"
                         "  .mreg https://t.me/CombatMafiaSOBot?start=abc123\n"
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
            }
        }
    } else {
        if (args.rfind("jointeam", 0) == 0 || args.rfind("join_", 0) == 0) {
            bot_username = KOTO_BOT;
        } else if (args.find("combat") != std::string::npos) {
            bot_username = COMBAT_BOT;
        }
    }

    auto accounts = get_active_accounts(ctx, api, s);
    api->c_reset(ctx);
    api->c_text(ctx, ("⏳ Регистрирую " + std::to_string(accounts.size()) + " аккаунтов в @" +
                      bot_username + " (" + start_param + ")...").c_str());
    api->c_edit(ctx, 0);

    long long bot_peer_id = api->resolve(ctx, bot_username.c_str());
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

// ── .mbvesh <ник> ────────────────────────────────────────────────────────────
static void cmd_mbvesh(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;

    const char* args_ptr = api->cmd_args(ctx);
    std::string target_raw = args_ptr ? trim(args_ptr) : "";
    if (target_raw.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Укажите ник: .mbvesh <ник>");
        api->c_edit(ctx, 0);
        return;
    }

    std::string target_norm = normalize_baku_btn(target_raw);
    auto accounts = get_active_accounts(ctx, api, s);

    api->c_reset(ctx);
    api->c_text(ctx, ("🗳 Baku: голос за " + target_raw + " (" + std::to_string(accounts.size()) + " акк.)...").c_str());
    api->c_edit(ctx, 0);

    long long bot_id = api->resolve(ctx, BAKU_BOT);
    int voted_count = 0;

    human_scatter(accounts, s, [&](const AccountInfo& acc) {
        char hbuf[65536];
        int hlen = api->twin_get_history_json(ctx, acc.uid, bot_id, 0, 30, 0, hbuf, sizeof(hbuf));
        if (hlen <= 0) return;
        auto msgs = parse_history_json(std::string(hbuf, hlen));
        for (const auto& m : msgs) {
            if (m.text.find("линчевать") == std::string::npos && m.text.find("виновных") == std::string::npos) {
                continue;
            }
            const MsgButton* best_btn = nullptr;
            int best_score = -1;
            for (const auto& btn : m.buttons) {
                std::string btn_norm = normalize_baku_btn(btn.text);
                int score = baku_match_score(target_norm, btn_norm);
                if (score > best_score) {
                    best_score = score;
                    best_btn = &btn;
                }
            }
            if (best_btn && best_score > 0) {
                api->twin_click_button(ctx, acc.uid, bot_id, m.id, best_btn->data.c_str());
                voted_count++;
                break;
            }
        }
    });

    api->c_reset(ctx);
    api->c_text(ctx, ("✅ Baku vesh: проголосовало " + std::to_string(voted_count) + " акк.").c_str());
    api->c_edit(ctx, 0);
}

// ── .mblike / .mbdis ─────────────────────────────────────────────────────────
static void baku_confirm(koto_ctx* ctx, const koto_api* api, const Settings& s, const std::string& action) {
    long long arena_id = s.arena_id ? s.arena_id : api->chat_id(ctx);
    auto accounts = get_active_accounts(ctx, api, s);

    api->c_reset(ctx);
    api->c_text(ctx, ("⏳ Baku " + (action == "like" ? std::string("👍") : std::string("👎")) + "...").c_str());
    api->c_edit(ctx, 0);

    int count = 0;
    human_scatter(accounts, s, [&](const AccountInfo& acc) {
        char hbuf[65536];
        int hlen = api->twin_get_history_json(ctx, acc.uid, arena_id, 0, 20, 0, hbuf, sizeof(hbuf));
        if (hlen <= 0) return;
        auto msgs = parse_history_json(std::string(hbuf, hlen));
        for (const auto& m : msgs) {
            if (m.text.find("Уверены") == std::string::npos && m.text.find("линчевать") == std::string::npos) {
                continue;
            }
            for (const auto& btn : m.buttons) {
                if (btn.data.rfind(action + ":", 0) == 0) {
                    api->twin_click_button(ctx, acc.uid, arena_id, m.id, btn.data.c_str());
                    count++;
                    return;
                }
            }
        }
    });

    api->c_reset(ctx);
    api->c_text(ctx, ("✅ Baku " + (action == "like" ? std::string("линч") : std::string("отмена")) +
                      ": " + std::to_string(count) + " акк.").c_str());
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

// ── .mrole <combat|true|baku|koto|oso> ───────────────────────────────────────
static void cmd_mrole(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;

    const char* args_ptr = api->cmd_args(ctx);
    std::string bot_choice = args_ptr ? to_lower(trim(args_ptr)) : "";

    std::string bot_username = COMBAT_BOT;
    std::string role_trigger = "Твоя роль в игре:";
    if (bot_choice == "combat") {
        bot_username = COMBAT_BOT;
        role_trigger = "Твоя роль в игре:";
    } else if (bot_choice == "true") {
        bot_username = TRUE_BOT;
        role_trigger = "Твоя роль в игре:";
    } else if (bot_choice == "baku") {
        bot_username = BAKU_BOT;
        role_trigger = "Твоя роль -";
    } else if (bot_choice == "koto") {
        bot_username = KOTO_BOT;
        role_trigger = "Твоя роль:";
    } else if (bot_choice == "oso") {
        bot_username = OSO_BOT;
        role_trigger = "Ты —";
    } else {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Укажите бота: .mrole <combat|true|baku|koto|oso>");
        api->c_edit(ctx, 0);
        return;
    }

    auto accounts = get_active_accounts(ctx, api, s);
    api->c_reset(ctx);
    api->c_text(ctx, ("🕵️ Сканирую роли из ЛС @" + bot_username + "...").c_str());
    api->c_edit(ctx, 0);

    long long bot_id = api->resolve(ctx, bot_username.c_str());
    int forwarded_count = 0;

    for (const auto& acc : accounts) {
        char hbuf[65536];
        int hlen = api->twin_get_history_json(ctx, acc.uid, bot_id, 0, 20, 0, hbuf, sizeof(hbuf));
        if (hlen <= 0) continue;
        auto msgs = parse_history_json(std::string(hbuf, hlen));
        for (const auto& m : msgs) {
            if (m.text.find(role_trigger) != std::string::npos) {
                int msg_id = m.id;
                api->twin_forward_messages(ctx, acc.uid, bot_id, &msg_id, 1, s.tourney_chat_id);
                forwarded_count++;
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                break;
            }
        }
    }

    api->c_reset(ctx);
    if (forwarded_count > 0) {
        api->c_text(ctx, ("✅ Роли пересланы в лог-чат с " + std::to_string(forwarded_count) + " акк.").c_str());
    } else {
        api->c_text(ctx, "❌ Свежие роли в ЛС бота не найдены.");
    }
    api->c_edit(ctx, 0);
}

// ── .mlike / .mdis ───────────────────────────────────────────────────────────
static void combat_or_bot_confirm(koto_ctx* ctx, const koto_api* api, const Settings& s, bool is_like, const std::string& target_bot) {
    long long arena = s.arena_id ? s.arena_id : api->chat_id(ctx);
    auto accounts = get_active_accounts(ctx, api, s);

    api->c_reset(ctx);
    api->c_text(ctx, ("⏳ Нажимаю " + (is_like ? std::string("👍") : std::string("👎")) + "...").c_str());
    api->c_edit(ctx, 0);

    int count = 0;
    human_scatter(accounts, s, [&](const AccountInfo& acc) {
        char hbuf[65536];
        int hlen = api->twin_get_history_json(ctx, acc.uid, arena, 0, 20, 0, hbuf, sizeof(hbuf));
        if (hlen <= 0) return;
        auto msgs = parse_history_json(std::string(hbuf, hlen));
        for (const auto& m : msgs) {
            for (const auto& btn : m.buttons) {
                bool match = false;
                if (target_bot == "oso") {
                    std::string pfx = is_like ? "like_" : "dislike_";
                    if (btn.data.rfind(pfx, 0) == 0) match = true;
                } else if (target_bot == "true") {
                    std::string sub = is_like ? "yes" : "no";
                    if (btn.data.find("lynch_confirm") != std::string::npos && btn.data.find(sub) != std::string::npos) match = true;
                } else if (target_bot == "koto") {
                    std::string suf = is_like ? ":like" : ":dislike";
                    if (btn.data.size() >= suf.size() && btn.data.compare(btn.data.size() - suf.size(), suf.size(), suf) == 0) match = true;
                } else {
                    // Combat default
                    std::string key = is_like ? "like_asilan" : "diss_asilan";
                    if (btn.data.find(key) != std::string::npos) match = true;
                }
                if (match) {
                    api->twin_click_button(ctx, acc.uid, arena, m.id, btn.data.c_str());
                    count++;
                    return;
                }
            }
        }
    });

    api->c_reset(ctx);
    api->c_text(ctx, ("✅ Нажато с " + std::to_string(count) + " аккаунтов.").c_str());
    api->c_edit(ctx, 0);
}

static void cmd_mlike(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;
    const char* args_ptr = api->cmd_args(ctx);
    std::string arg = args_ptr ? to_lower(trim(args_ptr)) : "";
    combat_or_bot_confirm(ctx, api, s, true, arg);
}

static void cmd_mdis(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;
    const char* args_ptr = api->cmd_args(ctx);
    std::string arg = args_ptr ? to_lower(trim(args_ptr)) : "";
    combat_or_bot_confirm(ctx, api, s, false, arg);
}

// ── .mprofile ────────────────────────────────────────────────────────────────
static void cmd_mprofile(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;
    auto accounts = get_active_accounts(ctx, api, s);

    long long combat_id = api->resolve(ctx, COMBAT_BOT);
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

    long long bot_id = api->resolve(ctx, TRUE_BOT);
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

    long long bot_id = api->resolve(ctx, KOTO_BOT);
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

// ── .movesh <ник> ────────────────────────────────────────────────────────────
static void cmd_movesh(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    Settings s = load_settings(ctx, api);
    if (!check_tourney(ctx, api, s)) return;
    const char* args_ptr = api->cmd_args(ctx);
    std::string target_raw = args_ptr ? trim(args_ptr) : "";
    if (target_raw.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Укажите ник: .movesh <ник>");
        api->c_edit(ctx, 0);
        return;
    }
    std::string target_norm = normalize_nick(target_raw);
    auto accounts = get_active_accounts(ctx, api, s);

    api->c_reset(ctx);
    api->c_text(ctx, ("🗳 Mafioso: голос за " + target_raw + "...").c_str());
    api->c_edit(ctx, 0);

    long long bot_id = api->resolve(ctx, OSO_BOT);
    int count = 0;
    human_scatter(accounts, s, [&](const AccountInfo& acc) {
        char hbuf[65536];
        int hlen = api->twin_get_history_json(ctx, acc.uid, bot_id, 0, 30, 0, hbuf, sizeof(hbuf));
        if (hlen <= 0) return;
        auto msgs = parse_history_json(std::string(hbuf, hlen));
        for (const auto& m : msgs) {
            std::string ltxt = to_lower(m.text);
            if (ltxt.find("повесить") == std::string::npos && ltxt.find("виноватых") == std::string::npos) continue;
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
    api->c_text(ctx, ("✅ Mafioso vesh: проголосовало " + std::to_string(count) + " акк.").c_str());
    api->c_edit(ctx, 0);
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
    char hbuf[131072];
    int hlen = api->get_history_json ? api->get_history_json(ctx, chat_id, (int)reply_id, 200, -200, hbuf, sizeof(hbuf)) : 0;
    if (hlen <= 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "⚠️ ");
        api->c_fmt(ctx, "mkgraf:", KOTO_ENT_BOLD);
        api->c_text(ctx, " Голоса не найдены в истории чата (200 сообщ.).");
        api->c_send(ctx, me_id, 0);
        return;
    }

    auto msgs = parse_history_json(std::string(hbuf, hlen));
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
    char hbuf[131072];
    int hlen = api->get_history_json ? api->get_history_json(ctx, chat_id, (int)reply_id, 200, -200, hbuf, sizeof(hbuf)) : 0;
    if (hlen <= 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "⚠️ ");
        api->c_fmt(ctx, "BakuSearch Ошибка:\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "Голоса не найдены в истории чата (200 сообщ.).");
        api->c_send(ctx, me_id, 0);
        return;
    }

    auto msgs = parse_history_json(std::string(hbuf, hlen));
    std::set<std::string> voted;
    for (const auto& m : msgs) {
        if (m.text.find("голосовал") != std::string::npos) {
            std::stringstream ss(m.text);
            std::string line;
            while (std::getline(ss, line)) {
                line = trim(line);
                if (line.find("голосовал") == std::string::npos) continue;
                if (line.find("Мэр голосовал") != std::string::npos) {
                    voted.insert(clean_nick_baku("🎖 Мэр"));
                    continue;
                }
                size_t gpos = line.find("голосовал");
                std::string nick = line.substr(0, gpos);
                std::string cl = clean_nick_baku(nick);
                if (!cl.empty()) voted.insert(cl);
            }
        }
    }

    std::vector<std::string> not_voted;
    for (size_t i = 0; i < player_lines.size(); ++i) {
        std::string cl = clean_nick_baku(player_identifiers[i]);
        if (voted.count(cl) == 0) not_voted.push_back(player_lines[i]);
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
    if (!s.enabled_combat || s.arena_id == 0) return 0;
    if (api->chat_id(ctx) != s.arena_id) return 0;

    const char* txt_ptr = api->msg_text(ctx);
    if (!txt_ptr || !*txt_ptr) return 0;
    std::string text = txt_ptr;

    // 1. Определение своего цвета ("Живые игроки:")
    if (text.find("Живые игроки:") != std::string::npos) {
        char me_name[256];
        int me_len = api->get_me_name(ctx, me_name, sizeof(me_name));
        std::string my_nick = me_len > 0 ? normalize_nick(me_name) : "";

        size_t start = text.find("Живые игроки:");
        size_t end = text.find("Из них:");
        if (start != std::string::npos && end != std::string::npos && end > start) {
            std::string section = text.substr(start, end - start);
            std::stringstream ss(section);
            std::string line;
            while (std::getline(ss, line)) {
                line = trim(line);
                std::string color;
                if (line.find("🌝") != std::string::npos) color = "🌝";
                else if (line.find("🌚") != std::string::npos) color = "🌚";
                if (!color.empty() && !my_nick.empty()) {
                    std::string norm_l = normalize_nick(line);
                    if (norm_l.find(my_nick) != std::string::npos || my_nick.find(norm_l) != std::string::npos) {
                        api->db_set(ctx, "my_combat_color", color.c_str());
                        break;
                    }
                }
            }
        }
    }
    // 2. Голосование ("Кого бы хотел убить?")
    else if (text.find("Кого бы хотел убить?") != std::string::npos) {
        char my_cbuf[32];
        int clen = api->db_get(ctx, "my_combat_color", my_cbuf, sizeof(my_cbuf));
        std::string my_color = clen > 0 ? std::string(my_cbuf, clen) : "";
        std::string opp_color = (my_color == "🌝") ? "🌚" : "🌝";

        char bbuf[16384];
        int blen = api->msg_buttons_json ? api->msg_buttons_json(ctx, bbuf, sizeof(bbuf)) : 0;
        if (blen <= 0) return 0;
        auto buttons = parse_buttons_json(std::string(bbuf, blen));
        if (buttons.empty()) return 0;

        const MsgButton* target_btn = nullptr;
        if (s.combat_mode == "name") {
            if (!s.combat_target_name.empty()) {
                std::string tnorm = normalize_nick(s.combat_target_name);
                for (const auto& btn : buttons) {
                    if (normalize_nick(btn.text).find(tnorm) != std::string::npos) {
                        target_btn = &btn;
                        break;
                    }
                }
            }
        } else {
            // "last" или "prelast"
            std::vector<const MsgButton*> enemies;
            for (const auto& btn : buttons) {
                if (btn.text.find(opp_color) != std::string::npos) {
                    enemies.push_back(&btn);
                }
            }
            if (!enemies.empty()) {
                if (s.combat_mode == "prelast" && enemies.size() >= 2) {
                    target_btn = enemies[enemies.size() - 2];
                } else {
                    target_btn = enemies.back();
                }
            }
        }

        if (target_btn && !target_btn->data.empty()) {
            if (s.vote_delay_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(s.vote_delay_ms));
            }
            api->twin_click_button(ctx, 0, api->chat_id(ctx), api->msg_id(ctx), target_btn->data.c_str());
            return 1;
        }
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
    {"mrole",     cmd_mrole,     KOTO_LEVEL_TRUSTED, "Скан ролей из ЛС в лог-чат", "<combat|true|baku|koto|oso>"},
    {"mlike",     cmd_mlike,     KOTO_LEVEL_TRUSTED, "Combat/Oso/True/Koto: подтвердить линч", "[oso|true|koto]"},
    {"mdis",      cmd_mdis,      KOTO_LEVEL_TRUSTED, "Combat/Oso/True/Koto: оправдать", "[oso|true|koto]"},
    {"mprofile",  cmd_mprofile,  KOTO_LEVEL_TRUSTED, "Отправить /profile в Combat со всех акк.", NULL},
    {"mtrue",     cmd_mtrue,     KOTO_LEVEL_TRUSTED, "Вкл/выкл TrueMafia режим", "<on/off>"},
    {"mtvesh",    cmd_mtvesh,    KOTO_LEVEL_TRUSTED, "TrueMafia: голос за игрока", "<ник>"},
    {"mkvesh",    cmd_mkvesh,    KOTO_LEVEL_TRUSTED, "KotoMafia: голос за игрока через ЛС", "<ник>"},
    {"mkgraf",    cmd_mkgraf,    KOTO_LEVEL_TRUSTED, "KotoMafia: поиск не проголосовавших", NULL},
    {"mbaku",     cmd_mbaku,     KOTO_LEVEL_TRUSTED, "Baku: поиск не проголосовавших", "[red/blue]"},
    {"movesh",    cmd_movesh,    KOTO_LEVEL_TRUSTED, "Mafioso: голос за игрока через ЛС", "<ник>"}
};

// ── Обработчик нажатий inline-кнопок (on_callback) ───────────────────────────
static void on_callback(koto_ctx* ctx, const koto_api* api) {
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
    const char* data_ptr = api->cb_data(ctx);
    if (!data_ptr) return;
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
    "1.2.7",
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
