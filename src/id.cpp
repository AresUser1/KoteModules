// id.cpp — Показывает ID, юзернейм и ссылку на пользователя (C++ ABI 3).
// Портирован из Python-модуля id.py с поддержкой аргументов, реплая и своих данных.
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include <string>
#include <cstring>
#include <cctype>

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

static bool is_all_digits(const std::string& s) {
    if (s.empty()) return false;
    size_t i = 0;
    if (s[0] == '-') i = 1;
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i) {
        if (!isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

static void cmd_id(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) {
        return;
    }

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? koto_utils::trim(raw) : "";

    long long target_id = 0;
    std::string username = "";
    bool explicit_arg = false;

    // 1. Проверяем аргументы (@username, t.me/..., численный ID)
    if (!args.empty()) {
        std::string clean_arg = args;
        size_t tme_pos = clean_arg.find("t.me/");
        if (tme_pos != std::string::npos) {
            clean_arg = clean_arg.substr(tme_pos + 5);
            size_t slash = clean_arg.find('/');
            if (slash != std::string::npos) clean_arg = clean_arg.substr(0, slash);
            size_t q = clean_arg.find('?');
            if (q != std::string::npos) clean_arg = clean_arg.substr(0, q);
        }

        if (is_all_digits(clean_arg)) {
            try {
                target_id = std::stoll(clean_arg);
                explicit_arg = true;
            } catch (...) {
                target_id = 0;
            }
        } else {
            std::string uname = clean_arg;
            if (!uname.empty() && uname[0] == '@') {
                uname = uname.substr(1);
            }
            if (!uname.empty()) {
                if (api->resolve) {
                    target_id = api->resolve(ctx, uname.c_str());
                }
                if (target_id != 0) {
                    username = uname;
                    explicit_arg = true;
                }
            }
        }
    }

    // 2. Если аргументов нет — проверяем реплай
    bool is_reply = false;
    if (target_id == 0 && !explicit_arg) {
        long long rep_mid = api->reply_msg_id ? api->reply_msg_id(ctx) : 0;
        if (rep_mid > 0) {
            if (api->reply_sender_id) {
                target_id = api->reply_sender_id(ctx);
            }
            if (target_id <= 0) {
                long long cid = api->chat_id ? api->chat_id(ctx) : 0;
                long long me = api->get_me ? api->get_me(ctx) : 0;
                if (cid > 0 && cid != me) {
                    target_id = cid;
                }
            }
            if (target_id != 0) {
                is_reply = true;
            }
        }
    }

    // 3. Если ни аргумента, ни реплая — берем автора команды / себя
    if (target_id == 0 && !explicit_arg && !is_reply) {
        if (api->sender_id) {
            target_id = api->sender_id(ctx);
        }
        if (target_id == 0 && api->get_me) {
            target_id = api->get_me(ctx);
        }
    }

    if (target_id == 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Не удалось определить пользователя.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    // Подгружаем юзернейм из кэша, если он еще не известен
    if (username.empty() && api->user_username) {
        char username_buf[128] = {0};
        api->user_username(ctx, target_id, username_buf, sizeof(username_buf));
        if (username_buf[0]) {
            username = username_buf;
        }
    }

    long long me = api->get_me ? api->get_me(ctx) : 0;
    bool is_self = (!explicit_arg && !is_reply && (target_id == me || target_id == api->sender_id(ctx)));

    api->c_reset(ctx);

    // 1. Первая строка: ID
    api->c_emoji(ctx, "🆔", ID_EMOJI_ID);
    if (is_self) {
        api->c_fmt(ctx, " Ваш ID: ", KOTO_ENT_BOLD);
    } else if (target_id < 0) {
        api->c_fmt(ctx, " ID чата: ", KOTO_ENT_BOLD);
    } else {
        api->c_fmt(ctx, " ID: ", KOTO_ENT_BOLD);
    }
    api->c_fmt(ctx, std::to_string(target_id).c_str(), KOTO_ENT_CODE);

    // 2. Вторая строка: Юзернейм (если есть)
    if (!username.empty()) {
        api->c_text(ctx, "\n");
        api->c_emoji(ctx, "👤", USERNAME_EMOJI_ID);
        if (is_self) {
            api->c_fmt(ctx, " Ваш юзернейм: ", KOTO_ENT_BOLD);
        } else {
            api->c_fmt(ctx, " Юзернейм: ", KOTO_ENT_BOLD);
        }
        api->c_fmt(ctx, ("@" + username).c_str(), KOTO_ENT_CODE);
    }

    // 3. Третья строка: Ссылка на аккаунт/чат
    api->c_text(ctx, "\n");
    api->c_emoji(ctx, "🔗", LINK_EMOJI_ID);
    if (is_self) {
        api->c_fmt(ctx, " Ваша ссылка: ", KOTO_ENT_BOLD);
    } else {
        api->c_fmt(ctx, " Ссылка: ", KOTO_ENT_BOLD);
    }

    if (!username.empty()) {
        std::string link_url = "https://t.me/" + username;
        api->c_url(ctx, link_url.c_str(), link_url.c_str());
    } else if (target_id > 0) {
        std::string link_url = "tg://user?id=" + std::to_string(target_id);
        api->c_url(ctx, "Перейти в профиль", link_url.c_str());
    } else {
        // Чат/канал без юзернейма
        long long norm_id = target_id;
        if (norm_id <= -1000000000000LL) norm_id = -(norm_id + 1000000000000LL);
        else if (norm_id < 0) norm_id = -norm_id;
        std::string link_url = "https://t.me/c/" + std::to_string(norm_id) + "/1";
        api->c_url(ctx, "Перейти в чат", link_url.c_str());
    }

    send_result(ctx, api);
}

static const koto_command COMMANDS[] = {
    {
        "ай",
        &cmd_id,
        KOTO_LEVEL_TRUSTED,
        "Показывает ваш ID, ID пользователя и ссылку (@username, ID, в ответ или без).",
        "[@username|id|реплай]"
    },
    {
        "id",
        &cmd_id,
        KOTO_LEVEL_TRUSTED,
        "Показывает ваш ID, ID пользователя и ссылку (@username, ID, в ответ или без).",
        "[@username|id|реплай]"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "id",
    "Показывает ID, юзернейм и ссылку на пользователя (C++ ABI 3).",
    "1.1.1",
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
