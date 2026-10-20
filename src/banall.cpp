// Mass moderation through the host queue. No Telegram calls are made directly.
#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {
using nlohmann::json;
void send_result(koto_ctx* ctx, const koto_api* api, const std::string& text) {
    api->c_reset(ctx);
    api->c_text(ctx, text.c_str());
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}
long long positive_id(const json& value) {
    if (value.is_number_unsigned()) {
        auto n = value.get<unsigned long long>();
        if (n <= static_cast<unsigned long long>(std::numeric_limits<long long>::max()))
            return static_cast<long long>(n);
    } else if (value.is_number_integer()) {
        auto n = value.get<long long>();
        if (n > 0) return n;
    }
    return 0;
}
void clean_chat(koto_ctx* ctx, const koto_api* api, bool kick) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) return;
    int type = api->chat_type(ctx);
    if (type != KOTO_CHAT_GROUP && type != KOTO_CHAT_CHANNEL) {
        send_result(ctx, api, "❌ Эта команда работает только в группах и каналах.");
        return;
    }
    long long my_id = api->get_me ? api->get_me(ctx) : 0;
    if (my_id <= 0) {
        send_result(ctx, api, "❌ Ядро не вернуло ID текущего аккаунта. Нельзя исключить себя из очистки. Запросы не отправлены.");
        return;
    }
    long long chat_id = api->chat_id(ctx);
    auto snapshot = json::parse(koto_utils::get_chat_full_json(api, ctx, chat_id), nullptr, false);
    if (!snapshot.is_object() || !snapshot.contains("participant_ids") || !snapshot["participant_ids"].is_array()) {
        send_result(ctx, api, "❌ Ядро не вернуло список participant_ids. Нельзя определить участников для очистки. Запросы не отправлены.\nПовторите команду после обновления сведений о группе.");
        return;
    }
    if (!snapshot.contains("admin_ids") || !snapshot["admin_ids"].is_array()) {
        send_result(ctx, api, "❌ Ядро не вернуло список admin_ids. Нельзя исключить администраторов из очистки. Запросы не отправлены.");
        return;
    }
    std::set<long long> admins, seen;
    for (const auto& entry : snapshot["admin_ids"]) {
        if (auto id = positive_id(entry)) admins.insert(id);
    }
    if (snapshot.contains("creator_id")) {
        if (auto id = positive_id(snapshot["creator_id"])) admins.insert(id);
    }
    std::vector<long long> targets;
    std::vector<std::string> skipped;
    size_t self_count = 0, admin_count = 0, invalid_count = 0, duplicates = 0;
    for (const auto& entry : snapshot["participant_ids"]) {
        auto uid = positive_id(entry);
        if (uid <= 0) { ++invalid_count; continue; }
        if (!seen.insert(uid).second) { ++duplicates; continue; }
        if (uid == my_id) {
            ++self_count;
            skipped.push_back("ID " + std::to_string(uid) + " — текущий аккаунт");
        } else if (admins.count(uid)) {
            ++admin_count;
            skipped.push_back("ID " + std::to_string(uid) + " — администратор или владелец");
        } else targets.push_back(uid);
    }
    const bool available = kick ? api->kick_user != nullptr : api->ban_user != nullptr;
    if (available) {
        for (auto uid : targets) {
            if (kick) api->kick_user(ctx, chat_id, uid);
            else api->ban_user(ctx, chat_id, uid, 0);
        }
    }
    std::ostringstream out;
    if (!available) out << "❌ Очистка не началась: ядро не предоставляет " << (kick ? "kick_user" : "ban_user") << ".\n";
    else if (targets.empty()) out << "ℹ️ Очистка не началась: в полученном списке нет подходящих участников.\n";
    else out << "Очистка чата\n";
    out << "ID чата: " << chat_id << "\n";
    out << "Получено участников: " << seen.size() << "\n";
    long long total = snapshot.contains("participants_count") ? positive_id(snapshot["participants_count"]) : 0;
    if (total > 0) out << "Всего по сведениям группы: " << total << "\n";
    out << (kick ? "Запросов на кик: " : "Запросов на бан: ") << (available ? targets.size() : 0) << "\n";
    out << "Пропущен текущий аккаунт: " << self_count << "\n";
    out << "Пропущены администраторы/владелец: " << admin_count << "\n";
    if (invalid_count) out << "Некорректных ID в списке: " << invalid_count << "\n";
    if (duplicates) out << "Повторных записей исключено: " << duplicates << "\n";
    if (!available && !targets.empty()) out << "Не отправлены запросы для участников: " << targets.size() << "\n";
    constexpr size_t detail_limit = 12;
    if (!skipped.empty()) {
        out << "\nКто пропущен:\n";
        for (size_t i = 0; i < skipped.size() && i < detail_limit; ++i) out << "• " << skipped[i] << "\n";
        if (skipped.size() > detail_limit) out << "… ещё " << skipped.size() - detail_limit << "\n";
    }
    if (!targets.empty()) {
        out << (available ? "\nУчастники для запросов:\n" : "\nНе обработаны из-за отсутствия функции:\n");
        for (size_t i = 0; i < targets.size() && i < detail_limit; ++i) out << "• ID " << targets[i] << "\n";
        if (targets.size() > detail_limit) out << "… ещё " << targets.size() - detail_limit << "\n";
    }
    if (total > static_cast<long long>(seen.size()))
        out << "\n⚠️ Список неполный: получено " << seen.size() << " из " << total << ". Остальные участники не обработаны.\n";
    if (snapshot.value("participants_hidden", json(false)) == true || snapshot.value("can_view_participants", json(true)) == false)
        out << "\n⚠️ Telegram ограничивает просмотр участников; список может быть неполным.\n";
    if (targets.empty())
        out << "\nЕсли участник вступил недавно, повторите команду через минуту: сведения об участниках могут обновляться с задержкой.\n";
    send_result(ctx, api, out.str());
}
void cmd_banall(koto_ctx* ctx, const koto_api* api) { clean_chat(ctx, api, false); }
void cmd_kickall(koto_ctx* ctx, const koto_api* api) { clean_chat(ctx, api, true); }
const koto_command COMMANDS[] = {
    {"banall", &cmd_banall, KOTO_LEVEL_OWNER, "Забанить обычных участников; показать причины пропусков.", nullptr},
    {"kickall", &cmd_kickall, KOTO_LEVEL_OWNER, "Исключить обычных участников; показать причины пропусков.", nullptr}
};
const koto_module MODULE = {
    KOTO_MODULE_ABI, "banall", "Очистка чата с отчётом по участникам и причинам пропуска.",
    "1.0.2", 60, 0, COMMANDS, sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0, nullptr, nullptr, 0, nullptr
};
}
extern "C" const koto_module* koto_module_register(void) { return &MODULE; }
