// delme.cpp — Удаление всех своих сообщений из чата (C++ ABI 3).
// Портирован из модуля DelMe (KeyZenD).
// Автор: KeyZenD / адаптация Kote

#include <string>
#include <vector>
#include <cstdlib>
#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

static void fallback_delete_history(koto_ctx* ctx, const koto_api* api, long long cid, long long my_id, long long exclude_id) {
    if (!api->get_history_json) return;
    int current_offset_id = 0;
    std::vector<char> hbuf(131072, 0);

    for (int iter = 0; iter < 500; ++iter) {
        int hlen = api->get_history_json(ctx, cid, current_offset_id, 100, 0, hbuf.data(), (int)hbuf.size() - 1);
        if (hlen <= 2) break;

        try {
            json arr = json::parse(std::string(hbuf.data(), hlen));
            if (!arr.is_array() || arr.empty()) break;

            long long oldest_id = 0;
            std::vector<long long> to_del;
            for (const auto& m : arr) {
                long long mid = m.value("id", 0LL);
                long long sid = m.value("sender_id", 0LL);
                bool is_out = m.value("out", false);

                if (mid > 0) {
                    if (oldest_id == 0 || mid < oldest_id) oldest_id = mid;
                    if (mid != exclude_id && (is_out || (my_id > 0 && sid == my_id))) {
                        to_del.push_back(mid);
                    }
                }
            }

            for (long long d_id : to_del) {
                api->delete_message(ctx, cid, d_id);
            }

            if (oldest_id == 0 || oldest_id == current_offset_id || arr.size() < 100) break;
            current_offset_id = (int)oldest_id;
        } catch (...) {
            break;
        }
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
        send_result(ctx, api);
        return;
    }

    long long expected_code = cid + my_id;

    const char* raw = api->cmd_args(ctx);
    std::string arg_str = raw ? koto_utils::trim(raw) : "";

    if (arg_str != std::to_string(expected_code)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "Если ты точно хочешь это сделать, то напиши:\n", KOTO_ENT_BOLD);
        std::string cmd = ".delme " + std::to_string(expected_code);
        api->c_fmt(ctx, cmd.c_str(), KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

    long long cmd_msg_id = api->msg_id ? api->msg_id(ctx) : 0;

    int total_count = -1;
    if (api->host_call_json) {
        char qbuf[512] = {0};
        int qlen = api->host_call_json(ctx, "telegram.delme.v1", "{}", qbuf, sizeof(qbuf) - 1);
        if (qlen > 0) {
            try {
                auto qj = json::parse(std::string(qbuf, qlen));
                if (qj.value("ok", false)) {
                    total_count = qj.value("count", -1);
                }
            } catch (...) {}
        }
    }

    api->c_reset(ctx);
    std::string notice;
    if (total_count >= 0) {
        notice = std::to_string(total_count) + " сообщений будет удалено!";
    } else {
        notice = "Сообщения удаляются...";
    }
    api->c_fmt(ctx, notice.c_str(), KOTO_ENT_BOLD);
    send_result(ctx, api);

    bool handled = false;
    if (api->host_action_json) {
        json req = {
            {"exclude_ids", json::array({cmd_msg_id})}
        };
        int hres = api->host_action_json(ctx, "telegram.delme.v1", req.dump().c_str());
        if (hres >= 0) handled = true;
    }

    if (!handled) {
        fallback_delete_history(ctx, api, cid, my_id, cmd_msg_id);
    }

    api->delete_msg(ctx);
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
        send_result(ctx, api);
        return;
    }

    api->delete_msg(ctx);

    bool handled = false;
    if (api->host_action_json) {
        int hres = api->host_action_json(ctx, "telegram.delme.v1", "{}");
        if (hres >= 0) handled = true;
    }

    if (!handled) {
        fallback_delete_history(ctx, api, cid, my_id, 0);
    }
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
    "1.1.0",
    71,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    nullptr, 0,
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/delme.so"
};

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register(void) {
    return &MODULE;
}
