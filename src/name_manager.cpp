// name_manager.cpp — Управление именем профиля, его историей и аватаром (C++ ABI 3).
// Портирован из Python-модуля name_manager.py (v1.2.0).
// Автор: Kote

#include "module_abi.h"
#include <string>
#include <vector>
#include <sstream>
#include <ctime>
#include <cstring>
#include <algorithm>

static constexpr long long SUCCESS_EMOJI_ID = 5774022692642492953LL; // ✅
static constexpr long long NAME_EMOJI_ID    = 6035084557378654059LL; // 👤
static constexpr long long LIST_EMOJI_ID    = 5956561916573782596LL; // 📋

struct NameEntry {
    std::string name;
    std::string date; // DD.MM.YYYY
};

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

static std::string current_date_str() {
    time_t now = time(nullptr);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d.%02d.%04d", tm_buf.tm_mday, tm_buf.tm_mon + 1, tm_buf.tm_year + 1900);
    return std::string(buf);
}

static std::vector<NameEntry> load_history(koto_ctx* ctx, const koto_api* api) {
    std::vector<NameEntry> out;
    char buf[65536];
    int len = api->db_get(ctx, "name_history", buf, sizeof(buf));
    if (len <= 0) return out;

    std::string s(buf, len);
    std::istringstream iss(s);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        size_t tab = line.find('\t');
        if (tab != std::string::npos) {
            out.push_back({ line.substr(tab + 1), line.substr(0, tab) });
        } else {
            out.push_back({ line, "01.01.2026" });
        }
    }
    return out;
}

static void save_history(koto_ctx* ctx, const koto_api* api, const std::vector<NameEntry>& entries) {
    std::string s;
    for (const auto& e : entries) {
        s += e.date + "\t" + e.name + "\n";
    }
    api->db_set(ctx, "name_history", s.c_str());
}

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

// ─── ИМЯ ────────────────────────────────────────────────────────────────────

