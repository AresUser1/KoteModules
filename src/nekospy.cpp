// nekospy.cpp — Модуль слежки за удалёнными и изменёнными сообщениями (C++ ABI 3).
// Портирован из Python-модуля NekoSpy (by Dan Gazizullin, ported by kote).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"

#include <string>
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <mutex>
#include <ctime>
#include <sstream>
#include <cstring>
#include <cstdlib>

namespace {
using json = nlohmann::json;

constexpr const char* VERSION = "1.0.0";

struct CachedMsg {
    long long msg_id = 0;
    long long sender_id = 0;
    long long chat_id = 0;
    std::string text;
    time_t timestamp = 0;
};

static std::mutex g_spy_mu;
static std::deque<CachedMsg> g_msg_cache;
static constexpr size_t MAX_CACHE_SIZE = 1000;

static void send_res(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

static bool is_spy_enabled(koto_ctx* ctx, const koto_api* api) {
    char buf[16] = {0};
    if (api->db_get && api->db_get(ctx, "nekospy_enabled", buf, sizeof(buf) - 1) > 0) {
        return strcmp(buf, "1") == 0;
    }
    return true; // Включен по умолчанию
}

static void set_spy_enabled(koto_ctx* ctx, const koto_api* api, bool en) {
    if (api->db_set) {
        api->db_set(ctx, "nekospy_enabled", en ? "1" : "0");
    }
}

static std::set<long long> get_id_set(koto_ctx* ctx, const koto_api* api, const char* key) {
    std::set<long long> res;
    char buf[4096] = {0};
    if (api->db_get && api->db_get(ctx, key, buf, sizeof(buf) - 1) > 0) {
        std::istringstream iss(buf);
        long long val;
        while (iss >> val) res.insert(val);
    }
    return res;
}

static void save_id_set(koto_ctx* ctx, const koto_api* api, const char* key, const std::set<long long>& s) {
    std::string out;
    for (auto id : s) {
        out += std::to_string(id) + " ";
    }
    if (api->db_set) {
        api->db_set(ctx, key, out.c_str());
    }
}

// ── Command: .spymode [on|off] ────────────────────────────────────────────────
void cmd_spymode(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* args = api->cmd_args(ctx);
    bool en = !is_spy_enabled(ctx, api);
    if (args && args[0]) {
        if (strcmp(args, "on") == 0 || strcmp(args, "1") == 0) en = true;
        else if (strcmp(args, "off") == 0 || strcmp(args, "0") == 0) en = false;
    }
    set_spy_enabled(ctx, api, en);

    api->c_reset(ctx);
    api->c_fmt(ctx, ("🕵️ Режим NekoSpy: " + std::string(en ? "✅ ВКЛЮЧЕН" : "❌ ВЫКЛЮЧЕН")).c_str(), KOTO_ENT_BOLD);
    send_res(ctx, api);
}

// ── Command: .spybl — добавить текущий чат в ЧС ───────────────────────────────
void cmd_spybl(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    auto bl = get_id_set(ctx, api, "nekospy_bl");
    bl.insert(cid);
    save_id_set(ctx, api, "nekospy_bl", bl);

    api->c_reset(ctx);
    api->c_fmt(ctx, "🚫 Текущий чат добавлен в черный список NekoSpy (игнорируется).", KOTO_ENT_BOLD);
    send_res(ctx, api);
}

// ── Command: .spyblclear ──────────────────────────────────────────────────────
void cmd_spyblclear(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::set<long long> empty_set;
    save_id_set(ctx, api, "nekospy_bl", empty_set);

    api->c_reset(ctx);
    api->c_fmt(ctx, "✅ Черный список NekoSpy очищен.", KOTO_ENT_BOLD);
    send_res(ctx, api);
}

// ── Command: .spywl — добавить текущий чат в БС ───────────────────────────────
void cmd_spywl(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    auto wl = get_id_set(ctx, api, "nekospy_wl");
    wl.insert(cid);
    save_id_set(ctx, api, "nekospy_wl", wl);

    api->c_reset(ctx);
    api->c_fmt(ctx, "⭐ Текущий чат добавлен в белый список NekoSpy (приоритетное слежение).", KOTO_ENT_BOLD);
    send_res(ctx, api);
}

// ── Command: .spywlclear ──────────────────────────────────────────────────────
void cmd_spywlclear(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::set<long long> empty_set;
    save_id_set(ctx, api, "nekospy_wl", empty_set);

    api->c_reset(ctx);
    api->c_fmt(ctx, "✅ Белый список NekoSpy очищен.", KOTO_ENT_BOLD);
    send_res(ctx, api);
}

// ── Command: .spyinfo — статус и информация ───────────────────────────────────
void cmd_spyinfo(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    bool en = is_spy_enabled(ctx, api);
    auto bl = get_id_set(ctx, api, "nekospy_bl");
    auto wl = get_id_set(ctx, api, "nekospy_wl");

    api->c_reset(ctx);
    api->c_fmt(ctx, "🕵️ NekoSpy · Информация и статус (by kote)\n\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "• Статус слежки: ");
    api->c_fmt(ctx, en ? "✅ Активен" : "❌ Отключен", KOTO_ENT_BOLD);
    api->c_text(ctx, "\n• Кэшировано сообщений: ");
    {
        std::lock_guard<std::mutex> lk(g_spy_mu);
        api->c_fmt(ctx, std::to_string(g_msg_cache.size()).c_str(), KOTO_ENT_CODE);
    }
    api->c_text(ctx, "\n• Чатов в черном списке: ");
    api->c_fmt(ctx, std::to_string(bl.size()).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n• Чатов в белом списке: ");
    api->c_fmt(ctx, std::to_string(wl.size()).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n\n");
    api->c_fmt(ctx, "Команды модуля:\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "• .spymode [on|off] — включить/выключить слежку\n");
    api->c_text(ctx, "• .spyinfo — этот статус\n");
    api->c_text(ctx, "• .spybl — добавить чат в ЧС (игнор)\n");
    api->c_text(ctx, "• .spyblclear — очистить ЧС\n");
    api->c_text(ctx, "• .spywl — добавить чат в БС (приоритет)\n");
    api->c_text(ctx, "• .spywlclear — очистить БС\n");

    send_res(ctx, api);
}

// ── Watcher: перехват сообщений ───────────────────────────────────────────────
int on_watch(koto_ctx* ctx, const koto_api* api) {
    if (!is_spy_enabled(ctx, api)) return 0;

    long long cid = api->chat_id ? api->chat_id(ctx) : 0;
    long long sid = api->sender_id ? api->sender_id(ctx) : 0;
    long long mid = api->msg_id ? api->msg_id(ctx) : 0;
    long long my_id = api->get_me ? api->get_me(ctx) : 0;

    if (cid == 0 || mid == 0) return 0;

    // Не следим за чатом с самими собой (Избранное)
    if (cid == my_id) return 0;

    auto bl = get_id_set(ctx, api, "nekospy_bl");
    if (bl.count(cid)) return 0;

    auto wl = get_id_set(ctx, api, "nekospy_wl");
    if (!wl.empty() && !wl.count(cid)) return 0;

    const char* txt = api->msg_text ? api->msg_text(ctx) : nullptr;
    std::string text = txt ? txt : "";

    std::lock_guard<std::mutex> lk(g_spy_mu);

    // Проверяем, есть ли такое сообщение в кэше (редактирование)
    for (auto& item : g_msg_cache) {
        if (item.chat_id == cid && item.msg_id == mid) {
            // Если текст изменился — это РЕДАКТИРОВАНИЕ!
            if (!item.text.empty() && item.text != text) {
                std::string old_txt = item.text;
                item.text = text; // обновляем

                // Отправляем отчёт в Избранное (Saved Messages)
                api->c_reset(ctx);
                api->c_fmt(ctx, "🔏 [NekoSpy] Сообщение отредактировано!\n\n", KOTO_ENT_BOLD);
                api->c_text(ctx, "👤 Отправитель ID: ");
                api->c_fmt(ctx, std::to_string(sid).c_str(), KOTO_ENT_CODE);
                api->c_text(ctx, "\n💬 Чат ID: ");
                api->c_fmt(ctx, std::to_string(cid).c_str(), KOTO_ENT_CODE);
                api->c_text(ctx, "\n\n");
                api->c_fmt(ctx, "Старый текст:\n", KOTO_ENT_BOLD);
                api->c_text(ctx, (old_txt + "\n\n").c_str());
                api->c_fmt(ctx, "Новый текст:\n", KOTO_ENT_BOLD);
                api->c_text(ctx, text.c_str());

                api->c_send(ctx, my_id, 0);
            }
            return 0;
        }
    }

    // Добавляем новое сообщение в кэш
    if (!text.empty()) {
        CachedMsg entry;
        entry.msg_id = mid;
        entry.sender_id = sid;
        entry.chat_id = cid;
        entry.text = text;
        entry.timestamp = time(nullptr);

        g_msg_cache.push_back(entry);
        if (g_msg_cache.size() > MAX_CACHE_SIZE) {
            g_msg_cache.pop_front();
        }
    }

    return 0;
}

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    (void)ctx; (void)api;
}

static const koto_command COMMANDS[] = {
    {"spymode", cmd_spymode, KOTO_LEVEL_TRUSTED, "[ru] Включить/выключить режим слежки NekoSpy [en] Toggle NekoSpy mode.", "[on|off]"},
    {"spyinfo", cmd_spyinfo, KOTO_LEVEL_TRUSTED, "[ru] Статус и статистика модуля NekoSpy [en] NekoSpy status and statistics.", nullptr},
    {"spybl", cmd_spybl, KOTO_LEVEL_TRUSTED, "[ru] Добавить чат в черный список NekoSpy [en] Add chat to NekoSpy blacklist.", nullptr},
    {"spyblclear", cmd_spyblclear, KOTO_LEVEL_TRUSTED, "[ru] Очистить черный список NekoSpy [en] Clear NekoSpy blacklist.", nullptr},
    {"spywl", cmd_spywl, KOTO_LEVEL_TRUSTED, "[ru] Добавить чат в белый список NekoSpy [en] Add chat to NekoSpy whitelist.", nullptr},
    {"spywlclear", cmd_spywlclear, KOTO_LEVEL_TRUSTED, "[ru] Очистить белый список NekoSpy [en] Clear NekoSpy whitelist.", nullptr}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "nekospy",
    "[ru] Модуль слежки за удалёнными и изменёнными сообщениями с отчётом в Избранное (by kote). [en] NekoSpy: tracks edited and deleted messages to Saved Messages (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    on_watch,
    KOTO_WATCH_ALL,
    on_callback,
    nullptr, 0,
    "https://github.com/AresUser1/KoteModules/blob/main/src/nekospy.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
