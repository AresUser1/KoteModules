// whois.cpp — Модуль подробной информации о пользователе и анализа чата (C++ ABI 3).
// Портирован из Python-модуля whois.py (v1.2.0) с расширением анализа чатов.
// Команды, конфликтующие с системными (info, profile, user), удалены.
// Доступные команды: .whois, .whoischat, .chatanalysis.
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace {

static constexpr long long EMO_USER    = 5283097055852503586LL; // 👤
static constexpr long long EMO_CHAT    = 5778661935927004845LL; // 💬
static constexpr long long EMO_STAR    = 5469947168523558652LL; // ⭐
static constexpr long long EMO_SHIELD  = 5470049770997292425LL; // 🛡️
static constexpr long long EMO_BOT     = 5373067498193166118LL; // 🤖

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

// ── Обработчик информации о пользователе (.whois) ───────────────────────────

static void cmd_whois(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? koto_utils::trim(raw) : "";

    long long target_uid = 0;

    if (!args.empty()) {
        if (is_all_digits(args)) {
            try { target_uid = std::stoll(args); } catch (...) { target_uid = 0; }
        } else {
            std::string uname = args;
            if (!uname.empty() && uname[0] == '@') uname = uname.substr(1);
            if (api->resolve) {
                target_uid = api->resolve(ctx, uname.c_str());
            }
        }
    } else {
        if (api->reply_sender_id) {
            target_uid = api->reply_sender_id(ctx);
        }
        if (target_uid == 0 && api->sender_id) {
            target_uid = api->sender_id(ctx);
        }
        if (target_uid == 0 && api->get_me) {
            target_uid = api->get_me(ctx);
        }
    }

    if (target_uid == 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, "Пользователь не найден.\n\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, "📌 Подсказка:\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "• Укажите @username или ID: ");
        api->c_fmt(ctx, ".whois @durov", KOTO_ENT_CODE);
        api->c_text(ctx, "\n• Либо ответьте командой ");
        api->c_fmt(ctx, ".whois", KOTO_ENT_CODE);
        api->c_text(ctx, " на сообщение пользователя.");
        send_result(ctx, api);
        return;
    }

    std::string first_name = "";
    std::string last_name = "";
    std::string username = "";
    std::string display_name = "";
    std::string about = "";
    std::string phone = "";
    bool is_premium = false;
    bool is_bot = false;
    long long stargifts_count = 0;
    long long common_chats_count = 0;

    // Пробуем получить полный JSON через user_full_json
    bool got_full = false;
    if (api->user_full_json) {
        std::vector<char> buf(8192, 0);
        int n = api->user_full_json(ctx, target_uid, buf.data(), static_cast<int>(buf.size()));
        if (n > 0) {
            try {
                auto j = nlohmann::json::parse(std::string(buf.data(), static_cast<size_t>(n)));
                first_name = j.value("first_name", "");
                last_name = j.value("last_name", "");
                username = j.value("username", "");
                display_name = j.value("display_name", "");
                about = j.value("about", "");
                phone = j.value("phone", "");
                is_premium = j.value("is_premium", false);
                is_bot = j.value("is_bot", false);
                stargifts_count = j.value("stargifts_count", 0LL);
                common_chats_count = j.value("common_chats_count", 0LL);
                got_full = true;
            } catch (...) {}
        }
    }

    // Если full json не дал данные, используем точечные API
    if (!got_full) {
        char buf[512] = {0};
        if (api->user_first_name && api->user_first_name(ctx, target_uid, buf, sizeof(buf))) first_name = buf;
        buf[0] = '\0';
        if (api->user_last_name && api->user_last_name(ctx, target_uid, buf, sizeof(buf))) last_name = buf;
        buf[0] = '\0';
        if (api->user_username && api->user_username(ctx, target_uid, buf, sizeof(buf))) username = buf;
        buf[0] = '\0';
        if (api->user_name && api->user_name(ctx, target_uid, buf, sizeof(buf))) display_name = buf;
    }

    if (display_name.empty()) {
        display_name = first_name;
        if (!last_name.empty()) display_name += " " + last_name;
        if (display_name.empty()) display_name = "User " + std::to_string(target_uid);
    }

    // Формируем красивую карточку
    api->c_reset(ctx);
    api->c_emoji(ctx, "👤", EMO_USER);
    api->c_fmt(ctx, " Информация о пользователе:\n\n", KOTO_ENT_BOLD);

    api->c_text(ctx, "🆔 ");
    api->c_fmt(ctx, "ID: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, std::to_string(target_uid).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n");

    api->c_text(ctx, "👤 ");
    api->c_fmt(ctx, "Имя: ", KOTO_ENT_BOLD);
    api->c_text(ctx, (!first_name.empty() ? first_name : "Не указано").c_str());
    api->c_text(ctx, "\n");

    if (!last_name.empty()) {
        api->c_text(ctx, "👤 ");
        api->c_fmt(ctx, "Фамилия: ", KOTO_ENT_BOLD);
        api->c_text(ctx, last_name.c_str());
        api->c_text(ctx, "\n");
    }

    api->c_text(ctx, "🔖 ");
    api->c_fmt(ctx, "Username: ", KOTO_ENT_BOLD);
    if (!username.empty()) {
        api->c_fmt(ctx, ("@" + username).c_str(), KOTO_ENT_CODE);
    } else {
        api->c_text(ctx, "Не указан");
    }
    api->c_text(ctx, "\n");

    // Флаги и статусы
    std::vector<std::string> flags;
    if (is_bot) flags.push_back("🤖 Бот");
    if (is_premium) flags.push_back("🌟 Premium");
    if (target_uid == 777000 || target_uid == 42777) flags.push_back("⚡ Telegram");

    api->c_text(ctx, "🏳️ ");
    api->c_fmt(ctx, "Статус: ", KOTO_ENT_BOLD);
    if (!flags.empty()) {
        std::string flg_str;
        for (size_t i = 0; i < flags.size(); ++i) {
            if (i > 0) flg_str += " | ";
            flg_str += flags[i];
        }
        api->c_fmt(ctx, flg_str.c_str(), KOTO_ENT_ITALIC);
    } else {
        api->c_text(ctx, "Обычный пользователь");
    }
    api->c_text(ctx, "\n");

    if (common_chats_count > 0) {
        api->c_text(ctx, "👥 ");
        api->c_fmt(ctx, "Общих чатов: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, std::to_string(common_chats_count).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n");
    }

    if (stargifts_count > 0) {
        api->c_text(ctx, "⭐ ");
        api->c_fmt(ctx, "Подарков (звёзды): ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, std::to_string(stargifts_count).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n");
    }

    if (!about.empty()) {
        api->c_text(ctx, "\n📝 ");
        api->c_fmt(ctx, "О себе:\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, about.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n");
    }

    api->c_text(ctx, "\n🔗 ");
    std::string profile_link = "tg://user?id=" + std::to_string(target_uid);
    api->c_url(ctx, "Открыть профиль", profile_link.c_str());

    send_result(ctx, api);
}

