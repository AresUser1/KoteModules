// autoresponder.cpp — Продвинутый автоответчик для ЛС (C++ ABI 3).
// Портирован из Python-модуля autoresponder.py (v2.4.2).
// Автор: Kote

#include "module_abi.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <algorithm>

// ── Кастомные премиум-эмодзи ──────────────────────────────────────────────────
static constexpr long long ICON_BOT   = 6030400221232501136LL; // 🤖
static constexpr long long ICON_ON    = 5776375003280838798LL; // ✅
static constexpr long long ICON_OFF   = 5778527486270770928LL; // ❌
static constexpr long long ICON_WARN  = 5881702736843511327LL; // ⚠️
static constexpr long long ICON_SAVE  = 5877307202888273539LL; // 💾
static constexpr long long ICON_TRASH = 5879896690210639947LL; // 🗑
static constexpr long long ICON_READ  = 5823396554345549784LL; // 👁
static constexpr long long ICON_GHOST = 5897962422169243693LL; // 👻
static constexpr long long ICON_BLOCK = 5775887550262546277LL; // 🛡
static constexpr long long ICON_TIME  = 5985616167740379273LL; // ⏰
static constexpr long long ICON_PREM  = 5956148757899776734LL; // 💎
static constexpr long long ICON_PIC   = 5776004296063585809LL; // 📸

// Кэш времени последнего ответа каждому пользователю (user_id -> unix_time)
static std::map<long long, time_t> g_cooldown_cache;

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

// ── Проверка нахождения в заданном временном окне ("HH:MM-HH:MM") ─────────────
static bool is_in_time_range(const std::string& time_range_str) {
    if (time_range_str.empty() || time_range_str == "Always") return true;

    size_t dash = time_range_str.find('-');
    if (dash == std::string::npos) return true;

    std::string s_str = trim(time_range_str.substr(0, dash));
    std::string e_str = trim(time_range_str.substr(dash + 1));

    int s_h = 0, s_m = 0, e_h = 0, e_m = 0;
    if (sscanf(s_str.c_str(), "%d:%d", &s_h, &s_m) != 2) return true;
    if (sscanf(e_str.c_str(), "%d:%d", &e_h, &e_m) != 2) return true;

    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    int now_mins = tm_now.tm_hour * 60 + tm_now.tm_min;
    int start_mins = s_h * 60 + s_m;
    int end_mins = e_h * 60 + e_m;

    if (start_mins <= end_mins) {
        return (now_mins >= start_mins && now_mins <= end_mins);
    } else {
        // Переход через полночь (например, 22:00-06:00)
        return (now_mins >= start_mins || now_mins <= end_mins);
    }
}

// ── Чтение / запись списка игнорируемых чатов ─────────────────────────────────
static std::set<long long> load_ignored_chats(koto_ctx* ctx, const koto_api* api) {
    std::set<long long> res;
    char buf[16384];
    int len = api->db_get(ctx, "ignored_chats", buf, sizeof(buf) - 1);
    if (len <= 0) return res;
    buf[len] = '\0';

    std::istringstream iss(buf);
    std::string line;
    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty()) continue;
        char* end = nullptr;
        long long id = strtoll(line.c_str(), &end, 10);
        if (id != 0) res.insert(id);
    }
    return res;
}

static void save_ignored_chats(koto_ctx* ctx, const koto_api* api, const std::set<long long>& chats) {
    std::string s;
    for (long long id : chats) {
        s += std::to_string(id) + "\n";
    }
    api->db_set(ctx, "ignored_chats", s.c_str());
}

