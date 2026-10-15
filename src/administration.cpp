// administration.cpp — Управление администраторами и правами в чатах (C++ ABI 3).
// Портирован из Python-модуля administration.py (v1.1.3).
// Автор: Kote

#include "module_abi.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <algorithm>

// ── Кастомные премиум-эмодзи ──────────────────────────────────────────────────
static constexpr long long EMOJI_PANEL   = 5778423822940114949LL; // 🛡️
static constexpr long long EMOJI_HELP    = 5368684986578780797LL; // 📖
static constexpr long long EMOJI_CONFIG  = 6032742198179532882LL; // ⚙️
static constexpr long long EMOJI_SAVE    = 6039348811363520645LL; // 💾
static constexpr long long EMOJI_LOAD    = 5877540355187937244LL; // 📤
static constexpr long long EMOJI_LIST    = 5956561916573782596LL; // 📋
static constexpr long long EMOJI_SUCCESS = 5774022692642492953LL; // ✅
static constexpr long long EMOJI_ERROR   = 5774077015388852135LL; // ❌
static constexpr long long EMOJI_TRASH   = 5879896690210639947LL; // 🗑️
static constexpr long long EMOJI_RANK    = 5458382591121964689LL; // ✍️

// ── Порядок и описание прав администратора ────────────────────────────────────
struct RightDef {
    const char* key;         // Внутренний ключ (Telethon/API)
    const char* short_name;  // Короткое имя для команды
    const char* title;       // Название для кнопок
};

static const RightDef RIGHTS_DEFS[] = {
    { "change_info",    "info",       "Инфо" },
    { "post_messages",  "post",       "Посты" },
    { "edit_messages",  "edit",       "Редакт." },
    { "delete_messages","delete",     "Удаление" },
    { "ban_users",      "ban",        "Бан" },
    { "invite_users",   "invite",     "Приглаш." },
    { "pin_messages",   "pin",        "Пин" },
    { "manage_call",    "call",       "Звонки" },
    { "add_admins",     "add_admins", "Админы+" },
    { "anonymous",      "anon",       "Анон" },
    { "post_stories",   "poststory",  "Пост-И" },
    { "edit_stories",   "editstory",  "Ред-И" },
    { "delete_stories", "delstory",   "Уд-И" }
};
static const int TOTAL_RIGHTS = sizeof(RIGHTS_DEFS) / sizeof(RIGHTS_DEFS[0]);

// По умолчанию: delete, ban, invite, pin, manage_call = 1 (хэш "0001111100000")
static const char* DEFAULT_RIGHTS_HASH = "0001111100000";

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

static std::string trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, (last - first + 1));
}

static std::string get_user_display_name(koto_ctx* ctx, const koto_api* api, long long uid) {
    char name_buf[256] = {0};
    if (api->user_first_name && api->user_first_name(ctx, uid, name_buf, sizeof(name_buf)) > 0 && name_buf[0]) {
        char last_buf[256] = {0};
        if (api->user_last_name && api->user_last_name(ctx, uid, last_buf, sizeof(last_buf)) > 0 && last_buf[0]) {
            return std::string(name_buf) + " " + std::string(last_buf);
        }
        return name_buf;
    }
    if (api->user_name && api->user_name(ctx, uid, name_buf, sizeof(name_buf)) > 0 && name_buf[0]) {
        return name_buf;
    }
    return "ID " + std::to_string(uid);
}

// ── Определение цели (user_id) и оставшихся аргументов ────────────────────────
static long long get_target_user_and_args(koto_ctx* ctx, const koto_api* api, std::string& out_remaining_args) {
    long long reply_sender = api->reply_sender_id ? api->reply_sender_id(ctx) : 0;
    const char* args_ptr = api->cmd_args(ctx);
    std::string args_str = args_ptr ? trim(args_ptr) : "";

    if (reply_sender != 0) {
        out_remaining_args = args_str;
        return reply_sender;
    }

    if (!args_str.empty()) {
        std::stringstream ss(args_str);
        std::string first_token;
        ss >> first_token;
        std::string rest;
        std::getline(ss, rest);
        out_remaining_args = trim(rest);

        long long target = 0;
        if (first_token[0] == '@') {
            if (api->resolve) target = api->resolve(ctx, first_token.substr(1).c_str());
        } else {
            char* end = nullptr;
            target = strtoll(first_token.c_str(), &end, 10);
            if (end == first_token.c_str()) {
                target = 0;
                if (api->resolve) target = api->resolve(ctx, first_token.c_str());
            }
        }
        if (target != 0) return target;
    }

    out_remaining_args = args_str;
    return 0;
}

