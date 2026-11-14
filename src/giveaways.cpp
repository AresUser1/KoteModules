// giveaways.cpp — Автоматическое участие в розыгрышах Telegram (C++ ABI 3).
// Портирован из Python-модуля Giveaways.py (v2.0.0).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ctime>

using json = nlohmann::json;

// ── Кастомные премиум-эмодзи / иконки ─────────────────────────────────────────
static constexpr long long ICON_GIFT  = 5956148757899776734LL; // 🎁 / 💎
static constexpr long long ICON_STAR  = 5985616167740379273LL; // ⭐
static constexpr long long ICON_ON    = 5776375003280838798LL; // ✅
static constexpr long long ICON_OFF   = 5778527486270770928LL; // ❌
static constexpr long long ICON_TIME  = 5985616167740379273LL; // ⏳
static constexpr long long ICON_CH    = 6030400221232501136LL; // 📢
static constexpr long long ICON_STATS = 5877307202888273539LL; // 📊

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

// ── Хранилище состояния в БД модуля ──────────────────────────────────────────
static json load_giveaways(koto_ctx* ctx, const koto_api* api) {
    char buf[65536] = {0};
    int len = api->db_get(ctx, "giveaways", buf, sizeof(buf) - 1);
    if (len > 0) {
        try {
            auto j = json::parse(std::string(buf, len));
            if (j.is_array()) return j;
        } catch (...) {}
    }
    return json::array();
}

static void save_giveaways(koto_ctx* ctx, const koto_api* api, const json& j) {
    std::string s = j.dump();
    api->db_set(ctx, "giveaways", s.c_str());
}

static json load_channels(koto_ctx* ctx, const koto_api* api) {
    char buf[65536] = {0};
    int len = api->db_get(ctx, "channels", buf, sizeof(buf) - 1);
    if (len > 0) {
        try {
            auto j = json::parse(std::string(buf, len));
            if (j.is_object()) return j;
        } catch (...) {}
    }
    return json::object();
}

static void save_channels(koto_ctx* ctx, const koto_api* api, const json& j) {
    std::string s = j.dump();
    api->db_set(ctx, "channels", s.c_str());
}

static std::set<std::string> load_skips(koto_ctx* ctx, const koto_api* api) {
    std::set<std::string> skips;
    char buf[16384] = {0};
    int len = api->db_get(ctx, "skips", buf, sizeof(buf) - 1);
    if (len > 0) {
        try {
            auto j = json::parse(std::string(buf, len));
            if (j.is_array()) {
                for (const auto& el : j) if (el.is_string()) skips.insert(el.get<std::string>());
            }
        } catch (...) {}
    }
    return skips;
}

static void save_skips(koto_ctx* ctx, const koto_api* api, const std::set<std::string>& skips) {
    json j = json::array();
    for (const auto& s : skips) j.push_back(s);
    std::string out = j.dump();
    api->db_set(ctx, "skips", out.c_str());
}

// ── Форматирование времени ───────────────────────────────────────────────────
static std::string format_remaining(long long until_ts) {
    time_t now = time(nullptr);
    long long diff = until_ts - now;
    if (diff <= 0) return "завершён";
    long long days = diff / 86400;
    long long hours = (diff % 86400) / 3600;
    long long mins = (diff % 3600) / 60;
    if (days > 0) {
        return std::to_string(days) + " д. " + std::to_string(hours) + " ч.";
    }
    if (hours > 0) {
        return std::to_string(hours) + " ч. " + std::to_string(mins) + " мин.";
    }
    return std::to_string(mins) + " мин.";
}

static std::string format_prize(const json& gw) {
    int months = gw.value("months", 0);
    long long stars = gw.value("stars", 0LL);
    std::string desc = gw.value("desc", "");
    if (!desc.empty()) return desc;
    if (months > 0 && stars > 0) {
        return "Premium (" + std::to_string(months) + " мес.) + " + std::to_string(stars) + " Stars";
    }
    if (months > 0) {
        return "Premium на " + std::to_string(months) + " мес.";
    }
    if (stars > 0) {
        return std::to_string(stars) + " Stars";
    }
    return "Приз";
}

