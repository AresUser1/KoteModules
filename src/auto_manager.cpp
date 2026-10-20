// auto_manager.cpp — Модуль автоматизации (авточтение, автоодобрение заявок, твинки) (C++ ABI 3).
// Портирован из Python-модуля auto_manager.py (v1.1.2).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <set>
#include <sstream>

namespace {

static constexpr long long EMO_SUCCESS = 5774022692642492953LL; // ✅
static constexpr long long EMO_ERROR   = 5985346521103604145LL; // ❌
static constexpr long long EMO_LIST    = 5956561916573782596LL; // 📋

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

// ── Хранение списков чатов в БД модуля ────────────────────────────────────────

static std::set<long long> load_chat_set(const koto_api* api, koto_ctx* ctx, const char* key) {
    std::set<long long> s;
    char buf[16384] = {0};
    int n = api->db_get(ctx, key, buf, sizeof(buf) - 1);
    if (n <= 0) return s;
    buf[n] = '\0';

    std::istringstream iss(buf);
    std::string item;
    while (std::getline(iss, item, '\n')) {
        std::string trimmed = koto_utils::trim(item);
        if (trimmed.empty()) continue;
        long long id = strtoll(trimmed.c_str(), nullptr, 10);
        s.insert(id);
    }
    return s;
}

static void save_chat_set(const koto_api* api, koto_ctx* ctx, const char* key, const std::set<long long>& s) {
    std::string out;
    for (long long id : s) {
        out += std::to_string(id) + "\n";
    }
    api->db_set(ctx, key, out.c_str());
}

// ══════════════════════════════════════════════════════════════════════════════
// АВТОЧТЕНИЕ
// ══════════════════════════════════════════════════════════════════════════════

static void cmd_autread(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? koto_utils::trim(raw) : "";

    std::vector<std::string> parts = koto_utils::split(args, ' ');
    if (parts.empty() || (parts[0] != "on" && parts[0] != "off")) {
        char pbuf[32] = ".";
        api->setting_get(ctx, "prefix", pbuf, sizeof(pbuf));
        std::string p = pbuf;

        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMO_ERROR);
        api->c_fmt(ctx, " Использование: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (p + "autread on [this] или " + p + "autread off [this]").c_str(), KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

    bool on = (parts[0] == "on");
    bool this_chat = (parts.size() >= 2 && parts[1] == "this");

    long long target_id = this_chat ? api->chat_id(ctx) : 0;
    std::string chat_desc = this_chat ? "текущем чате" : "всех чатах";

    std::set<long long> chats = load_chat_set(api, ctx, "autoread_chats");
    if (on) {
        chats.insert(target_id);
    } else {
        chats.erase(target_id);
    }
    save_chat_set(api, ctx, "autoread_chats", chats);

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", EMO_SUCCESS);
    api->c_fmt(ctx, " Авточтение ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, on ? "включено" : "выключено", KOTO_ENT_CODE);
    api->c_fmt(ctx, (" в " + chat_desc + ".").c_str(), KOTO_ENT_BOLD);
    send_result(ctx, api);
}

static void cmd_autreadlist(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::set<long long> chats = load_chat_set(api, ctx, "autoread_chats");

    api->c_reset(ctx);
    api->c_emoji(ctx, "📋", EMO_LIST);
    api->c_fmt(ctx, " Чаты с включённым авточтением:\n\n", KOTO_ENT_BOLD);

    if (chats.empty()) {
        api->c_text(ctx, "Список авточтения пуст.");
    } else {
        for (long long cid : chats) {
            api->c_text(ctx, "• ");
            if (cid == 0) {
                api->c_fmt(ctx, "Все чаты\n", KOTO_ENT_BOLD);
            } else {
                api->c_fmt(ctx, (std::to_string(cid) + "\n").c_str(), KOTO_ENT_CODE);
            }
        }
    }
    send_result(ctx, api);
}

static void cmd_autreadnow(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    long long mid = api->msg_id ? api->msg_id(ctx) : 0;

    api->c_reset(ctx);
    api->c_text(ctx, "⏳ Читаю историю и упоминания...");
    send_result(ctx, api);

    if (api->read_history && cid != 0) {
        api->read_history(ctx, cid, mid);
    }
    if (api->read_mentions && cid != 0) {
        api->read_mentions(ctx, cid);
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", EMO_SUCCESS);
    api->c_fmt(ctx, " История и упоминания прочитаны!", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// ══════════════════════════════════════════════════════════════════════════════
// АВТООДОБРЕНИЕ
// ══════════════════════════════════════════════════════════════════════════════

static void cmd_autoapprove(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? koto_utils::trim(raw) : "";

    std::vector<std::string> parts = koto_utils::split(args, ' ');
    if (parts.empty() || (parts[0] != "on" && parts[0] != "off")) {
        char pbuf[32] = ".";
        api->setting_get(ctx, "prefix", pbuf, sizeof(pbuf));
        std::string p = pbuf;

        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMO_ERROR);
        api->c_fmt(ctx, " Использование: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (p + "autoapprove on [this] или " + p + "autoapprove off [this]").c_str(), KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

    bool on = (parts[0] == "on");
    bool this_chat = (parts.size() >= 2 && parts[1] == "this");

    long long target_id = this_chat ? api->chat_id(ctx) : 0;
    std::string chat_desc = this_chat ? "текущем чате" : "всех чатах";

    std::set<long long> chats = load_chat_set(api, ctx, "autoapprove_chats");
    if (on) {
        chats.insert(target_id);
    } else {
        chats.erase(target_id);
    }
    save_chat_set(api, ctx, "autoapprove_chats", chats);

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", EMO_SUCCESS);
    api->c_fmt(ctx, " Автоодобрение заявок ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, on ? "включено" : "выключено", KOTO_ENT_CODE);
    api->c_fmt(ctx, (" в " + chat_desc + ".").c_str(), KOTO_ENT_BOLD);
    send_result(ctx, api);
}

static void cmd_autoapprovelist(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::set<long long> chats = load_chat_set(api, ctx, "autoapprove_chats");

    api->c_reset(ctx);
    api->c_emoji(ctx, "📋", EMO_LIST);
    api->c_fmt(ctx, " Чаты с автоодобрением заявок:\n\n", KOTO_ENT_BOLD);

    if (chats.empty()) {
        api->c_text(ctx, "Список автоодобрения пуст.");
    } else {
        for (long long cid : chats) {
            api->c_text(ctx, "• ");
            if (cid == 0) {
                api->c_fmt(ctx, "Все чаты (где есть права)\n", KOTO_ENT_BOLD);
            } else {
                api->c_fmt(ctx, (std::to_string(cid) + "\n").c_str(), KOTO_ENT_CODE);
            }
        }
    }
    send_result(ctx, api);
}

// ══════════════════════════════════════════════════════════════════════════════
// УПРАВЛЕНИЕ ТВИНКАМИ (.amtwins)
// ══════════════════════════════════════════════════════════════════════════════

struct TwinItem {
    long long uid = 0;
    std::string name;
};

static std::vector<TwinItem> get_all_twins(koto_ctx* ctx, const koto_api* api) {
    std::vector<TwinItem> twins;

    // 1. Попытка через twin_list ("uid\tname\tphone\n...")
    if (api->twin_list) {
        char tw_buf[4096] = {0};
        int tw_len = api->twin_list(ctx, tw_buf, sizeof(tw_buf) - 1);
        if (tw_len > 0) {
            std::istringstream iss(tw_buf);
            std::string line;
            while (std::getline(iss, line)) {
                std::vector<std::string> parts = koto_utils::split(line, '\t');
                if (parts.empty()) continue;
                long long tw_id = strtoll(parts[0].c_str(), nullptr, 10);
                if (tw_id <= 0) continue;
                std::string tw_name = (parts.size() > 1 && !parts[1].empty()) ? parts[1] : ("UID " + parts[0]);
                twins.push_back({tw_id, tw_name});
            }
        }
    }

    // 2. Fallback через get_twins_json (если twin_list пуст)
    if (twins.empty() && api->get_twins_json) {
        char json_buf[16384] = {0};
        int jlen = api->get_twins_json(ctx, json_buf, sizeof(json_buf) - 1);
        if (jlen > 0) {
            try {
                auto j = nlohmann::json::parse(json_buf);
                if (j.is_array()) {
                    for (const auto& item : j) {
                        bool is_main = item.value("is_main", false);
                        if (is_main) continue;
                        long long uid = item.value("uid", 0LL);
                        std::string name = item.value("name", "");
                        if (uid > 0) {
                            if (name.empty()) name = "UID " + std::to_string(uid);
                            twins.push_back({uid, name});
                        }
                    }
                }
            } catch (...) {}
        }
    }

    return twins;
}

static void render_amtwins_panel(koto_ctx* ctx, const koto_api* api) {
    api->c_reset(ctx);
    api->c_fmt(ctx, "⚙️ AutoManager — Твинки (авточтение)\n\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "• ❌ Выкл — твин не читает сообщения\n");
    api->c_text(ctx, "• ✅ Вкл  — твин читает входящие чаты\n\n");

    std::vector<TwinItem> twins = get_all_twins(ctx, api);

    if (twins.empty()) {
        api->c_text(ctx, "Нет добавленных твинков.");
        api->c_button(ctx, "❌ Закрыть", "am_close", 1);
        return;
    }

    for (const auto& tw : twins) {
        std::string mode_key = "twin_mode:" + std::to_string(tw.uid);
        long long mode = koto_utils::db_get_int64(api, ctx, mode_key.c_str(), 0);

        std::string btn_label = "👤 " + tw.name + " [" + (mode ? "✅ Вкл" : "❌ Выкл") + "]";
        api->c_button(ctx, btn_label.c_str(), "am_noop", 1);

        std::string toggle_cb = "amtwins_toggle:" + std::to_string(tw.uid);
        api->c_button(ctx, mode ? "Отключить ❌" : "Включить ✅", toggle_cb.c_str(), 1);
    }

    api->c_button(ctx, "❌ Закрыть", "am_close", 1);
}

static void cmd_amtwins(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    render_amtwins_panel(ctx, api);
    send_result(ctx, api);
}

// ── Watcher: авточтение сообщений ─────────────────────────────────────────────

static int on_watch(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) return 0;

    long long cid = api->chat_id(ctx);
    if (cid == 0) return 0;

    long long my_id = api->get_me ? api->get_me(ctx) : 0;

    static std::set<long long> s_twin_uids;
    static long long s_last_check_ts = 0;
    long long now_sec = api->uptime_sec ? api->uptime_sec(ctx) : 0;
    if (s_twin_uids.empty() || (now_sec - s_last_check_ts > 15)) {
        s_last_check_ts = now_sec;
        auto tw_list = get_all_twins(ctx, api);
        s_twin_uids.clear();
        for (const auto& tw : tw_list) {
            s_twin_uids.insert(tw.uid);
        }
    }

    // Проверяем: выполняется ли watcher на твинке
    if (my_id > 0 && s_twin_uids.find(my_id) != s_twin_uids.end()) {
        std::string mode_key = "twin_mode:" + std::to_string(my_id);
        long long mode = koto_utils::db_get_int64(api, ctx, mode_key.c_str(), 0);
        if (mode == 1) {
            long long mid = api->msg_id ? api->msg_id(ctx) : 0;
            if (api->read_history) {
                api->read_history(ctx, cid, mid);
            }
            if (api->read_mentions) {
                api->read_mentions(ctx, cid);
            }
        }
        return 0;
    }

    // Основной аккаунт: проверяем список autoread_chats
    std::set<long long> autoread_chats = load_chat_set(api, ctx, "autoread_chats");
    if (autoread_chats.find(0) != autoread_chats.end() || autoread_chats.find(cid) != autoread_chats.end()) {
        long long mid = api->msg_id ? api->msg_id(ctx) : 0;
        if (api->read_history) {
            api->read_history(ctx, cid, mid);
        }
        if (api->read_mentions) {
            api->read_mentions(ctx, cid);
        }
    }

    return 0;
}

// ── Коллбэки инлайн кнопок ───────────────────────────────────────────────────

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) {
        if (api->cb_answer) api->cb_answer(ctx, "❌ Нет доступа", 1);
        return;
    }

    const char* data = api->cb_data(ctx);
    if (!data) return;
    std::string d = data;

    if (d == "am_noop") {
        if (api->cb_answer) api->cb_answer(ctx, "", 0);
        return;
    }

    if (d == "am_close") {
        if (api->cb_answer) api->cb_answer(ctx, "Закрыто", 0);
        api->delete_msg(ctx);
        return;
    }

    if (d.rfind("amtwins_toggle:", 0) == 0) {
        long long tw_id = strtoll(d.c_str() + 15, nullptr, 10);
        if (tw_id > 0) {
            std::string mode_key = "twin_mode:" + std::to_string(tw_id);
            long long current = koto_utils::db_get_int64(api, ctx, mode_key.c_str(), 0);
            koto_utils::db_set_int64(api, ctx, mode_key.c_str(), current ? 0 : 1);
            if (api->cb_answer) api->cb_answer(ctx, current ? "Твинк выключен ❌" : "Твинк включен ✅", 0);
            render_amtwins_panel(ctx, api);
            api->c_edit(ctx, 0);
        }
        return;
    }
}

const koto_command COMMANDS[] = {
    {
        "autread",
        &cmd_autread,
        KOTO_LEVEL_TRUSTED,
        "Управление авточтением чатов.",
        "<on|off> [this]"
    },
    {
        "autreadlist",
        &cmd_autreadlist,
        KOTO_LEVEL_TRUSTED,
        "Список чатов с авточтением.",
        nullptr
    },
    {
        "autreadnow",
        &cmd_autreadnow,
        KOTO_LEVEL_TRUSTED,
        "Прочитать текущий чат и очистить непрочитанные упоминания прямо сейчас.",
        nullptr
    },
    {
        "autoapprove",
        &cmd_autoapprove,
        KOTO_LEVEL_TRUSTED,
        "Управление автоодобрением заявок на вступление.",
        "<on|off> [this]"
    },
    {
        "autoapprovelist",
        &cmd_autoapprovelist,
        KOTO_LEVEL_TRUSTED,
        "Список чатов с автоодобрением заявок.",
        nullptr
    },
    {
        "amtwins",
        &cmd_amtwins,
        KOTO_LEVEL_TRUSTED,
        "Инлайн-панель управления авточтением твинков.",
        nullptr
    }
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "auto_manager",
    "Автоматизация: авточтение сообщений, автоодобрение заявок и поддержка твинков (C++ ABI 3).",
    "1.1.3",
    40, // read_history и read_mentions появились в versionCode 40+
    KOTO_MOD_TWINS, // Явная поддержка работы на твинках
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    &on_watch,
    KOTO_WATCH_INCOMING,
    &on_callback,
    nullptr, 0,
    nullptr
};

} // namespace

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