static void cmd_name(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::string new_name = trim(api->cmd_args(ctx) ? api->cmd_args(ctx) : "");
    if (new_name.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Укажите новое имя!", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    if (api->set_name) {
        api->set_name(ctx, new_name.c_str(), "");
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "👤", NAME_EMOJI_ID);
    api->c_fmt(ctx, " Имя изменено на ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, new_name.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, "!", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

static void cmd_sname(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    char cur_name_buf[256] = {0};
    if (api->get_me_name && api->get_me_name(ctx, cur_name_buf, sizeof(cur_name_buf)) > 0 && cur_name_buf[0]) {
        // ok
    } else if (api->user_first_name) {
        api->user_first_name(ctx, api->get_me(ctx), cur_name_buf, sizeof(cur_name_buf));
    }

    std::string current_name = cur_name_buf[0] ? cur_name_buf : "User";
    auto history = load_history(ctx, api);

    if (!history.empty() && history.back().name == current_name) {
        api->c_reset(ctx);
        api->c_text(ctx, "ℹ️ Имя ");
        api->c_fmt(ctx, current_name.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, " уже является последним в истории.");
        send_result(ctx, api);
        return;
    }

    history.push_back({ current_name, current_date_str() });
    save_history(ctx, api, history);

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", SUCCESS_EMOJI_ID);
    api->c_fmt(ctx, " Имя ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, current_name.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, " сохранено в историю.", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

static void cmd_lname(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    auto history = load_history(ctx, api);
    api->c_reset(ctx);

    if (history.empty()) {
        api->c_emoji(ctx, "📋", LIST_EMOJI_ID);
        api->c_fmt(ctx, " История имён пуста.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    api->c_emoji(ctx, "📋", LIST_EMOJI_ID);
    api->c_fmt(ctx, " История ваших имён:", KOTO_ENT_BOLD);
    api->c_text(ctx, "\n\n");

    for (size_t i = 0; i < history.size(); ++i) {
        api->c_text(ctx, (std::to_string(i + 1) + ". ").c_str());
        api->c_fmt(ctx, history[i].name.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, (" (сохр. " + history[i].date + ")\n").c_str());
    }

    send_result(ctx, api);
}

static void cmd_dname(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::string arg = trim(api->cmd_args(ctx) ? api->cmd_args(ctx) : "");
    if (arg.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Укажите номер или 'all'.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    auto history = load_history(ctx, api);
    std::string message;

    std::string lower_arg = arg;
    std::transform(lower_arg.begin(), lower_arg.end(), lower_arg.begin(), ::tolower);

    if (lower_arg == "all") {
        api->db_del(ctx, "name_history");
        message = "🗑️ История имён полностью очищена.";
    } else {
        char* end = nullptr;
        long num = strtol(arg.c_str(), &end, 10);
        if (end == arg.c_str() || num < 1 || num > (long)history.size()) {
            if (history.empty()) {
                message = "❌ История имён пуста.";
            } else {
                message = "❌ Неверный номер. Доступно: 1-" + std::to_string(history.size());
            }
        } else {
            std::string name_to_delete = history[num - 1].name;
            history.erase(history.begin() + (num - 1));
            save_history(ctx, api, history);
            message = "🗑️ Удалено имя `" + name_to_delete + "` из истории.";
        }
    }

    api->c_reset(ctx);
    api->c_fmt(ctx, message.c_str(), KOTO_ENT_BOLD);
    send_result(ctx, api);
}

static void cmd_namelist(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    auto history = load_history(ctx, api);
    api->c_reset(ctx);

    if (history.empty()) {
        api->c_emoji(ctx, "📋", LIST_EMOJI_ID);
        api->c_fmt(ctx, " Ваша история имён пуста.", KOTO_ENT_BOLD);
        if (api->c_button_ex) {
            api->c_button_ex(ctx, "❌ Закрыть", "close_panel", 1, KOTO_BTN_DANGER, 0);
        } else {
            api->c_button(ctx, "❌ Закрыть", "close_panel", 1);
        }
    } else {
        api->c_emoji(ctx, "📋", LIST_EMOJI_ID);
        api->c_fmt(ctx, " Выберите имя из списка:", KOTO_ENT_BOLD);

        int total = (int)history.size();
        int start = std::max(0, total - 25);
        for (int i = total - 1; i >= start; --i) {
            std::string btn_label = "👤 " + history[i].name;
            std::string btn_data = "setname:" + std::to_string(i);
            api->c_button(ctx, btn_label.c_str(), btn_data.c_str(), 1);
        }
        if (api->c_button_ex) {
            api->c_button_ex(ctx, "❌ Закрыть", "close_panel", 1, KOTO_BTN_DANGER, 0);
        } else {
            api->c_button(ctx, "❌ Закрыть", "close_panel", 1);
        }
    }

    send_result(ctx, api);
}

// ─── АВАТАР ─────────────────────────────────────────────────────────────────

static void cmd_setava(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    static unsigned char fbuf[16 * 1024 * 1024];
    long long got = api->reply_file ? api->reply_file(ctx, fbuf, sizeof(fbuf)) : -1;
    if (got <= 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Ответьте на фото или отправьте команду вместе с картинкой!", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    char cdir[512] = {0};
    std::string path = "avatar.jpg";
    if (api->cache_dir && api->cache_dir(ctx, cdir, sizeof(cdir)) > 0 && cdir[0]) {
        path = std::string(cdir) + "/avatar.jpg";
    }

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Ошибка записи временного файла аватара.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }
    fwrite(fbuf, 1, (size_t)got, f);
    fclose(f);

    if (api->set_profile_photo) {
        api->set_profile_photo(ctx, path.c_str());
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", SUCCESS_EMOJI_ID);
    api->c_fmt(ctx, " Аватар успешно установлен!", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

static void cmd_delava(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    if (api->delete_profile_photo) {
        api->delete_profile_photo(ctx);
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", SUCCESS_EMOJI_ID);
    api->c_fmt(ctx, " Текущий аватар удалён.", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// ─── ИНЛАЙН / КОЛБЭКИ ───────────────────────────────────────────────────────

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    long long sender = api->sender_id ? api->sender_id(ctx) : 0;
    long long me = api->get_me ? api->get_me(ctx) : 0;

    bool allowed = (me != 0 && sender == me);
    if (!allowed && api->check_permission) {
        allowed = (api->check_permission(ctx, KOTO_LEVEL_TRUSTED) != 0);
    }

    if (!allowed) {
        api->cb_answer(ctx, "⛔ Эта панель только для владельца!", 1);
        return;
    }

    const char* data = api->cb_data(ctx);
    if (!data) return;

    if (strcmp(data, "close_panel") == 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "Панель закрыта.");
        api->c_edit_message(ctx, api->chat_id(ctx), api->cb_message_id(ctx), 0);
        api->cb_answer(ctx, "Панель закрыта.", 0);
        return;
    }

    if (strncmp(data, "setname:", 8) == 0) {
        int record_index = atoi(data + 8);
        auto history = load_history(ctx, api);

        if (record_index < 0 || record_index >= (int)history.size()) {
            api->cb_answer(ctx, "❌ Имя не найдено в истории (возможно, было удалено).", 1);
            return;
        }

        std::string name_to_set = history[record_index].name;
        if (api->set_name) {
            api->set_name(ctx, name_to_set.c_str(), "");
        }

        std::string ans = "✅ Имя изменено на: " + name_to_set;
        api->cb_answer(ctx, ans.c_str(), 1);

        api->c_reset(ctx);
        api->c_emoji(ctx, "✅", SUCCESS_EMOJI_ID);
        api->c_fmt(ctx, " Имя успешно изменено.", KOTO_ENT_BOLD);
        api->c_edit_message(ctx, api->chat_id(ctx), api->cb_message_id(ctx), 0);
    }
}

static const koto_command COMMANDS[] = {
    {
        "name",
        &cmd_name,
        KOTO_LEVEL_TRUSTED,
        "Изменить имя профиля.",
        "<новое_имя>"
    },
    {
        "sname",
        &cmd_sname,
        KOTO_LEVEL_TRUSTED,
        "Сохранить текущее имя в историю.",
        nullptr
    },
    {
        "lname",
        &cmd_lname,
        KOTO_LEVEL_TRUSTED,
        "Показать историю сохранённых имён.",
        nullptr
    },
    {
        "dname",
        &cmd_dname,
        KOTO_LEVEL_TRUSTED,
        "Очистить историю имён.",
        "<номер|all>"
    },
    {
        "namelist",
        &cmd_namelist,
        KOTO_LEVEL_TRUSTED,
        "Показать инлайн-меню с историей имён.",
        nullptr
    },
    {
        "setava",
        &cmd_setava,
        KOTO_LEVEL_TRUSTED,
        "Установить аватар из фото (ответ на фото или документ-изображение).",
        nullptr
    },
    {
        "delava",
        &cmd_delava,
        KOTO_LEVEL_TRUSTED,
        "Удалить текущий аватар профиля.",
        nullptr
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "name_manager",
    "Управление именем профиля, его историей и аватаром (C++ ABI 3).",
    "1.2.1",
    26,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    &on_callback,
    nullptr, 0,
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/name_manager.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
