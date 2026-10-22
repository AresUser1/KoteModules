// delme.cpp — Удаление всех своих сообщений из чата (C++ ABI 3).
// Портирован из модуля DelMe (KeyZenD).
// Автор: KeyZenD / адаптация Kote

#include <string>
#include <vector>
#include <cstdlib>
#include "module_abi.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

static void do_delete_all(koto_ctx* ctx, const koto_api* api, bool show_notice) {
    long long cid = api->chat_id(ctx);
    long long my_id = api->get_me ? api->get_me(ctx) : 0;
    if (my_id == 0) my_id = api->sender_id(ctx);
    long long cmd_msg_id = api->msg_id ? api->msg_id(ctx) : 0;

    std::vector<long long> to_delete;

    if (api->get_history_json) {
        int current_offset_id = 0;
        int total_scanned = 0;
        const int max_to_scan = 1000;
        std::vector<char> hbuf(131072, 0);

        while (total_scanned < max_to_scan) {
            int hlen = api->get_history_json(ctx, cid, current_offset_id, 100, 0, hbuf.data(), (int)hbuf.size() - 1);
            if (hlen <= 2) break; // "[]" или ошибка

            try {
                json arr = json::parse(std::string(hbuf.data(), hlen));
                if (!arr.is_array() || arr.empty()) break;

                long long oldest_id = 0;
                for (const auto& m : arr) {
                    long long mid = m.value("id", 0LL);
                    long long sid = m.value("sender_id", 0LL);
                    bool is_out = m.value("out", false);

                    if (mid > 0) {
                        if (oldest_id == 0 || mid < oldest_id) {
                            oldest_id = mid;
                        }
                        total_scanned++;
                        if (mid != cmd_msg_id && (is_out || (my_id > 0 && sid == my_id))) {
                            to_delete.push_back(mid);
                        }
                    }
                }
                if (oldest_id == 0 || arr.size() < 100) break;
                current_offset_id = (int)oldest_id;
            } catch (...) {
                break;
            }
        }
    }

    if (to_delete.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "ℹ️ Сообщений для удаления не найдено.", KOTO_ENT_ITALIC);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    if (show_notice) {
        api->c_reset(ctx);
        std::string txt = std::to_string(to_delete.size()) + " сообщений будет удалено!";
        api->c_fmt(ctx, txt.c_str(), KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    } else {
        api->delete_msg(ctx);
    }

    for (long long mid : to_delete) {
        api->delete_message(ctx, cid, mid);
    }

    if (show_notice) {
        api->delete_msg(ctx);
    }
}

static void cmd_delme(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    long long my_id = api->get_me ? api->get_me(ctx) : 0;
    if (my_id == 0) my_id = api->sender_id(ctx);

    int ctype = api->chat_type ? api->chat_type(ctx) : KOTO_CHAT_UNKNOWN;
    if (ctype == KOTO_CHAT_PRIVATE || (my_id != 0 && cid == my_id)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "В лс не чищу!", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    long long expected_code = cid + my_id;

    const char* args = api->cmd_args(ctx);
    std::string arg_str = args ? args : "";
    while (!arg_str.empty() && (arg_str.front() == ' ' || arg_str.front() == '\t')) arg_str.erase(0, 1);
    while (!arg_str.empty() && (arg_str.back() == ' ' || arg_str.back() == '\t')) arg_str.pop_back();

    if (arg_str != std::to_string(expected_code)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "Если ты точно хочешь это сделать, то напиши:\n", KOTO_ENT_BOLD);
        std::string cmd = ".delme " + std::to_string(expected_code);
        api->c_fmt(ctx, cmd.c_str(), KOTO_ENT_CODE);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    do_delete_all(ctx, api, true);
}

static void cmd_delmenow(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    long long my_id = api->get_me ? api->get_me(ctx) : 0;
    if (my_id == 0) my_id = api->sender_id(ctx);

    int ctype = api->chat_type ? api->chat_type(ctx) : KOTO_CHAT_UNKNOWN;
    if (ctype == KOTO_CHAT_PRIVATE || (my_id != 0 && cid == my_id)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "В лс не чищу!", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    do_delete_all(ctx, api, false);
}

static const koto_command COMMANDS[] = {
    {
        "delme",
        &cmd_delme,
        KOTO_LEVEL_TRUSTED,
        "Удаляет все сообщения от тебя с подтверждением.",
        "[код]"
    },
    {
        "delmenow",
        &cmd_delmenow,
        KOTO_LEVEL_TRUSTED,
        "Удаляет все сообщения от тебя без вопросов.",
        NULL
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "delme",
    "Удаляет все сообщения от вас в текущем чате (KeyZenD port).",
    "1.0.1",
    71,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    nullptr, 0,
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/delme.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
