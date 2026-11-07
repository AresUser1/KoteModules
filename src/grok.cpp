// grok.cpp — Модуль для нейросети Grok через Telegram-бота @GrokAI (C++ ABI 3).
// Портирован из Python/Telethon модуля GrokAIMod (by kote).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"

#include <string>
#include <vector>
#include <mutex>
#include <ctime>
#include <cstring>
#include <cstdlib>

namespace {

constexpr const char* VERSION = "1.0.0";
constexpr long long DEFAULT_BOT_ID = 7828964235LL;
constexpr const char* DEFAULT_BOT_USERNAME = "@GrokAI";

struct PendingRequest {
    long long chat_id = 0;
    long long msg_id = 0;
    std::string query;
    time_t timestamp = 0;
    bool is_reset = false;
};

static std::mutex g_mu;
static PendingRequest g_pending;

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

static long long get_target_bot_id(koto_ctx* ctx, const koto_api* api) {
    long long bid = 0;
    if (api->setting_get_int) {
        bid = api->setting_get_int(ctx, "bot_id", DEFAULT_BOT_ID);
    }
    return bid > 0 ? bid : DEFAULT_BOT_ID;
}

// ── Command: .grok <текст> или в ответ ────────────────────────────────────────
void cmd_grok(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    // 1. Извлекаем текст запроса: аргументы или текст из reply
    std::string query;
    const char* raw_args = api->cmd_args(ctx);
    if (raw_args && raw_args[0]) {
        query = trim(raw_args);
    }

    if (query.empty() && api->reply_text) {
        const char* r_text = api->reply_text(ctx);
        if (r_text && r_text[0]) {
            query = trim(r_text);
        }
    }

    // 2. Валидация длины
    if (query.length() < 3) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "🚫 Ошибка!\nСлишком маленький запрос.", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    // 3. Отправляем прогресс пользователю
    api->c_reset(ctx);
    api->c_fmt(ctx, "🤖 Нейросеть обрабатывает ваш запрос…", KOTO_ENT_BOLD);
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }

    long long cur_chat_id = api->chat_id(ctx);
    long long cur_msg_id = api->msg_id(ctx);
    long long bot_id = get_target_bot_id(ctx, api);

    // 4. Фиксируем ожидание ответа
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_pending.chat_id = cur_chat_id;
        g_pending.msg_id = cur_msg_id;
        g_pending.query = query;
        g_pending.timestamp = time(nullptr);
        g_pending.is_reset = false;
    }

    // 5. Отправляем запрос боту Grok
    api->c_reset(ctx);
    api->c_text(ctx, query.c_str());
    api->c_send(ctx, bot_id, 0);
}

// ── Command: .grokdel / .grokreset — очистка контекста ─────────────────────────
void cmd_grokdel(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    api->c_reset(ctx);
    api->c_fmt(ctx, "🔄 Очищаю контекст переписки с Grok…", KOTO_ENT_BOLD);
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }

    long long cur_chat_id = api->chat_id(ctx);
    long long cur_msg_id = api->msg_id(ctx);
    long long bot_id = get_target_bot_id(ctx, api);

    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_pending.chat_id = cur_chat_id;
        g_pending.msg_id = cur_msg_id;
        g_pending.query = "";
        g_pending.timestamp = time(nullptr);
        g_pending.is_reset = true;
    }

    // Отправляем /newchat боту
    api->c_reset(ctx);
    api->c_text(ctx, "/newchat");
    api->c_send(ctx, bot_id, 0);
}

