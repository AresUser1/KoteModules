// KoteModules_repo/src/kodel.cpp
// Портирован из Hikka / FTG Kodel (Kovalsky) с расширением функционала и безопасным поиском сообщений
#include <string>
#include <vector>
#include <cstdlib>
#include <cctype>
#include "module_abi.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

static void cmd_d(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    long long my_id = api->msg_id ? api->msg_id(ctx) : 0;
    long long rid = api->reply_msg_id ? api->reply_msg_id(ctx) : 0;

    const char* raw = api->cmd_args(ctx);
    while (raw && (*raw == ' ' || *raw == '\t')) ++raw;

    int count = 0;
    if (raw && *raw) {
        char* endp = nullptr;
        long val = std::strtol(raw, &endp, 10);
        if (val > 0) count = static_cast<int>(val);
    }

    if (rid > 0) {
        if (count > 1 && api->get_history_json) {
            // Удаляем N сообщений начиная с реплая (строго из текущего чата cid)
            std::vector<char> buf(65536, 0);
            int hlen = api->get_history_json(ctx, cid, static_cast<int>(rid) + 1, count, 0, buf.data(), static_cast<int>(buf.size()) - 1);
            if (hlen > 2) {
                try {
                    json arr = json::parse(std::string(buf.data(), hlen));
                    if (arr.is_array()) {
                        for (const auto& m : arr) {
                            long long mid = m.value("id", 0LL);
                            if (mid > 0 && mid != my_id) {
                                api->delete_message(ctx, cid, mid);
                            }
                        }
                    }
                } catch (...) {
                    api->delete_message(ctx, cid, rid);
                }
            } else {
                api->delete_message(ctx, cid, rid);
            }
        } else {
            // Удаляем только сообщение, на которое сделан ответ
            api->delete_message(ctx, cid, rid);
        }
    } else {
        // Удаление без реплая: .d или .d N (строго из текущего чата cid!)
        int to_delete = (count > 0) ? count : 1;
        if (api->get_history_json && my_id > 0) {
            std::vector<char> buf(65536, 0);
            int hlen = api->get_history_json(ctx, cid, static_cast<int>(my_id), to_delete, 0, buf.data(), static_cast<int>(buf.size()) - 1);
            if (hlen > 2) {
                try {
                    json arr = json::parse(std::string(buf.data(), hlen));
                    if (arr.is_array()) {
                        for (const auto& m : arr) {
                            long long mid = m.value("id", 0LL);
                            if (mid > 0 && mid != my_id) {
                                api->delete_message(ctx, cid, mid);
                            }
                        }
                    }
                } catch (...) {}
            }
        }
    }

    // Удаляем саму команду .d, чтобы не засорять историю чата
    if (api->delete_msg) {
        api->delete_msg(ctx);
    } else if (my_id > 0) {
        api->delete_message(ctx, cid, my_id);
    }
}

static const koto_command COMMANDS[] = {
    { "d",     &cmd_d, KOTO_LEVEL_TRUSTED, "[ru] Быстрое удаление сообщений: по ответу или по кол-ву (.d, .d 5). [en] Fast message deletion: by reply or count (.d, .d 5).", "[кол-во]" },
    { "del",   &cmd_d, KOTO_LEVEL_TRUSTED, "[ru] Алиас для команды .d. [en] Alias for .d.", "[кол-во]" },
    { "kodel", &cmd_d, KOTO_LEVEL_TRUSTED, "[ru] Алиас для команды .d. [en] Alias for .d.", "[кол-во]" },
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "kodel",
    "[ru] Быстрое удаление сообщений командой .d (по ответу или по количеству). [en] Fast message deletion via .d (by reply or count).",
    "1.1.0",
    0,
    KOTO_MOD_TWINS,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    /*on_watch=*/nullptr,
    /*watch_flags=*/0,
    /*on_callback=*/nullptr,
    /*settings=*/nullptr,
    /*settings_count=*/0,
    /*update_url=*/"https://github.com/AresUser1/KoteModules/blob/main/src/kodel.cpp"
};

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register(void) {
    return &MODULE;
}
