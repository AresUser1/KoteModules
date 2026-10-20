// stags.cpp — Модуль Silent Tags ("Тихие теги") для Kotogram (C++ ABI 3).
// Портирован из Python-модуля stags.py (v2.8.5).
// Автор: Kote

#include "module_abi.h"
#include <string>
#include <vector>
#include <set>
#include <map>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <algorithm>

// ── Кастомные премиум-эмодзи ──────────────────────────────────────────────────
static constexpr long long SUCCESS_EMOJI_ID = 5774022692642492953LL; // ✅
static constexpr long long ERROR_EMOJI_ID   = 5774077015388852135LL; // ❌
static constexpr long long INFO_EMOJI_ID    = 5256230583717079814LL; // 📋 / 📜
static constexpr long long SILENT_EMOJI_ID  = 5289904905434189369LL; // 🤫

// ── Хелперы отправки ──────────────────────────────────────────────────────────
static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

static std::string trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, (last - first + 1));
}

// ── Конфигурация модуля ───────────────────────────────────────────────────────
struct StagsConfig {
    bool enabled;
    bool silent;
    bool process_bots;
    bool process_pm;
    bool ignore_blocked;
    long long log_group_id;
    long long silent_emoji_id;
    std::string silent_emoji_fallback;
};

static StagsConfig load_stags_config(koto_ctx* ctx, const koto_api* api) {
    StagsConfig c;
    c.enabled = false;
    c.silent = false;
    c.process_bots = true;
    c.process_pm = false;
    c.ignore_blocked = false;
    c.log_group_id = 0;
    c.silent_emoji_id = SILENT_EMOJI_ID;
    c.silent_emoji_fallback = "🤫";

    char buf[128];
    if (api->cfg_get && api->cfg_get(ctx, "enabled", buf, sizeof(buf) - 1) > 0) {
        c.enabled = (strcmp(buf, "1") == 0 || strcmp(buf, "True") == 0 || strcmp(buf, "true") == 0);
    } else if (api->db_get(ctx, "enabled", buf, sizeof(buf) - 1) > 0) {
        c.enabled = (strcmp(buf, "1") == 0 || strcmp(buf, "True") == 0);
    }

    if (api->cfg_get && api->cfg_get(ctx, "silent", buf, sizeof(buf) - 1) > 0) {
        c.silent = (strcmp(buf, "1") == 0 || strcmp(buf, "True") == 0 || strcmp(buf, "true") == 0);
    } else if (api->db_get(ctx, "silent", buf, sizeof(buf) - 1) > 0) {
        c.silent = (strcmp(buf, "1") == 0 || strcmp(buf, "True") == 0);
    }

    if (api->cfg_get && api->cfg_get(ctx, "process_bots", buf, sizeof(buf) - 1) > 0) {
        c.process_bots = (strcmp(buf, "0") != 0 && strcmp(buf, "False") != 0 && strcmp(buf, "false") != 0);
    } else if (api->db_get(ctx, "process_bots", buf, sizeof(buf) - 1) > 0) {
        c.process_bots = (strcmp(buf, "0") != 0 && strcmp(buf, "False") != 0);
    }

    if (api->cfg_get && api->cfg_get(ctx, "process_pm", buf, sizeof(buf) - 1) > 0) {
        c.process_pm = (strcmp(buf, "1") == 0 || strcmp(buf, "True") == 0 || strcmp(buf, "true") == 0);
    } else if (api->db_get(ctx, "process_pm", buf, sizeof(buf) - 1) > 0) {
        c.process_pm = (strcmp(buf, "1") == 0 || strcmp(buf, "True") == 0);
    }

    if (api->cfg_get && api->cfg_get(ctx, "ignore_blocked", buf, sizeof(buf) - 1) > 0) {
        c.ignore_blocked = (strcmp(buf, "1") == 0 || strcmp(buf, "True") == 0 || strcmp(buf, "true") == 0);
    } else if (api->db_get(ctx, "ignore_blocked", buf, sizeof(buf) - 1) > 0) {
        c.ignore_blocked = (strcmp(buf, "1") == 0 || strcmp(buf, "True") == 0);
    }

    if (api->cfg_get && api->cfg_get(ctx, "log_group_id", buf, sizeof(buf) - 1) > 0) {
        c.log_group_id = strtoll(buf, nullptr, 10);
    } else {
        c.log_group_id = api->db_get_int(ctx, "log_group_id", 0);
    }

    c.silent_emoji_id = api->db_get_int(ctx, "silent_emoji_id", SILENT_EMOJI_ID);

    char fbuf[32];
    if (api->db_get(ctx, "silent_emoji_fallback", fbuf, sizeof(fbuf) - 1) > 0) {
        c.silent_emoji_fallback = fbuf;
    }
    return c;
}

static void save_stags_config(koto_ctx* ctx, const koto_api* api, const StagsConfig& c) {
    if (api->cfg_set) {
        api->cfg_set(ctx, "enabled", c.enabled ? "1" : "0");
        api->cfg_set(ctx, "silent", c.silent ? "1" : "0");
        api->cfg_set(ctx, "process_bots", c.process_bots ? "1" : "0");
        api->cfg_set(ctx, "process_pm", c.process_pm ? "1" : "0");
        api->cfg_set(ctx, "ignore_blocked", c.ignore_blocked ? "1" : "0");
        api->cfg_set(ctx, "log_group_id", std::to_string(c.log_group_id).c_str());
    }
    api->db_set(ctx, "enabled", c.enabled ? "1" : "0");
    api->db_set(ctx, "silent", c.silent ? "1" : "0");
    api->db_set(ctx, "process_bots", c.process_bots ? "1" : "0");
    api->db_set(ctx, "process_pm", c.process_pm ? "1" : "0");
    api->db_set(ctx, "ignore_blocked", c.ignore_blocked ? "1" : "0");
    api->db_set_int(ctx, "log_group_id", c.log_group_id);
    api->db_set_int(ctx, "silent_emoji_id", c.silent_emoji_id);
    api->db_set(ctx, "silent_emoji_fallback", c.silent_emoji_fallback.c_str());
}