// ── Фоновое обслуживание: проверка результатов и отписка ─────────────────────
static void run_maintenance(koto_ctx* ctx, const koto_api* api) {
    if (!api->host_call_json) return;

    time_t now = time(nullptr);
    auto gws = load_giveaways(ctx, api);
    bool gws_changed = false;

    char call_buf[2048] = {0};

    // 1. Проверяем завершившиеся розыгрыши
    for (auto& gw : gws) {
        std::string res = gw.value("result", "");
        long long until = gw.value("until", 0LL);
        if (!res.empty() || until > now) continue;

        long long cid = gw.value("chat_id", 0LL);
        int mid = gw.value("msg_id", 0);
        if (cid == 0 || mid <= 0) continue;

        json qreq = {{"chat_id", cid}, {"msg_id", mid}};
        int qlen = api->host_call_json(ctx, "telegram.giveaway.info.v1", qreq.dump().c_str(), call_buf, sizeof(call_buf) - 1);
        if (qlen > 0) {
            try {
                auto res_j = json::parse(std::string(call_buf, qlen));
                if (res_j.value("ok", false) && res_j.value("status", "") == "finished") {
                    bool winner = res_j.value("winner", false);
                    gw["result"] = winner ? "win" : "lose";
                    if (res_j.contains("stars_prize")) {
                        gw["stars_prize"] = res_j["stars_prize"].get<long long>();
                    }
                    gws_changed = true;
                }
            } catch (...) {}
        }
    }
    if (gws_changed) {
        save_giveaways(ctx, api, gws);
    }

    // 2. Проверяем каналы для авто-отписки
    char auto_leave_buf[32] = {0};
    int al_len = api->db_get(ctx, "auto_leave", auto_leave_buf, sizeof(auto_leave_buf) - 1);
    bool auto_leave = (al_len <= 0 || strcmp(auto_leave_buf, "1") == 0 || strcmp(auto_leave_buf, "true") == 0);

    if (auto_leave) {
        auto channels = load_channels(ctx, api);
        bool ch_changed = false;

        for (auto it = channels.begin(); it != channels.end(); ++it) {
            auto& c = it.value();
            std::string state = c.value("state", "");
            long long leave_at = c.value("leave_at", 0LL);
            if (state == "joined" && leave_at > 0 && leave_at <= now) {
                long long ch_id = c.value("id", 0LL);
                if (ch_id != 0) {
                    json lreq = {{"channel_id", ch_id}};
                    int llen = api->host_call_json(ctx, "telegram.channel.leave.v1", lreq.dump().c_str(), call_buf, sizeof(call_buf) - 1);
                    if (llen > 0) {
                        try {
                            auto lj = json::parse(std::string(call_buf, llen));
                            if (lj.value("ok", false)) {
                                c["state"] = "left";
                                c["left_at"] = (long long)now;
                                ch_changed = true;
                            }
                        } catch (...) {}
                    }
                }
            }
        }
        if (ch_changed) {
            save_channels(ctx, api, channels);
        }
    }
}