// ── Обработчик анализа чата (.whoischat / .chatanalysis) ─────────────────────

static void cmd_chat_analysis(koto_ctx* ctx, const koto_api* api) {
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
            if (api->resolve) {
                target_cid = api->resolve(ctx, uname.c_str());
            }
        }
    } else {
        if (api->chat_id) {
            target_cid = api->chat_id(ctx);
        }
    }

    if (target_cid == 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, "Чат не найден.\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "Используйте команду в чате или укажите ID / @username: ");
        api->c_fmt(ctx, ".whoischat -100123456789", KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

    std::string title = "";
    std::string username = "";
    std::string about = "";
    int participants_count = 0;
    int admins_count = 0;
    int pinned_msg_id = 0;
    bool is_channel = false;
    bool is_group = false;
    bool participants_hidden = false;
    bool can_view_participants = true;
    std::vector<long long> admin_ids;
    std::vector<long long> bot_ids;

    bool got_full = false;
    if (api->chat_full_json) {
        std::vector<char> buf(16384, 0);
        int n = api->chat_full_json(ctx, target_cid, buf.data(), static_cast<int>(buf.size()));
        if (n > 0) {
            try {
                auto j = nlohmann::json::parse(std::string(buf.data(), static_cast<size_t>(n)));
                title = j.value("title", "");
                username = j.value("username", "");
                about = j.value("about", "");
                participants_count = j.value("participants_count", 0);
                admins_count = j.value("admins_count", 0);
                pinned_msg_id = j.value("pinned_msg_id", 0);
                is_channel = j.value("is_channel", false);
                is_group = j.value("is_group", false);
                participants_hidden = j.value("participants_hidden", false);
                can_view_participants = j.value("can_view_participants", true);
                if (j.contains("admin_ids")) admin_ids = j.value("admin_ids", std::vector<long long>{});
                if (j.contains("bot_ids")) bot_ids = j.value("bot_ids", std::vector<long long>{});
                got_full = true;
            } catch (...) {}
        }
    }

    int ctype = 0;
    if (api->chat_type) {
        ctype = api->chat_type(ctx);
    }

    std::string type_name = "Чат";
    if (is_channel || ctype == KOTO_CHAT_CHANNEL) type_name = "📢 Канал";
    else if (is_group || ctype == KOTO_CHAT_GROUP) type_name = "👥 Супергруппа / Группа";
    else if (ctype == KOTO_CHAT_PRIVATE) type_name = "👤 Личный диалог";

    if (title.empty()) {
        title = "Чат " + std::to_string(target_cid);
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "📊", EMO_CHAT);
    api->c_fmt(ctx, " Анализ чата: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, title.c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n\n");

    api->c_text(ctx, "🆔 ");
    api->c_fmt(ctx, "Chat ID: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, std::to_string(target_cid).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n");

    api->c_text(ctx, "🏷 ");
    api->c_fmt(ctx, "Тип: ", KOTO_ENT_BOLD);
    api->c_text(ctx, type_name.c_str());
    api->c_text(ctx, "\n");

    if (!username.empty()) {
        api->c_text(ctx, "🔖 ");
        api->c_fmt(ctx, "Username: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ("@" + username).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n");
    }

    if (participants_count > 0) {
        api->c_text(ctx, "👥 ");
        api->c_fmt(ctx, "Всего участников: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, std::to_string(participants_count).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n");
    }

    if (admins_count > 0 || !admin_ids.empty()) {
        int cnt = admins_count > 0 ? admins_count : static_cast<int>(admin_ids.size());
        api->c_text(ctx, "👮‍♂️ ");
        api->c_fmt(ctx, "Администраторов: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, std::to_string(cnt).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n");
    }

    if (!bot_ids.empty()) {
        api->c_text(ctx, "🤖 ");
        api->c_fmt(ctx, "Ботов обнаружено: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, std::to_string(bot_ids.size()).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n");
    }

    if (pinned_msg_id > 0) {
        api->c_text(ctx, "📌 ");
        api->c_fmt(ctx, "Закреплённое сообщение: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ("#" + std::to_string(pinned_msg_id)).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n");
    }

    // Метрики и безопасность
    api->c_text(ctx, "\n🛡️ ");
    api->c_fmt(ctx, "Безопасность и настройки:\n", KOTO_ENT_BOLD);

    api->c_text(ctx, "• Список участников: ");
    if (participants_hidden) {
        api->c_fmt(ctx, "🔒 Скрыт", KOTO_ENT_ITALIC);
    } else {
        api->c_fmt(ctx, "👁 Открыт", KOTO_ENT_ITALIC);
    }
    api->c_text(ctx, "\n");

    api->c_text(ctx, "• Доступ к участникам: ");
    api->c_text(ctx, can_view_participants ? "✅ Есть" : "❌ Ограничен");
    api->c_text(ctx, "\n");

    if (participants_count > 0 && admins_count > 0) {
        double admin_ratio = (static_cast<double>(admins_count) / participants_count) * 100.0;
        char r_buf[32];
        snprintf(r_buf, sizeof(r_buf), "%.1f%%", admin_ratio);
        api->c_text(ctx, "• Доля администрации: ");
        api->c_fmt(ctx, r_buf, KOTO_ENT_CODE);
        api->c_text(ctx, "\n");
    }

    if (!about.empty()) {
        api->c_text(ctx, "\n📝 ");
        api->c_fmt(ctx, "Описание:\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, about.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n");
    }

    send_result(ctx, api);
}

const koto_command COMMANDS[] = {
    {
        "whois",
        &cmd_whois,
        KOTO_LEVEL_TRUSTED,
        "Подробная информация о пользователе (по reply, ID, @username или о себе).",
        "[id|@username]"
    },
    {
        "whoischat",
        &cmd_chat_analysis,
        KOTO_LEVEL_TRUSTED,
        "Анализ чата или канала (участники, админы, боты, тип, права).",
        "[chat_id|@username]"
    },
    {
        "chatanalysis",
        &cmd_chat_analysis,
        KOTO_LEVEL_TRUSTED,
        "Анализ текущего или указанного чата (псевдоним whoischat).",
        "[chat_id|@username]"
    }
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "whois",
    "Подробная информация о пользователях и глубокий анализ чатов (C++ ABI 3).",
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
