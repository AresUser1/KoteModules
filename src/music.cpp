// music.cpp
// Музыкальный модуль для Kotogram (C++ ABI 3).
// Поиск и мгновенная отправка трека через Telegram-ботов (@lybot → @vkmusic_bot).
// Файл приходит напрямую через серверы Telegram — ничего не качается на телефон.
//
// (YouTube-ветка вырезана: публичные зеркала Invidious нестабильны — то за
//  анти-бот challenge, то IP-лочат прямой googlevideo-URL, то 5xx. Путь через
//  inline-ботов Telegram оказался единственным надёжным, поэтому модуль теперь
//  тонкая обёртка над ним.)
//
// Команды:
// .mus <запрос>  - Найти и прислать трек через Telegram-бота
// .musconfig     - Статус модуля и список ботов

#include "module_abi.h"
#include <string>

namespace {

// Музыкальные inline-боты Telegram в порядке приоритета.
// Фоллбэк @lybot → @vkmusic_bot выполняет хост (c_send_inline_bot).
const char* const MUSIC_BOTS[] = {
    "lybot",
    "vkmusic_bot"
};
constexpr size_t NUM_BOTS = sizeof(MUSIC_BOTS) / sizeof(MUSIC_BOTS[0]);

// ── 1. Команда .mus <запрос> ──────────────────────────────────────────────────
void cmd_mus(koto_ctx* ctx, const koto_api* api) {
    const char* raw_args = api->cmd_args(ctx);
    while (raw_args && (*raw_args == ' ' || *raw_args == '\t')) raw_args++;
    if (!raw_args || !*raw_args) {
        api->c_reset(ctx);
        api->c_markdown(ctx,
            "⚠️ **Использование:** `.mus <название песни / артист>`\n"
            "> Трек ищется и присылается через Telegram-ботов (@lybot, @vkmusic_bot) —\n"
            "> файл приходит напрямую с серверов Telegram, ничего не качается на телефон.\n"
            "> _Например:_ `.mus Linkin Park Numb`");
        api->c_edit(ctx, 0);
        return;
    }

    std::string query = raw_args;
    while (!query.empty() && (query.back() == ' ' || query.back() == '\t')) query.pop_back();

    // Inline-боты доступны с host v13+; c_send_inline_bot должен быть прошит хостом.
    if (api->c_send_inline_bot == nullptr) {
        api->c_reset(ctx);
        api->c_markdown(ctx,
            "❌ **Отправка через ботов недоступна в этой версии KoteLoader.**\n"
            "> Обновите приложение, чтобы искать музыку.");
        api->c_edit(ctx, 0);
        return;
    }

    long long cid = api->chat_id(ctx);
    long long mid = api->msg_id(ctx);

    api->c_reset(ctx);
    api->c_markdown(ctx, "🔍 **Ищу музыку через Telegram-ботов...**\n> 🎵 `");
    api->c_text(ctx, query.c_str());
    api->c_markdown(ctx, "`");
    api->c_edit(ctx, 0);

    api->c_send_inline_bot(ctx, "lybot", query.c_str(), cid, mid);
}

// ── 2. Команда .musconfig ─────────────────────────────────────────────────────
void cmd_mus_config(koto_ctx* ctx, const koto_api* api) {
    api->c_reset(ctx);
    api->c_markdown(ctx,
        "🎵 **Kotogram Music Player (C++ ABI 3)**\n\n"
        "Поиск и мгновенная отправка трека через Telegram-ботов — файл приходит\n"
        "напрямую с серверов Telegram, без загрузки на телефон.\n\n"
        "📌 **Команда:** `.mus <песня / артист>`\n\n"
        "🤖 **Telegram-боты (с фоллбэком по порядку):**\n");
    for (size_t i = 0; i < NUM_BOTS; ++i) {
        api->c_markdown(ctx, "• `@");
        api->c_text(ctx, MUSIC_BOTS[i]);
        api->c_markdown(ctx, "`\n");
    }
    api->c_edit(ctx, 0);
}

// Декларация команд модуля
const koto_command COMMANDS[] = {
    {
        "mus",
        &cmd_mus,
        KOTO_LEVEL_ALL,
        "Найти и прислать трек через Telegram-бота.",
        "<запрос>"
    },
    {
        "musconfig",
        &cmd_mus_config,
        KOTO_LEVEL_ALL,
        "Информация о музыкальном модуле.",
        ""
    }
};

// Дескриптор модуля
const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "music",
    "Музыкальный плеер: поиск трека через Telegram-ботов (C++ ABI 3)",
    "3.0.0",
    13, // inline-боты требуют KoteLoader с host v13+
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    nullptr, 0,
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/music.so"
};

} // namespace

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