// ── Обработка розыгрыша из сообщения ──────────────────────────────────────────
static bool process_giveaway(koto_ctx* ctx, const koto_api* api, long long chat_id, int msg_id, const json& gw_obj) {
    if (!api->host_call_json) return false;

    // 1. Проверяем включение модуля
    char en_buf[32] = {0};
    int en_len = api->db_get(ctx, "enabled", en_buf, sizeof(en_buf) - 1);
    bool enabled = (en_len <= 0 || strcmp(en_buf, "1") == 0 || strcmp(en_buf, "true") == 0);
    if (!enabled) return false;

    std::string uid = std::to_string(chat_id) + "_" + std::to_string(msg_id);
    auto skips = load_skips(ctx, api);
    if (skips.find(uid) != skips.end()) return false;

    auto gws = load_giveaways(ctx, api);
    for (const auto& g : gws) {
        if (g.value("uid", "") == uid) return false;
    }

    // 2. Проверяем фильтр призов
    char tgt_buf[64] = {0};
    int tgt_len = api->db_get(ctx, "target", tgt_buf, sizeof(tgt_buf) - 1);
    std::string target = tgt_len > 0 ? std::string(tgt_buf, tgt_len) : "All";

    int months = gw_obj.value("months", 0);
    long long stars = gw_obj.value("stars", 0LL);
    if (target == "Premium" && months <= 0) return false;
    if (target == "Stars" && stars <= 0) return false;

    long long until = gw_obj.value("until_date", 0LL);
    time_t now = time(nullptr);
    if (until <= now) {
        skips.insert(uid);
        save_skips(ctx, api, skips);
        return false;
    }

    // 3. Вычисляем fingerprint для дедупликации
    std::vector<int64_t> sponsor_channels;
    if (gw_obj.contains("channels") && gw_obj["channels"].is_array()) {
        for (const auto& cid : gw_obj["channels"]) {
            if (cid.is_number_integer()) sponsor_channels.push_back(cid.get<int64_t>());
        }
    }

    std::string fp = std::to_string(until) + "_" + std::to_string(months) + "_" + std::to_string(stars);
    for (int64_t sc : sponsor_channels) fp += "_" + std::to_string(sc);

    for (const auto& g : gws) {
        if (g.value("fp", "") == fp) {
            skips.insert(uid);
            save_skips(ctx, api, skips);
            return false;
        }
    }

    // 4. Подписываемся на спонсорские каналы
    char mute_buf[32] = {0};
    int m_len = api->db_get(ctx, "mute_channels", mute_buf, sizeof(mute_buf) - 1);
    bool mute = (m_len <= 0 || strcmp(mute_buf, "1") == 0 || strcmp(mute_buf, "true") == 0);

    char arch_buf[32] = {0};
    int a_len = api->db_get(ctx, "archive_channels", arch_buf, sizeof(arch_buf) - 1);
    bool archive = (a_len <= 0 || strcmp(arch_buf, "1") == 0 || strcmp(arch_buf, "true") == 0);

    long long leave_after = api->db_get_int(ctx, "leave_after_hours", 24);
    long long leave_at = until + leave_after * 3600;

    auto channels = load_channels(ctx, api);
    char call_buf[2048] = {0};

    int joined_new = 0;
    for (int64_t ch_id : sponsor_channels) {
        std::string key = std::to_string(ch_id);
        if (channels.contains(key) && channels[key].value("state", "") == "joined") {
            long long prev_leave = channels[key].value("leave_at", 0LL);
            channels[key]["leave_at"] = std::max(prev_leave, leave_at);
            continue;
        }

        json jreq = {
            {"channel_id", ch_id},
            {"mute", mute},
            {"archive", archive}
        };
        int jlen = api->host_call_json(ctx, "telegram.channel.join.v1", jreq.dump().c_str(), call_buf, sizeof(call_buf) - 1);
        if (jlen > 0) {
            try {
                auto jres = json::parse(std::string(call_buf, jlen));
                if (jres.value("ok", false)) {
                    channels[key] = {
                        {"id", ch_id},
                        {"state", "joined"},
                        {"joined_at", (long long)now},
                        {"leave_at", leave_at}
                    };
                    joined_new++;
                }
            } catch (...) {}
        }
    }
    save_channels(ctx, api, channels);

    // 5. Проверяем статус подтверждения участия
    json qreq = {{"chat_id", chat_id}, {"msg_id", msg_id}};
    int qlen = api->host_call_json(ctx, "telegram.giveaway.info.v1", qreq.dump().c_str(), call_buf, sizeof(call_buf) - 1);
    bool participating = false;
    std::string reject_reason;

    if (qlen > 0) {
        try {
            auto qres = json::parse(std::string(call_buf, qlen));
            if (qres.value("ok", false)) {
                participating = qres.value("participating", false);
                if (!participating) {
                    if (qres.value("joined_too_early_date", 0) > 0) {
                        reject_reason = "Слишком ранняя подписка";
                    } else if (qres.value("admin_disallowed_chat_id", 0LL) != 0) {
                        reject_reason = "Канал запретил участие этому аккаунту";
                    } else if (!qres.value("disallowed_country", "").empty()) {
                        reject_reason = "Недоступно для страны " + qres.value("disallowed_country", "");
                    } else if (qres.value("preparing_results", false)) {
                        reject_reason = "Telegram уже подводит итоги";
                    }
                }
            }
        } catch (...) {}
    }

    // 6. Сохраняем результат
    json rec = {
        {"uid", uid},
        {"chat_id", chat_id},
        {"msg_id", msg_id},
        {"fp", fp},
        {"until", until},
        {"channels", sponsor_channels},
        {"months", months},
        {"stars", stars},
        {"desc", gw_obj.value("prize_description", "")},
        {"joined_at", (long long)now},
        {"participating", participating},
        {"result", ""}
    };

    gws.push_back(rec);
    save_giveaways(ctx, api, gws);
    return true;
}

