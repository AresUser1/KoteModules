// spammer.cpp
// Модуль для быстрого спама сообщениями с поддержкой форматирования Markdown/HTML,
// цитирования отвеченных сообщений и настраиваемого количества сообщений в приложении.
// Портирован из Python-модуля Spammer (SynForge). Автор: AresUser1 / Коте.
//
// Команды:
// .spam [количество] <текст> — начать спам (число опционально, по умолчанию из настроек)
// .stopspam                  — остановить спам
//
// Настройки:
// default_count              — количество сообщений по умолчанию (настраивается в UI приложения и .cfg)

#include "module_abi.h"
#include <string>
#include <vector>
#include <cctype>
#include <cstring>
#include <cstdlib>

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ── Декларация настроек модуля для панели приложения и команды .cfg ──────────
static const koto_setting_field SETTINGS[] = {
    {
        "default_count",
        "Количество сообщений по умолчанию",
        KOTO_FIELD_INT,
        "20",
        nullptr
    }
};

static long long get_default_count(koto_ctx* ctx, const koto_api* api) {
    long long def = 20;
    if (api && api->cfg_get) {
        char buf[32] = {0};
        int n = api->cfg_get(ctx, "default_count", buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            try {
                long long v = std::stoll(buf);
                if (v > 0) def = v;
            } catch (...) {}
        }
    }
    return def;
}

static void cmd_spam(koto_ctx* ctx, const koto_api* api) {
    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? trim(raw) : "";

    long long default_cnt = get_default_count(ctx, api);
    long long count = default_cnt;
    std::string user_text = "";

    // Проверяем наличие reply-сообщения
    const char* rep = (api->reply_text) ? api->reply_text(ctx) : nullptr;
    std::string rep_text = rep ? rep : "";
    bool has_reply = !rep_text.empty();

    // Разбираем аргументы команды
    if (!args.empty()) {
        size_t sp = args.find_first_of(" \t\n");
        std::string first_token = (sp == std::string::npos) ? args : args.substr(0, sp);

        bool is_num = !first_token.empty();
        for (char c : first_token) {
            if (!std::isdigit((unsigned char)c)) {
                is_num = false;
                break;
            }
        }

        if (is_num) {
            try {
                long long parsed = std::stoll(first_token);
                if (parsed > 0) {
                    if (sp != std::string::npos) {
                        // Число + пробел + текст: например, "10 Привет" -> count=10, user_text="Привет"
                        count = parsed;
                        user_text = trim(args.substr(sp + 1));
                    } else {
                        // Только число без текста
                        if (has_reply) {
                            // Если есть ответ на сообщение -> count=50, спамим ответом
                            count = parsed;
                        } else {
                            // Если ответа нет -> спамим текст "50" default_cnt раз
                            count = default_cnt;
                            user_text = first_token;
                        }
                    }
                }
            } catch (...) {
                count = default_cnt;
                user_text = args;
            }
        } else {
            // Текст без числа в начале -> берем дефолтное количество
            user_text = args;
            count = default_cnt;
        }
    }

    // Защита от ошибочных или чрезмерных значений
    if (count <= 0) count = default_cnt;
    if (count > 500) count = 500;

    // Проверяем, есть ли что спамить
    if (user_text.empty() && !has_reply) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Не найден текст для спама. Напишите текст или ответьте на сообщение.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    long long cid = api->chat_id(ctx);

    // 1. Удаляем сообщение команды
    api->delete_msg(ctx);

    // 2. Отправляем подтверждение старта спама
    api->c_reset(ctx);
    api->c_fmt(ctx, "🚀 Спам запущен! ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, "Количество: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, std::to_string(count).c_str(), KOTO_ENT_CODE);
    api->c_send(ctx, cid, 0);

    // 3. Отправляем сообщения для спама с сохранением стилей и цитаты
    for (long long i = 0; i < count; ++i) {
        api->c_reset(ctx);
        if (has_reply && !user_text.empty()) {
            api->c_fmt(ctx, rep_text.c_str(), KOTO_ENT_BLOCKQUOTE);
            api->c_text(ctx, "\n");
            api->c_markdown(ctx, user_text.c_str());
        } else if (has_reply) {
            api->c_markdown(ctx, rep_text.c_str());
        } else {
            api->c_markdown(ctx, user_text.c_str());
        }
        api->c_send(ctx, cid, 0);
    }
}

static void cmd_stopspam(koto_ctx* ctx, const koto_api* api) {
    api->c_reset(ctx);
    api->c_fmt(ctx, "ℹ️ Активных задач спама нет.", KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

static const koto_command COMMANDS[] = {
    {
        "spam",
        &cmd_spam,
        KOTO_LEVEL_TRUSTED,
        "Начать спам сообщениями (с поддержкой цитат и форматирования).",
        "[количество] <текст>"
    },
    {
        "stopspam",
        &cmd_stopspam,
        KOTO_LEVEL_TRUSTED,
        "Остановить задачу спама.",
        nullptr
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "spammer",
    "Быстрый спам сообщениями с поддержкой разметки, цитат и настройкой количества сообщений по умолчанию в приложении. C++ ABI 3.",
    "1.2.0",
    24,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    SETTINGS,
    sizeof(SETTINGS) / sizeof(SETTINGS[0]),
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/spammer.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