// ── Проверка статуса Premium у текущего аккаунта ──────────────────────────────
static bool check_is_premium(koto_ctx* ctx, const koto_api* api) {
    long long me = api->get_me ? api->get_me(ctx) : 0;
    if (me != 0 && api->user_full_json) {
        char buf[4096];
        int len = api->user_full_json(ctx, me, buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            std::string j(buf);
            if (j.find("\"is_premium\":true") != std::string::npos ||
                j.find("\"premium\":true") != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

// ── .pm ─────────────────────────────────────────────────────────────────────
static void cmd_pm(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    bool is_premium = check_is_premium(ctx, api);

    const char* raw_args = api->cmd_args(ctx);
    std::string args_str = raw_args ? trim(raw_args) : "";

    std::string sub_cmd = "stats";
    std::string sub_arg = "";

    if (!args_str.empty()) {
        size_t sp = args_str.find(' ');
        if (sp != std::string::npos) {
            sub_cmd = args_str.substr(0, sp);
            sub_arg = trim(args_str.substr(sp + 1));
        } else {
            sub_cmd = args_str;
        }
    }
    for (auto& c : sub_cmd) c = (char)std::tolower((unsigned char)c);

    // .pm on / .pm off
    if (sub_cmd == "on" || sub_cmd == "off") {
        bool state = (sub_cmd == "on");
        api->db_set(ctx, "enabled", state ? "1" : "0");

        api->c_reset(ctx);
        api->c_emoji(ctx, "🤖", ICON_BOT);
        api->c_fmt(ctx, " Автоответчик: ", KOTO_ENT_BOLD);
        api->c_emoji(ctx, state ? "✅" : "❌", state ? ICON_ON : ICON_OFF);
        api->c_fmt(ctx, state ? " Включен" : " Выключен", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    // .pm text <текст>
    if (sub_cmd == "text") {
        if (sub_arg.empty()) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "⚠️", ICON_WARN);
            api->c_fmt(ctx, " Укажите текст для автоответа!", KOTO_ENT_BOLD);
            send_result(ctx, api);
            return;
        }
        api->db_set(ctx, "message_text", sub_arg.c_str());

        api->c_reset(ctx);
        api->c_emoji(ctx, "💾", ICON_SAVE);
        api->c_fmt(ctx, " Обычный текст сохранен.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    // .pm premtext <текст>
    if (sub_cmd == "premtext") {
        if (sub_arg.empty()) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "⚠️", ICON_WARN);
            api->c_fmt(ctx, " Укажите текст для Premium автоответа!", KOTO_ENT_BOLD);
            send_result(ctx, api);
            return;
        }
        api->db_set(ctx, "message_text_prem", sub_arg.c_str());

        api->c_reset(ctx);
        api->c_emoji(ctx, "💎", ICON_PREM);
        api->c_fmt(ctx, " Premium текст сохранен.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    // .pm time <HH:MM-HH:MM | clear>
    if (sub_cmd == "time") {
        if (sub_arg.empty() || sub_arg == "clear") {
            api->db_set(ctx, "working_hours", "Always");
            api->c_reset(ctx);
            api->c_emoji(ctx, "⏰", ICON_TIME);
            api->c_fmt(ctx, " Режим: 24/7 (Always)", KOTO_ENT_BOLD);
        } else {
            api->db_set(ctx, "working_hours", sub_arg.c_str());
            api->c_reset(ctx);
            api->c_emoji(ctx, "⏰", ICON_TIME);
            api->c_fmt(ctx, (" Время работы: " + sub_arg).c_str(), KOTO_ENT_BOLD);
        }
        send_result(ctx, api);
        return;
    }

    // .pm cd <сек>
    if (sub_cmd == "cd") {
        int cd_val = atoi(sub_arg.c_str());
        if (cd_val < 0) cd_val = 0;
        api->db_set_int(ctx, "cooldown", cd_val);

        api->c_reset(ctx);
        api->c_emoji(ctx, "⏰", ICON_TIME);
        api->c_fmt(ctx, (" Кулдаун установлен: " + std::to_string(cd_val) + "с").c_str(), KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    // .pm pic / .pm nopic
    if (sub_cmd == "pic") {
        // Прикрепление медиа через путь в аргументах или сохраненный URL
        if (sub_arg.empty()) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "⚠️", ICON_WARN);
            api->c_fmt(ctx, " Укажите путь к локальному файлу или ссылку на медиа: .pm pic <path/url>", KOTO_ENT_BOLD);
            send_result(ctx, api);
            return;
        }
        api->db_set(ctx, "media_path", sub_arg.c_str());

        api->c_reset(ctx);
        api->c_emoji(ctx, "📸", ICON_PIC);
        api->c_fmt(ctx, " Медиа прикреплено.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }
    if (sub_cmd == "nopic") {
        api->db_del(ctx, "media_path");

        api->c_reset(ctx);
        api->c_emoji(ctx, "🗑", ICON_TRASH);
        api->c_fmt(ctx, " Медиа удалено.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    // .pm block [on/off]
    if (sub_cmd == "block") {
        bool b_state = (!sub_arg.empty() && sub_arg == "on");
        if (sub_arg.empty()) {
            char bbuf[32] = {0};
            int len = api->db_get(ctx, "action_block", bbuf, sizeof(bbuf) - 1);
            b_state = !(len > 0 && strcmp(bbuf, "1") == 0);
        }
        api->db_set(ctx, "action_block", b_state ? "1" : "0");

        api->c_reset(ctx);
        api->c_emoji(ctx, b_state ? "🛡" : "❌", b_state ? ICON_BLOCK : ICON_OFF);
        api->c_fmt(ctx, b_state ? " Авто-блокировка: ВКЛ" : " Авто-блокировка: ВЫКЛ", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    // .pm read / .pm ghost
    if (sub_cmd == "read" || sub_cmd == "ghost") {
        char rbuf[32] = {0};
        int len = api->db_get(ctx, "action_read", rbuf, sizeof(rbuf) - 1);
        bool current_read = !(len > 0 && strcmp(rbuf, "0") == 0);

        bool new_read = current_read;
        if (!sub_arg.empty()) {
            bool arg_on = (sub_arg == "on");
            new_read = (sub_cmd == "read") ? arg_on : !arg_on;
        } else {
            new_read = !current_read;
        }
        api->db_set(ctx, "action_read", new_read ? "1" : "0");

        api->c_reset(ctx);
        api->c_emoji(ctx, new_read ? "👁" : "👻", new_read ? ICON_READ : ICON_GHOST);
        api->c_fmt(ctx, new_read ? " Режим чтения (сообщения помечаются прочитанными)" : " Режим Призрака (без отметки о прочтении)", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    // .pm stats (главная панель конфигурации)
    char e_buf[32] = {0};
    int e_len = api->db_get(ctx, "enabled", e_buf, sizeof(e_buf) - 1);
    bool is_enabled = (e_len > 0 && strcmp(e_buf, "1") == 0);

    char txt_buf[4096] = {0};
    int txt_len = api->db_get(ctx, "message_text", txt_buf, sizeof(txt_buf) - 1);
    std::string text_norm = (txt_len > 0) ? std::string(txt_buf, txt_len) : "Auto-reply";

    char ptxt_buf[4096] = {0};
    int ptxt_len = api->db_get(ctx, "message_text_prem", ptxt_buf, sizeof(ptxt_buf) - 1);
    std::string text_prem = (ptxt_len > 0) ? std::string(ptxt_buf, ptxt_len) : "";

    char h_buf[64] = {0};
    int h_len = api->db_get(ctx, "working_hours", h_buf, sizeof(h_buf) - 1);
    std::string hours = (h_len > 0) ? std::string(h_buf, h_len) : "Always";

    long long cooldown = api->db_get_int(ctx, "cooldown", 300);

    char m_buf[1024] = {0};
    int m_len = api->db_get(ctx, "media_path", m_buf, sizeof(m_buf) - 1);
    bool has_media = (m_len > 0 && m_buf[0]);

    char blk_buf[32] = {0};
    int blk_len = api->db_get(ctx, "action_block", blk_buf, sizeof(blk_buf) - 1);
    bool is_block = (blk_len > 0 && strcmp(blk_buf, "1") == 0);

    char rd_buf[32] = {0};
    int rd_len = api->db_get(ctx, "action_read", rd_buf, sizeof(rd_buf) - 1);
    bool is_read = !(rd_len > 0 && strcmp(rd_buf, "0") == 0);

    std::string active_mode = (is_premium && !text_prem.empty()) ? "PREM" : "NORM";

    auto preview = [](const std::string& t) {
        if (t.empty()) return std::string("—");
        return (t.size() > 40) ? t.substr(0, 40) + "..." : t;
    };

    api->c_reset(ctx);
    api->c_emoji(ctx, "🤖", ICON_BOT);
    api->c_fmt(ctx, " AutoResponder Config\n\n", KOTO_ENT_BOLD);

    api->c_emoji(ctx, is_enabled ? "✅" : "❌", is_enabled ? ICON_ON : ICON_OFF);
    api->c_fmt(ctx, is_enabled ? " Статус: ВКЛ\n" : " Статус: ВЫКЛ\n", KOTO_ENT_BOLD);

    api->c_emoji(ctx, is_premium ? "💎" : "🤖", is_premium ? ICON_PREM : ICON_BOT);
    api->c_fmt(ctx, is_premium ? " Аккаунт: Premium\n\n" : " Аккаунт: Basic\n\n", KOTO_ENT_BOLD);

    api->c_fmt(ctx, (active_mode == "NORM" ? "✅ " : "◽️ "), KOTO_ENT_BOLD);
    api->c_fmt(ctx, "Обычный текст:\n", KOTO_ENT_BOLD);
    api->c_fmt(ctx, (preview(text_norm) + "\n\n").c_str(), KOTO_ENT_CODE);

    api->c_fmt(ctx, (active_mode == "PREM" ? "✅ " : "◽️ "), KOTO_ENT_BOLD);
    api->c_emoji(ctx, "💎", ICON_PREM);
    api->c_fmt(ctx, " Premium текст:\n", KOTO_ENT_BOLD);
    api->c_fmt(ctx, (preview(text_prem) + "\n\n").c_str(), KOTO_ENT_CODE);

    api->c_emoji(ctx, has_media ? "📸" : "🗑", has_media ? ICON_PIC : ICON_TRASH);
    api->c_fmt(ctx, has_media ? " Медиа: ЕСТЬ  " : " Медиа: НЕТ  ", KOTO_ENT_BOLD);

    api->c_emoji(ctx, "⏰", ICON_TIME);
    api->c_fmt(ctx, (" " + std::to_string(cooldown) + "с  ").c_str(), KOTO_ENT_BOLD);

    api->c_emoji(ctx, is_block ? "🛡" : "❌", is_block ? ICON_BLOCK : ICON_OFF);
    api->c_fmt(ctx, is_block ? " Блок: ДА  " : " Блок: НЕТ  ", KOTO_ENT_BOLD);

    api->c_emoji(ctx, is_read ? "👁" : "👻", is_read ? ICON_READ : ICON_GHOST);
    api->c_fmt(ctx, is_read ? " Чтение: ДА\n" : " Чтение: НЕТ (Ghost)\n", KOTO_ENT_BOLD);

    api->c_emoji(ctx, "⏰", ICON_TIME);
    api->c_fmt(ctx, (" Время работы: " + hours).c_str(), KOTO_ENT_BOLD);

    send_result(ctx, api);
}

// ── .pmignore ───────────────────────────────────────────────────────────────
static void cmd_pmignore(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    auto ignored = load_ignored_chats(ctx, api);

    bool added = false;
    if (ignored.find(cid) != ignored.end()) {
        ignored.erase(cid);
        added = false;
    } else {
        ignored.insert(cid);
        added = true;
    }
    save_ignored_chats(ctx, api, ignored);

    api->c_reset(ctx);
    api->c_emoji(ctx, added ? "✅" : "🗑", added ? ICON_ON : ICON_TRASH);
    api->c_fmt(ctx, added ? " Чат добавлен в игнор-лист автоответчика." : " Чат удален из игнор-листа автоответчика.", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// ── Watcher (Перехват входящих ЛС) ──────────────────────────────────────────
static int on_watch(koto_ctx* ctx, const koto_api* api) {
    // Только входящие
    if (api->is_outgoing(ctx)) return 0;

    long long cid = api->chat_id(ctx);
    long long sid = api->sender_id(ctx);
    long long me = api->get_me ? api->get_me(ctx) : 0;

    // Тип чата задаётся Telegram peer; положительный ID бывает и у группы.
    if (api->chat_type(ctx) != KOTO_CHAT_PRIVATE || sid == me) return 0;

    // Проверяем включение модуля
    char e_buf[32] = {0};
    int e_len = api->db_get(ctx, "enabled", e_buf, sizeof(e_buf) - 1);
    if (e_len <= 0 || strcmp(e_buf, "1") != 0) return 0;

    // Проверяем игнор-лист
    auto ignored = load_ignored_chats(ctx, api);
    if (ignored.find(cid) != ignored.end()) return 0;

    // Проверяем рабочие часы
    char h_buf[64] = {0};
    int h_len = api->db_get(ctx, "working_hours", h_buf, sizeof(h_buf) - 1);
    std::string hours = (h_len > 0) ? std::string(h_buf, h_len) : "Always";
    if (!is_in_time_range(hours)) return 0;

    // Проверяем кулдаун
    long long cd_sec = api->db_get_int(ctx, "cooldown", 300);
    time_t now = time(nullptr);
    auto it = g_cooldown_cache.find(sid);
    if (it != g_cooldown_cache.end()) {
        if (now - it->second < cd_sec) return 0;
    }
    g_cooldown_cache[sid] = now;

    // Выбираем текст ответа (обычный или Premium)
    bool is_premium = check_is_premium(ctx, api);

    char txt_buf[4096] = {0};
    int txt_len = api->db_get(ctx, "message_text", txt_buf, sizeof(txt_buf) - 1);
    std::string text_norm = (txt_len > 0) ? std::string(txt_buf, txt_len) : "Auto-reply";

    char ptxt_buf[4096] = {0};
    int ptxt_len = api->db_get(ctx, "message_text_prem", ptxt_buf, sizeof(ptxt_buf) - 1);
    std::string text_prem = (ptxt_len > 0) ? std::string(ptxt_buf, ptxt_len) : "";

    std::string final_text = (is_premium && !text_prem.empty()) ? text_prem : text_norm;

    // Проверяем наличие прикрепленного медиа
    char m_buf[1024] = {0};
    int m_len = api->db_get(ctx, "media_path", m_buf, sizeof(m_buf) - 1);
    bool has_media = (m_len > 0 && m_buf[0]);

    if (has_media) {
        api->c_reset(ctx);
        api->c_markdown(ctx, final_text.c_str());
        api->c_send_file(ctx, cid, m_buf, 0);
    } else {
        api->c_reset(ctx);
        api->c_markdown(ctx, final_text.c_str());
        api->c_reply(ctx, 0);
    }

    // Если включена авто-блокировка
    char blk_buf[32] = {0};
    int blk_len = api->db_get(ctx, "action_block", blk_buf, sizeof(blk_buf) - 1);
    if (blk_len > 0 && strcmp(blk_buf, "1") == 0) {
        if (api->ban_user) {
            api->ban_user(ctx, cid, sid, 0);
        }
    }

    return 0;
}

// ── Колбэк (защита владельца) ───────────────────────────────────────────────
static void on_callback(koto_ctx* ctx, const koto_api* api) {
    (void)ctx; (void)api;
}

// ── Схема настроек модуля для панели .cfg ────────────────────────────────────
static const koto_setting_field SETTINGS[] = {
    {
        "enabled",
        "Включить автоответчик",
        KOTO_FIELD_BOOL,
        "false",
        nullptr
    },
    {
        "cooldown",
        "Кулдаун между ответами (секунды)",
        KOTO_FIELD_INT,
        "300",
        nullptr
    },
    {
        "working_hours",
        "Часы работы (Always или HH:MM-HH:MM)",
        KOTO_FIELD_STR,
        "Always",
        nullptr
    },
    {
        "action_block",
        "Блокировать отправителя после ответа",
        KOTO_FIELD_BOOL,
        "false",
        nullptr
    },
    {
        "action_read",
        "Помечать входящие сообщения прочитанными",
        KOTO_FIELD_BOOL,
        "true",
        nullptr
    }
};

static const koto_command COMMANDS[] = {
    {
        "pm",
        &cmd_pm,
        KOTO_LEVEL_TRUSTED,
        "Главная панель настройки автоответчика в ЛС.",
        "[on|off|text|premtext|time|pic|nopic|cd|block|read|ghost|stats]"
    },
    {
        "pmignore",
        &cmd_pmignore,
        KOTO_LEVEL_TRUSTED,
        "Добавить или удалить текущий чат из игнор-листа автоответчика.",
        nullptr
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "autoresponder",
    "Продвинутый автоответчик для ЛС с Rich Text, медиа и кулдауном (C++ ABI 3).",
    "2.4.3", 60,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    &on_watch,
    KOTO_WATCH_INCOMING,
    &on_callback,
    SETTINGS,
    sizeof(SETTINGS) / sizeof(SETTINGS[0]),
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/autoresponder.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
