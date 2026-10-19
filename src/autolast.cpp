// autolast.cpp — Автоматический 'ласт' ответ с поддержкой ошибок и хардмода (C++ ABI 3).
// Портирован из Python-модуля autolast.py (v1.7.1).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include <string>
#include <vector>
#include <set>
#include <map>
#include <sstream>
#include <regex>
#include <ctime>
#include <cctype>

namespace {

static constexpr long long EMO_OK    = 5774022692642492953LL; // ✅
static constexpr long long EMO_DEL   = 6039522349517115015LL; // 🗑
static constexpr long long EMO_USER  = 6035084557378654059LL; // 👤
static constexpr long long EMO_ADMIN = 6030445631921721471LL; // 🛡
static constexpr long long EMO_WARN  = 6030563507299160824LL; // ⚠️
static constexpr long long EMO_INFO  = 6028435952299413210LL; // ℹ️
static constexpr long long EMO_LIST  = 5778299625370817409LL; // 📝
static constexpr long long EMO_HARD  = 5956148757899776734LL; // 🔴

static std::map<long long, time_t> g_last_notify_time; // chat_id -> timestamp

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

// ── Хранилище списков в БД ───────────────────────────────────────────────────

static std::set<long long> load_id_set(const koto_api* api, koto_ctx* ctx, const char* key) {
    std::set<long long> s;
    char buf[16384] = {0};
    int n = api->db_get(ctx, key, buf, sizeof(buf) - 1);
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

static void save_id_set(const koto_api* api, koto_ctx* ctx, const char* key, const std::set<long long>& s) {
    std::string out;
    for (long long id : s) {
        out += std::to_string(id) + "\n";
    }
    api->db_set(ctx, key, out.c_str());
}

static bool get_flag(const koto_api* api, koto_ctx* ctx, const char* key) {
    return koto_utils::db_get_int64(api, ctx, key, 0) == 1;
}

static void set_flag(const koto_api* api, koto_ctx* ctx, const char* key, bool val) {
    koto_utils::db_set_int64(api, ctx, key, val ? 1 : 0);
}

// Определение целевого ID (из аргументов, реплая или текущего чата)
static long long resolve_target(koto_ctx* ctx, const koto_api* api) {
    long long r_sender = api->reply_sender_id ? api->reply_sender_id(ctx) : 0;
    if (r_sender > 0) return r_sender;

    const char* raw = api->cmd_args(ctx);
    std::string arg = raw ? koto_utils::trim(raw) : "";
    if (!arg.empty()) {
        char* end = nullptr;
        long long val = strtoll(arg.c_str(), &end, 10);
        if (*end == '\0') return val;

        if (arg[0] == '@' && api->resolve) {
            long long res = api->resolve(ctx, arg.c_str() + 1);
            if (res != 0) return res;
        }
    }
    return api->chat_id(ctx);
}

// ── Проверка триггеров "Готовы", "го" ─────────────────────────────────────────

static bool check_trigger(const std::string& text) {
    if (text.empty()) return false;

    std::string msg;
    for (char c : text) {
        msg += static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }

    // 1. Очистка текста от всего кроме букв и цифр (UTF-8 сохраняем)
    std::string clean;
    for (size_t i = 0; i < msg.size();) {
        unsigned char c = msg[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            clean += static_cast<char>(c);
            i++;
        } else if (c >= 0xC0) { // UTF-8 кириллица
            size_t len = 2;
            if ((c & 0xF0) == 0xE0) len = 3;
            else if ((c & 0xF8) == 0xF0) len = 4;
            if (i + len <= msg.size()) {
                clean += msg.substr(i, len);
            }
            i += len;
        } else {
            i++;
        }
    }

    // Проверка "го" как отдельного слова
    if (clean == "го" || clean == "гоо" || clean == "гооо" || clean == "go") {
        return true;
    }

    // Поиск "готовы" и вариаций
    if (clean.find("готов") != std::string::npos ||
        clean.find("готова") != std::string::npos ||
        clean.find("готови") != std::string::npos ||
        clean.find("гатов") != std::string::npos ||
        clean.find("ready") != std::string::npos) {
        return true;
    }

    return false;
}

// ── Команды ───────────────────────────────────────────────────────────────────

static void cmd_addlastchat(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long target = resolve_target(ctx, api);
    if (target == 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "⚠️", EMO_WARN);
        api->c_fmt(ctx, " Не удалось найти чат.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    std::set<long long> chats = load_id_set(api, ctx, "last_chats");
    if (chats.find(target) == chats.end()) {
        chats.insert(target);
        save_id_set(api, ctx, "last_chats", chats);
        api->c_reset(ctx);
        api->c_emoji(ctx, "✅", EMO_OK);
        api->c_text(ctx, " Чат ");
        api->c_fmt(ctx, std::to_string(target).c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, " добавлен в список.", KOTO_ENT_BOLD);
    } else {
        api->c_reset(ctx);
        api->c_emoji(ctx, "ℹ️", EMO_INFO);
        api->c_fmt(ctx, " Чат уже в списке.", KOTO_ENT_BOLD);
    }
    send_result(ctx, api);
}

static void cmd_rmlastchat(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long target = resolve_target(ctx, api);
    std::set<long long> chats = load_id_set(api, ctx, "last_chats");
    if (chats.find(target) != chats.end()) {
        chats.erase(target);
        save_id_set(api, ctx, "last_chats", chats);
        api->c_reset(ctx);
        api->c_emoji(ctx, "🗑", EMO_DEL);
        api->c_text(ctx, " Чат ");
        api->c_fmt(ctx, std::to_string(target).c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, " удален.", KOTO_ENT_BOLD);
    } else {
        api->c_reset(ctx);
        api->c_emoji(ctx, "⚠️", EMO_WARN);
        api->c_fmt(ctx, " Чата нет в списке.", KOTO_ENT_BOLD);
    }
    send_result(ctx, api);
}

static void cmd_addlastuser(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long target = 0;
    if (api->reply_sender_id) target = api->reply_sender_id(ctx);
    if (target == 0) {
        const char* raw = api->cmd_args(ctx);
        if (raw) target = strtoll(koto_utils::trim(raw).c_str(), nullptr, 10);
    }

    if (target == 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Укажите пользователя (ID или ответ на сообщение).");
        send_result(ctx, api);
        return;
    }

    std::set<long long> users = load_id_set(api, ctx, "last_users");
    if (users.find(target) == users.end()) {
        users.insert(target);
        save_id_set(api, ctx, "last_users", users);
        api->c_reset(ctx);
        api->c_emoji(ctx, "👤", EMO_USER);
        api->c_text(ctx, " Юзер ");
        api->c_fmt(ctx, std::to_string(target).c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, " добавлен в список.", KOTO_ENT_BOLD);
    } else {
        api->c_reset(ctx);
        api->c_emoji(ctx, "ℹ️", EMO_INFO);
        api->c_fmt(ctx, " Юзер уже в списке.", KOTO_ENT_BOLD);
    }
    send_result(ctx, api);
}

static void cmd_rmlastuser(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long target = 0;
    if (api->reply_sender_id) target = api->reply_sender_id(ctx);
    if (target == 0) {
        const char* raw = api->cmd_args(ctx);
        if (raw) target = strtoll(koto_utils::trim(raw).c_str(), nullptr, 10);
    }

    std::set<long long> users = load_id_set(api, ctx, "last_users");
    if (users.find(target) != users.end()) {
        users.erase(target);
        save_id_set(api, ctx, "last_users", users);
        api->c_reset(ctx);
        api->c_emoji(ctx, "🗑", EMO_DEL);
        api->c_text(ctx, " Юзер ");
        api->c_fmt(ctx, std::to_string(target).c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, " удален.", KOTO_ENT_BOLD);
    } else {
        api->c_reset(ctx);
        api->c_emoji(ctx, "⚠️", EMO_WARN);
        api->c_fmt(ctx, " Юзера нет в списке.", KOTO_ENT_BOLD);
    }
    send_result(ctx, api);
}

static void cmd_lastadmin(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    bool current = get_flag(api, ctx, "last_admin_only");
    set_flag(api, ctx, "last_admin_only", !current);

    api->c_reset(ctx);
    api->c_emoji(ctx, "🛡", EMO_ADMIN);
    api->c_fmt(ctx, " Режим 'Только админы': ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, !current ? "ВКЛ" : "ВЫКЛ", KOTO_ENT_CODE);
    send_result(ctx, api);
}

static void cmd_lasthard(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    bool current = get_flag(api, ctx, "last_hardmode");
    set_flag(api, ctx, "last_hardmode", !current);

    api->c_reset(ctx);
    api->c_emoji(ctx, "🔴", EMO_HARD);
    api->c_fmt(ctx, " Хардмод (отвечать на всё): ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, !current ? "ВКЛ" : "ВЫКЛ", KOTO_ENT_CODE);
    send_result(ctx, api);
}

static void cmd_listlast(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::set<long long> chats = load_id_set(api, ctx, "last_chats");
    std::set<long long> users = load_id_set(api, ctx, "last_users");
    bool admin_only = get_flag(api, ctx, "last_admin_only");
    bool hardmode = get_flag(api, ctx, "last_hardmode");

    api->c_reset(ctx);
    api->c_emoji(ctx, "📝", EMO_LIST);
    api->c_fmt(ctx, " Настройки Last:\n\n", KOTO_ENT_BOLD);

    api->c_emoji(ctx, "✅", EMO_OK);
    api->c_text(ctx, " Чаты: ");
    api->c_fmt(ctx, (std::to_string(chats.size()) + "\n").c_str(), KOTO_ENT_CODE);

    api->c_emoji(ctx, "👤", EMO_USER);
    api->c_text(ctx, " Юзеры: ");
    api->c_fmt(ctx, (std::to_string(users.size()) + "\n").c_str(), KOTO_ENT_CODE);

    api->c_emoji(ctx, "🛡", EMO_ADMIN);
    api->c_text(ctx, " Только админы: ");
    api->c_text(ctx, admin_only ? "✅\n" : "❌\n");

    api->c_emoji(ctx, "🔴", EMO_HARD);
    api->c_text(ctx, " Хардмод: ");
    api->c_text(ctx, hardmode ? "✅\n" : "❌\n");

    send_result(ctx, api);
}

// ── Watcher: автоматический ответ "ласт" ──────────────────────────────────────

static int on_watch(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) return 0;

    long long chat_id = api->chat_id(ctx);
    if (chat_id == 0) return 0;

    std::set<long long> chats = load_id_set(api, ctx, "last_chats");
    if (chats.find(chat_id) == chats.end()) return 0;

    long long sender_id = api->sender_id(ctx);
    std::set<long long> users = load_id_set(api, ctx, "last_users");
    if (!users.empty() && users.find(sender_id) == users.end()) return 0;

    const char* text_raw = api->msg_text(ctx);
    if (!text_raw) return 0;
    std::string text = text_raw;

    // Кулдаун 2 секунды на чат
    time_t now = time(nullptr);
    auto it = g_last_notify_time.find(chat_id);
    if (it != g_last_notify_time.end() && (now - it->second) < 2) {
        return 0;
    }

    bool hardmode = get_flag(api, ctx, "last_hardmode");
    bool triggered = hardmode || check_trigger(text);

    if (triggered) {
        g_last_notify_time[chat_id] = now;
        api->c_reset(ctx);
        api->c_text(ctx, "ласт");
        api->c_reply(ctx, 0);
    }

    return 0;
}

const koto_command COMMANDS[] = {
    {
        "addlastchat",
        &cmd_addlastchat,
        KOTO_LEVEL_TRUSTED,
        "Добавить чат в белый список autolast.",
        "<ID/ответ>"
    },
    {
        "rmlastchat",
        &cmd_rmlastchat,
        KOTO_LEVEL_TRUSTED,
        "Удалить чат из белого списка autolast.",
        "<ID/ответ>"
    },
    {
        "addlastuser",
        &cmd_addlastuser,
        KOTO_LEVEL_TRUSTED,
        "Добавить юзера в белый список autolast.",
        "<ID/ответ>"
    },
    {
        "rmlastuser",
        &cmd_rmlastuser,
        KOTO_LEVEL_TRUSTED,
        "Удалить юзера из белого списка autolast.",
        "<ID/ответ>"
    },
    {
        "lastadmin",
        &cmd_lastadmin,
        KOTO_LEVEL_TRUSTED,
        "Переключить режим ответа только админам.",
        nullptr
    },
    {
        "lasthard",
        &cmd_lasthard,
        KOTO_LEVEL_TRUSTED,
        "Переключить режим хардмода (отвечать на всё подряд).",
        nullptr
    },
    {
        "listlast",
        &cmd_listlast,
        KOTO_LEVEL_TRUSTED,
        "Показать конфигурацию и списки autolast.",
        nullptr
    }
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "autolast",
    "Продвинутый 'ласт' автоответчик для игр (готовы / го) с хардмодом (C++ ABI 3).",
    "1.7.1",
    26,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    &on_watch,
    KOTO_WATCH_INCOMING,
    nullptr,
    nullptr, 0,
    nullptr
};

} // namespace

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