// ── Команда .gw — Включение / отключение авто-участия ─────────────────────────
static void cmd_gw(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    char en_buf[32] = {0};
    int en_len = api->db_get(ctx, "enabled", en_buf, sizeof(en_buf) - 1);
    bool current = (en_len <= 0 || strcmp(en_buf, "1") == 0 || strcmp(en_buf, "true") == 0);
    bool next = !current;

    api->db_set(ctx, "enabled", next ? "1" : "0");

    api->c_reset(ctx);
    if (next) {
        api->c_emoji(ctx, "✅", ICON_ON);
        api->c_fmt(ctx, " Авто-участие в розыгрышах включено\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, "Бот будет автоматически вступать в каналы спонсоров и отслеживать призы.", KOTO_ENT_ITALIC);
    } else {
        api->c_emoji(ctx, "❌", ICON_OFF);
        api->c_fmt(ctx, " Авто-участие в розыгрышах отключено\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, "Новые розыгрыши не будут отслеживаться.", KOTO_ENT_ITALIC);
    }
    send_result(ctx, api);
}

// ── Команда .gwst — Список активных розыгрышей ────────────────────────────────
static void cmd_gwst(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    run_maintenance(ctx, api);

    time_t now = time(nullptr);
    auto gws = load_giveaways(ctx, api);

    std::vector<json> active;
    for (const auto& g : gws) {
        if (g.value("until", 0LL) > now) {
            active.push_back(g);
        }
    }

    std::sort(active.begin(), active.end(), [](const json& a, const json& b) {
        return a.value("until", 0LL) < b.value("until", 0LL);
    });

    api->c_reset(ctx);
    api->c_emoji(ctx, "🎁", ICON_GIFT);
    api->c_fmt(ctx, " Активные розыгрыши", KOTO_ENT_BOLD);
    std::string header_cnt = " (всего: " + std::to_string(active.size()) + ")\n\n";
    api->c_fmt(ctx, header_cnt.c_str(), KOTO_ENT_NONE);

    if (active.empty()) {
        api->c_fmt(ctx, "Нет активных розыгрышей.\nИспользуйте ", KOTO_ENT_ITALIC);
        api->c_fmt(ctx, ".gwscan", KOTO_ENT_CODE);
        api->c_fmt(ctx, " для сканирования истории чатов.", KOTO_ENT_ITALIC);
        send_result(ctx, api);
        return;
    }

    int count = 0;
    for (const auto& gw : active) {
        if (++count > 25) {
            std::string more = "...\nи ещё " + std::to_string(active.size() - 25) + " розыгрышей.";
            api->c_fmt(ctx, more.c_str(), KOTO_ENT_ITALIC);
            break;
        }

        long long until = gw.value("until", 0LL);
        long long cid = gw.value("chat_id", 0LL);
        int mid = gw.value("msg_id", 0);
        int ch_count = gw.contains("channels") && gw["channels"].is_array() ? (int)gw["channels"].size() : 0;

        api->c_emoji(ctx, "⭐", ICON_STAR);
        std::string prize_line = " " + format_prize(gw) + "\n";
        api->c_fmt(ctx, prize_line.c_str(), KOTO_ENT_BOLD);

        api->c_fmt(ctx, "  ⏳ До завершения: ", KOTO_ENT_NONE);
        std::string rem_line = format_remaining(until) + "\n";
        api->c_fmt(ctx, rem_line.c_str(), KOTO_ENT_CODE);

        api->c_fmt(ctx, "  📢 Спонсоров: ", KOTO_ENT_NONE);
        std::string ch_line = std::to_string(ch_count) + " каналов\n";
        api->c_fmt(ctx, ch_line.c_str(), KOTO_ENT_NONE);

        std::string link;
        if (cid < 0) {
            std::string bare = std::to_string(-cid);
            if (bare.rfind("100", 0) == 0) bare = bare.substr(3);
            link = "https://t.me/c/" + bare + "/" + std::to_string(mid);
        } else {
            link = "https://t.me/c/" + std::to_string(cid) + "/" + std::to_string(mid);
        }
        api->c_fmt(ctx, "  🔗 ", KOTO_ENT_NONE);
        api->c_url(ctx, "Перейти к розыгрышу", link.c_str());
        api->c_fmt(ctx, "\n\n", KOTO_ENT_NONE);
    }

    send_result(ctx, api);
}

// ── Команда .gwstats — Полная статистика ──────────────────────────────────────
static void cmd_gwstats(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    run_maintenance(ctx, api);

    time_t now = time(nullptr);
    auto gws = load_giveaways(ctx, api);
    auto channels = load_channels(ctx, api);

    int total_joined = (int)gws.size();
    int wins = 0;
    int active = 0;
    int waiting = 0;

    for (const auto& g : gws) {
        std::string res = g.value("result", "");
        if (res == "win") wins++;
        long long until = g.value("until", 0LL);
        if (until > now) active++;
        else if (res.empty()) waiting++;
    }

    int total_channels = (int)channels.size();
    int subscribed_now = 0;
    int left_channels = 0;

    for (auto it = channels.begin(); it != channels.end(); ++it) {
        std::string state = it.value().value("state", "");
        if (state == "joined") subscribed_now++;
        else if (state == "left") left_channels++;
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "📊", ICON_STATS);
    api->c_fmt(ctx, " Статистика розыгрышей\n\n", KOTO_ENT_BOLD);

    api->c_fmt(ctx, "🎁 Участвовал всего: ", KOTO_ENT_NONE);
    std::string s_total = std::to_string(total_joined) + "\n";
    api->c_fmt(ctx, s_total.c_str(), KOTO_ENT_CODE);

    api->c_fmt(ctx, "🏆 Побед: ", KOTO_ENT_NONE);
    std::string s_wins = std::to_string(wins) + "\n";
    api->c_fmt(ctx, s_wins.c_str(), KOTO_ENT_CODE);

    api->c_fmt(ctx, "⏳ Активных сейчас: ", KOTO_ENT_NONE);
    std::string s_active = std::to_string(active) + "\n";
    api->c_fmt(ctx, s_active.c_str(), KOTO_ENT_CODE);

    api->c_fmt(ctx, "🕒 Ожидают итогов: ", KOTO_ENT_NONE);
    std::string s_waiting = std::to_string(waiting) + "\n\n";
    api->c_fmt(ctx, s_waiting.c_str(), KOTO_ENT_CODE);

    api->c_fmt(ctx, "📢 Спонсорских каналов всего: ", KOTO_ENT_NONE);
    std::string s_ch_tot = std::to_string(total_channels) + "\n";
    api->c_fmt(ctx, s_ch_tot.c_str(), KOTO_ENT_CODE);

    api->c_fmt(ctx, "✅ Сейчас в подписках: ", KOTO_ENT_NONE);
    std::string s_sub = std::to_string(subscribed_now) + "\n";
    api->c_fmt(ctx, s_sub.c_str(), KOTO_ENT_CODE);

    api->c_fmt(ctx, "🚪 Покинул каналов: ", KOTO_ENT_NONE);
    std::string s_left = std::to_string(left_channels) + "\n";
    api->c_fmt(ctx, s_left.c_str(), KOTO_ENT_CODE);

    send_result(ctx, api);
}

// ── Команда .gwscan — Сканирование чатов на пропущенные розыгрыши ────────────
static void cmd_gwscan(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    if (!api->host_call_json) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "Хост не поддерживает сканирование.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    int history_limit = (int)api->db_get_int(ctx, "scan_history_limit", 50);
    const char* args = api->cmd_args(ctx);
    if (args && *args) {
        int parsed = atoi(koto_utils::trim(args).c_str());
        if (parsed > 0 && parsed <= 200) history_limit = parsed;
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "⏳", ICON_TIME);
    api->c_fmt(ctx, " Сканирование чатов...\n", KOTO_ENT_BOLD);
    api->c_fmt(ctx, "Поиск пропущенных розыгрышей в диалогах.", KOTO_ENT_ITALIC);
    send_result(ctx, api);

    // 1. Получаем список диалогов
    char d_buf[65536] = {0};
    int d_len = api->host_call_json(ctx, "telegram.dialogs.v1", "{\"limit\": 100}", d_buf, sizeof(d_buf) - 1);
    if (d_len <= 0) return;

    json d_json;
    try {
        d_json = json::parse(std::string(d_buf, d_len));
    } catch (...) {
        return;
    }

    if (!d_json.value("ok", false) || !d_json.contains("dialogs") || !d_json["dialogs"].is_array()) return;

    int scanned_chats = 0;
    int found_giveaways = 0;

    char h_buf[65536] = {0};

    for (const auto& dlg : d_json["dialogs"]) {
        if (!dlg.contains("peer") || !dlg["peer"].is_object()) continue;
        const auto& peer = dlg["peer"];
        std::string ptype = peer.value("type", "");
        if (ptype != "channel" && ptype != "chat") continue;

        int64_t chat_id = peer.value("id", 0LL);
        if (chat_id <= 0) continue;
        if (ptype == "channel") chat_id = -1000000000000LL - chat_id;
        else if (ptype == "chat") chat_id = -chat_id;

        scanned_chats++;

        // Запрашиваем историю
        json h_req = {
            {"peer", peer},
            {"limit", history_limit},
            {"include_text", false}
        };
        int h_len = api->host_call_json(ctx, "telegram.history.v1", h_req.dump().c_str(), h_buf, sizeof(h_buf) - 1);
        if (h_len <= 0) continue;

        try {
            auto hj = json::parse(std::string(h_buf, h_len));
            if (hj.value("ok", false) && hj.contains("messages") && hj["messages"].is_array()) {
                for (const auto& m : hj["messages"]) {
                    if (m.contains("giveaway") && m["giveaway"].is_object()) {
                        int mid = m.value("id", 0);
                        if (mid > 0 && process_giveaway(ctx, api, chat_id, mid, m["giveaway"])) {
                            found_giveaways++;
                        }
                    }
                }
            }
        } catch (...) {}
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", ICON_ON);
    api->c_fmt(ctx, " Сканирование завершено!\n\n", KOTO_ENT_BOLD);

    api->c_fmt(ctx, "Проверено диалогов: ", KOTO_ENT_NONE);
    std::string s_scanned = std::to_string(scanned_chats) + "\n";
    api->c_fmt(ctx, s_scanned.c_str(), KOTO_ENT_CODE);

    api->c_fmt(ctx, "Найдено новых розыгрышей: ", KOTO_ENT_NONE);
    std::string s_found = std::to_string(found_giveaways) + "\n";
    api->c_fmt(ctx, s_found.c_str(), KOTO_ENT_CODE);

    if (found_giveaways > 0) {
        api->c_fmt(ctx, "\nИспользуйте ", KOTO_ENT_NONE);
        api->c_fmt(ctx, ".gwst", KOTO_ENT_CODE);
        api->c_fmt(ctx, " для просмотра активных розыгрышей.", KOTO_ENT_NONE);
    }
    send_result(ctx, api);
}

// ── Watcher входящих/исходящих сообщений ──────────────────────────────────────
static int on_watch(koto_ctx* ctx, const koto_api* api) {
    if (!api->host_call_json) return 0;

    // Быстрый запрос: есть ли в текущем сообщении розыгрыш?
    char gw_buf[4096] = {0};
    int qlen = api->host_call_json(ctx, "telegram.message.giveaway.v1", "{}", gw_buf, sizeof(gw_buf) - 1);
    if (qlen <= 0) return 0;

    try {
        auto j = json::parse(std::string(gw_buf, qlen));
        if (j.value("ok", false) && j.value("is_giveaway", false) && j.contains("giveaway") && j["giveaway"].is_object()) {
            long long cid = api->chat_id(ctx);
            int mid = api->msg_id ? (int)api->msg_id(ctx) : 0;
            if (cid != 0 && mid > 0) {
                process_giveaway(ctx, api, cid, mid, j["giveaway"]);
            }
        }
    } catch (...) {}

    return 0;
}

// ── Колбэк (защита) ─────────────────────────────────────────────────────────
static void on_callback(koto_ctx* ctx, const koto_api* api) {
    (void)ctx; (void)api;
}

// ── Схема настроек модуля для панели .cfg ────────────────────────────────────
static const koto_setting_field SETTINGS[] = {
    {
        "enabled",
        "Включить авто-участие в розыгрышах",
        KOTO_FIELD_BOOL,
        "true",
        nullptr
    },
    {
        "target",
        "Какие розыгрыши отслеживать (All, Premium, Stars)",
        KOTO_FIELD_STR,
        "All",
        nullptr
    },
    {
        "mute_channels",
        "Заглушать уведомления спонсорских каналов",
        KOTO_FIELD_BOOL,
        "true",
        nullptr
    },
    {
        "archive_channels",
        "Отправлять спонсорские каналы в архив",
        KOTO_FIELD_BOOL,
        "true",
        nullptr
    },
    {
        "auto_leave",
        "Автоматически выходить из каналов после розыгрыша",
        KOTO_FIELD_BOOL,
        "true",
        nullptr
    },
    {
        "leave_after_hours",
        "Через сколько часов после окончания выходить из каналов",
        KOTO_FIELD_INT,
        "24",
        nullptr
    },
    {
        "scan_history_limit",
        "Лимит сообщений на чат при сканировании (.gwscan)",
        KOTO_FIELD_INT,
        "50",
        nullptr
    }
};

static const koto_command COMMANDS[] = {
    {
        "gw",
        &cmd_gw,
        KOTO_LEVEL_TRUSTED,
        "Включить или выключить авто-участие в розыгрышах.",
        nullptr
    },
    {
        "gwst",
        &cmd_gwst,
        KOTO_LEVEL_TRUSTED,
        "Показать список всех активных розыгрышей и оставшееся время.",
        nullptr
    },
    {
        "gwstats",
        &cmd_gwstats,
        KOTO_LEVEL_TRUSTED,
        "Подробная статистика участий, побед и спонсорских каналов.",
        nullptr
    },
    {
        "gwscan",
        &cmd_gwscan,
        KOTO_LEVEL_TRUSTED,
        "Просканировать историю чатов и диалогов на пропущенные розыгрыши.",
        "[лимит_сообщений_на_чат]"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "giveaways",
    "Автоматическое участие в Telegram-розыгрышах (Premium & Stars) с отпиской от спонсоров.",
    "2.0.0", 87,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    &on_watch,
    KOTO_WATCH_ALL,
    &on_callback,
    SETTINGS,
    sizeof(SETTINGS) / sizeof(SETTINGS[0]),
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/giveaways.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
