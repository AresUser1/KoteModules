#pragma once
#include "module_abi.h"
#include <algorithm>
#include <string>
#include <vector>

// Host 74+ / 110+: JSON strings keep future named services independent of ABI layout.
// Declare module.min_host_version=74 (or 110 for channel/giveaway services),
// then discover methods with capabilities().
namespace koto::services {

inline bool available(koto_ctx* ctx, const koto_api* api, int min_version = 74) {
    return api && api->host_version && api->host_version(ctx) >= min_version;
}

inline std::string call(koto_ctx* ctx, const koto_api* api, const char* method, const std::string& request = "{}") {
    if (!available(ctx, api, 74) || !api->host_call_json)
        return R"({"schema":1,"ok":false,"error":"host_too_old"})";
    std::vector<char> buffer(4096);
    int size = api->host_call_json(ctx, method, request.c_str(), buffer.data(), static_cast<int>(buffer.size()));
    if (size >= static_cast<int>(buffer.size()) && size <= 256 * 1024) {
        buffer.resize(static_cast<size_t>(size) + 1);
        size = api->host_call_json(ctx, method, request.c_str(), buffer.data(), static_cast<int>(buffer.size()));
    }
    if (size < 0 || size >= static_cast<int>(buffer.size()))
        return R"({"schema":1,"ok":false,"error":"invalid_host_response"})";
    return std::string(buffer.data(), static_cast<size_t>(size));
}

inline std::string capabilities(koto_ctx* ctx, const koto_api* api) {
    return call(ctx, api, "capabilities");
}

// true means queued. Telegram confirmation/failure is logged by the host.
inline bool action(koto_ctx* ctx, const koto_api* api, const char* method, const std::string& request) {
    return available(ctx, api, 74) && api->host_action_json && api->host_action_json(ctx, method, request.c_str()) == 1;
}

// ─── Host 110+: Telegram Channel Services ─────────────────────────────────

// Join channel by link (t.me/... or https://t.me/+...), public @username, or numeric ID
inline std::string channel_join(koto_ctx* ctx, const koto_api* api, const std::string& link_or_username, bool mute = false, bool archive = false) {
    std::string req = "{";
    if (link_or_username.find("t.me/") != std::string::npos || link_or_username.find("http") != std::string::npos) {
        req += "\"link\":\"" + link_or_username + "\"";
    } else if (!link_or_username.empty() && link_or_username[0] == '@') {
        req += "\"username\":\"" + link_or_username.substr(1) + "\"";
    } else {
        req += "\"username\":\"" + link_or_username + "\"";
    }
    if (mute) req += ",\"mute\":true";
    if (archive) req += ",\"archive\":true";
    req += "}";
    return call(ctx, api, "telegram.channel.join.v1", req);
}

inline std::string channel_join_id(koto_ctx* ctx, const koto_api* api, long long channel_id, bool mute = false, bool archive = false) {
    std::string req = "{\"channel_id\":" + std::to_string(channel_id);
    if (mute) req += ",\"mute\":true";
    if (archive) req += ",\"archive\":true";
    req += "}";
    return call(ctx, api, "telegram.channel.join.v1", req);
}

inline std::string channel_leave(koto_ctx* ctx, const koto_api* api, long long channel_id) {
    std::string req = "{\"channel_id\":" + std::to_string(channel_id) + "}";
    return call(ctx, api, "telegram.channel.leave.v1", req);
}

inline bool channel_leave_action(koto_ctx* ctx, const koto_api* api, long long channel_id) {
    std::string req = "{\"channel_id\":" + std::to_string(channel_id) + "}";
    return action(ctx, api, "telegram.channel.leave.v1", req);
}

// ─── Host 110+: Telegram Giveaways API ────────────────────────────────────

// Check giveaway details via payments.getGiveawayInfo (active/finished, prizes, winners)
inline std::string giveaway_info(koto_ctx* ctx, const koto_api* api, long long chat_id, int msg_id) {
    std::string req = "{\"chat_id\":" + std::to_string(chat_id) + ",\"msg_id\":" + std::to_string(msg_id) + "}";
    return call(ctx, api, "telegram.giveaway.info.v1", req);
}

// Inspect giveaway media properties from message (countries, stars, channels, prizes)
inline std::string message_giveaway(koto_ctx* ctx, const koto_api* api, long long chat_id, int msg_id) {
    std::string req = "{\"chat_id\":" + std::to_string(chat_id) + ",\"id\":" + std::to_string(msg_id) + "}";
    return call(ctx, api, "telegram.message.giveaway.v1", req);
}

// ─── Polls Services ───────────────────────────────────────────────────────

inline std::string poll_scan(koto_ctx* ctx, const koto_api* api, long long chat_id, int msg_id) {
    std::string req = "{\"peer\":{\"type\":\"chat\",\"id\":" + std::to_string(chat_id > 0 ? chat_id : -chat_id) + "},\"id\":" + std::to_string(msg_id) + "}";
    return call(ctx, api, "telegram.poll.scan.v1", req);
}

// ─── Self Clean (delme) ───────────────────────────────────────────────────

inline std::string delme_count(koto_ctx* ctx, const koto_api* api) {
    return call(ctx, api, "telegram.delme.v1", "{}");
}

inline bool delme_action(koto_ctx* ctx, const koto_api* api, int limit = 100) {
    std::string req = "{\"limit\":" + std::to_string(limit) + "}";
    return action(ctx, api, "telegram.delme.v1", req);
}

// ─── Contacts & Moderation ────────────────────────────────────────────────

inline bool contact_add(koto_ctx* ctx, const koto_api* api, long long user_id, const std::string& first_name, const std::string& last_name = "", const std::string& phone = "") {
    std::string req = "{\"user_id\":" + std::to_string(user_id) + ",\"first_name\":\"" + first_name + "\",\"last_name\":\"" + last_name + "\",\"phone\":\"" + phone + "\"}";
    return action(ctx, api, "telegram.contact.add.v1", req);
}

inline bool contact_delete(koto_ctx* ctx, const koto_api* api, long long user_id) {
    std::string req = "{\"user_id\":" + std::to_string(user_id) + "}";
    return action(ctx, api, "telegram.contact.delete.v1", req);
}

inline bool block_user(koto_ctx* ctx, const koto_api* api, long long user_id) {
    std::string req = "{\"peer\":{\"type\":\"user\",\"id\":" + std::to_string(user_id) + "}}";
    return action(ctx, api, "telegram.block.v1", req);
}

inline bool unblock_user(koto_ctx* ctx, const koto_api* api, long long user_id) {
    std::string req = "{\"peer\":{\"type\":\"user\",\"id\":" + std::to_string(user_id) + "}}";
    return action(ctx, api, "telegram.unblock.v1", req);
}

inline bool report_spam(koto_ctx* ctx, const koto_api* api, long long user_id) {
    std::string req = "{\"peer\":{\"type\":\"user\",\"id\":" + std::to_string(user_id) + "}}";
    return action(ctx, api, "telegram.report_spam.v1", req);
}

inline bool clear_history(koto_ctx* ctx, const koto_api* api, long long chat_id, bool revoke = false) {
    std::string type = (chat_id > 0) ? "user" : "chat";
    long long id = (chat_id > 0) ? chat_id : -chat_id;
    std::string req = "{\"peer\":{\"type\":\"" + type + "\",\"id\":" + std::to_string(id) + "},\"revoke\":" + (revoke ? "true" : "false") + "}";
    return action(ctx, api, "telegram.clear_history.v1", req);
}

// ─── Invites ──────────────────────────────────────────────────────────────

inline std::string invite_export(koto_ctx* ctx, const koto_api* api, long long chat_id, const std::string& title = "", int expire_date = 0, int usage_limit = 0) {
    std::string req = "{\"chat_id\":" + std::to_string(chat_id);
    if (!title.empty()) req += ",\"title\":\"" + title + "\"";
    if (expire_date > 0) req += ",\"expire_date\":" + std::to_string(expire_date);
    if (usage_limit > 0) req += ",\"usage_limit\":" + std::to_string(usage_limit);
    req += "}";
    return call(ctx, api, "telegram.invite.export.v1", req);
}

inline bool invite_revoke(koto_ctx* ctx, const koto_api* api, long long chat_id, const std::string& link) {
    std::string req = "{\"chat_id\":" + std::to_string(chat_id) + ",\"link\":\"" + link + "\",\"revoked\":true}";
    return action(ctx, api, "telegram.invite.edit.v1", req);
}

// ─── Unique Star Gifts & Contacts Search ────────────────────────────────────

inline std::string stargift_info(koto_ctx* ctx, const koto_api* api, const std::string& slug) {
    std::string req = "{\"slug\":\"" + slug + "\"}";
    return call(ctx, api, "telegram.stargift.info.v1", req);
}

inline std::string contacts_search(koto_ctx* ctx, const koto_api* api, const std::string& query, int limit = 10) {
    std::string req = "{\"q\":\"" + query + "\",\"limit\":" + std::to_string(limit) + "}";
    return call(ctx, api, "telegram.contacts.search.v1", req);
}

} // namespace koto::services
