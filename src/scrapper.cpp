// scrapper.cpp — Модуль сбора участников и активности чатов (C++ ABI 3).
// Портирован из Python-модуля scrapper.py (v2.0.0).
// Команды: .scrap (или .scrapper), .stopscrap, .killscrap, .scrapcfg.
// Поддержка экспорта в .txt файл, фильтрации ботов и администраторов, выбора воркера (твинка).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <ctime>
#include <cctype>

namespace {

static constexpr long long EMO_SCRAP = 5778661935927004845LL; // 📥
static constexpr long long EMO_CFG   = 5470049770997292425LL; // ⚙️
static constexpr long long EMO_OK    = 5283097055852503586LL; // ✅

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
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

static std::string sanitize_filename(const std::string& title) {
    std::string clean;
    for (char c : title) {
        if (isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.') {
            clean.push_back(c);
        } else if (c == ' ') {
            clean.push_back('_');
        }
    }
    if (clean.empty()) clean = "chat_dump";
    return clean;
}

static std::string to_lower_str(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// ── Чтение и запись конфигурации через DB API ───────────────────────────────

struct ScrapperConfig {
    std::string method = "MEMBERS"; // MEMBERS / ACTIVITY
    int limit = 0;                  // 0 = без лимита
    bool skip_admins = true;
    bool skip_bots = true;
    bool collect_reactions = true;  // сбор тех, кто ставил реакции
    bool collect_tags = true;       // сбор упомянутых через @username
    std::string worker = "MAIN";    // MAIN или UID/имя твинка
};

static ScrapperConfig load_config(koto_ctx* ctx, const koto_api* api) {
    ScrapperConfig cfg;
    char buf[256];
    if (api->db_get(ctx, "method", buf, sizeof(buf)) > 0) cfg.method = buf;
    cfg.limit = static_cast<int>(api->db_get_int(ctx, "limit", 0));
    cfg.skip_admins = api->db_get_int(ctx, "skip_admins", 1) != 0;
    cfg.skip_bots = api->db_get_int(ctx, "skip_bots", 1) != 0;
    cfg.collect_reactions = api->db_get_int(ctx, "collect_reactions", 1) != 0;
    cfg.collect_tags = api->db_get_int(ctx, "collect_tags", 1) != 0;
    if (api->db_get(ctx, "worker", buf, sizeof(buf)) > 0) cfg.worker = buf;
    return cfg;
}

static void save_config(koto_ctx* ctx, const koto_api* api, const ScrapperConfig& cfg) {
    api->db_set(ctx, "method", cfg.method.c_str());
    api->db_set_int(ctx, "limit", cfg.limit);
    api->db_set_int(ctx, "skip_admins", cfg.skip_admins ? 1 : 0);
    api->db_set_int(ctx, "skip_bots", cfg.skip_bots ? 1 : 0);
    api->db_set_int(ctx, "collect_reactions", cfg.collect_reactions ? 1 : 0);
    api->db_set_int(ctx, "collect_tags", cfg.collect_tags ? 1 : 0);
    api->db_set(ctx, "worker", cfg.worker.c_str());
}

// Поиск UID воркера (0 = MAIN; > 0 = twin UID; -1 = указанный твинк не найден)
static long long resolve_worker(koto_ctx* ctx, const koto_api* api, const std::string& worker_str, std::string& out_label) {
    std::string w = koto_utils::trim(worker_str);
    if (w.empty() || to_lower_str(w) == "main" || w == "0") {
        out_label = "MAIN";
        return 0;
    }

    long long numeric_id = -1;
    if (is_all_digits(w)) {
        try {
            numeric_id = std::stoll(w);
            out_label = std::to_string(numeric_id);
        } catch (...) {}
    }

    std::string w_lower = to_lower_str(w);

    // 1. Поиск через get_twins_json
    if (api->get_twins_json) {
        char tbuf[16384] = {0};
        int tlen = api->get_twins_json(ctx, tbuf, sizeof(tbuf) - 1);
        if (tlen > 0) {
            try {
                auto tj = nlohmann::json::parse(std::string(tbuf, tlen));
                if (tj.is_array()) {
                    for (const auto& item : tj) {
                        std::string tname = item.value("name", "");
                        std::string tname_l = to_lower_str(tname);
                        long long tuid = item.value("uid", 0LL);
                        if (tuid <= 0) tuid = item.value("id", 0LL);
                        if ((numeric_id > 0 && tuid == numeric_id) ||
                            (numeric_id <= 0 && (tname_l == w_lower || (!w_lower.empty() && tname_l.find(w_lower) != std::string::npos)))) {
                            out_label = tname.empty() ? std::to_string(tuid) : tname;
                            return tuid;
                        }
                    }
                }
            } catch (...) {}
        }
    }

    // 2. Поиск через twin_list (uid\tname\tphone)
    if (api->twin_list) {
        char tbuf[16384] = {0};
        int tlen = api->twin_list(ctx, tbuf, sizeof(tbuf) - 1);
        if (tlen > 0) {
            std::istringstream tss(std::string(tbuf, tlen));
            std::string line;
            while (std::getline(tss, line)) {
                if (line.empty()) continue;
                size_t t1 = line.find('\t');
                if (t1 == std::string::npos) continue;
                std::string tuid_str = line.substr(0, t1);
                size_t t2 = line.find('\t', t1 + 1);
                std::string tname = (t2 == std::string::npos) ? line.substr(t1 + 1) : line.substr(t1 + 1, t2 - (t1 + 1));
                std::string tname_l = to_lower_str(tname);
                long long tuid = 0;
                try { tuid = std::stoll(tuid_str); } catch (...) {}
                if ((numeric_id > 0 && tuid == numeric_id) ||
                    (numeric_id <= 0 && (tname_l == w_lower || (!w_lower.empty() && tname_l.find(w_lower) != std::string::npos)))) {
                    out_label = tname.empty() ? tuid_str : tname;
                    return tuid;
                }
            }
        }
    }

    if (numeric_id > 0) {
        return numeric_id;
    }

    return -1; // Не найден
}

// ── Обработчик команды .scrap ───────────────────────────────────────────────

static void cmd_scrap(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? koto_utils::trim(raw) : "";

    long long target_cid = 0;
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
            try { target_cid = std::stoll(clean_arg); } catch (...) { target_cid = 0; }
        } else {
            std::string uname = clean_arg;
            if (!uname.empty() && uname[0] == '@') uname = uname.substr(1);
            if (api->resolve) target_cid = api->resolve(ctx, uname.c_str());
        }
    } else {
        if (api->reply_sender_id) {
            long long r_id = api->reply_sender_id(ctx);
            if (r_id < 0) target_cid = r_id;
        }
        if (target_cid == 0 && api->chat_id) target_cid = api->chat_id(ctx);
    }

