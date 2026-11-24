// nekospy.cpp — Модуль слежки за удалёнными и изменёнными сообщениями (C++ ABI 3).
// Поддержка основы и твинков, сохранение фото и «одноразок» (view-once media).
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
#include <strings.h>
#include <cstdlib>
#include <unistd.h>

namespace {
using json = nlohmann::json;

constexpr const char* VERSION = "1.2.3";

struct CachedMsg {
    long long msg_id = 0;
    long long sender_id = 0;
    long long chat_id = 0;
    std::string text;
    std::string media_path;
    bool is_view_once = false;
    time_t timestamp = 0;
};

static std::mutex g_spy_mu;
static std::deque<CachedMsg> g_msg_cache;
static constexpr size_t MAX_CACHE_SIZE = 2000;

static bool is_spy_enabled(koto_ctx* ctx, const koto_api* api) {
    char buf[16] = {0};
    if (api->db_get && api->db_get(ctx, "nekospy_enabled", buf, sizeof(buf) - 1) > 0) {
        if (strcmp(buf, "0") == 0 || strcasecmp(buf, "false") == 0 || strcasecmp(buf, "off") == 0) {
            return false;
        }
        return true;
    }
    return true; // Включен по умолчанию
}

static void set_spy_enabled(koto_ctx* ctx, const koto_api* api, bool en) {
    if (api->db_set) {
        api->db_set(ctx, "nekospy_enabled", en ? "1" : "0");
    }
}

static std::string get_twins_mode(koto_ctx* ctx, const koto_api* api) {
    char buf[32] = {0};
    if (api->db_get && api->db_get(ctx, "nekospy_twins_mode", buf, sizeof(buf) - 1) > 0) {
        return std::string(buf);
    }
    return "all"; // По умолчанию: все чаты твинков
}

static void set_twins_mode(koto_ctx* ctx, const koto_api* api, const std::string& mode) {
    if (api->db_set) {
        api->db_set(ctx, "nekospy_twins_mode", mode.c_str());
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

// Поиск локально сохранённого фото/видео медиа
static std::string find_cached_media(koto_ctx* ctx, const koto_api* api, long long cid, long long mid) {
    if (mid <= 0) return "";
    char cdir[512] = {0};
    if (api->cache_dir) api->cache_dir(ctx, cdir, sizeof(cdir) - 1);
    std::string base = (cdir[0]) ? std::string(cdir) : "/data/user/0/dev.kotogram.loader/files/cache";
    auto bare = [](long long id) -> long long {
        if (id <= -1000000000000LL) return -(id + 1000000000000LL);
        if (id < 0) return -id;
        return id;
    };
    long long bc = bare(cid);
    std::string p_jpg = base + "/spy_media/spy_" + std::to_string(bc) + "_" + std::to_string(mid) + ".jpg";
    if (access(p_jpg.c_str(), F_OK) == 0) return p_jpg;
    std::string p_mp4 = base + "/spy_media/spy_" + std::to_string(bc) + "_" + std::to_string(mid) + ".mp4";
    if (access(p_mp4.c_str(), F_OK) == 0) return p_mp4;
    return "";
}

// ── Рендер интерактивной панели управления NekoSpy ────────────────────────────
static void render_panel(koto_ctx* ctx, const koto_api* api, long long cur_cid, bool is_cb) {
    bool en = is_spy_enabled(ctx, api);
    std::string tw_mode = get_twins_mode(ctx, api);
    auto bl = get_id_set(ctx, api, "nekospy_bl");
    auto wl = get_id_set(ctx, api, "nekospy_wl");

    std::string tw_str;
    if (tw_mode == "all") tw_str = "👥 Все чаты";
    else if (tw_mode == "wl") tw_str = "⭐ Только БС";
    else tw_str = "❌ Отключены";

    bool in_bl = (cur_cid != 0 && bl.count(cur_cid));
    bool in_wl = (cur_cid != 0 && wl.count(cur_cid));

    std::string cur_status = "👁️ Отслеживается";
    if (in_bl) cur_status = "🚫 В черном списке (игнор)";
    else if (in_wl) cur_status = "⭐ В белом списке (приоритет)";
    else if (!wl.empty()) cur_status = "⏳ Не в БС (пропуск)";

    size_t cache_count = 0;
    {
        std::lock_guard<std::mutex> lk(g_spy_mu);
        cache_count = g_msg_cache.size();
    }

    api->c_reset(ctx);
    api->c_fmt(ctx, "🕵️ NekoSpy · Панель управления (v1.2.3)\n\n", KOTO_ENT_BOLD);

    api->c_text(ctx, "• Слежка NekoSpy: ");
    api->c_fmt(ctx, en ? "✅ Активна" : "❌ Выключена", KOTO_ENT_BOLD);
    api->c_text(ctx, (" (кэш: " + std::to_string(cache_count) + " сообщений)\n").c_str());

    api->c_text(ctx, "• Режим Твинков: ");
    api->c_fmt(ctx, tw_str.c_str(), KOTO_ENT_BOLD);
    api->c_text(ctx, "\n• Черный список (ЧС): ");
    api->c_fmt(ctx, (std::to_string(bl.size()) + " чатов").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n• Белый список (БС): ");
    api->c_fmt(ctx, (std::to_string(wl.size()) + " чатов").c_str(), KOTO_ENT_CODE);

    if (cur_cid != 0) {
        api->c_text(ctx, "\n• Текущий чат: ");
        api->c_fmt(ctx, std::to_string(cur_cid).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, (" — " + cur_status + "\n").c_str());
    }

    api->c_text(ctx, "\n");
    api->c_fmt(ctx, "Быстрые команды:\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "• .spymode [on|off] — вкл/выкл NekoSpy (основа + твинки + медиа)\n");
    api->c_text(ctx, "• .spytwins [all|wl|off] — режим для твинков\n");
    api->c_text(ctx, "• .spybl — добавить/убрать текущий чат в ЧС\n");
    api->c_text(ctx, "• .spyblclear — очистить ЧС\n");
    api->c_text(ctx, "• .spywl — добавить/убрать текущий чат в БС\n");
    api->c_text(ctx, "• .spywlclear — очистить БС\n");

    // Инлайн-кнопки
    if (api->c_button) {
        std::string btn_spy = en ? "🕵️ NekoSpy: ВКЛ" : "🕵️ NekoSpy: ВЫКЛ";
        std::string btn_twins = "👥 Твинки: " + (tw_mode == "all" ? std::string("Все") : (tw_mode == "wl" ? std::string("БС") : std::string("ВЫКЛ")));
        api->c_button(ctx, btn_spy.c_str(), "spy_tog_main", 1);
        api->c_button(ctx, btn_twins.c_str(), "spy_tog_twins", 0);

        if (cur_cid != 0) {
            std::string btn_bl = in_bl ? "🚫 Убрать из ЧС" : "🚫 В ЧС";
            std::string btn_wl = in_wl ? "⭐ Убрать из БС" : "⭐ В БС";
            api->c_button(ctx, btn_bl.c_str(), "spy_tog_bl", 1);
            api->c_button(ctx, btn_wl.c_str(), "spy_tog_wl", 0);
        }

        api->c_button(ctx, "🗑️ Очистить ЧС", "spy_clr_bl", 1);
        api->c_button(ctx, "🗑️ Очистить БС", "spy_clr_wl", 0);

        api->c_button(ctx, "🔄 Обновить", "spy_refresh", 1);
        api->c_button(ctx, "❌ Закрыть", "spy_close", 0);
    }

    if (is_cb) {
        long long mid = api->cb_message_id(ctx);
        api->c_edit_message(ctx, cur_cid, mid, 0);
    } else {
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    }
}

// ── Command: .spymode [on|off] ────────────────────────────────────────────────
void cmd_spymode(koto_ctx* ctx, const koto_api* api) {
    if (!api->is_outgoing(ctx)) return; // Твинки не выполняют команды в чате
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
    api->c_edit(ctx, 0);
}

// ── Command: .spytwins [all|wl|off] ──────────────────────────────────────────
void cmd_spytwins(koto_ctx* ctx, const koto_api* api) {
    if (!api->is_outgoing(ctx)) return; // Твинки не выполняют команды в чате
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* args = api->cmd_args(ctx);
    std::string cur = get_twins_mode(ctx, api);
    std::string next = "all";
    if (args && args[0]) {
        if (strcmp(args, "all") == 0 || strcmp(args, "1") == 0) next = "all";
        else if (strcmp(args, "wl") == 0 || strcmp(args, "white") == 0) next = "wl";
        else if (strcmp(args, "off") == 0 || strcmp(args, "0") == 0) next = "off";
    } else {
        if (cur == "all") next = "wl";
        else if (cur == "wl") next = "off";
        else next = "all";
    }
    set_twins_mode(ctx, api, next);

    std::string desc = (next == "all") ? "👥 ВСЕ ЧАТЫ" : ((next == "wl") ? "⭐ ТОЛЬКО БЕЛЫЙ СПИСОК (.spywl)" : "❌ ВЫКЛЮЧЕНА");
    api->c_reset(ctx);
    api->c_fmt(ctx, ("👥 Режим слежки для твинков: " + desc).c_str(), KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

// ── Command: .spyinfo — панель управления ────────────────────────────────────
void cmd_spyinfo(koto_ctx* ctx, const koto_api* api) {
    if (!api->is_outgoing(ctx)) return; // Твинки не выполняют команды в чате
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    render_panel(ctx, api, cid, false);
}

// ── Command: .spybl — добавить/убрать текущий чат в ЧС ────────────────────────
void cmd_spybl(koto_ctx* ctx, const koto_api* api) {
    if (!api->is_outgoing(ctx)) return;
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    const char* args = api->cmd_args(ctx);
    if (args && args[0]) {
        try { cid = std::stoll(args); } catch (...) {}
    }

    auto bl = get_id_set(ctx, api, "nekospy_bl");
    bool added = false;
    if (bl.count(cid)) {
        bl.erase(cid);
    } else {
        bl.insert(cid);
        added = true;
    }
    save_id_set(ctx, api, "nekospy_bl", bl);

    api->c_reset(ctx);
    if (added) {
        api->c_fmt(ctx, ("🚫 Чат " + std::to_string(cid) + " добавлен в черный список NekoSpy (игнорируется).").c_str(), KOTO_ENT_BOLD);
    } else {
        api->c_fmt(ctx, ("✅ Чат " + std::to_string(cid) + " удален из черного списка NekoSpy.").c_str(), KOTO_ENT_BOLD);
    }
    api->c_edit(ctx, 0);
}

// ── Command: .spyblclear ──────────────────────────────────────────────────────
void cmd_spyblclear(koto_ctx* ctx, const koto_api* api) {
    if (!api->is_outgoing(ctx)) return;
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::set<long long> empty_set;
    save_id_set(ctx, api, "nekospy_bl", empty_set);

    api->c_reset(ctx);
    api->c_fmt(ctx, "✅ Черный список NekoSpy очищен.", KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

// ── Command: .spywl — добавить/убрать текущий чат в БС ────────────────────────
void cmd_spywl(koto_ctx* ctx, const koto_api* api) {
    if (!api->is_outgoing(ctx)) return;
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    const char* args = api->cmd_args(ctx);
    if (args && args[0]) {
        try { cid = std::stoll(args); } catch (...) {}
    }

    auto wl = get_id_set(ctx, api, "nekospy_wl");
    bool added = false;
    if (wl.count(cid)) {
        wl.erase(cid);
    } else {
        wl.insert(cid);
        added = true;
    }
    save_id_set(ctx, api, "nekospy_wl", wl);

    api->c_reset(ctx);
    if (added) {
        api->c_fmt(ctx, ("⭐ Чат " + std::to_string(cid) + " добавлен в белый список NekoSpy (приоритетное слежение).").c_str(), KOTO_ENT_BOLD);
    } else {
        api->c_fmt(ctx, ("✅ Чат " + std::to_string(cid) + " удален из белого списка NekoSpy.").c_str(), KOTO_ENT_BOLD);
    }
    api->c_edit(ctx, 0);
}

// ── Command: .spywlclear ──────────────────────────────────────────────────────
void cmd_spywlclear(koto_ctx* ctx, const koto_api* api) {
    if (!api->is_outgoing(ctx)) return;
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::set<long long> empty_set;
    save_id_set(ctx, api, "nekospy_wl", empty_set);

    api->c_reset(ctx);
    api->c_fmt(ctx, "✅ Белый список NekoSpy очищен.", KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

// Сравнение ID чатов с поддержкой каналов (-100... и bare ID)
static inline bool is_same_chat(long long c1, long long c2) {
    if (c1 == 0 || c2 == 0) return true;
    if (c1 == c2) return true;
    auto bare = [](long long id) {
        if (id <= -1000000000000LL) return -(id + 1000000000000LL);
        if (id < 0) return -id;
        return id;
    };
    return bare(c1) == bare(c2);
}

// Форматирование информации о чате с поддержкой супергрупп/каналов (-100...)
static void format_chat_info(koto_ctx* ctx, const koto_api* api, long long cid, long long mid) {
    std::string cid_str = std::to_string(cid);
    std::string title = "";
    if (api->chat_full_json && cid != 0) {
        std::vector<char> cbuf(2048, 0);
        int cn = api->chat_full_json(ctx, cid, cbuf.data(), static_cast<int>(cbuf.size()) - 1);
        if (cn > 0) {
            try {
                auto j = nlohmann::json::parse(std::string(cbuf.data(), static_cast<size_t>(cn)));
                title = j.value("title", "");
            } catch (...) {}
        }
    }
    if (!title.empty()) {
        api->c_text(ctx, "\n💬 Чат: ");
        api->c_fmt(ctx, title.c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, " (ID: ");
        api->c_fmt(ctx, cid_str.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, ")");
    } else {
        api->c_text(ctx, "\n💬 Чат ID: ");
        api->c_fmt(ctx, cid_str.c_str(), KOTO_ENT_CODE);
    }
    if (cid <= -1000000000000LL && mid > 0) {
        long long clean_ch = -(cid + 1000000000000LL);
        std::string link = "https://t.me/c/" + std::to_string(clean_ch) + "/" + std::to_string(mid);
        api->c_text(ctx, "\n🔗 ");
        api->c_url(ctx, "Перейти к сообщению", link.c_str());
    }
}

// ── Watcher: логирование входящих/исходящих и перехват удалений/изменений ────
static int on_watch(koto_ctx* ctx, const koto_api* api) {
    if (!is_spy_enabled(ctx, api)) return 0;

    long long cid = api->chat_id(ctx);
    long long mid = api->msg_id(ctx);
    long long sid = api->sender_id(ctx);
    const char* txt_c = api->msg_text(ctx);
    std::string text = txt_c ? txt_c : "";
    long long my_id = api->get_me ? api->get_me(ctx) : 0;

    // Определяем ID владельца (основы)
    char s_buf[64] = {0};
    long long owner_id = my_id;
    if (api->setting_get && api->setting_get(ctx, "self_id", s_buf, sizeof(s_buf) - 1) > 0) {
        try {
            long long parsed = std::stoll(s_buf);
            if (parsed > 0) owner_id = parsed;
        } catch (...) {}
    }
    bool is_twin = (owner_id != 0 && my_id != 0 && my_id != owner_id);
    bool is_self = (api->is_outgoing(ctx) || (my_id != 0 && sid == my_id) || (owner_id != 0 && sid == owner_id));

    // Списки исключений
    auto bl = get_id_set(ctx, api, "nekospy_bl");
    auto wl = get_id_set(ctx, api, "nekospy_wl");
    std::string tw_mode = get_twins_mode(ctx, api);

    if (is_twin && tw_mode == "off") return 0;

    // Не следим за чатом с самим собой (Saved Messages)
    if (cid != 0 && (cid == my_id || cid == owner_id)) return 0;

    // 1. Проверяем, является ли это событием УДАЛЕНИЯ сообщения
    if (text == "__KOTO_DELETED__") {
        std::lock_guard<std::mutex> lk(g_spy_mu);
        for (auto it = g_msg_cache.begin(); it != g_msg_cache.end(); ++it) {
            if (it->msg_id == mid && is_same_chat(it->chat_id, cid)) {
                std::string del_txt = it->text;
                long long del_sid = it->sender_id;
                long long del_cid = (cid != 0) ? cid : it->chat_id;
                std::string del_media = it->media_path;
                bool del_vo = it->is_view_once;
                g_msg_cache.erase(it);

                if (del_txt.empty() && del_media.empty()) return 0;

                // Не логируем удаление собственных сообщений
                if (del_sid == my_id || (owner_id != 0 && del_sid == owner_id)) return 0;

                // Проверка ЧС и БС для удаленного сообщения
                if (del_cid != 0 && bl.count(del_cid)) return 0;
                if (is_twin) {
                    if (tw_mode == "wl" && !wl.count(del_cid)) return 0;
                } else {
                    if (!wl.empty() && !wl.count(del_cid)) return 0;
                }

                api->c_reset(ctx);
                if (is_twin) {
                    char tw_name[128] = {0};
                    if (api->get_me_name) api->get_me_name(ctx, tw_name, sizeof(tw_name) - 1);
                    std::string header = "🗑️ [NekoSpy • Твинк";
                    if (tw_name[0]) {
                        header += " " + std::string(tw_name);
                    }
                    header += del_vo ? "] Удалена одноразка!\n\n" : "] Сообщение удалено!\n\n";
                    api->c_fmt(ctx, header.c_str(), KOTO_ENT_BOLD);
                } else {
                    api->c_fmt(ctx, del_vo ? "🗑️ [NekoSpy] Удалена одноразка!\n\n" : "🗑️ [NekoSpy] Сообщение удалено!\n\n", KOTO_ENT_BOLD);
                }

                api->c_text(ctx, "👤 Отправитель ID: ");
                api->c_fmt(ctx, std::to_string(del_sid).c_str(), KOTO_ENT_CODE);
                format_chat_info(ctx, api, del_cid, mid);

                if (!del_txt.empty() && del_txt != "[Фото]" && del_txt != "[Одноразка]" && del_txt != "[Одноразовое фото]" && del_txt != "[Одноразовое видео]") {
                    api->c_text(ctx, "\n\n");
                    api->c_fmt(ctx, "Текст удалённого сообщения:\n", KOTO_ENT_BOLD);
                    api->c_text(ctx, del_txt.c_str());
                }

                // Отправляем в СВОЁ Избранное (Saved Messages) текущего аккаунта (основа в свои, твинки в свои)
                if (!del_media.empty() && access(del_media.c_str(), F_OK) == 0) {
                    api->c_send_file(ctx, my_id, del_media.c_str(), 0);
                } else {
                    api->c_send(ctx, my_id, 0);
                }
                return 0;
            }
        }
        return 0;
    }

    if (cid == 0) return 0;

    // Черный список (игнор)
    if (bl.count(cid)) return 0;

    // Белый список
    if (is_twin) {
        if (tw_mode == "wl" && !wl.count(cid)) return 0;
    } else {
        if (!wl.empty() && !wl.count(cid)) return 0;
    }

    std::lock_guard<std::mutex> lk(g_spy_mu);

    // 2. Проверяем, есть ли такое сообщение в кэше (РЕДАКТИРОВАНИЕ)
    for (auto& item : g_msg_cache) {
        if (is_same_chat(item.chat_id, cid) && item.msg_id == mid) {
            // Если текст изменился — это РЕДАКТИРОВАНИЕ!
            if (!item.text.empty() && !text.empty() && item.text != text) {
                std::string old_txt = item.text;
                item.text = text; // обновляем кэш

                // Не логируем редактирование своих собственных сообщений
                if (is_self || item.sender_id == my_id || (owner_id != 0 && item.sender_id == owner_id)) {
                    return 0;
                }

                api->c_reset(ctx);
                if (is_twin) {
                    char tw_name[128] = {0};
                    if (api->get_me_name) api->get_me_name(ctx, tw_name, sizeof(tw_name) - 1);
                    std::string header = "🔏 [NekoSpy • Твинк";
                    if (tw_name[0]) {
                        header += " " + std::string(tw_name);
                    }
                    header += "] Сообщение отредактировано!\n\n";
                    api->c_fmt(ctx, header.c_str(), KOTO_ENT_BOLD);
                } else {
                    api->c_fmt(ctx, "🔏 [NekoSpy] Сообщение отредактировано!\n\n", KOTO_ENT_BOLD);
                }

                api->c_text(ctx, "👤 Отправитель ID: ");
                api->c_fmt(ctx, std::to_string(sid != 0 ? sid : item.sender_id).c_str(), KOTO_ENT_CODE);
                format_chat_info(ctx, api, cid, mid);
                api->c_text(ctx, "\n\n");
                api->c_fmt(ctx, "Старый текст:\n", KOTO_ENT_BOLD);
                api->c_text(ctx, (old_txt + "\n\n").c_str());
                api->c_fmt(ctx, "Новый текст:\n", KOTO_ENT_BOLD);
                api->c_text(ctx, text.c_str());

                // Отправляем в СВОЁ Избранное (Saved Messages) текущего аккаунта
                api->c_send(ctx, my_id, 0);
            }
            return 0;
        }
    }

    std::string media_path = find_cached_media(ctx, api, cid, mid);
    bool is_vo = (text.rfind("[Одноразка", 0) == 0 || text.rfind("[Одноразовое", 0) == 0);

    // 3. Добавляем новое сообщение в кэш (только чужие сообщения)
    if ((!text.empty() || !media_path.empty()) && !is_self) {
        CachedMsg entry;
        entry.msg_id = mid;
        entry.sender_id = (sid != 0) ? sid : cid;
        entry.chat_id = cid;
        entry.text = text;
        entry.media_path = media_path;
        entry.is_view_once = is_vo;
        entry.timestamp = time(nullptr);

        g_msg_cache.push_back(entry);
        if (g_msg_cache.size() > MAX_CACHE_SIZE) {
            g_msg_cache.pop_front();
        }

        // Если пришла «одноразка» (view-once) — сразу сейфим её в Избранное текущего аккаунта!
        if (is_vo && !media_path.empty()) {
            api->c_reset(ctx);
            if (is_twin) {
                char tw_name[128] = {0};
                if (api->get_me_name) api->get_me_name(ctx, tw_name, sizeof(tw_name) - 1);
                std::string header = "👁️ [NekoSpy • Твинк";
                if (tw_name[0]) header += " " + std::string(tw_name);
                header += "] Перехвачена одноразка!\n\n";
                api->c_fmt(ctx, header.c_str(), KOTO_ENT_BOLD);
            } else {
                api->c_fmt(ctx, "👁️ [NekoSpy] Перехвачена одноразка!\n\n", KOTO_ENT_BOLD);
            }
            api->c_text(ctx, "👤 Отправитель ID: ");
            api->c_fmt(ctx, std::to_string(entry.sender_id).c_str(), KOTO_ENT_CODE);
            format_chat_info(ctx, api, cid, mid);
            if (text != "[Одноразка]" && text != "[Одноразовое фото]" && text != "[Одноразовое видео]") {
                api->c_text(ctx, "\n\n");
                api->c_fmt(ctx, "Подпись:\n", KOTO_ENT_BOLD);
                api->c_text(ctx, text.c_str());
            }
            api->c_send_file(ctx, my_id, media_path.c_str(), 0);
        }
    }

    return 0;
}

// ── Интерактивные нажатия кнопок в панели ─────────────────────────────────────
static void on_callback(koto_ctx* ctx, const koto_api* api) {
    if (!api || !api->cb_data) return;
    const char* d = api->cb_data(ctx);
    if (!d || strncmp(d, "spy_", 4) != 0) return;
    if (api->check_permission && !api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) {
        if (api->cb_answer) api->cb_answer(ctx, "❌ Нет доступа", 1);
        return;
    }
    std::string data(d);
    long long cid = api->chat_id(ctx);

    if (data == "spy_tog_main") {
        bool en = !is_spy_enabled(ctx, api);
        set_spy_enabled(ctx, api, en);
        if (api->cb_answer) api->cb_answer(ctx, en ? "NekoSpy: ВКЛ" : "NekoSpy: ВЫКЛ", 0);
        render_panel(ctx, api, cid, true);
    } else if (data == "spy_tog_twins") {
        std::string cur = get_twins_mode(ctx, api);
        std::string next = (cur == "all") ? "wl" : ((cur == "wl") ? "off" : "all");
        set_twins_mode(ctx, api, next);
        std::string toast = "Твинки: " + (next == "all" ? std::string("Все чаты") : (next == "wl" ? std::string("Только БС") : std::string("ВЫКЛ")));
        if (api->cb_answer) api->cb_answer(ctx, toast.c_str(), 0);
        render_panel(ctx, api, cid, true);
    } else if (data == "spy_tog_bl") {
        if (cid != 0) {
            auto bl = get_id_set(ctx, api, "nekospy_bl");
            if (bl.count(cid)) {
                bl.erase(cid);
                if (api->cb_answer) api->cb_answer(ctx, "Чат удален из ЧС", 0);
            } else {
                bl.insert(cid);
                if (api->cb_answer) api->cb_answer(ctx, "Чат добавлен в ЧС", 0);
            }
            save_id_set(ctx, api, "nekospy_bl", bl);
        }
        render_panel(ctx, api, cid, true);
    } else if (data == "spy_tog_wl") {
        if (cid != 0) {
            auto wl = get_id_set(ctx, api, "nekospy_wl");
            if (wl.count(cid)) {
                wl.erase(cid);
                if (api->cb_answer) api->cb_answer(ctx, "Чат удален из БС", 0);
            } else {
                wl.insert(cid);
                if (api->cb_answer) api->cb_answer(ctx, "Чат добавлен в БС", 0);
            }
            save_id_set(ctx, api, "nekospy_wl", wl);
        }
        render_panel(ctx, api, cid, true);
    } else if (data == "spy_clr_bl") {
        std::set<long long> empty;
        save_id_set(ctx, api, "nekospy_bl", empty);
        if (api->cb_answer) api->cb_answer(ctx, "Черный список очищен", 0);
        render_panel(ctx, api, cid, true);
    } else if (data == "spy_clr_wl") {
        std::set<long long> empty;
        save_id_set(ctx, api, "nekospy_wl", empty);
        if (api->cb_answer) api->cb_answer(ctx, "Белый список очищен", 0);
        render_panel(ctx, api, cid, true);
    } else if (data == "spy_refresh") {
        if (api->cb_answer) api->cb_answer(ctx, "Панель обновлена", 0);
        render_panel(ctx, api, cid, true);
    } else if (data == "spy_close") {
        if (api->cb_answer) api->cb_answer(ctx, "Панель закрыта", 0);
        long long mid = api->cb_message_id ? api->cb_message_id(ctx) : 0;
        if (api->delete_message && mid != 0) {
            api->delete_message(ctx, cid, mid);
        } else {
            api->c_reset(ctx);
            api->c_fmt(ctx, "🕵️ NekoSpy: панель закрыта.", KOTO_ENT_ITALIC);
            api->c_edit_message(ctx, cid, mid, 0);
        }
    }
}

static const koto_command COMMANDS[] = {
    {"spymode", cmd_spymode, KOTO_LEVEL_TRUSTED, "[ru] Включить/выключить режим слежки NekoSpy [en] Toggle NekoSpy mode.", "[on|off]"},
    {"spytwins", cmd_spytwins, KOTO_LEVEL_TRUSTED, "[ru] Режим слежки для твинков (all|wl|off) [en] Twin spy mode (all|wl|off).", "[all|wl|off]"},
    {"spyinfo", cmd_spyinfo, KOTO_LEVEL_TRUSTED, "[ru] Панель управления и статус NekoSpy [en] NekoSpy control panel and status.", nullptr},
    {"spy", cmd_spyinfo, KOTO_LEVEL_TRUSTED, "[ru] Панель управления NekoSpy [en] NekoSpy control panel.", nullptr},
    {"spybl", cmd_spybl, KOTO_LEVEL_TRUSTED, "[ru] Добавить/убрать текущий чат в ЧС [en] Toggle chat in NekoSpy blacklist.", "[chat_id]"},
    {"spyblclear", cmd_spyblclear, KOTO_LEVEL_TRUSTED, "[ru] Очистить черный список NekoSpy [en] Clear NekoSpy blacklist.", nullptr},
    {"spywl", cmd_spywl, KOTO_LEVEL_TRUSTED, "[ru] Добавить/убрать текущий чат в БС [en] Toggle chat in NekoSpy whitelist.", "[chat_id]"},
    {"spywlclear", cmd_spywlclear, KOTO_LEVEL_TRUSTED, "[ru] Очистить белый список NekoSpy [en] Clear NekoSpy whitelist.", nullptr}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "nekospy",
    "[ru] Слежка за удалёнными и изменёнными сообщениями, сохранение фото и одноразок (основа + твинки) с интерактивной панелью (by kote). [en] NekoSpy: tracks edited and deleted messages, saves photos and view-once media for main and twins (by kote).",
    VERSION,
    26,
    KOTO_MOD_TWINS, // Флаг KOTO_MOD_TWINS включен для watcher'а, а команды твинков безопасно заглушены
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
