// afk.cpp — Модуль автоматического ответа (AFK) для Kotogram / KoteLoader (C++ ABI 3).
// Портирован из Python-модуля afk.py (v2.1.2) с сохранением 1:1 всех возможностей:
// • 10 кастомных премиум-эмодзи с текстовыми фоллбэками
// • Команды: .afk, .unafk, .afkexc, .setstatus, .removestatus
// • Watcher на входящие сообщения (ЛС или упоминания/ответы) с настраиваемым кулдауном
// • Поддержка исключений (exemptions)
// • Обновление профиля аккаунта (добавление/снятие " || AFK")
// Автор: Kote

#include "module_abi.h"
#include <string>
#include <vector>
#include <sstream>
#include <ctime>
#include <cstring>
#include <cstdio>
#include <cstdlib>

// ── Кастомные премиум-эмодзи ──────────────────────────────────────────────────
static constexpr long long EMOJI_ON         = 5776375003280838798LL; // ✅
static constexpr long long EMOJI_OFF        = 5774022692642492953LL; // ✅
static constexpr long long EMOJI_SLEEP      = 5201714546649104619LL; // 😴
static constexpr long long EMOJI_TIME       = 5814247475141153332LL; // 🌀
static constexpr long long EMOJI_REASON     = 5879841310902324730LL; // ✏️
static constexpr long long EMOJI_STATUS_ON  = 5897962422169243693LL; // 🦋
static constexpr long long EMOJI_STATUS_OFF = 5255831443816327915LL; // 🗑
static constexpr long long EMOJI_ERROR      = 5778527486270770928LL; // ❌
static constexpr long long EMOJI_LIST       = 5956561916573782596LL; // 📋
static constexpr long long EMOJI_SHIELD     = 5436136010578663781LL; // 🛡

static const int DEFAULT_COOLDOWN_SECONDS = 300;

// ── Вспомогательные функции ───────────────────────────────────────────────────

static std::string format_time_diff(long long start_time) {
    if (start_time <= 0) return "0:00:00";
    long long now = (long long)time(nullptr);
    long long diff = now - start_time;
    if (diff < 0) diff = 0;

    long long days = diff / 86400;
    long long rem = diff % 86400;
    long long hours = rem / 3600;
    rem %= 3600;
    long long mins = rem / 60;
    long long secs = rem % 60;

    char buf[64];
    if (days > 0) {
        snprintf(buf, sizeof(buf), "%lld days, %lld:%02lld:%02lld", days, hours, mins, secs);
    } else {
        snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", hours, mins, secs);
    }
    return std::string(buf);
}

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

// ── Управление исключениями (exemptions) ───────────────────────────────────────

static std::vector<long long> get_exemptions(koto_ctx* ctx, const koto_api* api) {
    std::vector<long long> list;
    char buf[2048] = {0};
    int len = api->db_get(ctx, "exemptions", buf, sizeof(buf));
    if (len <= 0) return list;

    std::stringstream ss(buf);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            char* end = nullptr;
            long long val = strtoll(item.c_str(), &end, 10);
            if (end != item.c_str()) {
                list.push_back(val);
            }
        }
    }
    return list;
}

static void save_exemptions(koto_ctx* ctx, const koto_api* api, const std::vector<long long>& list) {
    std::string res;
    for (size_t i = 0; i < list.size(); ++i) {
        if (i > 0) res += ",";
        res += std::to_string(list[i]);
    }
    api->db_set(ctx, "exemptions", res.c_str());
}

static bool is_exempt(koto_ctx* ctx, const koto_api* api, long long id) {
    if (id == 0) return false;
    auto list = get_exemptions(ctx, api);
    for (long long v : list) {
        if (v == id) return true;
    }
    return false;
}

static bool add_exemption(koto_ctx* ctx, const koto_api* api, long long id) {
    auto list = get_exemptions(ctx, api);
    for (long long v : list) {
        if (v == id) return false; // уже есть
    }
    list.push_back(id);
    save_exemptions(ctx, api, list);
    return true;
}