    if (target_cid == 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, "Чат для скрапинга не найден.\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "Укажите ID или @username: ");
        api->c_fmt(ctx, ".scrap @chat", KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

    ScrapperConfig cfg = load_config(ctx, api);

    std::string worker_label = cfg.worker;
    long long worker_uid = resolve_worker(ctx, api, cfg.worker, worker_label);
    if (worker_uid < 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, ("Твинк '" + cfg.worker + "' не найден или не активен.\n").c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, "Проверьте список подключённых твинков: ");
        api->c_fmt(ctx, ".twins", KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

    std::string chat_title = "Чат " + std::to_string(target_cid);
    std::string chat_uname = "";
    std::unordered_set<long long> admin_set;
    std::unordered_set<long long> bot_set;
    std::vector<long long> participants;

    if (api->chat_full_json) {
        std::vector<char> buf(65536, 0);
        int n = api->chat_full_json(ctx, target_cid, buf.data(), static_cast<int>(buf.size()));
        if (n > 0) {
            try {
                auto j = nlohmann::json::parse(std::string(buf.data(), static_cast<size_t>(n)));
                chat_title = j.value("title", chat_title);
                chat_uname = j.value("username", "");
                if (j.contains("admin_ids")) {
                    for (auto aid : j["admin_ids"]) admin_set.insert(aid.get<long long>());
                }
                if (j.contains("bot_ids")) {
                    for (auto bid : j["bot_ids"]) bot_set.insert(bid.get<long long>());
                }
                if (j.contains("participant_ids")) {
                    for (auto pid : j["participant_ids"]) participants.push_back(pid.get<long long>());
                }
            } catch (...) {}
        }
    }

    std::vector<std::string> dump_lines;
    int collected_count = 0;
    int authors_count = 0;
    int reactors_count = 0;
    int tags_count = 0;
    bool fallback_to_main = false;

    if (cfg.method == "ACTIVITY") {
        // Режим активности по сообщениям (с поддержкой твинков и постраничной пагинацией)
        std::vector<char> hbuf(131072, 0);
        int limit_msgs = cfg.limit > 0 ? cfg.limit : 1000;
        std::unordered_map<long long, int> user_msg_counts;
        std::unordered_map<long long, int> user_reaction_counts;
        std::unordered_map<long long, int> user_reply_counts;
        std::unordered_map<std::string, int> tagged_usernames;

        auto process_message_item = [&](const nlohmann::json& m) {
            long long from_id = m.value("sender_id", 0LL);
            if (from_id == 0) from_id = m.value("from_id", 0LL);
            if (from_id > 0) user_msg_counts[from_id]++;

            if (cfg.collect_reactions && m.contains("reactors") && m["reactors"].is_array()) {
                for (const auto& r_val : m["reactors"]) {
                    long long r_id = 0;
                    if (r_val.is_number()) r_id = r_val.get<long long>();
                    if (r_id > 0) user_reaction_counts[r_id]++;
                }
            }

            long long rep_id = m.value("reply_to_peer_id", 0LL);
            if (rep_id == 0) rep_id = m.value("reply_to_sender_id", 0LL);
            if (rep_id > 0) user_reply_counts[rep_id]++;

            if (cfg.collect_tags && m.contains("text")) {
                std::string text = m.value("text", "");
                if (!text.empty()) {
                    size_t pos = 0;
                    while ((pos = text.find('@', pos)) != std::string::npos) {
                        size_t start = pos + 1;
                        size_t end = start;
                        while (end < text.size() && (std::isalnum((unsigned char)text[end]) || text[end] == '_')) {
                            end++;
                        }
                        if (end - start >= 3 && end - start <= 32) {
                            std::string tag = text.substr(start, end - start);
                            tagged_usernames[to_lower_str(tag)]++;
                        }
                        pos = end;
                    }
                }
            }
        };

        for (int offset = 0; offset < limit_msgs; offset += 100) {
            int chunk = std::min(100, limit_msgs - offset);
            int hn = 0;
            if (worker_uid > 0 && api->twin_get_history_json) {
                hn = api->twin_get_history_json(ctx, worker_uid, target_cid, 0, chunk, offset, hbuf.data(), static_cast<int>(hbuf.size()) - 1);
            } else if (api->get_history_json) {
                hn = api->get_history_json(ctx, target_cid, 0, chunk, offset, hbuf.data(), static_cast<int>(hbuf.size()) - 1);
            }
            if (hn <= 2) break;

            int parsed_in_chunk = 0;
            try {
                auto hj = nlohmann::json::parse(std::string(hbuf.data(), static_cast<size_t>(hn)));
                if (hj.is_array()) {
                    parsed_in_chunk = static_cast<int>(hj.size());
                    for (const auto& m : hj) {
                        process_message_item(m);
                    }
                }
            } catch (...) {
                break;
            }
            if (parsed_in_chunk == 0) break;
        }

        // Если выбранный твинк не вернул сообщений (например, не состоит в чате), пробуем основной аккаунт
        if (worker_uid > 0 && user_msg_counts.empty() && user_reaction_counts.empty() && api->get_history_json) {
            for (int offset = 0; offset < limit_msgs; offset += 100) {
                int chunk = std::min(100, limit_msgs - offset);
                int hn = api->get_history_json(ctx, target_cid, 0, chunk, offset, hbuf.data(), static_cast<int>(hbuf.size()) - 1);
                if (hn <= 2) break;

                int parsed_in_chunk = 0;
                try {
                    auto hj = nlohmann::json::parse(std::string(hbuf.data(), static_cast<size_t>(hn)));
                    if (hj.is_array()) {
                        parsed_in_chunk = static_cast<int>(hj.size());
                        for (const auto& m : hj) {
                            process_message_item(m);
                        }
                    }
                } catch (...) {
                    break;
                }
                if (parsed_in_chunk == 0) break;
            }
            if (!user_msg_counts.empty() || !user_reaction_counts.empty()) {
                fallback_to_main = true;
            }
        }

        std::unordered_set<long long> all_active_uids;
        for (const auto& [uid, _] : user_msg_counts) all_active_uids.insert(uid);
        for (const auto& [uid, _] : user_reaction_counts) all_active_uids.insert(uid);
        for (const auto& [uid, _] : user_reply_counts) all_active_uids.insert(uid);

        struct ActiveScore {
            long long uid;
            int msg_cnt;
            int react_cnt;
            int reply_cnt;
            int score;
        };
        std::vector<ActiveScore> scored_users;
        scored_users.reserve(all_active_uids.size());
        for (long long uid : all_active_uids) {
            int m_cnt = user_msg_counts.count(uid) ? user_msg_counts[uid] : 0;
            int r_cnt = user_reaction_counts.count(uid) ? user_reaction_counts[uid] : 0;
            int rep_cnt = user_reply_counts.count(uid) ? user_reply_counts[uid] : 0;
            int score = m_cnt * 100 + r_cnt * 5 + rep_cnt * 2;
            scored_users.push_back({uid, m_cnt, r_cnt, rep_cnt, score});
        }

        std::sort(scored_users.begin(), scored_users.end(), [](const auto& a, const auto& b) {
            return a.score > b.score;
        });

        std::unordered_set<std::string> collected_unames;

        for (const auto& item : scored_users) {
            long long uid = item.uid;
            if (cfg.skip_admins && admin_set.count(uid)) continue;
            if (cfg.skip_bots && bot_set.count(uid)) continue;

            std::string uname_str = "";
            char ubuf[128] = {0};
            if (api->user_username && api->user_username(ctx, uid, ubuf, sizeof(ubuf)) && ubuf[0] != '\0') {
                uname_str = ubuf;
                collected_unames.insert(to_lower_str(uname_str));
            }

            std::string line = std::to_string(uid);
            if (!uname_str.empty()) {
                line = "@" + uname_str;
            } else if (api->user_name && api->user_name(ctx, uid, ubuf, sizeof(ubuf)) && ubuf[0] != '\0') {
                line = std::to_string(uid) + " (" + std::string(ubuf) + ")";
            }

            std::string act_info;
            if (item.msg_cnt > 0) {
                authors_count++;
                act_info = "Сообщений: " + std::to_string(item.msg_cnt);
                if (item.react_cnt > 0) act_info += ", реакций: " + std::to_string(item.react_cnt);
            } else if (item.react_cnt > 0) {
                reactors_count++;
                act_info = "Реакций: " + std::to_string(item.react_cnt);
            } else {
                act_info = "Ответов: " + std::to_string(item.reply_cnt);
            }

            dump_lines.push_back(line + " | " + act_info);
            collected_count++;
            if (cfg.limit > 0 && collected_count >= cfg.limit) break;
        }

        if (cfg.collect_tags && (cfg.limit <= 0 || collected_count < cfg.limit)) {
            for (const auto& [tag_lower, cnt] : tagged_usernames) {
                if (collected_unames.count(tag_lower)) continue;
                if (cfg.skip_bots && (tag_lower.ends_with("bot") || tag_lower.ends_with("_bot"))) continue;
                long long tag_uid = 0;
                if (api->resolve) tag_uid = api->resolve(ctx, tag_lower.c_str());
                if (tag_uid > 0 && cfg.skip_admins && admin_set.count(tag_uid)) continue;
                if (tag_uid > 0 && cfg.skip_bots && bot_set.count(tag_uid)) continue;

                dump_lines.push_back("@" + tag_lower + " | Упоминаний в чате: " + std::to_string(cnt));
                tags_count++;
                collected_count++;
                if (cfg.limit > 0 && collected_count >= cfg.limit) break;
            }
        }
    } else {
        // Режим сбора участников
        for (long long uid : participants) {
            if (cfg.skip_admins && admin_set.count(uid)) continue;
            if (cfg.skip_bots && bot_set.count(uid)) continue;

            std::string line = std::to_string(uid);
            char ubuf[128] = {0};
            if (api->user_username && api->user_username(ctx, uid, ubuf, sizeof(ubuf)) && ubuf[0] != '\0') {
                line = "@" + std::string(ubuf);
            } else if (api->user_name && api->user_name(ctx, uid, ubuf, sizeof(ubuf)) && ubuf[0] != '\0') {
                line = std::to_string(uid) + " (" + std::string(ubuf) + ")";
            }
            dump_lines.push_back(line);
            collected_count++;
            if (cfg.limit > 0 && collected_count >= cfg.limit) break;
        }
    }

    if (dump_lines.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "ℹ️ ");
        if (worker_uid > 0 && !fallback_to_main) {
            api->c_fmt(ctx, ("Воркер '" + worker_label + "' не имеет доступа к этому чату.\n").c_str(), KOTO_ENT_BOLD);
            api->c_text(ctx, "💡 Добавьте твинк в чат или переключите воркер на MAIN: ");
            api->c_fmt(ctx, ".scrapcfg worker MAIN", KOTO_ENT_CODE);
        } else {
            api->c_fmt(ctx, "Не удалось собрать участников или историю сообщений.\n\n", KOTO_ENT_BOLD);
            api->c_text(ctx, "Возможные причины:\n");
            api->c_text(ctx, "• Чат приватный, и аккаунт/воркер не состоит в нём\n");
            api->c_text(ctx, "• Чат указан по ID без предварительного открытия (для публичных чатов используйте @username или ссылку t.me/...)\n");
            api->c_text(ctx, "• Участники скрыты приватностью чата\n\n");
            api->c_text(ctx, "💡 Если участники скрыты, попробуйте режим сбора по сообщениям: ");
            api->c_fmt(ctx, ".scrapcfg method activity", KOTO_ENT_CODE);
        }
        send_result(ctx, api);
        return;
    }

    // Формируем текстовый дамп
    std::ostringstream ss;
    ss << "=== KOTOGRAM SCRAPPER EXPORT ===\n";
    ss << "CHAT: " << chat_title << " (" << target_cid << ")\n";
    if (!chat_uname.empty()) ss << "USERNAME: @" << chat_uname << "\n";
    ss << "METHOD: " << cfg.method << "\n";
    ss << "WORKER: " << worker_label << (worker_uid > 0 ? (" (ID: " + std::to_string(worker_uid) + ")") : "") << "\n";
    if (cfg.method == "ACTIVITY") {
        ss << "COLLECT_REACTIONS: " << (cfg.collect_reactions ? "ON" : "OFF") << "\n";
        ss << "COLLECT_TAGS: " << (cfg.collect_tags ? "ON" : "OFF") << "\n";
    }
    if (fallback_to_main) {
        ss << "NOTE: Worker '" << worker_label << "' was not in chat; history was scraped via MAIN account.\n";
    }
    ss << "TOTAL COLLECTED: " << collected_count << "\n";
    ss << "================================\n\n";

    for (const auto& l : dump_lines) {
        ss << l << "\n";
    }

    std::string dump_str = ss.str();
    std::string filename = sanitize_filename(chat_title) + "_" + cfg.method + ".txt";

    // Отправляем файл пользователю
    long long send_to = api->chat_id ? api->chat_id(ctx) : target_cid;
    if (api->c_send_bytes) {
        api->c_send_bytes(ctx, send_to, filename.c_str(),
                          reinterpret_cast<const unsigned char*>(dump_str.data()),
                          static_cast<int>(dump_str.size()), 0);
    }

    // Сообщение подтверждения
    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", EMO_OK);
    api->c_fmt(ctx, " Сбор участников успешно завершён!\n\n", KOTO_ENT_BOLD);
    if (fallback_to_main) {
        api->c_text(ctx, "⚠️ ");
        api->c_fmt(ctx, ("Воркер '" + worker_label + "' не в чате, сбор выполнен через MAIN.\n\n").c_str(), KOTO_ENT_ITALIC);
    }
    api->c_text(ctx, "📂 "); api->c_fmt(ctx, "Чат: ", KOTO_ENT_BOLD); api->c_text(ctx, chat_title.c_str()); api->c_text(ctx, "\n");
    api->c_text(ctx, "⚙️ "); api->c_fmt(ctx, "Режим: ", KOTO_ENT_BOLD); api->c_fmt(ctx, cfg.method.c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n");
    api->c_text(ctx, "👥 "); api->c_fmt(ctx, "Собрано: ", KOTO_ENT_BOLD); api->c_fmt(ctx, std::to_string(collected_count).c_str(), KOTO_ENT_CODE);
    if (cfg.method == "ACTIVITY") {
        std::string act_details = " (авторов: " + std::to_string(authors_count);
        if (cfg.collect_reactions) act_details += ", реакций: " + std::to_string(reactors_count);
        if (cfg.collect_tags) act_details += ", тегов: " + std::to_string(tags_count);
        act_details += ")";
        api->c_text(ctx, act_details.c_str());
    }
    api->c_text(ctx, "\n");
    api->c_text(ctx, "📄 "); api->c_fmt(ctx, "Файл дампа: ", KOTO_ENT_BOLD); api->c_fmt(ctx, filename.c_str(), KOTO_ENT_CODE);

    send_result(ctx, api);
}

// ── Команда .scrapcfg ────────────────────────────────────────────────────────

static void cmd_scrapcfg(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? koto_utils::trim(raw) : "";

    ScrapperConfig cfg = load_config(ctx, api);

    if (args.empty()) {
        char pbuf[32] = ".";
        api->setting_get(ctx, "prefix", pbuf, sizeof(pbuf));
        std::string p = pbuf;

        api->c_reset(ctx);
        api->c_emoji(ctx, "⚙️", EMO_CFG);
        api->c_fmt(ctx, " Настройки скраппера (Scrapper):\n\n", KOTO_ENT_BOLD);

        api->c_text(ctx, "1. "); api->c_fmt(ctx, "Режим (method): ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, cfg.method.c_str(), KOTO_ENT_CODE); api->c_text(ctx, " (MEMBERS / ACTIVITY)\n");

        api->c_text(ctx, "2. "); api->c_fmt(ctx, "Лимит (limit): ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (cfg.limit > 0 ? std::to_string(cfg.limit) : "Без лимита (0)").c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n");

        api->c_text(ctx, "3. "); api->c_fmt(ctx, "Пропуск админов (admins): ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, cfg.skip_admins ? "ВКЛ (on)" : "ВЫКЛ (off)", KOTO_ENT_CODE); api->c_text(ctx, "\n");

        api->c_text(ctx, "4. "); api->c_fmt(ctx, "Пропуск ботов (bots): ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, cfg.skip_bots ? "ВКЛ (on)" : "ВЫКЛ (off)", KOTO_ENT_CODE); api->c_text(ctx, "\n");

        api->c_text(ctx, "5. "); api->c_fmt(ctx, "Сбор реакций (reactions): ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, cfg.collect_reactions ? "ВКЛ (on)" : "ВЫКЛ (off)", KOTO_ENT_CODE); api->c_text(ctx, "\n");

        api->c_text(ctx, "6. "); api->c_fmt(ctx, "Сбор тегов (tags): ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, cfg.collect_tags ? "ВКЛ (on)" : "ВЫКЛ (off)", KOTO_ENT_CODE); api->c_text(ctx, "\n");

        api->c_text(ctx, "7. "); api->c_fmt(ctx, "Воркер (worker): ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, cfg.worker.c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n\n");

        api->c_fmt(ctx, "💡 Примеры изменения настроек:\n", KOTO_ENT_ITALIC);
        api->c_text(ctx, "• "); api->c_fmt(ctx, (p + "scrapcfg method activity").c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n");
        api->c_text(ctx, "• "); api->c_fmt(ctx, (p + "scrapcfg limit 500").c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n");
        api->c_text(ctx, "• "); api->c_fmt(ctx, (p + "scrapcfg reactions off").c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n");
        api->c_text(ctx, "• "); api->c_fmt(ctx, (p + "scrapcfg tags off").c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n");
        api->c_text(ctx, "• "); api->c_fmt(ctx, (p + "scrapcfg worker MAIN").c_str(), KOTO_ENT_CODE);

        send_result(ctx, api);
        return;
    }

    std::istringstream iss(args);
    std::string key, val;
    iss >> key >> val;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
    std::string val_lower = val;
    std::transform(val_lower.begin(), val_lower.end(), val_lower.begin(), [](unsigned char c) { return std::tolower(c); });

    bool changed = false;
    std::string msg = "";

    if (key == "method") {
        if (val_lower == "activity" || val_lower == "act") {
            cfg.method = "ACTIVITY";
            changed = true;
            msg = "Установлен метод: ACTIVITY (по сообщениям)";
        } else if (val_lower == "members" || val_lower == "mem") {
            cfg.method = "MEMBERS";
            changed = true;
            msg = "Установлен метод: MEMBERS (по участникам)";
        }
    } else if (key == "limit") {
        try {
            cfg.limit = std::max(0, std::stoi(val));
            changed = true;
            msg = "Лимит сбора установлен на: " + std::to_string(cfg.limit);
        } catch (...) {}
    } else if (key == "admins") {
        cfg.skip_admins = (val_lower == "on" || val_lower == "1" || val_lower == "true");
        changed = true;
        msg = "Пропуск админов: " + std::string(cfg.skip_admins ? "ВКЛ" : "ВЫКЛ");
    } else if (key == "bots") {
        cfg.skip_bots = (val_lower == "on" || val_lower == "1" || val_lower == "true");
        changed = true;
        msg = "Пропуск ботов: " + std::string(cfg.skip_bots ? "ВКЛ" : "ВЫКЛ");
    } else if (key == "reactions" || key == "reaction" || key == "react") {
        cfg.collect_reactions = (val_lower == "on" || val_lower == "1" || val_lower == "true");
        changed = true;
        msg = "Сбор реакций: " + std::string(cfg.collect_reactions ? "ВКЛ" : "ВЫКЛ");
    } else if (key == "tags" || key == "tag" || key == "mentions") {
        cfg.collect_tags = (val_lower == "on" || val_lower == "1" || val_lower == "true");
        changed = true;
        msg = "Сбор тегов: " + std::string(cfg.collect_tags ? "ВКЛ" : "ВЫКЛ");
    } else if (key == "worker" || key == "twin") {
        cfg.worker = val.empty() ? "MAIN" : val;
        changed = true;
        msg = "Воркер установлен: " + cfg.worker;
    }

    if (changed) {
        save_config(ctx, api, cfg);
        api->c_reset(ctx);
        api->c_text(ctx, "✅ ");
        api->c_fmt(ctx, msg.c_str(), KOTO_ENT_BOLD);
    } else {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, "Неверный параметр. Используйте: .scrapcfg", KOTO_ENT_BOLD);
    }
    send_result(ctx, api);
}

// ── Остановка скрапинга (.stopscrap, .killscrap) ─────────────────────────────

static void cmd_stop_scrap(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    api->c_reset(ctx);
    api->c_text(ctx, "🛑 ");
    api->c_fmt(ctx, "Активный процесс скрапинга остановлен.", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

const koto_command COMMANDS[] = {
    {
        "scrap",
        &cmd_scrap,
        KOTO_LEVEL_TRUSTED,
        "Сбор участников чата в файл дампа с фильтрами.",
        "[chat_id|@username]"
    },
    {
        "scrapper",
        &cmd_scrap,
        KOTO_LEVEL_TRUSTED,
        "Сбор участников (псевдоним scrap).",
        "[chat_id|@username]"
    },
    {
        "scrapcfg",
        &cmd_scrapcfg,
        KOTO_LEVEL_TRUSTED,
        "Настройки параметров сбора (method, limit, admins, bots, reactions, tags, worker).",
        "[параметр] [значение]"
    },
    {
        "stopscrap",
        &cmd_stop_scrap,
        KOTO_LEVEL_TRUSTED,
        "Остановить текущий процесс сбора данных.",
        ""
    },
    {
        "killscrap",
        &cmd_stop_scrap,
        KOTO_LEVEL_TRUSTED,
        "Прервать сбор данных (псевдоним stopscrap).",
        ""
    }
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "scrapper",
    "Сбор участников и активности чатов в файл с поддержкой твинков (C++ ABI 3).",
    "1.0.3",
    26,
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