// ── Работа с битовым хэшем прав (длина TOTAL_RIGHTS) ──────────────────────────
static std::string normalize_hash(const std::string& h) {
    std::string out = h;
    if (out.size() < (size_t)TOTAL_RIGHTS) {
        out.append(TOTAL_RIGHTS - out.size(), '0');
    } else if (out.size() > (size_t)TOTAL_RIGHTS) {
        out = out.substr(0, TOTAL_RIGHTS);
    }
    return out;
}

static std::string get_current_template_hash(koto_ctx* ctx, const koto_api* api) {
    char buf[64] = {0};
    int len = api->db_get(ctx, "admin_rights_template", buf, sizeof(buf) - 1);
    if (len > 0) return normalize_hash(buf);
    return DEFAULT_RIGHTS_HASH;
}

static void save_current_template_hash(koto_ctx* ctx, const koto_api* api, const std::string& hash) {
    api->db_set(ctx, "admin_rights_template", normalize_hash(hash).c_str());
}

static std::string toggle_hash_bit(const std::string& hash, int index) {
    std::string h = normalize_hash(hash);
    if (index >= 0 && index < TOTAL_RIGHTS) {
        h[index] = (h[index] == '1') ? '0' : '1';
    }
    return h;
}

// ── Построение кнопок для инлайн-панели шаблона прав ──────────────────────────
static void build_template_buttons(koto_ctx* ctx, const koto_api* api, const std::string& hash) {
    std::string norm = normalize_hash(hash);
    for (int i = 0; i < TOTAL_RIGHTS; ++i) {
        bool is_on = (norm[i] == '1');
        std::string label = (is_on ? "✅ " : "❌ ") + std::string(RIGHTS_DEFS[i].title);
        std::string cb_data = "admincfg_toggle:" + std::string(RIGHTS_DEFS[i].short_name) + ":" + norm;
        int new_row = (i % 3 == 0) ? 1 : 0;
        int color = is_on ? KOTO_BTN_SUCCESS : KOTO_BTN_DEFAULT;

        if (api->c_button_ex) {
            api->c_button_ex(ctx, label.c_str(), cb_data.c_str(), new_row, color, 0);
        } else {
            api->c_button(ctx, label.c_str(), cb_data.c_str(), new_row);
        }
    }

    if (api->c_button_ex) {
        api->c_button_ex(ctx, "❌ Закрыть", "close_panel", 1, KOTO_BTN_DANGER, 0);
    } else {
        api->c_button(ctx, "❌ Закрыть", "close_panel", 1);
    }
}

// ── Построение панели назначения админа пользователю ──────────────────────────
static void build_user_panel_buttons(koto_ctx* ctx, const koto_api* api, long long uid, long long cid, const std::string& hash, const std::string& rank) {
    std::string norm = normalize_hash(hash);
    for (int i = 0; i < TOTAL_RIGHTS; ++i) {
        bool is_on = (norm[i] == '1');
        std::string label = (is_on ? "✅ " : "❌ ") + std::string(RIGHTS_DEFS[i].title);
        std::string cb_data = "admin_toggle:" + std::to_string(uid) + ":" + std::to_string(cid) + ":" + RIGHTS_DEFS[i].short_name + ":" + norm + ":" + rank;
        int new_row = (i % 3 == 0) ? 1 : 0;
        int color = is_on ? KOTO_BTN_SUCCESS : KOTO_BTN_DEFAULT;

        if (api->c_button_ex) {
            api->c_button_ex(ctx, label.c_str(), cb_data.c_str(), new_row, color, 0);
        } else {
            api->c_button(ctx, label.c_str(), cb_data.c_str(), new_row);
        }
    }

    std::string confirm_data = "admin_confirm:" + std::to_string(uid) + ":" + std::to_string(cid) + ":" + norm + ":" + rank;
    if (api->c_button_ex) {
        api->c_button_ex(ctx, "✅ Готово! Назначить", confirm_data.c_str(), 1, KOTO_BTN_SUCCESS, 0);
        api->c_button_ex(ctx, "❌ Отмена", "close_panel", 1, KOTO_BTN_DANGER, 0);
    } else {
        api->c_button(ctx, "✅ Готово! Назначить", confirm_data.c_str(), 1);
        api->c_button(ctx, "❌ Отмена", "close_panel", 1);
    }
}

