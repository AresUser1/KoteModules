// dep.cpp — Лудомания: Депни всё, квартиру, яхту, своё достоинство (C++ ABI 3).
// Портирован из Python-модуля dep.py (v1.0.0).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include <string>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <unistd.h>

namespace {

static constexpr long long ICON_BET  = 5897958754267174109LL; // 💸
static constexpr long long ICON_WIN  = 5987880246865565644LL; // 🤑
static constexpr long long ICON_LOSE = 5987583383021034169LL; // 📉

// Выигрышные значения Telegram-слота 🎰: 1 (BAR), 22 (Ягоды), 43 (Лимоны), 64 (777)
static const int WINNING_VALUES[] = {1, 22, 43, 64};

static bool is_winning(int val) {
    for (int w : WINNING_VALUES) {
        if (val == w) return true;
    }
    return false;
}

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

static void cmd_dep(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string stavka = raw ? koto_utils::trim(raw) : "";

    if (stavka.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "⚠️ ");
        api->c_fmt(ctx, "А что депать то?", KOTO_ENT_BOLD);
        api->c_text(ctx, "\nИспользование: ");
        api->c_fmt(ctx, ".депаю <предмет/квартиру/человека>", KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

    long long cur_chat = api->chat_id(ctx);

    // Сохраняем состояние игры в БД
    koto_utils::db_set_int64(api, ctx, "dep_state", 1);
    koto_utils::db_set_int64(api, ctx, "dep_chat", cur_chat);
    koto_utils::db_set_int64(api, ctx, "dep_ts", time(nullptr));
    api->db_set(ctx, "dep_stavka", stavka.c_str());

    // 1. Сначала пишем сообщение о ставке (как в оригинале dep.py)
    // 💸 Вы поставили: <предмет>
    // Ну что ж, начинается лудомания 😈
    api->c_reset(ctx);
    api->c_emoji(ctx, "💸", ICON_BET);
    api->c_fmt(ctx, " Вы поставили: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, (stavka + "\n").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "Ну что ж, начинается лудомания 😈");

    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }

    // 2. Отправляем слот-машину 🎰 отдельным сообщением в чат (0 = текущий чат)
    api->c_reset(ctx);
    api->c_text(ctx, "🎰");
    api->c_send(ctx, 0, 0);
}

// Перехват отправленного 🎰 барабана, ожидание прокрутки (~3.5 сек) и объявление результата
static int on_watch(koto_ctx* ctx, const koto_api* api) {
    long long state = koto_utils::db_get_int64(api, ctx, "dep_state", 0);
    if (state != 1) return 0;

    long long saved_chat = koto_utils::db_get_int64(api, ctx, "dep_chat", 0);
    long long cur_chat = api->chat_id(ctx);
    if (saved_chat != 0 && cur_chat != saved_chat) return 0;

    // Защита от зависших состояний (таймаут 30 секунд)
    long long ts = koto_utils::db_get_int64(api, ctx, "dep_ts", 0);
    if (ts > 0 && (time(nullptr) - ts) > 30) {
        koto_utils::db_set_int64(api, ctx, "dep_state", 0);
        return 0;
    }

    const char* txt = api->msg_text(ctx);
    if (!txt || strstr(txt, "🎰") == nullptr) return 0;

    // Сбрасываем флаг, чтобы не сработать повторно
    koto_utils::db_set_int64(api, ctx, "dep_state", 0);

    // Достаём ставку
    char stavka_buf[256];
    int sn = api->db_get(ctx, "dep_stavka", stavka_buf, sizeof(stavka_buf) - 1);
    std::string stavka = (sn > 0) ? std::string(stavka_buf, sn) : "всё";

    // 3. Ждём пока анимация барабана прокрутится (~3.5 секунды, как в dep.py)
    usleep(3500 * 1000);

    // 4. Проверяем результат
    static bool seeded = false;
    if (!seeded) {
        srand(static_cast<unsigned int>(time(nullptr) ^ getpid()));
        seeded = true;
    }

    int slot_val = (rand() % 64) + 1;
    bool win = is_winning(slot_val);

    api->c_reset(ctx);
    if (win) {
        api->c_emoji(ctx, "🎉", ICON_WIN);
        api->c_fmt(ctx, " Вы выиграли X2 ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (stavka + "\n").c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "На этот раз тебе повезло!");
    } else {
        api->c_emoji(ctx, "📉", ICON_LOSE);
        api->c_fmt(ctx, " Вы проиграли ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (stavka + "\n").c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "Давай, делай додеп...");
    }

    // Отвечаем на сообщение с барабаном (reply)
    api->c_reply(ctx, 0);
    return 1;
}

} // namespace

static const koto_command COMMANDS[] = {
    {
        "депаю",
        &cmd_dep,
        KOTO_LEVEL_TRUSTED,
        "Сделать ставку на всё что угодно (слот 🎰).",
        "<предмет>"
    },
    {
        "dep",
        &cmd_dep,
        KOTO_LEVEL_TRUSTED,
        "Сделать ставку (псевдоним депаю).",
        "<предмет>"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "dep",
    "Лудомания: Депни всё, квартиру, яхту, своё достоинство (C++ ABI 3).",
    "1.0.1",
    26,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    &on_watch,
    KOTO_WATCH_OUTGOING,
    nullptr,
    nullptr, 0,
    nullptr
};

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register(void) {
    return &MODULE;
}
