#include "module_abi.h"
#include "nlohmann/json.hpp"

#include <string>
#include <vector>
#include <sstream>
#include <ctime>
#include <algorithm>

namespace {
using json = nlohmann::json;

constexpr const char* VERSION = "1.0.0";

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

struct IrisState {
    long long coins = 0;
    int farm_count = 0;
    int next_farm_sec = 0;
    std::string last_farm_time = "Не запускалась";
    std::string last_log = "Нет записей";
    bool auto_farm = false;
};

void save_state(koto_ctx* ctx, const koto_api* api, const IrisState& st) {
    json j;
    j["coins"] = st.coins;
    j["farm_count"] = st.farm_count;
    j["next_farm_sec"] = st.next_farm_sec;
    j["last_farm_time"] = st.last_farm_time;
    j["last_log"] = st.last_log;
    j["auto_farm"] = st.auto_farm;
    api->db_set(ctx, "iris_state", j.dump().c_str());
}

IrisState load_state(koto_ctx* ctx, const koto_api* api) {
    IrisState st;
    std::vector<char> buf(4096, 0);
    int n = api->db_get(ctx, "iris_state", buf.data(), (int)buf.size());
    if (n > 0) {
        try {
            json j = json::parse(std::string(buf.data(), (size_t)n));
            st.coins = j.value("coins", 0LL);
            st.farm_count = j.value("farm_count", 0);
            st.next_farm_sec = j.value("next_farm_sec", 0);
            st.last_farm_time = j.value("last_farm_time", "Не запускалась");
            st.last_log = j.value("last_log", "Нет записей");
            st.auto_farm = j.value("auto_farm", false);
        } catch (...) {}
    }
    return st;
}

// ── Command: .iris / .irf ────────────────────────────────────────────────────
void cmd_iris(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    IrisState st = load_state(ctx, api);

    api->c_reset(ctx);
    api->c_fmt(ctx, "🌿 Iris Assistant (by kote)\n\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "💰 Нафармлено коинов: ");
    api->c_fmt(ctx, std::to_string(st.coins).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n🌾 Всего сборов фермы: ");
    api->c_fmt(ctx, std::to_string(st.farm_count).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n⏰ Последний сбор: ");
    api->c_fmt(ctx, st.last_farm_time.c_str(), KOTO_ENT_ITALIC);
    api->c_text(ctx, "\n⚙️ Авто-ферма: ");
    api->c_fmt(ctx, st.auto_farm ? "Включена ✅" : "Выключена ❌", KOTO_ENT_BOLD);
    api->c_text(ctx, "\n\n📝 Последнее событие:\n");
    api->c_fmt(ctx, st.last_log.c_str(), KOTO_ENT_BLOCKQUOTE);

    if (api->c_button) {
        api->c_button(ctx, "🌾 Собрать ферму", "ir:farm", 1);
        api->c_button(ctx, "🎒 Мешок", "ir:bag", 0);
        api->c_button(ctx, st.auto_farm ? "⏹ Выключить авто" : "▶ Включить авто", "ir:toggle", 1);
    }

    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Command: .irfarm ─────────────────────────────────────────────────────────
void cmd_irfarm(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    api->c_reset(ctx);
    api->c_text(ctx, "Фарма");
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Command: .irbag ──────────────────────────────────────────────────────────
void cmd_irbag(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    api->c_reset(ctx);
    api->c_text(ctx, "Мешок");
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Command: .irlogs ─────────────────────────────────────────────────────────
void cmd_irlogs(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    IrisState st = load_state(ctx, api);

    api->c_reset(ctx);
    api->c_fmt(ctx, "📋 Журнал Iris Helper (by kote)\n\n", KOTO_ENT_BOLD);
    api->c_fmt(ctx, st.last_log.c_str(), KOTO_ENT_CODE);

    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Command: .irreset ────────────────────────────────────────────────────────
void cmd_irreset(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    api->db_del(ctx, "iris_state");
    api->c_reset(ctx);
    api->c_fmt(ctx, "🔄 Состояние и статистика Iris сброшены. (by kote)", KOTO_ENT_BOLD);

    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Callback Handler ────────────────────────────────────────────────────────
void on_callback(koto_ctx* ctx, const koto_api* api) {
    const char* data_raw = api->cb_data ? api->cb_data(ctx) : nullptr;
    if (!data_raw) return;
    std::string data = data_raw;
    if (data.rfind("ir:", 0) != 0) return;

    IrisState st = load_state(ctx, api);

    if (data == "ir:farm") {
        api->cb_answer(ctx, "Отправляю запрос фермы…", 0);
        api->c_reset(ctx);
        api->c_text(ctx, "Фарма");
        api->c_send(ctx, api->chat_id(ctx), 0);
        return;
    }

    if (data == "ir:bag") {
        api->cb_answer(ctx, "Запрашиваю мешок…", 0);
        api->c_reset(ctx);
        api->c_text(ctx, "Мешок");
        api->c_send(ctx, api->chat_id(ctx), 0);
        return;
    }

    if (data == "ir:toggle") {
        st.auto_farm = !st.auto_farm;
        save_state(ctx, api, st);
        api->cb_answer(ctx, st.auto_farm ? "Авто-ферма включена" : "Авто-ферма выключена", 0);
        cmd_iris(ctx, api);
        return;
    }
}

// ── Watcher: Track Iris messages ────────────────────────────────────────────
int on_watch(koto_ctx* ctx, const koto_api* api) {
    const char* text_raw = api->msg_text(ctx);
    if (!text_raw) return 0;
    std::string text = text_raw;

    // Detect Iris farm replies
    if (text.find("Ферма") != std::string::npos || text.find("коинов") != std::string::npos ||
        text.find("мешок") != std::string::npos || text.find("Ирис") != std::string::npos) {

        IrisState st = load_state(ctx, api);

        std::time_t t = std::time(nullptr);
        char t_buf[32] = {0};
        std::strftime(t_buf, sizeof(t_buf), "%H:%M:%S", std::localtime(&t));

        st.last_log = "[" + std::string(t_buf) + "] " + (text.size() > 120 ? text.substr(0, 117) + "…" : text);

        if (text.find("Ферма") != std::string::npos || text.find("собрана") != std::string::npos) {
            st.farm_count++;
            st.last_farm_time = t_buf;
        }

        save_state(ctx, api, st);
    }

    return 0;
}

const koto_command COMMANDS[] = {
    {"iris", cmd_iris, KOTO_LEVEL_TRUSTED, "[ru] Меню помощника Iris бота (by kote) [en] Iris bot assistant menu (by kote).", nullptr},
    {"irf", cmd_iris, KOTO_LEVEL_TRUSTED, "[ru] Алиас для .iris (by kote) [en] Alias for .iris (by kote).", nullptr},
    {"irfarm", cmd_irfarm, KOTO_LEVEL_TRUSTED, "[ru] Собрать ферму Iris [en] Harvest Iris farm.", nullptr},
    {"irbag", cmd_irbag, KOTO_LEVEL_TRUSTED, "[ru] Посмотреть мешок Iris [en] View Iris inventory.", nullptr},
    {"irlogs", cmd_irlogs, KOTO_LEVEL_TRUSTED, "[ru] Логи событий Iris бота [en] View Iris bot logs.", nullptr},
    {"irreset", cmd_irreset, KOTO_LEVEL_TRUSTED, "[ru] Сбросить статистику Iris [en] Reset Iris statistics.", nullptr}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "iris",
    "[ru] Помощник и авто-ферма для бота Iris (by kote). [en] Iris bot farm assistant (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    on_watch,
    KOTO_WATCH_INCOMING,
    on_callback,
    nullptr,
    0,
    "https://github.com/AresUser1/KoteModules/blob/main/src/iris.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
