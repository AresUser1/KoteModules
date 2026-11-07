// amnesty.cpp — Модуль для массового разбана пользователей в чате или в ЛС (амнистия) (C++ ABI 3).
// Портирован из Python-модуля AmnestyMod (by kote).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"

#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <chrono>

namespace {
using json = nlohmann::json;

constexpr const char* VERSION = "1.0.0";

static void send_res(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

// ── Command: .amnesty — разблокирует всех забаненных в текущем чате ─────────────
void cmd_amnesty(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    int type = api->chat_type(ctx);
    if (type != KOTO_CHAT_GROUP && type != KOTO_CHAT_CHANNEL) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "🚫 Команда доступна только в супергруппах и каналах!", KOTO_ENT_BOLD);
        send_res(ctx, api);
        return;
    }

    long long chat_id = api->chat_id(ctx);

    // Считываем аргументы, если переданы конкретные ID
    std::vector<long long> target_ids;
    const char* raw_args = api->cmd_args(ctx);
    if (raw_args && raw_args[0]) {
        std::istringstream iss(raw_args);
        long long val;
        while (iss >> val) {
            if (val > 0) target_ids.push_back(val);
        }
    }

    // Если аргументов нет, пытаемся получить список участников из chat_full_json
    if (target_ids.empty() && api->chat_full_json) {
        std::vector<char> buf(65536, 0);
        int len = api->chat_full_json(ctx, chat_id, buf.data(), (int)buf.size() - 1);
        if (len > 0) {
            try {
                auto j = json::parse(std::string(buf.data(), len));
                if (j.contains("banned_ids") && j["banned_ids"].is_array()) {
                    for (const auto& item : j["banned_ids"]) {
                        if (item.is_number()) target_ids.push_back(item.get<long long>());
                    }
                }
            } catch (...) {}
        }
    }

    if (target_ids.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "ℹ️ Черный список чата пуст или укажите ID: .amnesty <id1> <id2>…", KOTO_ENT_BOLD);
        send_res(ctx, api);
        return;
    }

    api->c_reset(ctx);
    api->c_fmt(ctx, "♻️ Начинаю процесс амнистии в чате…", KOTO_ENT_BOLD);
    send_res(ctx, api);

    int unbanned = 0;
    for (size_t i = 0; i < target_ids.size(); ++i) {
        long long uid = target_ids[i];
        if (api->unban_user) {
            api->unban_user(ctx, chat_id, uid);
            unbanned++;
        }
        if ((i + 1) % 5 == 0 || i + 1 == target_ids.size()) {
            api->c_reset(ctx);
            std::string progress = "♻️ Разбанено пользователей: " + std::to_string(unbanned) + "/" + std::to_string(target_ids.size());
            api->c_fmt(ctx, progress.c_str(), KOTO_ENT_BOLD);
            send_res(ctx, api);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    api->c_reset(ctx);
    std::string done_text = "✅ Успешно! " + std::to_string(unbanned) + " пользователей разблокировано!";
    api->c_fmt(ctx, done_text.c_str(), KOTO_ENT_BOLD);
    send_res(ctx, api);
}

// ── Command: .amnistia — разблокирует пользователей в ЛС ───────────────────────
void cmd_amnistia(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) return;

    std::vector<long long> target_ids;
    const char* raw_args = api->cmd_args(ctx);
    if (raw_args && raw_args[0]) {
        std::istringstream iss(raw_args);
        long long val;
        while (iss >> val) {
            if (val > 0) target_ids.push_back(val);
        }
    }

    if (target_ids.empty() && api->reply_sender_id) {
        long long r_sid = api->reply_sender_id(ctx);
        if (r_sid > 0) target_ids.push_back(r_sid);
    }

    if (target_ids.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "ℹ️ Укажите ID пользователей или ответьте на сообщение: .amnistia <id1> <id2>…", KOTO_ENT_BOLD);
        send_res(ctx, api);
        return;
    }

    int unbanned = 0;
    for (size_t i = 0; i < target_ids.size(); ++i) {
        long long uid = target_ids[i];
        if (api->unban_user) {
            api->unban_user(ctx, 0, uid); // 0 = unblock in PM
            unbanned++;
        }
    }

    api->c_reset(ctx);
    std::string done_text = "✅ Успешно! Разблокировано в ЛС: " + std::to_string(unbanned) + " пользователей.";
    api->c_fmt(ctx, done_text.c_str(), KOTO_ENT_BOLD);
    send_res(ctx, api);
}

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    (void)ctx; (void)api;
}

static const koto_command COMMANDS[] = {
    {"amnesty", cmd_amnesty, KOTO_LEVEL_TRUSTED, "[ru] Разблокировать всех участников в чате (амнистия) (by kote) [en] Unban members in chat.", "[id1 id2...]"},
    {"amnistia", cmd_amnistia, KOTO_LEVEL_OWNER, "[ru] Разблокировать пользователей в ЛС (by kote) [en] Unblock users in private messages.", "[id1 id2...]"}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "amnesty",
    "[ru] Модуль для массового разбана пользователей в чате или в ЛС (амнистия) (by kote). [en] Mass unban users in chat or PM (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    nullptr, 0,
    on_callback,
    nullptr, 0,
    "https://github.com/AresUser1/KoteModules/blob/main/src/amnesty.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
