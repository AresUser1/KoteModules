// id.cpp — Показывает ID, юзернейм и ссылку на пользователя (C++ ABI 3).
// Портирован из Python-модуля id.py (v1.0.5).
// Автор: Kote

#include "module_abi.h"
#include <string>
#include <cstring>

static constexpr long long ID_EMOJI_ID       = 5818885490065017876LL; // 🆔
static constexpr long long USERNAME_EMOJI_ID = 4904848288345228262LL; // 👤
static constexpr long long LINK_EMOJI_ID     = 5778586619380503542LL; // 🔗

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, KOTO_NO_LINK_PREVIEW);
    } else {
        api->c_reply(ctx, KOTO_NO_LINK_PREVIEW);
    }
}

static void cmd_id(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) {
        return;
    }

    long long target_id = 0;
    std::string title_text = "Ваш";

    long long r_id = api->reply_sender_id(ctx);
    if (r_id > 0) {
        target_id = r_id;
        title_text = "Пользователя";
    } else {
        target_id = api->sender_id(ctx);
        if (target_id == 0) {
            target_id = api->get_me(ctx);
        }
    }

    if (target_id == 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Не удалось определить пользователя.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    char username_buf[128] = {0};
    if (api->user_username) {
        api->user_username(ctx, target_id, username_buf, sizeof(username_buf));
    }
    std::string username = username_buf;

    api->c_reset(ctx);

    // 1. Первая строка: ID
    api->c_emoji(ctx, "🆔", ID_EMOJI_ID);
    api->c_fmt(ctx, (" " + title_text + " ID: ").c_str(), KOTO_ENT_BOLD);
    api->c_fmt(ctx, std::to_string(target_id).c_str(), KOTO_ENT_CODE);

    // 2. Вторая строка: Юзернейм (если есть)
    if (!username.empty()) {
        api->c_text(ctx, "\n");
        api->c_emoji(ctx, "👤", USERNAME_EMOJI_ID);
        api->c_fmt(ctx, (" " + title_text + " юзернейм: ").c_str(), KOTO_ENT_BOLD);
        api->c_fmt(ctx, ("@" + username).c_str(), KOTO_ENT_CODE);
    }

    // 3. Третья строка: Ссылка на аккаунт
    api->c_text(ctx, "\n");
    api->c_emoji(ctx, "🔗", LINK_EMOJI_ID);
    api->c_fmt(ctx, (" " + title_text + " ссылка: ").c_str(), KOTO_ENT_BOLD);

    if (!username.empty()) {
        std::string link_url = "https://t.me/" + username;
        api->c_url(ctx, link_url.c_str(), link_url.c_str());
    } else {
        std::string link_url = "tg://user?id=" + std::to_string(target_id);
        api->c_url(ctx, "Перейти в профиль", link_url.c_str());
    }

    send_result(ctx, api);
}

static const koto_command COMMANDS[] = {
    {
        "ай",
        &cmd_id,
        KOTO_LEVEL_TRUSTED,
        "Показывает ваш ID, ID пользователя и ссылку в ответе.",
        "(в ответ или без)"
    },
    {
        "id",
        &cmd_id,
        KOTO_LEVEL_TRUSTED,
        "Показывает ваш ID, ID пользователя и ссылку в ответе (псевдоним).",
        "(в ответ или без)"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "id",
    "Показывает ID, юзернейм и ссылку на пользователя (C++ ABI 3).",
    "1.0.6",
    26,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    nullptr, 0,
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/id.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