// ── Черные списки (ignore_users, ignore_chats) ────────────────────────────────
static void save_id_set(koto_ctx* ctx, const koto_api* api, const char* key, const std::set<long long>& s) {
    std::string val;
    for (long long v : s) {
        val += std::to_string(v) + " ";
    }
    api->db_set(ctx, key, val.c_str());
}

static std::set<long long> load_id_set(koto_ctx* ctx, const koto_api* api, const char* key) {
    std::set<long long> out;
    char buf[16384];
    int len = api->db_get(ctx, key, buf, sizeof(buf) - 1);
    if (len <= 0) return out;
    buf[len] = '\0';

    std::istringstream iss(buf);
    std::string token;
    while (iss >> token) {
        long long val = strtoll(token.c_str(), nullptr, 10);
        if (val != 0) out.insert(val);
    }

    // Если это ignore_users, автоматически исключаем свой ID (me), если он там был
    if (strcmp(key, "ignore_users") == 0 && api->get_me) {
        long long me = api->get_me(ctx);
        if (me != 0) {
            bool had_me = (out.erase(me) > 0 || out.erase(-me) > 0 || out.erase(std::abs(me)) > 0);
            if (had_me) {
                save_id_set(ctx, api, key, out);
            }
        }
    }
    return out;
}

// ── Режимы твинков (twin_modes) ───────────────────────────────────────────────
// Режимы: "off", "semi", "all"
static std::map<std::string, std::string> load_twin_modes(koto_ctx* ctx, const koto_api* api) {
    std::map<std::string, std::string> out;
    char buf[16384];
    int len = api->db_get(ctx, "twin_modes", buf, sizeof(buf) - 1);
    if (len <= 0) return out;
    buf[len] = '\0';

    std::istringstream iss(buf);
    std::string line;
    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty()) continue;
        size_t tab = line.find('\t');
        if (tab != std::string::npos) {
            out[line.substr(0, tab)] = line.substr(tab + 1);
        }
    }
    return out;
}

static void save_twin_modes(koto_ctx* ctx, const koto_api* api, const std::map<std::string, std::string>& modes) {
    std::string s;
    for (const auto& kv : modes) {
        s += kv.first + "\t" + kv.second + "\n";
    }
    api->db_set(ctx, "twin_modes", s.c_str());
}

static std::string get_twin_mode_label(const std::string& mode) {
    if (mode == "semi") return "🔁 Semi";
    if (mode == "all") return "🌐 All";
    return "❌ Выкл";
}

// ── Получение имен пользователя и чата ────────────────────────────────────────
static std::string get_user_name_str(koto_ctx* ctx, const koto_api* api, long long uid) {
    if (!api) return "Пользователь " + std::to_string(uid);
    char name_buf[256] = {0};
    if (api->user_first_name && api->user_first_name(ctx, uid, name_buf, sizeof(name_buf)) > 0 && name_buf[0]) {
        char last_buf[256] = {0};
        if (api->user_last_name && api->user_last_name(ctx, uid, last_buf, sizeof(last_buf)) > 0 && last_buf[0]) {
            return std::string(name_buf) + " " + std::string(last_buf);
        }
        return name_buf;
    }
    if (api->user_name && api->user_name(ctx, uid, name_buf, sizeof(name_buf)) > 0 && name_buf[0]) {
        return name_buf;
    }
    if (api->user_username && api->user_username(ctx, uid, name_buf, sizeof(name_buf)) > 0 && name_buf[0]) {
        return "@" + std::string(name_buf);
    }
    return "Пользователь " + std::to_string(uid);
}

static std::string get_chat_title_str(koto_ctx* ctx, const koto_api* api, long long cid) {
    if (!api || !api->chat_full_json) return "Чат " + std::to_string(cid);

    long long candidates[] = {
        cid,
        (cid > 0 && cid < 1000000000000LL) ? (-1000000000000LL - cid) : 0,
        cid < 0 ? std::abs(cid) : 0,
        (std::abs(cid) > 1000000000000LL) ? -(std::abs(cid) - 1000000000000LL) : 0
    };

    for (long long c : candidates) {
        if (c == 0) continue;
        char buf[4096] = {0};
        int len = api->chat_full_json(ctx, c, buf, sizeof(buf) - 1);
        if (len > 2) {
            buf[len] = '\0';
            std::string j(buf);
            size_t pos = j.find("\"title\":\"");
            if (pos != std::string::npos) {
                size_t start = pos + 9;
                size_t end = start;
                while (end < j.size()) {
                    if (j[end] == '"' && (end == start || j[end - 1] != '\\')) break;
                    ++end;
                }
                if (end < j.size()) {
                    std::string t = j.substr(start, end - start);
                    if (!t.empty()) return t;
                }
            }
        }
    }
    return "Чат " + std::to_string(cid);
}

// ── Построение ссылки на сообщение в чате ─────────────────────────────────────
static std::string build_chat_message_link(long long chat_id, long long msg_id) {
    std::string cid_str = std::to_string(std::abs(chat_id));
    std::string link_cid = cid_str;
    if (cid_str.rfind("100", 0) == 0 && cid_str.size() > 3) {
        link_cid = cid_str.substr(3);
    }
    return "https://t.me/c/" + link_cid + "/" + std::to_string(msg_id);
}

