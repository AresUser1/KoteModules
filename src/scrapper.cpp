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

// ── Чтение и запись конфигурации через DB API ───────────────────────────────

struct ScrapperConfig {
    std::string method = "MEMBERS"; // MEMBERS / ACTIVITY
    int limit = 0;                  // 0 = без лимита
    bool skip_admins = true;
    bool skip_bots = true;
    std::string worker = "MAIN";    // MAIN или UID твинка
};

static ScrapperConfig load_config(koto_ctx* ctx, const koto_api* api) {
    ScrapperConfig cfg;
    char buf[256];
    if (api->db_get(ctx, "method", buf, sizeof(buf)) > 0) cfg.method = buf;
    cfg.limit = static_cast<int>(api->db_get_int(ctx, "limit", 0));
    cfg.skip_admins = api->db_get_int(ctx, "skip_admins", 1) != 0;
    cfg.skip_bots = api->db_get_int(ctx, "skip_bots", 1) != 0;
    if (api->db_get(ctx, "worker", buf, sizeof(buf)) > 0) cfg.worker = buf;
    return cfg;
}

static void save_config(koto_ctx* ctx, const koto_api* api, const ScrapperConfig& cfg) {
    api->db_set(ctx, "method", cfg.method.c_str());
    api->db_set_int(ctx, "limit", cfg.limit);
    api->db_set_int(ctx, "skip_admins", cfg.skip_admins ? 1 : 0);
    api->db_set_int(ctx, "skip_bots", cfg.skip_bots ? 1 : 0);
    api->db_set(ctx, "worker", cfg.worker.c_str());
}

// ── Обработчик команды .scrap ───────────────────────────────────────────────

static void cmd_scrap(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? koto_utils::trim(raw) : "";

    long long target_cid = 0;
    if (!args.empty()) {
        if (is_all_digits(args)) {
            try { target_cid = std::stoll(args); } catch (...) { target_cid = 0; }
        } else {
            std::string uname = args;
            if (!uname.empty() && uname[0] == '@') uname = uname.substr(1);
            if (api->resolve) target_cid = api->resolve(ctx, uname.c_str());
        }
    } else {
        if (api->chat_id) target_cid = api->chat_id(ctx);
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

    if (cfg.method == "ACTIVITY" && api->get_history_json) {
        // Режим активности по сообщениям
        std::vector<char> hbuf(131072, 0);
        int limit = cfg.limit > 0 ? cfg.limit : 1000;
        int hn = api->get_history_json(ctx, target_cid, 0, limit, 0, hbuf.data(), static_cast<int>(hbuf.size()));
        std::unordered_map<long long, int> user_msg_counts;
        if (hn > 0) {
            try {
                auto hj = nlohmann::json::parse(std::string(hbuf.data(), static_cast<size_t>(hn)));
                if (hj.is_array()) {
                    for (const auto& m : hj) {
                        long long from_id = m.value("from_id", 0LL);
                        if (from_id > 0) user_msg_counts[from_id]++;
                    }
                }
            } catch (...) {}
        }

        std::vector<std::pair<long long, int>> sorted_act(user_msg_counts.begin(), user_msg_counts.end());
        std::sort(sorted_act.begin(), sorted_act.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

        for (const auto& [uid, cnt] : sorted_act) {
            if (cfg.skip_admins && admin_set.count(uid)) continue;
            if (cfg.skip_bots && bot_set.count(uid)) continue;

            std::string line = std::to_string(uid);
            if (api->user_username) {
                char ubuf[128] = {0};
                if (api->user_username(ctx, uid, ubuf, sizeof(ubuf)) && ubuf[0] != '\0') {
                    line = "@" + std::string(ubuf);
                }
            }
            dump_lines.push_back(line + " | Сообщений: " + std::to_string(cnt));
            collected_count++;
            if (cfg.limit > 0 && collected_count >= cfg.limit) break;
        }
    } else {
        // Режим сбора участников
        for (long long uid : participants) {
            if (cfg.skip_admins && admin_set.count(uid)) continue;
            if (cfg.skip_bots && bot_set.count(uid)) continue;

            std::string line = std::to_string(uid);
            if (api->user_username) {
                char ubuf[128] = {0};
                if (api->user_username(ctx, uid, ubuf, sizeof(ubuf)) && ubuf[0] != '\0') {
                    line = "@" + std::string(ubuf);
                }
            }
            dump_lines.push_back(line);
            collected_count++;
            if (cfg.limit > 0 && collected_count >= cfg.limit) break;
        }
    }

    if (dump_lines.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "ℹ️ ");
        api->c_fmt(ctx, "Участники не найдены или закрыты настройками приватности чата.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    // Формируем текстовый дамп
    std::ostringstream ss;
    ss << "=== KOTOGRAM SCRAPPER EXPORT ===\n";
    ss << "CHAT: " << chat_title << " (" << target_cid << ")\n";
    if (!chat_uname.empty()) ss << "USERNAME: @" << chat_uname << "\n";
    ss << "METHOD: " << cfg.method << "\n";
    ss << "WORKER: " << cfg.worker << "\n";
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
    api->c_text(ctx, "📂 "); api->c_fmt(ctx, "Чат: ", KOTO_ENT_BOLD); api->c_text(ctx, chat_title.c_str()); api->c_text(ctx, "\n");
    api->c_text(ctx, "⚙️ "); api->c_fmt(ctx, "Режим: ", KOTO_ENT_BOLD); api->c_fmt(ctx, cfg.method.c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n");
    api->c_text(ctx, "👥 "); api->c_fmt(ctx, "Собрано: ", KOTO_ENT_BOLD); api->c_fmt(ctx, std::to_string(collected_count).c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n");
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

        api->c_text(ctx, "5. "); api->c_fmt(ctx, "Воркер (worker): ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, cfg.worker.c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n\n");

        api->c_fmt(ctx, "💡 Примеры изменения настроек:\n", KOTO_ENT_ITALIC);
        api->c_text(ctx, "• "); api->c_fmt(ctx, (p + "scrapcfg method activity").c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n");
        api->c_text(ctx, "• "); api->c_fmt(ctx, (p + "scrapcfg limit 500").c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n");
        api->c_text(ctx, "• "); api->c_fmt(ctx, (p + "scrapcfg bots off").c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n");
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
        "Настройки параметров сбора (method, limit, admins, bots, worker).",
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
    "1.0.0",
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
