// banall.cpp — Модуль для массовой очистки чата (бан или кик всех участников) (C++ ABI 3).
// Портирован из Python-модуля banall.py (v1.0.0).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <unistd.h>

namespace {

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

// Извлечение ID админов из chat_full_json
std::set<long long> get_chat_admin_ids(const koto_api* api, koto_ctx* ctx, long long chat_id) {
    std::set<long long> admin_ids;
    std::string full_json = koto_utils::get_chat_full_json(api, ctx, chat_id);

    // Поиск "admin_ids" или "creator_id" или участников с admin_rights
    size_t creator_pos = full_json.find("\"creator_id\":");
    if (creator_pos != std::string::npos) {
        long long cid = strtoll(full_json.c_str() + creator_pos + 13, nullptr, 10);
        if (cid > 0) admin_ids.insert(cid);
    }

    size_t admins_pos = full_json.find("\"admin_ids\":");
    if (admins_pos != std::string::npos) {
        size_t b = full_json.find('[', admins_pos);
        size_t e = (b != std::string::npos) ? full_json.find(']', b) : std::string::npos;
        if (b != std::string::npos && e != std::string::npos) {
            std::string sub = full_json.substr(b + 1, e - b - 1);
            std::stringstream ss(sub);
            std::string item;
            while (std::getline(ss, item, ',')) {
                long long id = strtoll(item.c_str(), nullptr, 10);
                if (id > 0) admin_ids.insert(id);
            }
        }
    }

    return admin_ids;
}

void cmd_banall(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) return;

    long long chat_id = api->chat_id(ctx);
    if (chat_id == 0) return;

    api->c_reset(ctx);
    api->c_text(ctx, "⌛ ");
    api->c_fmt(ctx, "Начинаю сбор данных и бан участников...", KOTO_ENT_BOLD);
    send_result(ctx, api);

    std::vector<long long> participants = koto_utils::get_chat_participants(api, ctx, chat_id);
    if (participants.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Не удалось получить список участников или чат пуст.");
        send_result(ctx, api);
        return;
    }

    long long my_id = api->get_me ? api->get_me(ctx) : 0;
    std::set<long long> admin_ids = get_chat_admin_ids(api, ctx, chat_id);

    int count = 0;
    int errors = 0;

    for (long long uid : participants) {
        if (uid <= 0 || uid == my_id) continue;
        if (admin_ids.count(uid)) continue;

        if (api->ban_user) {
            api->ban_user(ctx, chat_id, uid, 0);
            count++;
        } else {
            errors++;
        }
    }

    api->c_reset(ctx);
    api->c_text(ctx, "🎉 ");
    api->c_fmt(ctx, "Очистка завершена!\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "✅ Забанено: ");
    api->c_fmt(ctx, std::to_string(count).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n❌ Пропущено (админы/ошибки): ");
    api->c_fmt(ctx, std::to_string(errors + (int)admin_ids.size()).c_str(), KOTO_ENT_CODE);
    send_result(ctx, api);
}

void cmd_kickall(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) return;

    long long chat_id = api->chat_id(ctx);
    if (chat_id == 0) return;

    api->c_reset(ctx);
    api->c_text(ctx, "⌛ ");
    api->c_fmt(ctx, "Начинаю сбор данных и кик участников...", KOTO_ENT_BOLD);
    send_result(ctx, api);

    std::vector<long long> participants = koto_utils::get_chat_participants(api, ctx, chat_id);
    if (participants.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ Не удалось получить список участников или чат пуст.");
        send_result(ctx, api);
        return;
    }

    long long my_id = api->get_me ? api->get_me(ctx) : 0;
    std::set<long long> admin_ids = get_chat_admin_ids(api, ctx, chat_id);

    int count = 0;
    int errors = 0;

    for (long long uid : participants) {
        if (uid <= 0 || uid == my_id) continue;
        if (admin_ids.count(uid)) continue;

        if (api->kick_user) {
            api->kick_user(ctx, chat_id, uid);
            count++;
        } else {
            errors++;
        }
    }

    api->c_reset(ctx);
    api->c_text(ctx, "🎉 ");
    api->c_fmt(ctx, "Очистка завершена!\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "👞 Кикнуто: ");
    api->c_fmt(ctx, std::to_string(count).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n❌ Пропущено (админы/ошибки): ");
    api->c_fmt(ctx, std::to_string(errors + (int)admin_ids.size()).c_str(), KOTO_ENT_CODE);
    send_result(ctx, api);
}

const koto_command COMMANDS[] = {
    {
        "banall",
        &cmd_banall,
        KOTO_LEVEL_OWNER,
        "Забанить всех обычных участников чата (кроме админов и себя).",
        nullptr
    },
    {
        "kickall",
        &cmd_kickall,
        KOTO_LEVEL_OWNER,
        "Кикнуть всех обычных участников чата (кроме админов и себя).",
        nullptr
    }
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "banall",
    "Массовая очистка чата: бан или кик всех участников (C++ ABI 3).",
    "1.0.0",
    27, // ban_user / kick_user доступны в versionCode 27+
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    nullptr, 0,
    nullptr
};

} // namespace

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