// ── Построение панели твинков (.sttwins) ──────────────────────────────────────
static void build_sttwins_panel(koto_ctx* ctx, const koto_api* api) {
    auto modes = load_twin_modes(ctx, api);

    api->c_reset(ctx);
    api->c_emoji(ctx, "🤫", SILENT_EMOJI_ID);
    api->c_fmt(ctx, " Silent Tags — Режимы твинков\n\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "Выберите режим для каждого твинка:\n• ❌ Выкл — не участвует\n• 🔁 Semi — копирует настройки основы\n• 🌐 All — читает теги во всех чатах\n\n");

    // Получаем список твинков из хоста
    std::vector<std::pair<long long, std::string>> twins;
    if (api->get_twins_json) {
        char buf[16384];
        int len = api->get_twins_json(ctx, buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            std::string j(buf);
            size_t p = 0;
            while ((p = j.find('{', p)) != std::string::npos) {
                size_t end_p = j.find('}', p);
                if (end_p == std::string::npos) break;
                std::string obj = j.substr(p, end_p - p + 1);

                size_t uid_pos = obj.find("\"uid\":");
                size_t name_pos = obj.find("\"name\":\"");
                size_t is_main_pos = obj.find("\"is_main\":true");

                if (uid_pos != std::string::npos && is_main_pos == std::string::npos) {
                    long long uid = strtoll(obj.c_str() + uid_pos + 6, nullptr, 10);
                    std::string tname = "Twin " + std::to_string(uid);
                    if (name_pos != std::string::npos) {
                        size_t n_start = name_pos + 8;
                        size_t n_end = obj.find('"', n_start);
                        if (n_end != std::string::npos) tname = obj.substr(n_start, n_end - n_start);
                    }
                    if (uid != 0) twins.push_back({uid, tname});
                }
                p = end_p + 1;
            }
        }
    }

    if (twins.empty()) {
        api->c_fmt(ctx, "Нет добавленных твинков.\n", KOTO_ENT_ITALIC);
        if (api->c_button_ex) {
            api->c_button_ex(ctx, "❌ Закрыть", "close_panel", 1, KOTO_BTN_DANGER, 0);
        } else {
            api->c_button(ctx, "❌ Закрыть", "close_panel", 1);
        }
        return;
    }

    for (const auto& tw : twins) {
        std::string tw_key = std::to_string(tw.first);
        std::string cur_mode = "off";
        if (modes.find(tw_key) != modes.end()) cur_mode = modes[tw_key];

        std::string header_btn = "👤 " + tw.second + " [" + get_twin_mode_label(cur_mode) + "]";
        if (api->c_button_ex) {
            api->c_button_ex(ctx, header_btn.c_str(), "sttwins_noop", 1, KOTO_BTN_DEFAULT, 0);
        } else {
            api->c_button(ctx, header_btn.c_str(), "sttwins_noop", 1);
        }

        // Кнопки режимов: off, semi, all
        std::vector<std::string> m_keys = {"off", "semi", "all"};
        for (size_t mi = 0; mi < m_keys.size(); ++mi) {
            std::string m = m_keys[mi];
            std::string label = (m == cur_mode ? "▶️ " : "") + get_twin_mode_label(m);
            std::string cb = "sttwins_set:" + tw_key + ":" + m;
            int new_row = (mi == 0) ? 1 : 0;
            int color = (m == cur_mode) ? KOTO_BTN_SUCCESS : KOTO_BTN_DEFAULT;

            if (api->c_button_ex) {
                api->c_button_ex(ctx, label.c_str(), cb.c_str(), new_row, color, 0);
            } else {
                api->c_button(ctx, label.c_str(), cb.c_str(), new_row);
            }
        }
    }

    if (api->c_button_ex) {
        api->c_button_ex(ctx, "❌ Закрыть", "close_panel", 1, KOTO_BTN_DANGER, 0);
    } else {
        api->c_button(ctx, "❌ Закрыть", "close_panel", 1);
    }
}

// ── .stags <on/off> ─────────────────────────────────────────────────────────
static void cmd_stags(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string arg = raw ? trim(raw) : "";
    for (auto& c : arg) c = (char)std::tolower((unsigned char)c);

    auto cfg = load_stags_config(ctx, api);

    if (arg != "on" && arg != "off") {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
        api->c_fmt(ctx, " Использование: .stags <on/off>", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    bool new_state = (arg == "on");
    if (cfg.enabled == new_state) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "📋", INFO_EMOJI_ID);
        api->c_fmt(ctx, new_state ? " Silent Tags уже ВКЛЮЧЕНЫ." : " Silent Tags уже ВЫКЛЮЧЕНЫ.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    cfg.enabled = new_state;
    save_stags_config(ctx, api, cfg);

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", SUCCESS_EMOJI_ID);
    api->c_fmt(ctx, new_state ? " Silent Tags были ВКЛЮЧЕНЫ." : " Silent Tags были ВЫКЛЮЧЕНЫ.", KOTO_ENT_BOLD);

    if (new_state && !cfg.silent) {
        api->c_text(ctx, "\n");
        api->c_emoji(ctx, "📋", INFO_EMOJI_ID);
        api->c_text(ctx, " Установлен режим временного уведомления в чате (не silent).");
    }
    send_result(ctx, api);
}

// ── .stconfig ───────────────────────────────────────────────────────────────
static void cmd_stconfig(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args_str = raw ? trim(raw) : "";

    auto cfg = load_stags_config(ctx, api);

    // 1. HELP MENU
    if (args_str == "help") {
        api->c_reset(ctx);
        api->c_emoji(ctx, "📋", INFO_EMOJI_ID);
        api->c_fmt(ctx, " Silent Tags — Доступные команды:\n\n", KOTO_ENT_BOLD);

        api->c_fmt(ctx, "Управление модулем:\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "• .stags <on/off> — Включить/выключить Silent Tags\n");
        api->c_text(ctx, "• .stconfig — Показать текущую конфигурацию\n");
        api->c_text(ctx, "• .stconfig help — Показать эту справку\n\n");

        api->c_fmt(ctx, "Основные настройки (on/off):\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "• .stconfig silent <on/off> — Тихий режим (без временных сообщений)\n");
        api->c_text(ctx, "• .stconfig process_bots <on/off> — Читать теги ботов\n");
        api->c_text(ctx, "• .stconfig process_pm <on/off> — Читать теги в ЛС (по умолчанию: выкл)\n");
        api->c_text(ctx, "• .stconfig ignore_blocked <on/off> — Игнор заблокированных\n\n");

        api->c_fmt(ctx, "Списки игнорирования:\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "• .stconfig ignore_users <add/del/clear> [ID | reply]\n");
        api->c_text(ctx, "• .stconfig ignore_chats <add/del/clear> [ID | this]\n\n");

        api->c_fmt(ctx, "Дополнительно:\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "• .stconfig set_log_group [ID] — Установить группу логов\n");
        api->c_text(ctx, "• .stconfig set_emoji <ID> | <FALLBACK> — Сменить эмодзи\n");
        api->c_text(ctx, "• .sttwins — Панель управления твинками\n");

        send_result(ctx, api);
        return;
    }

    // 2. SHOW CONFIG (если аргументов нет)
    if (args_str.empty()) {
        auto ignore_users = load_id_set(ctx, api, "ignore_users");
        auto ignore_chats = load_id_set(ctx, api, "ignore_chats");

        api->c_reset(ctx);
        api->c_emoji(ctx, cfg.silent_emoji_fallback.c_str(), cfg.silent_emoji_id);
        api->c_fmt(ctx, " Silent Tags Конфигурация\n\n", KOTO_ENT_BOLD);

        int mark1 = api->c_mark(ctx);
        api->c_fmt(ctx, "Основные настройки:\n", KOTO_ENT_BOLD);
        api->c_text(ctx, ("• Режим Silent: " + std::string(cfg.silent ? "✅ Включено" : "❌ Выключено") + "\n").c_str());
        api->c_text(ctx, ("• Читать Теги Ботов: " + std::string(cfg.process_bots ? "✅ Включено" : "❌ Выключено") + "\n").c_str());
        api->c_text(ctx, ("• Читать Теги в ЛС: " + std::string(cfg.process_pm ? "✅ Включено" : "❌ Выключено") + "\n").c_str());
        api->c_text(ctx, ("• Игнор Заблокированных: " + std::string(cfg.ignore_blocked ? "✅ Включено" : "❌ Выключено") + "\n").c_str());
        api->c_wrap(ctx, mark1, KOTO_ENT_BLOCKQUOTE, 1);

        api->c_text(ctx, "\n\n");

        int mark2 = api->c_mark(ctx);
        api->c_fmt(ctx, "Списки игнорирования (Black Lists):\n", KOTO_ENT_BOLD);
        api->c_text(ctx, ("• Игнор Пользователи: " + std::to_string(ignore_users.size()) + " чел.\n").c_str());
        api->c_text(ctx, ("• Игнор Чаты: " + std::to_string(ignore_chats.size()) + " чатов\n").c_str());
        api->c_wrap(ctx, mark2, KOTO_ENT_BLOCKQUOTE, 1);

        api->c_text(ctx, "\n\n");

        int mark3 = api->c_mark(ctx);
        api->c_fmt(ctx, "Дополнительно:\n", KOTO_ENT_BOLD);
        if (cfg.log_group_id != 0) {
            std::string log_name = get_chat_title_str(ctx, api, cfg.log_group_id);
            api->c_text(ctx, ("• Группа Лога: " + log_name + " (" + std::to_string(cfg.log_group_id) + ")\n").c_str());
        } else {
            api->c_text(ctx, "• Группа Лога: Не установлена\n");
        }
        api->c_text(ctx, ("• Silent Эмодзи ID: " + std::to_string(cfg.silent_emoji_id) + "\n").c_str());
        api->c_text(ctx, ("• Fallback: " + cfg.silent_emoji_fallback + "\n").c_str());
        api->c_wrap(ctx, mark3, KOTO_ENT_BLOCKQUOTE, 1);

        send_result(ctx, api);
        return;
    }

    // 3. CHANGE CONFIG
    std::istringstream iss(args_str);
    std::string key, val;
    iss >> key;
    std::string rest;
    std::getline(iss, rest);
    val = trim(rest);

    for (auto& c : key) c = (char)std::tolower((unsigned char)c);
    if (key == "ignore_bots") key = "process_bots";
    if (key == "pm") key = "process_pm";

    // Булевы параметры
    if (key == "silent" || key == "process_bots" || key == "process_pm" || key == "ignore_blocked") {
        if (val.empty()) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
            api->c_fmt(ctx, (" Укажите значение (on/off) для " + key).c_str(), KOTO_ENT_BOLD);
            send_result(ctx, api);
            return;
        }
        bool b_val = (val == "on" || val == "true" || val == "1");
        if (key == "silent") cfg.silent = b_val;
        else if (key == "process_bots") cfg.process_bots = b_val;
        else if (key == "process_pm") cfg.process_pm = b_val;
        else if (key == "ignore_blocked") cfg.ignore_blocked = b_val;
        save_stags_config(ctx, api, cfg);

        api->c_reset(ctx);
        api->c_emoji(ctx, "✅", SUCCESS_EMOJI_ID);
        api->c_fmt(ctx, (" Параметр " + key + " установлен: " + (b_val ? "ВКЛ" : "ВЫКЛ")).c_str(), KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    // Черные списки (ignore_users, ignore_chats)
    if (key == "ignore_users" || key == "ignore_chats") {
        std::istringstream viss(val);
        std::string action, target_str;
        viss >> action >> target_str;
        for (auto& c : action) c = (char)std::tolower((unsigned char)c);

        // Если действие не указано или запрошен список: показываем текущее содержимое списка
        if (action.empty() || action == "list" || action == "ls" || action == "show") {
            auto current_set = load_id_set(ctx, api, key.c_str());
            api->c_reset(ctx);
            api->c_emoji(ctx, "📋", INFO_EMOJI_ID);
            if (key == "ignore_chats") {
                api->c_fmt(ctx, " Список игнорируемых чатов (ignore_chats):\n\n", KOTO_ENT_BOLD);
                if (current_set.empty()) {
                    api->c_text(ctx, "Список пуст.\n\n");
                } else {
                    int mark = api->c_mark(ctx);
                    int idx = 1;
                    for (auto cid : current_set) {
                        std::string title = get_chat_title_str(ctx, api, cid);
                        api->c_text(ctx, (std::to_string(idx++) + ". " + title + " (").c_str());
                        api->c_fmt(ctx, std::to_string(cid).c_str(), KOTO_ENT_CODE);
                        api->c_text(ctx, ")\n");
                    }
                    api->c_wrap(ctx, mark, KOTO_ENT_BLOCKQUOTE, 1);
                    api->c_text(ctx, "\n\n");
                }
                api->c_fmt(ctx, "Команды управления:\n", KOTO_ENT_BOLD);
                api->c_text(ctx, "• .stconfig ignore_chats add [ID | this]\n• .stconfig ignore_chats del [ID | this]\n• .stconfig ignore_chats clear");
            } else {
                api->c_fmt(ctx, " Список игнорируемых пользователей (ignore_users):\n\n", KOTO_ENT_BOLD);
                if (current_set.empty()) {
                    api->c_text(ctx, "Список пуст.\n\n");
                } else {
                    int mark = api->c_mark(ctx);
                    int idx = 1;
                    for (auto uid : current_set) {
                        std::string uname = get_user_name_str(ctx, api, uid);
                        api->c_text(ctx, (std::to_string(idx++) + ". " + uname + " (").c_str());
                        api->c_fmt(ctx, std::to_string(uid).c_str(), KOTO_ENT_CODE);
                        api->c_text(ctx, ")\n");
                    }
                    api->c_wrap(ctx, mark, KOTO_ENT_BLOCKQUOTE, 1);
                    api->c_text(ctx, "\n\n");
                }
                api->c_fmt(ctx, "Команды управления:\n", KOTO_ENT_BOLD);
                api->c_text(ctx, "• .stconfig ignore_users add [ID | reply]\n• .stconfig ignore_users del [ID | reply]\n• .stconfig ignore_users clear");
            }
            send_result(ctx, api);
            return;
        }

        if (action == "clear" || action == "cl") {
            save_id_set(ctx, api, key.c_str(), {});
            api->c_reset(ctx);
            api->c_emoji(ctx, "✅", SUCCESS_EMOJI_ID);
            api->c_fmt(ctx, (" Список " + key + " успешно очищен.").c_str(), KOTO_ENT_BOLD);
            send_result(ctx, api);
            return;
        }

        if (action != "add" && action != "del") {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
            api->c_fmt(ctx, (" Неизвестное действие: \"" + action + "\". Используйте: add, del, clear или list.").c_str(), KOTO_ENT_BOLD);
            send_result(ctx, api);
            return;
        }

        long long target_id = 0;
        if (!target_str.empty()) {
            if (target_str == "this" && key == "ignore_chats") {
                target_id = api->chat_id(ctx);
            } else {
                target_id = strtoll(target_str.c_str(), nullptr, 10);
            }
        } else {
            if (key == "ignore_chats") {
                target_id = api->chat_id(ctx);
            } else {
                target_id = api->reply_sender_id ? api->reply_sender_id(ctx) : 0;
            }
        }

        if (target_id == 0) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
            if (key == "ignore_chats") {
                api->c_fmt(ctx, " Укажите ID чата или используйте 'this': .stconfig ignore_chats <add/del> [ID | this]", KOTO_ENT_BOLD);
            } else {
                api->c_fmt(ctx, " Укажите ID пользователя или ответьте на его сообщение: .stconfig ignore_users <add/del> [ID | reply]", KOTO_ENT_BOLD);
            }
            send_result(ctx, api);
            return;
        }

        long long me = api->get_me ? api->get_me(ctx) : 0;
        if (key == "ignore_users" && action == "add" && me != 0 && (target_id == me || std::abs(target_id) == std::abs(me))) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
            api->c_fmt(ctx, " Нельзя добавить самого себя в список игнорирования.", KOTO_ENT_BOLD);
            send_result(ctx, api);
            return;
        }

        auto current_set = load_id_set(ctx, api, key.c_str());
        if (action == "add") {
            current_set.insert(target_id);
            save_id_set(ctx, api, key.c_str(), current_set);
            api->c_reset(ctx);
            api->c_emoji(ctx, "✅", SUCCESS_EMOJI_ID);
            if (key == "ignore_chats") {
                std::string chat_name = get_chat_title_str(ctx, api, target_id);
                api->c_text(ctx, " Чат ");
                api->c_fmt(ctx, chat_name.c_str(), KOTO_ENT_BOLD);
                api->c_text(ctx, " (");
                api->c_fmt(ctx, std::to_string(target_id).c_str(), KOTO_ENT_CODE);
                api->c_text(ctx, ") добавлен в ");
                api->c_fmt(ctx, "ignore_chats", KOTO_ENT_CODE);
                api->c_text(ctx, ".");
            } else {
                std::string user_name = get_user_name_str(ctx, api, target_id);
                api->c_text(ctx, " Пользователь ");
                std::string ulink = "tg://user?id=" + std::to_string(target_id);
                api->c_url(ctx, user_name.c_str(), ulink.c_str());
                api->c_text(ctx, " (");
                api->c_fmt(ctx, std::to_string(target_id).c_str(), KOTO_ENT_CODE);
                api->c_text(ctx, ") добавлен в ");
                api->c_fmt(ctx, "ignore_users", KOTO_ENT_CODE);
                api->c_text(ctx, ".");
            }
            send_result(ctx, api);
            return;
        } else if (action == "del") {
            current_set.erase(target_id);
            current_set.erase(-target_id);
            current_set.erase(std::abs(target_id));
            save_id_set(ctx, api, key.c_str(), current_set);
            api->c_reset(ctx);
            api->c_emoji(ctx, "✅", SUCCESS_EMOJI_ID);
            if (key == "ignore_chats") {
                std::string chat_name = get_chat_title_str(ctx, api, target_id);
                api->c_text(ctx, " Чат ");
                api->c_fmt(ctx, chat_name.c_str(), KOTO_ENT_BOLD);
                api->c_text(ctx, " (");
                api->c_fmt(ctx, std::to_string(target_id).c_str(), KOTO_ENT_CODE);
                api->c_text(ctx, ") удален из ");
                api->c_fmt(ctx, "ignore_chats", KOTO_ENT_CODE);
                api->c_text(ctx, ".");
            } else {
                std::string user_name = get_user_name_str(ctx, api, target_id);
                api->c_text(ctx, " Пользователь ");
                std::string ulink = "tg://user?id=" + std::to_string(target_id);
                api->c_url(ctx, user_name.c_str(), ulink.c_str());
                api->c_text(ctx, " (");
                api->c_fmt(ctx, std::to_string(target_id).c_str(), KOTO_ENT_CODE);
                api->c_text(ctx, ") удален из ");
                api->c_fmt(ctx, "ignore_users", KOTO_ENT_CODE);
                api->c_text(ctx, ".");
            }
            send_result(ctx, api);
            return;
        }
    }

    // set_log_group
    if (key == "set_log_group") {
        long long gid = api->chat_id(ctx);
        if (!val.empty()) {
            char* end = nullptr;
            long long maybe_id = strtoll(val.c_str(), &end, 10);
            if (maybe_id != 0) gid = maybe_id;
        }
        cfg.log_group_id = gid;
        save_stags_config(ctx, api, cfg);

        std::string gtitle = get_chat_title_str(ctx, api, gid);
        api->c_reset(ctx);
        api->c_emoji(ctx, "✅", SUCCESS_EMOJI_ID);
        api->c_fmt(ctx, (" Группа логов установлена: " + gtitle + " (" + std::to_string(gid) + ")").c_str(), KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    // set_emoji <ID> | <FALLBACK>
    if (key == "set_emoji") {
        size_t pipe = val.find('|');
        if (pipe == std::string::npos) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
            api->c_fmt(ctx, " Формат: .stconfig set_emoji <ID> | <FALLBACK>", KOTO_ENT_BOLD);
            send_result(ctx, api);
            return;
        }
        std::string eid_str = trim(val.substr(0, pipe));
        std::string fall_str = trim(val.substr(pipe + 1));
        long long eid = strtoll(eid_str.c_str(), nullptr, 10);

        if (eid != 0 && !fall_str.empty()) {
            cfg.silent_emoji_id = eid;
            cfg.silent_emoji_fallback = fall_str;
            save_stags_config(ctx, api, cfg);

            api->c_reset(ctx);
            api->c_emoji(ctx, "✅", SUCCESS_EMOJI_ID);
            api->c_fmt(ctx, " Silent Эмодзи успешно обновлен.", KOTO_ENT_BOLD);
            send_result(ctx, api);
            return;
        }
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
    api->c_fmt(ctx, (" Неизвестный ключ или команда: " + key).c_str(), KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// ── .sttwins ───────────────────────────────────────────────────────────────
static void cmd_sttwins(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    build_sttwins_panel(ctx, api);
    send_result(ctx, api);
}

// ── Watcher: перехват упоминаний и закреплений ──────────────────────────────
static int on_watch(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) return 0;

    auto cfg = load_stags_config(ctx, api);
    if (!cfg.enabled) return 0;

    long long cid = api->chat_id(ctx);
    long long sid = api->sender_id(ctx);
    long long me = api->get_me ? api->get_me(ctx) : 0;
    if (sid == me) return 0;

    // Определение: является ли текущий аккаунт твинком
    bool is_twin = false;
    std::string twin_name;
    if (api->get_twins_json) {
        char buf[16384];
        int len = api->get_twins_json(ctx, buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            std::string j(buf);
            std::string uid_needle = "\"uid\":" + std::to_string(me);
            size_t pos = j.find(uid_needle);
            if (pos != std::string::npos) {
                size_t obj_start = j.rfind('{', pos);
                size_t obj_end = j.find('}', pos);
                if (obj_start != std::string::npos && obj_end != std::string::npos) {
                    std::string obj = j.substr(obj_start, obj_end - obj_start + 1);
                    if (obj.find("\"is_main\":true") == std::string::npos) {
                        is_twin = true;
                        size_t name_pos = obj.find("\"name\":\"");
                        if (name_pos != std::string::npos) {
                            size_t n_start = name_pos + 8;
                            size_t n_end = obj.find('"', n_start);
                            if (n_end != std::string::npos) twin_name = obj.substr(n_start, n_end - n_start);
                        }
                    }
                }
            }
        }
    }

    std::string tw_mode = "off";
    if (is_twin) {
        auto modes = load_twin_modes(ctx, api);
        auto it = modes.find(std::to_string(me));
        if (it != modes.end()) tw_mode = it->second;
        // Если твинк выключен в Silent Tags — не обрабатываем:
        if (tw_mode == "off") return 0;
    }

    int type = api->chat_type(ctx);
    if (type == KOTO_CHAT_UNKNOWN) return 0;
    bool is_pm = type == KOTO_CHAT_PRIVATE;

    if (is_pm) {
        // Твинк в режиме "all" читает ВСЕ чаты (включая ЛС), независимо от настроек основы.
        // Для основы (или твинка в режиме "semi") проверяем тумблер cfg.process_pm:
        if (!is_twin && !cfg.process_pm) {
            return 0;
        }
        if (is_twin && tw_mode == "semi" && !cfg.process_pm) {
            return 0;
        }
    }

    // 1. Проверяем триггер упоминания или закрепления
    bool is_mention_of_me = false;
    bool is_pin_action = false;

    // Нативные флаги хоста (v0.3.22 / ABI Section 31)
    if (api->msg_is_pinned && api->msg_is_pinned(ctx)) {
        is_pin_action = true;
    }
    if (api->msg_is_mentioned && api->msg_is_mentioned(ctx)) {
        is_mention_of_me = true;
    }

    // Проверяем ответ на мое сообщение (reply)
    if (!is_mention_of_me && api->reply_sender_id) {
        if (api->reply_sender_id(ctx) == me) {
            is_mention_of_me = true;
        }
    }

    // Проверяем entities (KOTO_ENT_URL tg://user?id=me или KOTO_ENT_MENTION)
    if (!is_mention_of_me && api->msg_entity_count) {
        int cnt = api->msg_entity_count(ctx);
        for (int i = 0; i < cnt; ++i) {
            int etype = 0, eoff = 0, elen = 0;
            long long docid = 0;
            char url[512] = {0};
            if (api->msg_entity(ctx, i, &etype, &eoff, &elen, &docid, url, sizeof(url))) {
                if (etype == KOTO_ENT_URL && url[0]) {
                    std::string needle = "tg://user?id=" + std::to_string(me);
                    if (strstr(url, needle.c_str()) != nullptr) {
                        is_mention_of_me = true;
                        break;
                    }
                }
            }
        }
    }

    // Проверяем текст сообщения на @username
    if (!is_mention_of_me && api->user_username && api->msg_text) {
        char un_buf[128] = {0};
        if (api->user_username(ctx, me, un_buf, sizeof(un_buf)) > 0 && un_buf[0]) {
            const char* txt = api->msg_text(ctx);
            if (txt) {
                std::string target = "@" + std::string(un_buf);
                if (strstr(txt, target.c_str()) != nullptr) {
                    is_mention_of_me = true;
                }
            }
        }
    }

    if (!is_mention_of_me && !is_pin_action) return 0;

    // 2. Проверяем черные списки (ignore_users, ignore_chats)
    if (!is_pin_action) {
        auto ignore_users = load_id_set(ctx, api, "ignore_users");
        if (ignore_users.find(sid) != ignore_users.end() ||
            ignore_users.find(std::abs(sid)) != ignore_users.end()) return 0;
    }

    bool is_bot = false;
    if (api->user_full_json) {
        char ubuf[2048] = {0};
        int ulen = api->user_full_json(ctx, sid, ubuf, sizeof(ubuf) - 1);
        if (ulen > 0) {
            ubuf[ulen] = '\0';
            if (strstr(ubuf, "\"is_bot\":true") != nullptr) is_bot = true;
        }
    }

    // Если отправитель — бот, а чтение тегов ботов выключено
    if (is_bot && !cfg.process_bots) return 0;

    auto ignore_chats = load_id_set(ctx, api, "ignore_chats");
    long long abs_cid = std::abs(cid);
    long long norm_cid = abs_cid;
    std::string abs_str = std::to_string(abs_cid);
    if (abs_str.rfind("100", 0) == 0 && abs_str.size() > 3) {
        norm_cid = strtoll(abs_str.substr(3).c_str(), nullptr, 10);
    }
    bool chat_ignored = (ignore_chats.find(cid) != ignore_chats.end() ||
                         ignore_chats.find(-cid) != ignore_chats.end() ||
                         ignore_chats.find(abs_cid) != ignore_chats.end() ||
                         ignore_chats.find(norm_cid) != ignore_chats.end() ||
                         ignore_chats.find(-1000000000000LL - norm_cid) != ignore_chats.end());
    if (chat_ignored) {
        // Твинк в режиме "all" игнорирует список ignore_chats и читает ВСЕ чаты!
        if (is_twin && tw_mode == "all") {
            // Разрешено!
        } else {
            // В игнорируемом чате читаем тег ТОЛЬКО если это бот и включено process_bots
            if (!(is_bot && cfg.process_bots)) {
                return 0;
            }
        }
    }

    // 3. Отметка о прочтении упоминания и сообщения (Silent Tags)
    long long mid = api->msg_id ? api->msg_id(ctx) : 0;
    if (api->read_mentions) {
        api->read_mentions(ctx, cid);
    }
    if (api->read_history) {
        api->read_history(ctx, cid, mid);
    }

    // 4. Обработка уведомления (если не silent)
    if (!cfg.silent) {
        api->c_reset(ctx);
        api->c_emoji(ctx, cfg.silent_emoji_fallback.c_str(), cfg.silent_emoji_id);
        std::string action_str = is_pin_action ? "Закрепление получено." : "Тег получен.";
        api->c_text(ctx, (" " + action_str + " Silent Tags активен...").c_str());
        api->c_send(ctx, cid, 0);
    }

    // 5. Отправка подробного лога в группу логов
    if (cfg.log_group_id != 0) {
        long long mid = api->msg_id ? api->msg_id(ctx) : 0;
        std::string chat_link = build_chat_message_link(cid, mid);
        std::string sender_name = get_user_name_str(ctx, api, sid);
        std::string chat_name = get_chat_title_str(ctx, api, cid);

        api->c_reset(ctx);
        api->c_emoji(ctx, cfg.silent_emoji_fallback.c_str(), cfg.silent_emoji_id);
        if (is_pin_action) {
            api->c_text(ctx, " Silent Tags: Закрепление в чате ");
            api->c_fmt(ctx, chat_name.c_str(), KOTO_ENT_BOLD);
            api->c_fmt(ctx, (" (" + std::to_string(cid) + ")").c_str(), KOTO_ENT_CODE);
            if (is_twin) {
                api->c_text(ctx, " [");
                api->c_text(ctx, ("👤 Твинк: " + (twin_name.empty() ? std::to_string(me) : twin_name)).c_str());
                api->c_text(ctx, "]");
            }
        } else {
            api->c_text(ctx, " Вас упомянули в ");
            api->c_fmt(ctx, chat_name.c_str(), KOTO_ENT_BOLD);
            api->c_text(ctx, " пользователем ");
            std::string user_link = "tg://user?id=" + std::to_string(sid);
            api->c_url(ctx, sender_name.c_str(), user_link.c_str());
            if (is_twin) {
                api->c_text(ctx, " [");
                api->c_text(ctx, ("👤 Твинк: " + (twin_name.empty() ? std::to_string(me) : twin_name)).c_str());
                api->c_text(ctx, "]");
            }
        }
        api->c_text(ctx, "\n\n");

        const char* raw_txt = api->msg_text ? api->msg_text(ctx) : "";
        std::string preview_text = raw_txt ? raw_txt : "";
        if (preview_text.size() > 500) preview_text = preview_text.substr(0, 500) + "...";

        int qmark = api->c_mark(ctx);
        api->c_fmt(ctx, "Сообщение:", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ("\n" + preview_text).c_str(), KOTO_ENT_CODE);
        api->c_wrap(ctx, qmark, KOTO_ENT_BLOCKQUOTE, 1);

        api->c_text(ctx, "\n\n");
        api->c_url(ctx, "➡️ Перейти к сообщению", chat_link.c_str());

        api->c_send(ctx, cfg.log_group_id, 0);
    }

    return 0;
}

// ── Колбэки инлайн-панели (с защитой владельца) ─────────────────────────────
static void on_callback(koto_ctx* ctx, const koto_api* api) {
    long long sender = api->sender_id ? api->sender_id(ctx) : 0;
    long long me = api->get_me ? api->get_me(ctx) : 0;

    bool allowed = (me != 0 && sender == me);
    if (!allowed && api->check_permission) {
        allowed = (api->check_permission(ctx, KOTO_LEVEL_TRUSTED) != 0);
    }

    if (!allowed) {
        if (api->cb_answer) api->cb_answer(ctx, "⛔ Эта панель только для владельца!", 1);
        return;
    }

    const char* raw_data = api->cb_data ? api->cb_data(ctx) : nullptr;
    if (!raw_data) return;
    std::string data(raw_data);

    if (data == "close_panel") {
        api->c_reset(ctx);
        api->c_fmt(ctx, "Панель закрыта.", KOTO_ENT_ITALIC);
        api->c_edit_message(ctx, api->chat_id(ctx), api->cb_message_id(ctx), 0);
        if (api->cb_answer) api->cb_answer(ctx, "Панель закрыта.", 0);
        return;
    }

    if (data == "sttwins_noop") {
        if (api->cb_answer) api->cb_answer(ctx, "", 0);
        return;
    }

    // sttwins_set:<twin_key>:<mode>
    if (data.rfind("sttwins_set:", 0) == 0) {
        std::string payload = data.substr(12);
        size_t colon = payload.find(':');
        if (colon != std::string::npos) {
            std::string tw_key = payload.substr(0, colon);
            std::string new_mode = payload.substr(colon + 1);

            auto modes = load_twin_modes(ctx, api);
            modes[tw_key] = new_mode;
            save_twin_modes(ctx, api, modes);

            build_sttwins_panel(ctx, api);
            api->c_edit_message(ctx, api->chat_id(ctx), api->cb_message_id(ctx), 0);
            if (api->cb_answer) api->cb_answer(ctx, ("Режим обновлен: " + get_twin_mode_label(new_mode)).c_str(), 0);
        }
        return;
    }
}

// ── Список команд ────────────────────────────────────────────────────────────
static const koto_command COMMANDS[] = {
    {
        "stags",
        &cmd_stags,
        KOTO_LEVEL_TRUSTED,
        "Включить или выключить модуль Silent Tags.",
        "<on/off>"
    },
    {
        "stconfig",
        &cmd_stconfig,
        KOTO_LEVEL_TRUSTED,
        "Просмотр и детальная настройка модуля Silent Tags.",
        "[help/key] [value]"
    },
    {
        "sttwins",
        &cmd_sttwins,
        KOTO_LEVEL_TRUSTED,
        "Инлайн-панель управления режимами твинков для Silent Tags.",
        nullptr
    }
};

static const koto_setting_field SETTINGS[] = {
    {
        "enabled",
        "Включить Silent Tags",
        KOTO_FIELD_BOOL,
        "0",
        nullptr
    },
    {
        "silent",
        "Тихий режим (без временного сообщения в чате)",
        KOTO_FIELD_BOOL,
        "0",
        nullptr
    },
    {
        "process_bots",
        "Обрабатывать упоминания от ботов",
        KOTO_FIELD_BOOL,
        "1",
        nullptr
    },
    {
        "process_pm",
        "Обрабатывать теги в личных сообщениях (ЛС)",
        KOTO_FIELD_BOOL,
        "0",
        nullptr
    },
    {
        "ignore_blocked",
        "Игнорировать заблокированных пользователей",
        KOTO_FIELD_BOOL,
        "0",
        nullptr
    },
    {
        "log_group_id",
        "ID группы для логов упоминаний",
        KOTO_FIELD_STR,
        "0",
        nullptr
    }
};

#ifndef KOTO_MOD_TWINS
#define KOTO_MOD_TWINS (1 << 2)
#endif

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "stags",
    "Модуль Silent Tags (Тихие теги): перехват упоминаний и закреплений с отправкой логов и поддержкой твинков (C++ ABI 3).",
    "2.9.7", 60,
    KOTO_MOD_TWINS,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    &on_watch,
    KOTO_WATCH_INCOMING,
    &on_callback,
    SETTINGS,
    sizeof(SETTINGS) / sizeof(SETTINGS[0]),
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/stags.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
