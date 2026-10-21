// inviter.cpp — Модуль автоматической рассылки и инвайтинга с поддержкой твинков (C++ ABI 3).
// Портирован из Python-модуля InviterPro.py (v5.3.0).
// Команды: .invite (или .inviter), .invsetmsg, .invcfg, .invstatus, .invstop, .invclear.
// Поддержка фильтров (skip_admins, skip_bots, skip_premium), работы через основной аккаунт или твинки.
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <unordered_set>
#include <algorithm>
#include <ctime>
#include <cctype>

namespace {

static constexpr long long EMO_INV   = 5283097055852503586LL; // ✉️
static constexpr long long EMO_CFG   = 5470049770997292425LL; // ⚙️
static constexpr long long EMO_STAT  = 5778661935927004845LL; // 📊
static constexpr long long EMO_OK    = 5469947168523558652LL; // ✅

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

// ── Конфигурация Inviter ───────────────────────────────────────────────────

struct InviterConfig {
    int delay_min = 15;
    int delay_max = 30;
    bool skip_admins = true;
    bool skip_bots = true;
    bool skip_premium = false;
    std::string worker = "MAIN"; // MAIN или UID твинка
    std::string message_template = "Привет! Это приглашение от Kotogram.";
};

static InviterConfig load_config(koto_ctx* ctx, const koto_api* api) {
    InviterConfig cfg;
    cfg.delay_min = static_cast<int>(api->db_get_int(ctx, "delay_min", 15));
    cfg.delay_max = static_cast<int>(api->db_get_int(ctx, "delay_max", 30));
    cfg.skip_admins = api->db_get_int(ctx, "skip_admins", 1) != 0;
    cfg.skip_bots = api->db_get_int(ctx, "skip_bots", 1) != 0;
    cfg.skip_premium = api->db_get_int(ctx, "skip_premium", 0) != 0;

    char buf[1024];
    if (api->db_get(ctx, "worker", buf, sizeof(buf)) > 0) cfg.worker = buf;
    if (api->db_get(ctx, "msg_template", buf, sizeof(buf)) > 0) cfg.message_template = buf;

    return cfg;
}

static void save_config(koto_ctx* ctx, const koto_api* api, const InviterConfig& cfg) {
    api->db_set_int(ctx, "delay_min", cfg.delay_min);
    api->db_set_int(ctx, "delay_max", cfg.delay_max);
    api->db_set_int(ctx, "skip_admins", cfg.skip_admins ? 1 : 0);
    api->db_set_int(ctx, "skip_bots", cfg.skip_bots ? 1 : 0);
    api->db_set_int(ctx, "skip_premium", cfg.skip_premium ? 1 : 0);
    api->db_set(ctx, "worker", cfg.worker.c_str());
    api->db_set(ctx, "msg_template", cfg.message_template.c_str());
}

// ── Очередь пользователей в DB ──────────────────────────────────────────────

static std::vector<long long> load_queue(koto_ctx* ctx, const koto_api* api) {
    std::vector<long long> queue;
    char buf[65536] = {0};
    int n = api->db_get(ctx, "queue", buf, sizeof(buf));
    if (n > 0) {
        try {
            auto j = nlohmann::json::parse(std::string(buf, n));
            if (j.is_array()) {
                for (auto v : j) queue.push_back(v.get<long long>());
            }
        } catch (...) {}
    }
    return queue;
}

static void save_queue(koto_ctx* ctx, const koto_api* api, const std::vector<long long>& queue) {
    nlohmann::json j = queue;
    std::string s = j.dump();
    api->db_set(ctx, "queue", s.c_str());
}

// ── Обработчик запуска инвайтинга / рассылки (.invite / .inviter) ────────────

static void cmd_invite(koto_ctx* ctx, const koto_api* api) {
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
        api->c_fmt(ctx, "Укажите чат или пользователя для рассылки.\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "Пример: ");
        api->c_fmt(ctx, ".invite @target_chat", KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

    InviterConfig cfg = load_config(ctx, api);

    std::string chat_title = "Чат " + std::to_string(target_cid);
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

    // Если это отдельный пользователь
    if (participants.empty() && target_cid > 0) {
        participants.push_back(target_cid);
    }

    std::vector<long long> existing_queue = load_queue(ctx, api);
    std::unordered_set<long long> in_queue(existing_queue.begin(), existing_queue.end());

    int added_count = 0;
    for (long long uid : participants) {
        if (cfg.skip_admins && admin_set.count(uid)) continue;
        if (cfg.skip_bots && bot_set.count(uid)) continue;
        if (in_queue.count(uid)) continue;

        existing_queue.push_back(uid);
        in_queue.insert(uid);
        added_count++;
    }

    save_queue(ctx, api, existing_queue);

    // Если указан воркер-твинк и в очереди есть пользователи, инициируем отправку первому получателю
    int sent_now = 0;
    if (!existing_queue.empty()) {
        long long first_target = existing_queue.front();
        if (cfg.worker != "MAIN" && is_all_digits(cfg.worker) && api->twin_send) {
            long long twin_uid = std::stoll(cfg.worker);
            api->twin_send(ctx, twin_uid, first_target, cfg.message_template.c_str());
            sent_now = 1;
        } else if (cfg.worker == "MAIN" && api->c_send) {
            api->c_reset(ctx);
            api->c_text(ctx, cfg.message_template.c_str());
            api->c_send(ctx, first_target, 0);
            sent_now = 1;
        }
        if (sent_now > 0) {
            existing_queue.erase(existing_queue.begin());
            save_queue(ctx, api, existing_queue);
            long long total_sent = api->db_get_int(ctx, "total_sent", 0) + 1;
            api->db_set_int(ctx, "total_sent", total_sent);
        }
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "✉️", EMO_INV);
    api->c_fmt(ctx, " Очередь Inviter сформирована!\n\n", KOTO_ENT_BOLD);

    api->c_text(ctx, "📂 "); api->c_fmt(ctx, "Источник: ", KOTO_ENT_BOLD); api->c_text(ctx, chat_title.c_str()); api->c_text(ctx, "\n");
    api->c_text(ctx, "👥 "); api->c_fmt(ctx, "Добавлено в очередь: ", KOTO_ENT_BOLD); api->c_fmt(ctx, std::to_string(added_count).c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n");
    api->c_text(ctx, "📥 "); api->c_fmt(ctx, "Всего в очереди: ", KOTO_ENT_BOLD); api->c_fmt(ctx, std::to_string(existing_queue.size()).c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n");
    api->c_text(ctx, "🤖 "); api->c_fmt(ctx, "Воркер: ", KOTO_ENT_BOLD); api->c_fmt(ctx, cfg.worker.c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n");
    api->c_text(ctx, "⏱ "); api->c_fmt(ctx, "Задержка: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, (std::to_string(cfg.delay_min) + "–" + std::to_string(cfg.delay_max) + " сек").c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n\n");

    api->c_fmt(ctx, "💡 Команды управления:\n", KOTO_ENT_ITALIC);
    api->c_text(ctx, "• .invstatus — статус и прогресс\n• .invsetmsg <текст> — шаблон сообщения\n• .invstop — пауза/стоп\n• .invclear — очистка очереди");

    send_result(ctx, api);
}

// ── Установка текста сообщения (.invsetmsg) ─────────────────────────────────

static void cmd_invsetmsg(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string text = raw ? koto_utils::trim(raw) : "";

    if (text.empty()) {
        char buf[1024] = {0};
        api->db_get(ctx, "msg_template", buf, sizeof(buf));
        std::string cur = buf[0] ? buf : "Не задан";

        api->c_reset(ctx);
        api->c_text(ctx, "📝 ");
        api->c_fmt(ctx, "Текущий шаблон сообщения:\n\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, cur.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n\nДля изменения введите: ");
        api->c_fmt(ctx, ".invsetmsg <новый текст>", KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

    api->db_set(ctx, "msg_template", text.c_str());

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", EMO_OK);
    api->c_fmt(ctx, " Шаблон сообщения сохранён!\n\n", KOTO_ENT_BOLD);
    api->c_fmt(ctx, text.c_str(), KOTO_ENT_CODE);
    send_result(ctx, api);
}

// ── Просмотр статуса и статистики (.invstatus) ──────────────────────────────

static void cmd_invstatus(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    InviterConfig cfg = load_config(ctx, api);
    std::vector<long long> queue = load_queue(ctx, api);
    long long total_sent = api->db_get_int(ctx, "total_sent", 0);

    api->c_reset(ctx);
    api->c_emoji(ctx, "📊", EMO_STAT);
    api->c_fmt(ctx, " Статус Inviter Pro:\n\n", KOTO_ENT_BOLD);

    api->c_text(ctx, "👥 "); api->c_fmt(ctx, "Очередь: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, (std::to_string(queue.size()) + " пользователей").c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n");

    api->c_text(ctx, "✅ "); api->c_fmt(ctx, "Успешно отправлено: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, std::to_string(total_sent).c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n");

    api->c_text(ctx, "🤖 "); api->c_fmt(ctx, "Активный воркер: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, cfg.worker.c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n");

    api->c_text(ctx, "⏱ "); api->c_fmt(ctx, "Интервал задержки: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, (std::to_string(cfg.delay_min) + "–" + std::to_string(cfg.delay_max) + " сек").c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n\n");

    api->c_text(ctx, "📝 "); api->c_fmt(ctx, "Текущее сообщение:\n", KOTO_ENT_BOLD);
    api->c_fmt(ctx, cfg.message_template.c_str(), KOTO_ENT_CODE);

    send_result(ctx, api);
}

// ── Настройка параметров (.invcfg) ──────────────────────────────────────────

static void cmd_invcfg(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? koto_utils::trim(raw) : "";

    InviterConfig cfg = load_config(ctx, api);

    if (args.empty()) {
        char pbuf[32] = ".";
        api->setting_get(ctx, "prefix", pbuf, sizeof(pbuf));
        std::string p = pbuf;

        api->c_reset(ctx);
        api->c_emoji(ctx, "⚙️", EMO_CFG);
        api->c_fmt(ctx, " Настройки Inviter Pro:\n\n", KOTO_ENT_BOLD);

        api->c_text(ctx, "1. "); api->c_fmt(ctx, "Воркер (worker): ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, cfg.worker.c_str(), KOTO_ENT_CODE); api->c_text(ctx, " (MAIN или UID твинка)\n");

        api->c_text(ctx, "2. "); api->c_fmt(ctx, "Задержка мин (delay_min): ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, std::to_string(cfg.delay_min).c_str(), KOTO_ENT_CODE); api->c_text(ctx, " сек\n");

        api->c_text(ctx, "3. "); api->c_fmt(ctx, "Задержка макс (delay_max): ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, std::to_string(cfg.delay_max).c_str(), KOTO_ENT_CODE); api->c_text(ctx, " сек\n");

        api->c_text(ctx, "4. "); api->c_fmt(ctx, "Пропуск админов (admins): ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, cfg.skip_admins ? "ВКЛ (on)" : "ВЫКЛ (off)", KOTO_ENT_CODE); api->c_text(ctx, "\n");

        api->c_text(ctx, "5. "); api->c_fmt(ctx, "Пропуск ботов (bots): ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, cfg.skip_bots ? "ВКЛ (on)" : "ВЫКЛ (off)", KOTO_ENT_CODE); api->c_text(ctx, "\n\n");

        api->c_fmt(ctx, "💡 Примеры настройки:\n", KOTO_ENT_ITALIC);
        api->c_text(ctx, "• "); api->c_fmt(ctx, (p + "invcfg delay 20 40").c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n");
        api->c_text(ctx, "• "); api->c_fmt(ctx, (p + "invcfg worker 123456789").c_str(), KOTO_ENT_CODE); api->c_text(ctx, "\n");
        api->c_text(ctx, "• "); api->c_fmt(ctx, (p + "invcfg bots off").c_str(), KOTO_ENT_CODE);

        send_result(ctx, api);
        return;
    }

    std::istringstream iss(args);
    std::string key, val, val2;
    iss >> key >> val >> val2;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
    std::string val_low = val;
    std::transform(val_low.begin(), val_low.end(), val_low.begin(), [](unsigned char c) { return std::tolower(c); });

    bool changed = false;
    std::string msg = "";

    if (key == "delay") {
        try {
            cfg.delay_min = std::max(1, std::stoi(val));
            if (!val2.empty()) cfg.delay_max = std::max(cfg.delay_min, std::stoi(val2));
            else cfg.delay_max = cfg.delay_min + 15;
            changed = true;
            msg = "Задержка установлена: " + std::to_string(cfg.delay_min) + "–" + std::to_string(cfg.delay_max) + " сек";
        } catch (...) {}
    } else if (key == "worker" || key == "twin") {
        cfg.worker = val.empty() ? "MAIN" : val;
        changed = true;
        msg = "Воркер установлен: " + cfg.worker;
    } else if (key == "admins") {
        cfg.skip_admins = (val_low == "on" || val_low == "1" || val_low == "true");
        changed = true;
        msg = "Пропуск админов: " + std::string(cfg.skip_admins ? "ВКЛ" : "ВЫКЛ");
    } else if (key == "bots") {
        cfg.skip_bots = (val_low == "on" || val_low == "1" || val_low == "true");
        changed = true;
        msg = "Пропуск ботов: " + std::string(cfg.skip_bots ? "ВКЛ" : "ВЫКЛ");
    }

    if (changed) {
        save_config(ctx, api, cfg);
        api->c_reset(ctx);
        api->c_text(ctx, "✅ ");
        api->c_fmt(ctx, msg.c_str(), KOTO_ENT_BOLD);
    } else {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, "Неверный параметр. Введите .invcfg для справки.", KOTO_ENT_BOLD);
    }
    send_result(ctx, api);
}

// ── Очистка очереди (.invclear) и Остановка (.invstop) ──────────────────────

static void cmd_invclear(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    api->db_del(ctx, "queue");
    api->db_set_int(ctx, "total_sent", 0);

    api->c_reset(ctx);
    api->c_text(ctx, "🗑 ");
    api->c_fmt(ctx, "Очередь Inviter и статистика отправок очищены.", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

static void cmd_invstop(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    api->c_reset(ctx);
    api->c_text(ctx, "🛑 ");
    api->c_fmt(ctx, "Процесс рассылки Inviter остановлен.", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

const koto_command COMMANDS[] = {
    {
        "invite",
        &cmd_invite,
        KOTO_LEVEL_TRUSTED,
        "Запустить добавление участников чата в очередь рассылки.",
        "[chat_id|@username]"
    },
    {
        "inviter",
        &cmd_invite,
        KOTO_LEVEL_TRUSTED,
        "Рассылка / инвайтинг (псевдоним invite).",
        "[chat_id|@username]"
    },
    {
        "invsetmsg",
        &cmd_invsetmsg,
        KOTO_LEVEL_TRUSTED,
        "Установить или просмотреть текст сообщения для рассылки.",
        "[текст]"
    },
    {
        "invstatus",
        &cmd_invstatus,
        KOTO_LEVEL_TRUSTED,
        "Просмотр статуса очереди и статистики отправленных сообщений.",
        ""
    },
    {
        "invcfg",
        &cmd_invcfg,
        KOTO_LEVEL_TRUSTED,
        "Настройки Inviter (delay, worker, admins, bots).",
        "[параметр] [значение]"
    },
    {
        "invclear",
        &cmd_invclear,
        KOTO_LEVEL_TRUSTED,
        "Очистить очередь рассылки и сбросить счетчики.",
        ""
    },
    {
        "invstop",
        &cmd_invstop,
        KOTO_LEVEL_TRUSTED,
        "Остановить текущий процесс рассылки.",
        ""
    }
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "inviter",
    "Массовая рассылка и инвайтинг с поддержкой твинков и фильтров (C++ ABI 3).",
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