// ── Command: .grokstatus — информация о модуле ─────────────────────────────────
void cmd_grokstatus(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long bot_id = get_target_bot_id(ctx, api);

    api->c_reset(ctx);
    api->c_fmt(ctx, "🤖 Grok AI · Статус (by kote)\n\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "• Версия модуля: ");
    api->c_fmt(ctx, ("v" + std::string(VERSION)).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n• Бот: ");
    api->c_fmt(ctx, DEFAULT_BOT_USERNAME, KOTO_ENT_BOLD);
    api->c_text(ctx, (" (ID: " + std::to_string(bot_id) + ")\n").c_str());
    api->c_text(ctx, "• Статус: ✅ Активен\n\n");
    api->c_fmt(ctx, "Доступные команды:\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "• .grok <запрос> — задать вопрос нейросети Grok (или в ответ)\n");
    api->c_text(ctx, "• .grokdel — очистить контекст переписки (/newchat)\n");
    api->c_text(ctx, "• .grokstatus — этот статус\n\n");
    api->c_fmt(ctx, "Разработчик: by kote", KOTO_ENT_ITALIC);

    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Watcher: перехват ответов от @GrokAI ──────────────────────────────────────
int on_watch(koto_ctx* ctx, const koto_api* api) {
    long long sid = api->sender_id ? api->sender_id(ctx) : 0;
    long long cid = api->chat_id ? api->chat_id(ctx) : 0;
    long long target_bot_id = get_target_bot_id(ctx, api);

    // Слушаем только входящие сообщения от Grok бота
    if (sid != target_bot_id && cid != target_bot_id) {
        return 0;
    }

    const char* raw_text = api->msg_text ? api->msg_text(ctx) : nullptr;
    std::string bot_answer = raw_text ? raw_text : "";

    // Удаляем входящее сообщение бота в личном диалоге
    if (api->delete_msg) {
        api->delete_msg(ctx);
    }

    PendingRequest req;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (g_pending.chat_id == 0) {
            return 1;
        }
        // Проверяем актуальность запроса (таймаут 120 сек)
        if (time(nullptr) - g_pending.timestamp > 120) {
            g_pending.chat_id = 0;
            return 1;
        }
        req = g_pending;
        g_pending.chat_id = 0;
    }

    if (req.is_reset) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "✅ Контекст успешно очищен!", KOTO_ENT_BOLD);
        api->c_edit_message(ctx, req.chat_id, req.msg_id, 0);
    } else {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❓ Вопрос:\n", KOTO_ENT_BOLD);
        api->c_text(ctx, (req.query + "\n\n").c_str());
        api->c_fmt(ctx, "🤖 Ответ нейросети (Grok):\n", KOTO_ENT_BOLD);
        if (bot_answer.empty()) {
            api->c_text(ctx, "(пустой ответ от бота)");
        } else {
            api->c_markdown(ctx, bot_answer.c_str());
        }
        api->c_edit_message(ctx, req.chat_id, req.msg_id, 0);
    }

    return 1; // Событие поглощено
}

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    (void)ctx; (void)api;
}

// ── Настройки модуля в .cfg ──────────────────────────────────────────────────
static const koto_setting_field SETTINGS[] = {
    {
        "bot_id",
        "ID бота Grok (по умолчанию 7828964235)",
        KOTO_FIELD_INT,
        "7828964235",
        nullptr
    }
};

// ── Регистрация команд ────────────────────────────────────────────────────────
static const koto_command COMMANDS[] = {
    {"grok", cmd_grok, KOTO_LEVEL_TRUSTED, "[ru] Запрос к нейросети Grok (by kote) [en] Query Grok AI neural network (by kote).", "<текст>"},
    {"grokdel", cmd_grokdel, KOTO_LEVEL_TRUSTED, "[ru] Очистить контекст переписки с нейросетью Grok [en] Clear Grok AI conversation context.", nullptr},
    {"grokstatus", cmd_grokstatus, KOTO_LEVEL_TRUSTED, "[ru] Статус и информация о модуле Grok AI [en] Grok AI module status.", nullptr}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "grok",
    "[ru] Модуль для нейросети Grok через бота @GrokAI (by kote). [en] Grok AI neural network via @GrokAI bot (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    on_watch,
    KOTO_WATCH_INCOMING,
    on_callback,
    SETTINGS,
    static_cast<int>(sizeof(SETTINGS) / sizeof(SETTINGS[0])),
    "https://github.com/AresUser1/KoteModules/blob/main/src/grok.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
