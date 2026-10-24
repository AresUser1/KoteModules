// PMBL native port of the supplied PMBL.py 3.0.4.
// Original module: Copyright 2022 hikariatama, GNU AGPLv3.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pmbl {

struct Config {
    bool enabled = false;
    bool ignore_contacts = true;
    bool ignore_active = true;
    int active_threshold = 5;
    bool report_spam = false;
    bool delete_dialog = false;
    bool silent = false;
    // Optional strict whitelist mode. Never enabled in place of missing data.
    bool whitelist_only = false;
    std::string custom_message;
    std::string photo = "https://github.com/hikariatama/assets/raw/master/unit_sigma.png";
};

struct Peer {
    std::int64_t me = 0, user = 0, chat = 0, message = 0;
    bool private_chat = false, outgoing = false, approved = false, trusted = false;
    bool has_text = false;
    std::optional<bool> bot, contact;
    std::optional<std::int64_t> first_sender;
    std::optional<unsigned> own_messages;
};
enum class Action { ignore, approve, block, insufficient_data };
struct Decision { Action action; const char* reason; };

inline bool service(std::int64_t id, std::int64_t me) {
    return id <= 0 || id == me || id == 777000 || id == 1271266957;
}

inline Decision decide(const Config& c, const Peer& p) {
    if (!c.enabled || p.outgoing || !p.private_chat || p.chat != p.user || !p.me || service(p.user, p.me))
        return {Action::ignore, "not_target"};
    if (p.approved) return {Action::ignore, "whitelist"};
    if (p.trusted) return {Action::approve, "trusted"};
    if (p.bot == true) return {Action::approve, "bot"};
    if (!c.whitelist_only) {
        // Any positive trust evidence wins even if another field is unavailable.
        if (c.ignore_contacts && p.contact == true) return {Action::approve, "contact"};
        if (p.has_text && p.first_sender && *p.first_sender == p.me) return {Action::approve, "started_by_you"};
        if (c.ignore_active && p.own_messages && *p.own_messages >= static_cast<unsigned>(std::max(1, c.active_threshold)))
            return {Action::approve, "active_threshold"};
    }
    // Absence of data is not evidence that someone is a stranger.
    if (!p.bot.has_value()) return {Action::insufficient_data, "bot_unknown"};
    if (!c.whitelist_only) {
        if (c.ignore_contacts && !p.contact.has_value()) return {Action::insufficient_data, "contact_unknown"};
        if (p.has_text && !p.first_sender.has_value()) return {Action::insufficient_data, "first_message_unknown"};
        if (c.ignore_active && !p.own_messages.has_value()) return {Action::insufficient_data, "activity_unknown"};
    }
    return {Action::block, "unapproved"};
}

class NotificationLimit {
public:
    std::vector<std::int64_t> timestamps;
    void prune(std::int64_t now) {
        timestamps.erase(std::remove_if(timestamps.begin(), timestamps.end(), [now](std::int64_t t) {
            return t > now || t <= now - 300;
        }), timestamps.end());
    }
    bool take(std::int64_t now) {
        prune(now);
        if (timestamps.size() >= 10) return false;
        timestamps.push_back(now); return true;
    }
};

} // namespace pmbl