static bool del_exemption(koto_ctx* ctx, const koto_api* api, long long id) {
    auto list = get_exemptions(ctx, api);
    bool found = false;
    std::vector<long long> next;
    for (long long v : list) {
        if (v == id) {
            found = true;
        } else {
            next.push_back(v);
        }
    }
    if (found) {
        save_exemptions(ctx, api, next);
    }
    return found;
}

// ── Команда: .afk [причина] ───────────────────────────────────────────────────

static void cmd_afk(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Доступ запрещен.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    const char* args = api->cmd_args(ctx);
    while (args && *args == ' ') args++;
    std::string reason = (args && *args) ? args : "";

    char state_buf[512] = {0};
    int state_len = api->db_get(ctx, "state", state_buf, sizeof(state_buf));
    bool is_active = (state_len > 0 && strcmp(state_buf, "0") != 0);

    if (is_active) {
        // Уже в AFK — выводим текущее состояние
        long long start_time = api->db_get_int(ctx, "time", (long long)time(nullptr));
        std::string diff_str = format_time_diff(start_time);

        api->c_reset(ctx);
        api->c_emoji(ctx, "😴", EMOJI_SLEEP);
        api->c_fmt(ctx, " Сейчас я в AFK\n", KOTO_ENT_BOLD);
        api->c_emoji(ctx, "🌀", EMOJI_TIME);
        api->c_text(ctx, " Был в сети ");
        api->c_fmt(ctx, diff_str.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, " назад");

        if (state_len > 0 && strcmp(state_buf, "1") != 0) {
            api->c_text(ctx, "\n");
            api->c_emoji(ctx, "✏️", EMOJI_REASON);
            api->c_fmt(ctx, " Причина: ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, state_buf, KOTO_ENT_ITALIC);
        }
        send_result(ctx, api);
        return;
    }

    // Включаем AFK
    std::string new_state = reason.empty() ? "1" : reason;
    api->db_set(ctx, "state", new_state.c_str());
    api->db_set_int(ctx, "time", (long long)time(nullptr));

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", EMOJI_ON);
    api->c_fmt(ctx, " Вы успешно вошли в AFK режим!", KOTO_ENT_BOLD);

    if (!reason.empty()) {
        api->c_text(ctx, "\n");
        api->c_emoji(ctx, "✏️", EMOJI_REASON);
        api->c_fmt(ctx, " Причина: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, reason.c_str(), KOTO_ENT_ITALIC);
    }
    send_result(ctx, api);
}

// ── Команда: .unafk ───────────────────────────────────────────────────────────

static void cmd_unafk(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Доступ запрещен.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    char state_buf[512] = {0};
    int state_len = api->db_get(ctx, "state", state_buf, sizeof(state_buf));
    long long start_time = api->db_get_int(ctx, "time", 0);

    // Сбрасываем статус
    api->db_set(ctx, "state", "0");
    api->db_set_int(ctx, "time", 0);

    std::string diff_str = (start_time > 0) ? format_time_diff(start_time) : "неизвестно";

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", EMOJI_OFF);
    api->c_fmt(ctx, " Вы успешно вышли из AFK режима!", KOTO_ENT_BOLD);
    api->c_text(ctx, "\n\n");
    api->c_emoji(ctx, "🌀", EMOJI_TIME);
    api->c_fmt(ctx, " Отсутствовал: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, diff_str.c_str(), KOTO_ENT_CODE);

    if (state_len > 0 && strcmp(state_buf, "1") != 0 && strcmp(state_buf, "0") != 0) {
        api->c_text(ctx, "\n");
        api->c_emoji(ctx, "✏️", EMOJI_REASON);
        api->c_fmt(ctx, " Причина: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, state_buf, KOTO_ENT_ITALIC);
    }
    send_result(ctx, api);
}

// ── Команда: .afkexc <add/del/list> [user/id] ─────────────────────────────────

static void cmd_afkexc(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Доступ запрещен.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    const char* args = api->cmd_args(ctx);
    while (args && *args == ' ') args++;
    std::string args_str = args ? args : "";

    std::vector<std::string> tokens;
    {
        std::stringstream ss(args_str);
        std::string t;
        while (ss >> t) tokens.push_back(t);
    }

    if (tokens.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_text(ctx, " Использование: .afkexc <add/del/list> [user/id]");
        send_result(ctx, api);
        return;
    }

    std::string action = tokens[0];
    for (auto& c : action) c = (char)tolower(c);

    if (action == "list") {
        auto list = get_exemptions(ctx, api);
        api->c_reset(ctx);
        api->c_emoji(ctx, "📋", EMOJI_LIST);
        if (list.empty()) {
            api->c_text(ctx, " Список исключений пуст.");
        } else {
            api->c_fmt(ctx, " Список исключений AFK:\n", KOTO_ENT_BOLD);
            for (long long id : list) {
                std::string item = "• ID: " + std::to_string(id) + "\n";
                api->c_fmt(ctx, item.c_str(), KOTO_ENT_CODE);
            }
        }
        send_result(ctx, api);
        return;
    }

    // Определяем цель
    long long target_id = 0;
    std::string target_name = "Текущий чат";

    long long r_sid = api->reply_sender_id(ctx);
    if (r_sid != 0) {
        target_id = r_sid;
        char name_buf[128] = {0};
        if (api->user_name(ctx, target_id, name_buf, sizeof(name_buf)) > 0 && name_buf[0]) {
            target_name = name_buf;
        } else {
            target_name = "User " + std::to_string(target_id);
        }
    } else if (tokens.size() > 1) {
        std::string raw_target = tokens[1];
        char* end = nullptr;
        long long parsed_id = strtoll(raw_target.c_str(), &end, 10);
        if (end != raw_target.c_str() && *end == '\0') {
            target_id = parsed_id;
            target_name = "ID " + raw_target;
        } else {
            // Username
            std::string uname = raw_target;
            if (!uname.empty() && uname[0] == '@') uname = uname.substr(1);
            long long resolved = api->resolve(ctx, uname.c_str());
            if (resolved != 0) {
                target_id = resolved;
                target_name = "@" + uname;
            } else {
                target_name = raw_target;
            }
        }
    } else {
        target_id = api->chat_id(ctx);
    }

    if (target_id == 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_text(ctx, " Не удалось определить цель (пользователя или чат).");
        send_result(ctx, api);
        return;
    }

    if (action == "add") {
        if (!add_exemption(ctx, api, target_id)) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", EMOJI_ERROR);
            api->c_text(ctx, (" " + target_name + " уже в исключениях.").c_str());
            send_result(ctx, api);
            return;
        }
        api->c_reset(ctx);
        api->c_emoji(ctx, "🛡", EMOJI_SHIELD);
        api->c_fmt(ctx, (" " + target_name + " добавлен в исключения AFK.").c_str(), KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    if (action == "del") {
        if (!del_exemption(ctx, api, target_id)) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", EMOJI_ERROR);
            api->c_text(ctx, (" " + target_name + " не найден в исключениях.").c_str());
            send_result(ctx, api);
            return;
        }
        api->c_reset(ctx);
        api->c_emoji(ctx, "🗑", EMOJI_STATUS_OFF);
        api->c_fmt(ctx, (" " + target_name + " удален из исключений AFK.").c_str(), KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "❌", EMOJI_ERROR);
    api->c_text(ctx, " Неизвестное действие. Доступно: add, del, list.");
    send_result(ctx, api);
}

// ── Команда: .setstatus (добавляет ' || AFK' к фамилии/имени профиля) ─────────

static void cmd_setstatus(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Доступ запрещен.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    long long me_id = api->get_me(ctx);
    char fname[256] = {0};
    char lname[256] = {0};

    if (api->user_first_name) api->user_first_name(ctx, me_id, fname, sizeof(fname));
    if (api->user_last_name)  api->user_last_name(ctx, me_id, lname, sizeof(lname));

    if (fname[0] == '\0') {
        api->get_me_name(ctx, fname, sizeof(fname));
    }

    std::string s_fname = fname;
    std::string s_lname = lname;
    const std::string suffix = " || AFK";

    if (s_lname.find(suffix) != std::string::npos || s_fname.find(suffix) != std::string::npos) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_text(ctx, " Статус уже установлен.");
        send_result(ctx, api);
        return;
    }

    if (!s_lname.empty()) {
        s_lname += suffix;
    } else {
        s_fname += suffix;
    }

    if (api->set_name) {
        api->set_name(ctx, s_fname.c_str(), s_lname.c_str());
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "🦋", EMOJI_STATUS_ON);
    api->c_fmt(ctx, " Статус успешно установлен!", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// ── Команда: .removestatus (убирает ' || AFK') ───────────────────────────────

static void cmd_removestatus(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Доступ запрещен.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    long long me_id = api->get_me(ctx);
    char fname[256] = {0};
    char lname[256] = {0};

    if (api->user_first_name) api->user_first_name(ctx, me_id, fname, sizeof(fname));
    if (api->user_last_name)  api->user_last_name(ctx, me_id, lname, sizeof(lname));

    if (fname[0] == '\0') {
        api->get_me_name(ctx, fname, sizeof(fname));
    }

    std::string s_fname = fname;
    std::string s_lname = lname;
    const std::string suffix = " || AFK";
    bool found = false;

    if (s_lname.size() >= suffix.size() &&
        s_lname.compare(s_lname.size() - suffix.size(), suffix.size(), suffix) == 0) {
        s_lname.erase(s_lname.size() - suffix.size());
        found = true;
    } else if (s_fname.size() >= suffix.size() &&
               s_fname.compare(s_fname.size() - suffix.size(), suffix.size(), suffix) == 0) {
        s_fname.erase(s_fname.size() - suffix.size());
        found = true;
    }

    if (found) {
        if (api->set_name) {
            api->set_name(ctx, s_fname.c_str(), s_lname.c_str());
        }
        api->c_reset(ctx);
        api->c_emoji(ctx, "🗑", EMOJI_STATUS_OFF);
        api->c_fmt(ctx, " Статус успешно удален!", KOTO_ENT_BOLD);
    } else {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_text(ctx, " Статус не найден.");
    }
    send_result(ctx, api);
}

// ── Watcher: перехват входящих сообщений в ЛС или упоминаний ──────────────────

static int on_watch(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) return 0; // Только входящие

    char state_buf[512] = {0};
    int state_len = api->db_get(ctx, "state", state_buf, sizeof(state_buf));
    if (state_len <= 0 || strcmp(state_buf, "0") == 0) {
        return 0; // AFK выключен
    }

    long long cid = api->chat_id(ctx);
    long long sid = api->sender_id(ctx);
    long long me_id = api->get_me(ctx);

    if (sid == me_id) return 0; // Не реагируем на свои сообщения

    // Проверяем исключения
    if (is_exempt(ctx, api, cid) || is_exempt(ctx, api, sid)) {
        return 0;
    }

    // Проверяем: ЛС или упоминание/ответ
    bool is_private = (cid > 0 && cid == sid);
    bool is_mentioned = false;

    if (!is_private) {
        // Проверяем реплай
        long long r_sid = api->reply_sender_id(ctx);
        if (r_sid != 0 && r_sid == me_id) {
            is_mentioned = true;
        }

        // Проверяем юзернейм в тексте
        if (!is_mentioned) {
            char uname_buf[128] = {0};
            if (api->user_username && api->user_username(ctx, me_id, uname_buf, sizeof(uname_buf)) > 0 && uname_buf[0]) {
                const char* txt = api->msg_text(ctx);
                if (txt) {
                    std::string target = "@" + std::string(uname_buf);
                    if (strstr(txt, target.c_str()) != nullptr) {
                        is_mentioned = true;
                    }
                }
            }
        }

        // Проверяем entities (KOTO_ENT_MENTION или KOTO_ENT_URL tg://user?id=me)
        if (!is_mentioned && api->msg_entity_count) {
            int cnt = api->msg_entity_count(ctx);
            for (int i = 0; i < cnt; ++i) {
                int etype = 0, eoff = 0, elen = 0;
                long long doc_id = 0;
                char url[256] = {0};
                if (api->msg_entity(ctx, i, &etype, &eoff, &elen, &doc_id, url, sizeof(url))) {
                    if (etype == KOTO_ENT_URL) {
                        std::string needle = "tg://user?id=" + std::to_string(me_id);
                        if (strstr(url, needle.c_str()) != nullptr) {
                            is_mentioned = true;
                            break;
                        }
                    }
                }
            }
        }
    }

    if (!is_private && !is_mentioned) {
        return 0;
    }

    // Проверяем кулдаун
    int cooldown_sec = DEFAULT_COOLDOWN_SECONDS;
    char cfg_cd[32] = {0};
    if (api->cfg_get(ctx, "cooldown_sec", cfg_cd, sizeof(cfg_cd)) > 0) {
        int v = atoi(cfg_cd);
        if (v > 0) cooldown_sec = v;
    }

    std::string cd_key = "cd_" + std::to_string(cid);
    long long last_reply = api->db_get_int(ctx, cd_key.c_str(), 0);
    long long now = (long long)time(nullptr);

    if (now - last_reply < cooldown_sec) {
        return 0; // В пределах кулдауна
    }

    // Обновляем кулдаун
    api->db_set_int(ctx, cd_key.c_str(), now);

    long long start_time = api->db_get_int(ctx, "time", now);
    std::string diff_str = format_time_diff(start_time);

    api->c_reset(ctx);
    api->c_emoji(ctx, "😴", EMOJI_SLEEP);
    api->c_fmt(ctx, " Сейчас я в AFK\n", KOTO_ENT_BOLD);
    api->c_emoji(ctx, "🌀", EMOJI_TIME);
    api->c_text(ctx, " Был в сети ");
    api->c_fmt(ctx, diff_str.c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " назад\n");

    if (state_len > 0 && strcmp(state_buf, "1") != 0 && strcmp(state_buf, "0") != 0) {
        api->c_emoji(ctx, "✏️", EMOJI_REASON);
        api->c_fmt(ctx, " Причина: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, state_buf, KOTO_ENT_ITALIC);
    }

    api->c_reply(ctx, 0);
    return 0; // Не блокируем дальнейшую обработку
}

// ── Реестр команд модуля ──────────────────────────────────────────────────────

static const koto_command COMMANDS[] = {
    {
        "afk",
        cmd_afk,
        KOTO_LEVEL_TRUSTED,
        "Включить режим AFK или посмотреть статус",
        "[причина]"
    },
    {
        "unafk",
        cmd_unafk,
        KOTO_LEVEL_TRUSTED,
        "Выключить режим AFK",
        ""
    },
    {
        "afkexc",
        cmd_afkexc,
        KOTO_LEVEL_TRUSTED,
        "Управление исключениями AFK (add/del/list)",
        "<add/del/list> [user/id]"
    },
    {
        "setstatus",
        cmd_setstatus,
        KOTO_LEVEL_TRUSTED,
        "Добавить метку ' || AFK' к фамилии/имени профиля",
        ""
    },
    {
        "removestatus",
        cmd_removestatus,
        KOTO_LEVEL_TRUSTED,
        "Убрать метку ' || AFK' из профиля",
        ""
    }
};

static const koto_setting_field SETTINGS[] = {
    {
        "cooldown_sec",
        "Кулдаун ответа в чате (секунды)",
        KOTO_FIELD_INT,
        "300",
        nullptr
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "afk",
    "Модуль автоматического ответа (AFK). Включает в себя кулдаун ответов в ЛС и систему исключений.",
    "2.1.2",
    26, // min_host_version (требует v0.3.6+ для api->set_name)
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    on_watch,
    KOTO_WATCH_INCOMING,
    nullptr, // on_callback
    SETTINGS,
    sizeof(SETTINGS) / sizeof(SETTINGS[0]),
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/afk.so"
};

const koto_module* koto_module_register(void) {
    return &MODULE;
}