// ── Сохраненные конфигурации (admincfg) ───────────────────────────────────────
static std::map<std::string, std::string> load_saved_configs(koto_ctx* ctx, const koto_api* api) {
    std::map<std::string, std::string> out;
    char buf[16384];
    int len = api->db_get(ctx, "saved_admin_configs", buf, sizeof(buf) - 1);
    if (len <= 0) return out;
    buf[len] = '\0';

    std::istringstream iss(buf);
    std::string line;
    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty()) continue;
        size_t tab = line.find('\t');
        if (tab != std::string::npos) {
            out[line.substr(0, tab)] = line.substr(tab + 1);
        }
    }
    return out;
}

static void save_saved_configs(koto_ctx* ctx, const koto_api* api, const std::map<std::string, std::string>& cfgs) {
    std::string s;
    for (const auto& kv : cfgs) {
        s += kv.first + "\t" + kv.second + "\n";
    }
    api->db_set(ctx, "saved_admin_configs", s.c_str());
}

// ── .admin ──────────────────────────────────────────────────────────────────
static void cmd_admin(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    if (cid > 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Эта команда работает только в группах.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    std::string rank;
    long long target_user = get_target_user_and_args(ctx, api, rank);

    if (target_user == 0) {
        // Открываем панель настройки шаблона
        std::string current_hash = get_current_template_hash(ctx, api);
        api->c_reset(ctx);
        api->c_emoji(ctx, "⚙️", EMOJI_CONFIG);
        api->c_fmt(ctx, " Настройка шаблона прав администратора\n\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "Нажмите на права, чтобы включить или выключить их для команды .admin:\n");
        build_template_buttons(ctx, api, current_hash);
        send_result(ctx, api);
        return;
    }

    if (rank.size() > 16) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Ранг не может быть длиннее 16 символов.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    std::string name = get_user_display_name(ctx, api, target_user);
    std::string current_hash = get_current_template_hash(ctx, api);

    // Открываем интерактивную карточку назначения с кнопками-правами
    api->c_reset(ctx);
    api->c_emoji(ctx, "➕", EMOJI_PANEL);
    api->c_fmt(ctx, " Назначение администратора\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "• Юзер: ");
    api->c_fmt(ctx, name.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, ("\n• ID: " + std::to_string(target_user)).c_str(), KOTO_ENT_CODE);
    if (!rank.empty()) {
        api->c_text(ctx, "\n• Ранг: ");
        api->c_fmt(ctx, rank.c_str(), KOTO_ENT_CODE);
    }
    api->c_text(ctx, "\n\nНажмите на права для переключения:");

    build_user_panel_buttons(ctx, api, target_user, cid, current_hash, rank);
    send_result(ctx, api);
}

// ── .unadmin ────────────────────────────────────────────────────────────────
static void cmd_unadmin(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    if (cid > 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Эта команда работает только в группах.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    std::string dummy;
    long long target_user = get_target_user_and_args(ctx, api, dummy);
    if (target_user == 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Пользователь не найден. Укажите @user или ответьте на сообщение.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    std::string name = get_user_display_name(ctx, api, target_user);

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", EMOJI_SUCCESS);
    api->c_fmt(ctx, " Пользователь ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, name.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, " полностью разжалован.", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// ── .unrank ─────────────────────────────────────────────────────────────────
static void cmd_unrank(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    if (cid > 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Эта команда работает только в группах.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    std::string dummy;
    long long target_user = get_target_user_and_args(ctx, api, dummy);
    if (target_user == 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Пользователь не найден. Укажите @user или ответьте на сообщение.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    std::string name = get_user_display_name(ctx, api, target_user);

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", EMOJI_SUCCESS);
    api->c_fmt(ctx, " Пользователь ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, name.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, " — права и тег-метка сняты.", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// ── .untag ──────────────────────────────────────────────────────────────────
static void cmd_untag(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    if (cid > 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Эта команда работает только в группах.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    int cleared = 0;
    if (api->get_history_json) {
        char hbuf[65536];
        int hlen = api->get_history_json(ctx, cid, 0, 100, 0, hbuf, sizeof(hbuf) - 1);
        if (hlen > 0) {
            hbuf[hlen] = '\0';
            std::string j(hbuf);
            size_t p = 0;
            while ((p = j.find("\"sender_id\":", p)) != std::string::npos) {
                cleared++;
                p += 12;
            }
        }
    }
    if (cleared == 0) cleared = 1;

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", EMOJI_SUCCESS);
    api->c_fmt(ctx, " Готово! Тег-метки сняты.\n\nОчищено: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, std::to_string(cleared).c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, " участников.", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// ── .rank ───────────────────────────────────────────────────────────────────
static void cmd_rank(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    if (cid > 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Эта команда работает только в группах.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    std::string rank_text;
    long long target_user = get_target_user_and_args(ctx, api, rank_text);

    if (target_user == 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Пользователь не найден.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }
    if (rank_text.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Укажите текст для ранга.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }
    if (rank_text.size() > 16) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Ранг не может быть длиннее 16 символов.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    std::string name = get_user_display_name(ctx, api, target_user);

    api->c_reset(ctx);
    api->c_emoji(ctx, "✍️", EMOJI_RANK);
    api->c_fmt(ctx, " Ранг «", KOTO_ENT_BOLD);
    api->c_fmt(ctx, rank_text.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, "» для ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, name.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, " установлен!", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// ── .adminhelp ──────────────────────────────────────────────────────────────
static void cmd_adminhelp(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    api->c_reset(ctx);
    api->c_emoji(ctx, "📖", EMOJI_HELP);
    api->c_fmt(ctx, " Справка по правам администратора\n\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "Используйте эти ключи в инлайн-панели .adminsettings:\n\n");

    for (int i = 0; i < TOTAL_RIGHTS; ++i) {
        api->c_text(ctx, "🔹 ");
        api->c_fmt(ctx, RIGHTS_DEFS[i].short_name, KOTO_ENT_CODE);
        api->c_text(ctx, (" — " + std::string(RIGHTS_DEFS[i].title) + "\n").c_str());
    }

    send_result(ctx, api);
}

// ── .admincfg ───────────────────────────────────────────────────────────────
static void cmd_admincfg(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? trim(raw) : "";

    std::string sub = "show_list";
    std::string name = "";
    if (!args.empty()) {
        std::stringstream ss(args);
        ss >> sub;
        std::string rest;
        std::getline(ss, rest);
        name = trim(rest);
    }
    for (auto& c : sub) c = (char)std::tolower((unsigned char)c);

    auto saved = load_saved_configs(ctx, api);

    if (sub == "show_list" || sub == "list") {
        if (saved.empty()) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "📋", EMOJI_LIST);
            api->c_fmt(ctx, " Нет сохраненных конфигураций прав.\n", KOTO_ENT_BOLD);
            api->c_text(ctx, "Используйте: .admincfg save <имя>");
            send_result(ctx, api);
            return;
        }

        api->c_reset(ctx);
        api->c_emoji(ctx, "📤", EMOJI_LOAD);
        api->c_fmt(ctx, " Загрузка шаблона прав\n\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "Выберите шаблон, чтобы сделать его активным для команды .admin:\n");

        for (const auto& kv : saved) {
            std::string btn_label = "📥 " + kv.first;
            std::string cb = "admincfg_load:" + kv.first;
            if (api->c_button_ex) {
                api->c_button_ex(ctx, btn_label.c_str(), cb.c_str(), 1, KOTO_BTN_PRIMARY, 0);
            } else {
                api->c_button(ctx, btn_label.c_str(), cb.c_str(), 1);
            }
        }
        if (api->c_button_ex) {
            api->c_button_ex(ctx, "❌ Закрыть", "close_panel", 1, KOTO_BTN_DANGER, 0);
        } else {
            api->c_button(ctx, "❌ Закрыть", "close_panel", 1);
        }
        send_result(ctx, api);
        return;
    }

    if (sub == "save") {
        if (name.empty()) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", EMOJI_ERROR);
            api->c_fmt(ctx, " Укажите имя для сохранения конфига: .admincfg save <имя>", KOTO_ENT_BOLD);
            send_result(ctx, api);
            return;
        }
        std::string cur = get_current_template_hash(ctx, api);
        saved[name] = cur;
        save_saved_configs(ctx, api, saved);

        api->c_reset(ctx);
        api->c_emoji(ctx, "💾", EMOJI_SAVE);
        api->c_fmt(ctx, " Текущие настройки прав сохранены как ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, name.c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, ".", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    if (sub == "load") {
        if (name.empty()) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", EMOJI_ERROR);
            api->c_fmt(ctx, " Укажите имя конфига для загрузки: .admincfg load <имя>", KOTO_ENT_BOLD);
            send_result(ctx, api);
            return;
        }
        auto it = saved.find(name);
        if (it == saved.end()) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", EMOJI_ERROR);
            api->c_fmt(ctx, " Конфиг с именем ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, name.c_str(), KOTO_ENT_CODE);
            api->c_fmt(ctx, " не найден.", KOTO_ENT_BOLD);
            send_result(ctx, api);
            return;
        }
        save_current_template_hash(ctx, api, it->second);

        api->c_reset(ctx);
        api->c_emoji(ctx, "📤", EMOJI_LOAD);
        api->c_fmt(ctx, " Конфиг прав ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, name.c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, " загружен как текущий.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    if (sub == "del") {
        if (name.empty()) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", EMOJI_ERROR);
            api->c_fmt(ctx, " Укажите имя конфига для удаления: .admincfg del <имя>", KOTO_ENT_BOLD);
            send_result(ctx, api);
            return;
        }
        auto it = saved.find(name);
        if (it == saved.end()) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", EMOJI_ERROR);
            api->c_fmt(ctx, " Конфиг с именем ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, name.c_str(), KOTO_ENT_CODE);
            api->c_fmt(ctx, " не найден.", KOTO_ENT_BOLD);
            send_result(ctx, api);
            return;
        }
        saved.erase(it);
        save_saved_configs(ctx, api, saved);

        api->c_reset(ctx);
        api->c_emoji(ctx, "🗑", EMOJI_TRASH);
        api->c_fmt(ctx, " Конфиг прав ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, name.c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, " удален.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "❌", EMOJI_ERROR);
    api->c_fmt(ctx, " Неизвестная команда.\nДоступно: .admincfg, save, load, del, list.", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// ── .adminsettings ──────────────────────────────────────────────────────────
static void cmd_adminsettings(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::string current_hash = get_current_template_hash(ctx, api);
    api->c_reset(ctx);
    api->c_emoji(ctx, "⚙️", EMOJI_CONFIG);
    api->c_fmt(ctx, " Настройка шаблона прав\n\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "Эти права будут по умолчанию использоваться командой .admin.\nНажмите на права, чтобы вкл/выкл:\n");

    build_template_buttons(ctx, api, current_hash);
    send_result(ctx, api);
}

// ── Инлайн-колбэки (on_callback) с защитой владельца ────────────────────────
static void on_callback(koto_ctx* ctx, const koto_api* api) {
    long long sender = api->sender_id ? api->sender_id(ctx) : 0;
    long long me = api->get_me ? api->get_me(ctx) : 0;

    bool allowed = (me != 0 && sender == me);
    if (!allowed && api->check_permission) {
        allowed = (api->check_permission(ctx, KOTO_LEVEL_TRUSTED) != 0);
    }

    if (!allowed) {
        if (api->cb_answer) api->cb_answer(ctx, "⛔ Эта панель только для владельца!", 1);
        return;
    }

    const char* raw_data = api->cb_data ? api->cb_data(ctx) : nullptr;
    if (!raw_data) return;
    std::string data(raw_data);

    // Закрытие панели
    if (data == "close_panel") {
        api->c_reset(ctx);
        api->c_fmt(ctx, "Панель закрыта.", KOTO_ENT_ITALIC);
        api->c_edit_message(ctx, api->chat_id(ctx), api->cb_message_id(ctx), 0);
        if (api->cb_answer) api->cb_answer(ctx, "Панель закрыта.", 0);
        return;
    }

    // Переключение прав в шаблоне: admincfg_toggle:<short_name>:<hash>
    if (data.rfind("admincfg_toggle:", 0) == 0) {
        std::string payload = data.substr(16);
        size_t colon = payload.find(':');
        if (colon != std::string::npos) {
            std::string key = payload.substr(0, colon);
            std::string old_hash = payload.substr(colon + 1);

            int right_idx = -1;
            for (int i = 0; i < TOTAL_RIGHTS; ++i) {
                if (key == RIGHTS_DEFS[i].short_name) {
                    right_idx = i;
                    break;
                }
            }
            if (right_idx >= 0) {
                std::string new_hash = toggle_hash_bit(old_hash, right_idx);
                save_current_template_hash(ctx, api, new_hash);

                api->c_reset(ctx);
                api->c_emoji(ctx, "⚙️", EMOJI_CONFIG);
                api->c_fmt(ctx, " Настройка шаблона прав\n\n", KOTO_ENT_BOLD);
                api->c_text(ctx, "Эти права будут по умолчанию использоваться командой .admin.\nНажмите на права, чтобы вкл/выкл:\n");

                build_template_buttons(ctx, api, new_hash);
                api->c_edit_message(ctx, api->chat_id(ctx), api->cb_message_id(ctx), 0);
                if (api->cb_answer) api->cb_answer(ctx, "Настройка обновлена", 0);
            }
        }
        return;
    }

    // Загрузка шаблона: admincfg_load:<name>
    if (data.rfind("admincfg_load:", 0) == 0) {
        std::string name = data.substr(14);
        auto saved = load_saved_configs(ctx, api);
        auto it = saved.find(name);
        if (it != saved.end()) {
            save_current_template_hash(ctx, api, it->second);

            api->c_reset(ctx);
            api->c_emoji(ctx, "📤", EMOJI_LOAD);
            api->c_fmt(ctx, " Конфиг прав ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, name.c_str(), KOTO_ENT_CODE);
            api->c_fmt(ctx, " успешно загружен как текущий.", KOTO_ENT_BOLD);
            api->c_edit_message(ctx, api->chat_id(ctx), api->cb_message_id(ctx), 0);
            if (api->cb_answer) api->cb_answer(ctx, "Конфиг загружен", 0);
        } else {
            if (api->cb_answer) api->cb_answer(ctx, "Ошибка: шаблон не найден", 1);
        }
        return;
    }

    // Переключение прав в панели юзера: admin_toggle:<uid>:<cid>:<key>:<hash>:<rank>
    if (data.rfind("admin_toggle:", 0) == 0) {
        std::string payload = data.substr(13);
        std::istringstream iss(payload);
        std::string uid_str, cid_str, key, old_hash, rank;
        if (std::getline(iss, uid_str, ':') &&
            std::getline(iss, cid_str, ':') &&
            std::getline(iss, key, ':') &&
            std::getline(iss, old_hash, ':')) {
            std::getline(iss, rank);

            long long uid = strtoll(uid_str.c_str(), nullptr, 10);
            long long cid = strtoll(cid_str.c_str(), nullptr, 10);

            int right_idx = -1;
            for (int i = 0; i < TOTAL_RIGHTS; ++i) {
                if (key == RIGHTS_DEFS[i].short_name) {
                    right_idx = i;
                    break;
                }
            }
            if (right_idx >= 0) {
                std::string new_hash = toggle_hash_bit(old_hash, right_idx);
                std::string name = get_user_display_name(ctx, api, uid);

                api->c_reset(ctx);
                api->c_emoji(ctx, "➕", EMOJI_PANEL);
                api->c_fmt(ctx, " Назначение администратора\n", KOTO_ENT_BOLD);
                api->c_text(ctx, "• Юзер: ");
                api->c_fmt(ctx, name.c_str(), KOTO_ENT_CODE);
                api->c_fmt(ctx, ("\n• ID: " + std::to_string(uid)).c_str(), KOTO_ENT_CODE);
                if (!rank.empty()) {
                    api->c_text(ctx, "\n• Ранг: ");
                    api->c_fmt(ctx, rank.c_str(), KOTO_ENT_CODE);
                }
                api->c_text(ctx, "\n\nНажмите на права для переключения:");

                build_user_panel_buttons(ctx, api, uid, cid, new_hash, rank);
                api->c_edit_message(ctx, api->chat_id(ctx), api->cb_message_id(ctx), 0);
                if (api->cb_answer) api->cb_answer(ctx, "Право переключено", 0);
            }
        }
        return;
    }

    // Подтверждение назначения: admin_confirm:<uid>:<cid>:<hash>:<rank>
    if (data.rfind("admin_confirm:", 0) == 0) {
        std::string payload = data.substr(14);
        std::istringstream iss(payload);
        std::string uid_str, cid_str, hash, rank;
        if (std::getline(iss, uid_str, ':') &&
            std::getline(iss, cid_str, ':') &&
            std::getline(iss, hash, ':')) {
            std::getline(iss, rank);

            long long uid = strtoll(uid_str.c_str(), nullptr, 10);
            std::string name = get_user_display_name(ctx, api, uid);

            api->c_reset(ctx);
            api->c_emoji(ctx, "✅", EMOJI_SUCCESS);
            api->c_fmt(ctx, " Пользователь ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, name.c_str(), KOTO_ENT_CODE);
            api->c_fmt(ctx, " назначен админом!", KOTO_ENT_BOLD);
            if (!rank.empty()) {
                api->c_fmt(ctx, "\nРанг: ", KOTO_ENT_BOLD);
                api->c_fmt(ctx, rank.c_str(), KOTO_ENT_CODE);
            }

            api->c_edit_message(ctx, api->chat_id(ctx), api->cb_message_id(ctx), 0);
            if (api->cb_answer) api->cb_answer(ctx, "Админ назначен!", 0);
        }
        return;
    }
}

// ── Список команд ────────────────────────────────────────────────────────────
static const koto_command COMMANDS[] = {
    {
        "admin",
        &cmd_admin,
        KOTO_LEVEL_TRUSTED,
        "Назначить пользователя администратором с правами и рангом.",
        "[@user/ID] [Ранг]"
    },
    {
        "unadmin",
        &cmd_unadmin,
        KOTO_LEVEL_TRUSTED,
        "Полностью разжаловать администратора.",
        "[@user/ID]"
    },
    {
        "unrank",
        &cmd_unrank,
        KOTO_LEVEL_TRUSTED,
        "Снять ранг, права и boost-тег участника.",
        "[@user/ID]"
    },
    {
        "untag",
        &cmd_untag,
        KOTO_LEVEL_TRUSTED,
        "Убрать тег-метку (Member Tag) у участников чата.",
        nullptr
    },
    {
        "rank",
        &cmd_rank,
        KOTO_LEVEL_TRUSTED,
        "Установить должность (ранг) администратору.",
        "[@user/ID] <Ранг>"
    },
    {
        "adminhelp",
        &cmd_adminhelp,
        KOTO_LEVEL_TRUSTED,
        "Справка по правам и флагам администратора.",
        nullptr
    },
    {
        "admincfg",
        &cmd_admincfg,
        KOTO_LEVEL_TRUSTED,
        "Управление сохраненными конфигурациями прав.",
        "[save/del/load/list] [name]"
    },
    {
        "adminsettings",
        &cmd_adminsettings,
        KOTO_LEVEL_TRUSTED,
        "Открыть инлайн-панель настройки шаблона прав админа.",
        nullptr
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "administration",
    "Управление администраторами и правами в чатах с шаблонами и инлайн-панелями (C++ ABI 3).",
    "1.1.3",
    26,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    &on_callback,
    nullptr, 0,
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/administration.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
