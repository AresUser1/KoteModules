// dep.cpp — Лудомания: Депни всё, квартиру, яхту, своё достоинство (C++ ABI 3).
// Портирован из Python-модуля dep.py (v1.0.0).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include <string>

namespace {

static constexpr long long ICON_BET = 5897958754267174109LL; // 💸

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

    // 1. Сначала пишем сообщение о ставке
    api->c_reset(ctx);
    api->c_emoji(ctx, "💸", ICON_BET);
    api->c_fmt(ctx, " Вы поставили: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, (stavka + "\n").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "Ну что ж, начинается лудомания 😈");
    send_result(ctx, api);

    // 2. Отправляем интерактивный дайс слот-машины через движок (inputMediaDice)
    // Формат действия: dice:🎰:dep:<ставка>
    // Хост Kotogram сам отправляет анимированный барабан, ожидает 3.5 секунды прокрутки и отвечает результатом X2!
    std::string dice_action = "dice:🎰:dep:" + stavka;
    api->c_reset(ctx);
    api->c_send_file(ctx, 0, dice_action.c_str(), 0);
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
    "1.0.2",
    26,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr,
    0,
    nullptr,
    nullptr, 0,
    nullptr
};

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register(void) {
    return &MODULE;
}
