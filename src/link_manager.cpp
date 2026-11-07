// KoteModules_repo/src/link_manager.cpp
// Модуль для выдачи ссылок на чаты (C++ ABI 3).
// Автор: Kote. Понятный интерфейс без запутанных аббревиатур.

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cstdint>
#include <ctime>

using json = nlohmann::json;

namespace {

// ── Премиум эмодзи-константы ─────────────────────────────────────────────────
static constexpr long long LINK_EMOJI_ID    = 5963087934696459905LL; // 🔗
static constexpr long long SET_EMOJI_ID     = 6032742198179532882LL; // 📁
static constexpr long long TPL_EMOJI_ID     = 5886330010054168711LL; // 📝
static constexpr long long SUCCESS_EMOJI_ID = 5774022692642492953LL; // ✅
static constexpr long long ERROR_EMOJI_ID   = 5774077015388852135LL; // ❌
static constexpr long long WAIT_EMOJI_ID    = 5891211339170326418LL; // ⏳
static constexpr long long TRASH_EMOJI_ID   = 5879896690210639947LL; // 🗑
static constexpr long long SAVE_EMOJI_ID    = 5877316724830768997LL; // 💾
static constexpr long long LIST_EMOJI_ID    = 5956561916573782596LL; // 📂
static constexpr long long BULLET_EMOJI_ID  = 5980787993139481991LL; // 🔹
static constexpr long long RESET_EMOJI_ID   = 5877410604225924969LL; // 🔄
static constexpr long long CONFIG_EMOJI_ID  = 6032742198179532882LL; // ⚙️
static constexpr long long HELP_EMOJI_ID    = 5886330010054168711LL; // 💡

// ── UTF-8 / UTF-16 конвертация и подсчёт длины ───────────────────────────────
static std::vector<uint16_t> utf8_to_utf16(const std::string& s) {
    std::vector<uint16_t> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp; int len;
        if (c < 0x80)             { cp = c;         len = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F;  len = 2; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F;  len = 3; }
        else if ((c >> 3) == 0x1E){ cp = c & 0x07;  len = 4; }
        else                      { cp = 0xFFFD;    len = 1; }
        for (int k = 1; k < len && i + k < n; ++k)
            cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        i += len;
        if (cp <= 0xFFFF) out.push_back((uint16_t)cp);
        else {
            cp -= 0x10000;
            out.push_back((uint16_t)(0xD800 + (cp >> 10)));
            out.push_back((uint16_t)(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

static int utf16_len(const std::string& s) {
    return (int)utf8_to_utf16(s).size();
}

// ── Сущности форматирования ──────────────────────────────────────────────────
struct EntityItem {
    int type = 0;
    int offset = 0;
    int length = 0;
    long long doc_id = 0;
    std::string url;
};

static void to_json(json& j, const EntityItem& e) {
    j = json{{"type", e.type}, {"offset", e.offset}, {"length", e.length}, {"doc_id", e.doc_id}, {"url", e.url}};
}

static void from_json(const json& j, EntityItem& e) {
    e.type = j.value("type", 0);
    e.offset = j.value("offset", 0);
    e.length = j.value("length", 0);
    e.doc_id = j.value("doc_id", 0LL);
    e.url = j.value("url", "");
}

// ── Структура для замен {name}, {links}, {text} ──────────────────────────────
struct ReplacementItem {
    std::string text;
    std::vector<EntityItem> entities;
};

static std::pair<std::string, std::vector<EntityItem>> apply_replacements(
    std::string text,
    std::vector<EntityItem> entities,
    const std::vector<std::pair<std::string, ReplacementItem>>& reps)
{
    std::vector<EntityItem> final_entities = entities;
    std::sort(final_entities.begin(), final_entities.end(), [](const EntityItem& a, const EntityItem& b) {
        return a.offset < b.offset;
    });

    for (const auto& kv : reps) {
        const std::string& key = kv.first;
        const std::string& insert_text = kv.second.text;
        const std::vector<EntityItem>& insert_entities = kv.second.entities;

        while (true) {
            size_t start_idx = text.find(key);
            if (start_idx == std::string::npos) break;

            size_t end_idx = start_idx + key.length();
            std::string text_before = text.substr(0, start_idx);
            std::string text_after = text.substr(end_idx);

            text = text_before + insert_text + text_after;

            int key_u16 = utf16_len(key);
            int ins_u16 = utf16_len(insert_text);
            int delta = ins_u16 - key_u16;
            int ins_offset_u16 = utf16_len(text_before);

            for (auto& e : final_entities) {
                if (e.offset >= ins_offset_u16 + key_u16) {
                    e.offset += delta;
                } else if (e.offset >= ins_offset_u16 && e.offset < ins_offset_u16 + key_u16) {
                    e.offset = ins_offset_u16 + ins_u16;
                } else if (e.offset + e.length > ins_offset_u16) {
                    e.length += delta;
                }
            }

            for (const auto& ie : insert_entities) {
                EntityItem ne = ie;
                ne.offset += ins_offset_u16;
                final_entities.push_back(ne);
            }

            std::sort(final_entities.begin(), final_entities.end(), [](const EntityItem& a, const EntityItem& b) {
                return a.offset < b.offset;
            });
        }
    }

    return {text, final_entities};
}

// ── Вспомогательные функции работы с базой данных модуля ──────────────────────
static json get_db_json(const koto_api* api, koto_ctx* ctx, const std::string& key, const json& def = json::object()) {
    std::string s = koto_utils::db_get_str(api, ctx, key, "");
    if (s.empty()) return def;
    try {
        return json::parse(s);
    } catch (...) {
        return def;
    }
}

static void set_db_json(const koto_api* api, koto_ctx* ctx, const std::string& key, const json& j) {
    koto_utils::db_set_str(api, ctx, key, j.dump());
}

static std::string get_cmd_prefix(const koto_api* api, koto_ctx* ctx) {
    char buf[32] = {0};
    int n = api->setting_get(ctx, "prefix", buf, sizeof(buf));
    return (n > 0 && buf[0]) ? std::string(buf) : ".";
}

static std::vector<std::string> split_words(const std::string& str) {
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

// ── Интерактивное меню настроек категории .lset cfg ──────────────────────────
static void render_cfg_panel(const koto_api* api, koto_ctx* ctx, const std::string& category, bool is_callback, long long msg_id = 0) {
    json all_settings = get_db_json(api, ctx, "category_settings", json::object());
    json cat_cfg = all_settings.value(category, json::object());

    int limit = cat_cfg.value("limit", 0);
    int time_h = cat_cfg.value("time", 0);
    bool approve = cat_cfg.value("approve", false);

    std::string lim_str = (limit > 0) ? (std::to_string(limit) + " чел.") : "Без лимита (∞)";
    std::string tim_str = (time_h > 0) ? (std::to_string(time_h) + " ч.") : "Бессрочно (∞)";
    std::string app_str = approve ? "Включено ✅" : "Выключено ❌";

    api->c_reset(ctx);
    api->c_emoji(ctx, "⚙️", CONFIG_EMOJI_ID);
    api->c_fmt(ctx, " Настройки категории чатов: «", KOTO_ENT_BOLD);
    api->c_fmt(ctx, category.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, "»\n\n", KOTO_ENT_BOLD);

    api->c_text(ctx, "👥 ");
    api->c_fmt(ctx, "Лимит переходов: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, lim_str.c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " (макс. участников по ссылке)\n");

    api->c_text(ctx, "⏳ ");
    api->c_fmt(ctx, "Срок действия: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, tim_str.c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " (время жизни ссылки до сгорания)\n");

    api->c_text(ctx, "🛡 ");
    api->c_fmt(ctx, "Заявки на вход: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, (app_str + "\n\n").c_str(), KOTO_ENT_CODE);

    api->c_fmt(ctx, "💡 Нажимайте кнопки ниже для быстрого переключения параметров:", KOTO_ENT_ITALIC);

    // Кнопки
    std::string btn_lim = "👥 Лимит: " + ((limit > 0) ? (std::to_string(limit) + " чел") : "∞");
    std::string btn_tim = "⏳ Срок: " + ((time_h > 0) ? (std::to_string(time_h) + "ч") : "∞");
    std::string btn_app = "🛡 Заявки: " + std::string(approve ? "Вкл ✅" : "Выкл ❌");

    api->c_button(ctx, btn_lim.c_str(), ("lset:lim:" + category).c_str(), 1);
    api->c_button(ctx, btn_tim.c_str(), ("lset:tim:" + category).c_str(), 0);
    api->c_button(ctx, btn_app.c_str(), ("lset:tapp:" + category).c_str(), 1);
    api->c_button(ctx, "❌ Закрыть панель", "lset:close", 1);

    if (is_callback && msg_id != 0) {
        api->c_edit_message(ctx, api->chat_id(ctx), msg_id, 0);
    } else {
        api->c_edit(ctx, 0);
    }
}

// ── Подробная понятная справка .linkhelp / .lhelp ─────────────────────────────
static void cmd_linkhelp(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    std::string p = get_cmd_prefix(api, ctx);

    api->c_reset(ctx);
    api->c_emoji(ctx, "🔗", LINK_EMOJI_ID);
    api->c_fmt(ctx, " Link Manager — Система выдачи ссылок\n\n", KOTO_ENT_BOLD);

    api->c_fmt(ctx, "📌 Основные понятия (без путаницы):\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "• ");
    api->c_fmt(ctx, "Категория", KOTO_ENT_BOLD);
    api->c_text(ctx, " — это группа/набор чатов (например: «арены», «клан», «турнир»).\n• ");
    api->c_fmt(ctx, "Шаблон", KOTO_ENT_BOLD);
    api->c_text(ctx, " — заготовка текста сообщения с переменными.\n• ");
    api->c_fmt(ctx, "Тег", KOTO_ENT_BOLD);
    api->c_text(ctx, " — название команды или метка (например «NaVi»), чтобы позже сбросить ссылки именно для этой команды.\n\n");

    api->c_fmt(ctx, "🧩 Переменные в шаблонах:\n", KOTO_ENT_BOLD);
    api->c_fmt(ctx, "{name}", KOTO_ENT_CODE);
    api->c_text(ctx, " — кликабельное имя игрока/пользователя.\n");
    api->c_fmt(ctx, "{links}", KOTO_ENT_CODE);
    api->c_text(ctx, " — список сгенерированных ссылок на чаты категории.\n");
    api->c_fmt(ctx, "{text}", KOTO_ENT_CODE);
    api->c_text(ctx, " — переданный текст/тег команды (сохраняет стили и эмодзи).\n\n");

    api->c_fmt(ctx, "🎮 Команды категорий чатов:\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "• ");
    api->c_fmt(ctx, (p + "lset add <категория> [id_чата]").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — добавить чат в категорию (без ID добавит текущий чат).\n• ");
    api->c_fmt(ctx, (p + "lset rm <категория> [id_чата]").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — удалить чат (без ID удалит всю категорию).\n• ");
    api->c_fmt(ctx, (p + "lset list [категория]").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — показать список категорий и чатов в них.\n• ");
    api->c_fmt(ctx, (p + "lset cfg <категория>").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — меню настроек (кнопками задать лимит людей, время и заявки).\n\n");

    api->c_fmt(ctx, "📝 Команды шаблонов сообщений:\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "• ");
    api->c_fmt(ctx, (p + "ltpl save <имя> [текст]").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — сохранить шаблон (текстом или реплаем на оформленное сообщение).\n• ");
    api->c_fmt(ctx, (p + "ltpl view <имя>").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — предпросмотр шаблона с оформлением.\n• ");
    api->c_fmt(ctx, (p + "ltpl list").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — список сохранённых шаблонов.\n• ");
    api->c_fmt(ctx, (p + "ltpl del <имя>").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — удалить шаблон.\n\n");

    api->c_fmt(ctx, "🚀 Выдача и сброс ссылок:\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "• ");
    api->c_fmt(ctx, (p + "link [юзер] <категория> <шаблон> [название/тег]").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — выдать ссылки.\n• ");
    api->c_fmt(ctx, (p + "linkreset <категория> [тег]").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " — аннулировать ссылки (все в категории или только с указанным тегом).\n\n");

    api->c_fmt(ctx, "💡 Быстрый пример:\n", KOTO_ENT_BOLD);
    int mark = api->c_mark(ctx);
    api->c_text(ctx, (p + "lset add арены\n").c_str());
    api->c_text(ctx, (p + "ltpl save инвайт Привет, {name}! Ссылки для команды {text}:\n{links}\n").c_str());
    api->c_text(ctx, (p + "link @player арены инвайт NaVi").c_str());
    api->c_wrap(ctx, mark, KOTO_ENT_BLOCKQUOTE, 1);

    api->c_edit(ctx, 0);
}

// ── Обработчик команды .lset ────────────────────────────────────────────────
static void cmd_lset(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::string args_str = api->cmd_args(ctx) ? api->cmd_args(ctx) : "";
    std::vector<std::string> args = split_words(args_str);
    std::string prefix = get_cmd_prefix(api, ctx);

    if (args.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
        api->c_fmt(ctx, " Использование: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (prefix + "lset <add/rm/list/cfg> [категория] [id_чата]").c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n\n💡 Введите ");
        api->c_fmt(ctx, (prefix + "linkhelp").c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, " для подробной инструкции.");
        api->c_edit(ctx, 0);
        return;
    }

    std::string action = args[0];
    std::transform(action.begin(), action.end(), action.begin(), ::tolower);

    // Поддержка синонимов на русском и английском
    if (action == "добавить") action = "add";
    else if (action == "удалить" || action == "del") action = "rm";
    else if (action == "список") action = "list";
    else if (action == "настройки" || action == "настройка" || action == "config") action = "cfg";

    std::string category = (args.size() > 1) ? args[1] : "";
    std::transform(category.begin(), category.end(), category.begin(), ::tolower);

    std::string target_id_input = (args.size() > 2) ? args[2] : "";

    json sets = get_db_json(api, ctx, "sets", json::object());

    // 1. Конфигурация (.lset cfg <категория> [limit] [time_h] [approve])
    if (action == "cfg") {
        if (category.empty()) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
            api->c_fmt(ctx, " Укажите название категории чатов.\nПример: ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, (prefix + "lset cfg арены").c_str(), KOTO_ENT_CODE);
            api->c_edit(ctx, 0);
            return;
        }
        if (!sets.contains(category)) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
            api->c_fmt(ctx, (" Категория «" + category + "» не найдена. Сначала создайте её через: ").c_str(), KOTO_ENT_BOLD);
            api->c_fmt(ctx, (prefix + "lset add " + category).c_str(), KOTO_ENT_CODE);
            api->c_edit(ctx, 0);
            return;
        }

        // Если переданы параметры напрямую в строке
        if (args.size() >= 4) {
            int lim = std::atoi(args[2].c_str());
            int tim = std::atoi(args[3].c_str());
            bool app = (args.size() >= 5) && (args[4] == "1" || args[4] == "true" || args[4] == "yes" || args[4] == "+");
            json all_settings = get_db_json(api, ctx, "category_settings", json::object());
            all_settings[category] = {{"limit", lim}, {"time", tim}, {"approve", app}};
            set_db_json(api, ctx, "category_settings", all_settings);
        }

        render_cfg_panel(api, ctx, category, false);
        return;
    }

    // 2. Список категорий (.lset list [категория])
    if (action == "list") {
        if (sets.empty()) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "📂", LIST_EMOJI_ID);
            api->c_fmt(ctx, " Список категорий пуст.\nДобавьте чат командой: ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, (prefix + "lset add <категория>").c_str(), KOTO_ENT_CODE);
            api->c_edit(ctx, 0);
            return;
        }

        api->c_reset(ctx);
        api->c_emoji(ctx, "📂", LIST_EMOJI_ID);
        api->c_fmt(ctx, " Категории чатов и настройки:\n\n", KOTO_ENT_BOLD);

        json all_settings = get_db_json(api, ctx, "category_settings", json::object());

        std::vector<std::string> cats_to_show;
        if (!category.empty()) {
            if (sets.contains(category)) cats_to_show.push_back(category);
        } else {
            for (auto it = sets.begin(); it != sets.end(); ++it) {
                cats_to_show.push_back(it.key());
            }
        }

        for (const auto& cat : cats_to_show) {
            auto chat_ids = sets.value(cat, json::array());
            json cat_cfg = all_settings.value(cat, json::object());
            int lim = cat_cfg.value("limit", 0);
            int hours = cat_cfg.value("time", 0);
            bool approve = cat_cfg.value("approve", false);

            std::string settings_str;
            std::vector<std::string> parts;
            if (lim > 0) parts.push_back("👥 Лимит: " + std::to_string(lim));
            else parts.push_back("👥 Без лимита");
            if (hours > 0) parts.push_back("⏳ Срок: " + std::to_string(hours) + "ч");
            else parts.push_back("⏳ Бессрочно");
            if (approve) parts.push_back("🛡 Заявки: Вкл");

            settings_str = " [";
            for (size_t p = 0; p < parts.size(); ++p) {
                if (p > 0) settings_str += " | ";
                settings_str += parts[p];
            }
            settings_str += "]";

            api->c_emoji(ctx, "🔹", BULLET_EMOJI_ID);
            api->c_fmt(ctx, (" «" + cat + "» ").c_str(), KOTO_ENT_BOLD);
            std::string cnt_str = "(" + std::to_string(chat_ids.size()) + " чатов)";
            api->c_fmt(ctx, cnt_str.c_str(), KOTO_ENT_CODE);
            api->c_fmt(ctx, settings_str.c_str(), KOTO_ENT_CODE);
            api->c_text(ctx, "\n");

            // Цитата со списком чатов
            int mark = api->c_mark(ctx);
            if (chat_ids.empty()) {
                api->c_text(ctx, "В категории пока нет чатов.\n");
            } else {
                for (const auto& cid_val : chat_ids) {
                    long long cid = cid_val.get<long long>();
                    std::string j = koto_utils::get_chat_full_json(api, ctx, cid);
                    std::string title;
                    try {
                        auto j_obj = json::parse(j);
                        title = j_obj.value("title", "");
                    } catch (...) {}
                    if (title.empty()) title = "Чат " + std::to_string(cid);
                    std::string line = "• " + title + " (" + std::to_string(cid) + ")\n";
                    api->c_text(ctx, line.c_str());
                }
            }
            api->c_wrap(ctx, mark, KOTO_ENT_BLOCKQUOTE, 1);
            api->c_text(ctx, "\n\n");
        }

        api->c_edit(ctx, 0);
        return;
    }

    if (category.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
        api->c_fmt(ctx, " Укажите название категории чатов.\nПример: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (prefix + "lset " + action + " арены").c_str(), KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    // 3. Добавление чата (.lset add <категория> [id_чата])
    if (action == "add") {
        long long target_id = target_id_input.empty() ? api->chat_id(ctx) : std::stoll(target_id_input);
        if (!sets.contains(category)) {
            sets[category] = json::array();
        }
        auto& arr = sets[category];
        bool exists = false;
        for (const auto& el : arr) {
            if (el.get<long long>() == target_id) { exists = true; break; }
        }
        api->c_reset(ctx);
        if (!exists) {
            arr.push_back(target_id);
            set_db_json(api, ctx, "sets", sets);
            api->c_emoji(ctx, "✅", SUCCESS_EMOJI_ID);
            api->c_fmt(ctx, " Чат успешно добавлен в категорию «", KOTO_ENT_BOLD);
            api->c_fmt(ctx, category.c_str(), KOTO_ENT_CODE);
            api->c_fmt(ctx, "» (ID: ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, std::to_string(target_id).c_str(), KOTO_ENT_CODE);
            api->c_fmt(ctx, ").", KOTO_ENT_BOLD);
        } else {
            api->c_emoji(ctx, "ℹ️", LINK_EMOJI_ID);
            api->c_text(ctx, (" Этот чат уже находится в категории «" + category + "».").c_str());
        }
        api->c_edit(ctx, 0);
        return;
    }

    // 4. Удаление чата или всей категории (.lset rm <категория> [id_чата])
    if (action == "rm") {
        api->c_reset(ctx);
        if (!sets.contains(category)) {
            api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
            api->c_fmt(ctx, (" Категория «" + category + "» не существует.").c_str(), KOTO_ENT_BOLD);
            api->c_edit(ctx, 0);
            return;
        }

        if (!target_id_input.empty()) {
            long long target_id = std::stoll(target_id_input);
            auto& arr = sets[category];
            auto it = std::find(arr.begin(), arr.end(), target_id);
            if (it != arr.end()) {
                arr.erase(it);
                if (arr.empty()) sets.erase(category);
                set_db_json(api, ctx, "sets", sets);
                api->c_emoji(ctx, "🗑️", TRASH_EMOJI_ID);
                api->c_fmt(ctx, " Чат удалён из категории «", KOTO_ENT_BOLD);
                api->c_fmt(ctx, category.c_str(), KOTO_ENT_CODE);
                api->c_fmt(ctx, "».", KOTO_ENT_BOLD);
            } else {
                api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
                api->c_fmt(ctx, (" Чата " + std::to_string(target_id) + " нет в категории «" + category + "».").c_str(), KOTO_ENT_BOLD);
            }
        } else {
            sets.erase(category);
            set_db_json(api, ctx, "sets", sets);

            json all_settings = get_db_json(api, ctx, "category_settings", json::object());
            if (all_settings.contains(category)) {
                all_settings.erase(category);
                set_db_json(api, ctx, "category_settings", all_settings);
            }

            api->c_emoji(ctx, "🗑️", TRASH_EMOJI_ID);
            api->c_fmt(ctx, " Категория «", KOTO_ENT_BOLD);
            api->c_fmt(ctx, category.c_str(), KOTO_ENT_CODE);
            api->c_fmt(ctx, "» со всеми настройками удалена.", KOTO_ENT_BOLD);
        }
        api->c_edit(ctx, 0);
        return;
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
    api->c_fmt(ctx, " Неизвестное действие: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, action.c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, ". Доступно: add, rm, list, cfg.");
    api->c_edit(ctx, 0);
}

// ── Обработчик команды .ltpl ────────────────────────────────────────────────
static void cmd_ltpl(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::string args_str = api->cmd_args(ctx) ? api->cmd_args(ctx) : "";
    std::vector<std::string> args = split_words(args_str);
    std::string prefix = get_cmd_prefix(api, ctx);

    if (args.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
        api->c_fmt(ctx, " Использование: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (prefix + "ltpl <save/del/list/view> [имя_шаблона] [текст]").c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n\n💡 Введите ");
        api->c_fmt(ctx, (prefix + "linkhelp").c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, " для примеров шаблонов.");
        api->c_edit(ctx, 0);
        return;
    }

    std::string action = args[0];
    std::transform(action.begin(), action.end(), action.begin(), ::tolower);

    if (action == "сохранить") action = "save";
    else if (action == "удалить" || action == "rm") action = "del";
    else if (action == "список") action = "list";
    else if (action == "показать" || action == "просмотр") action = "view";

    json templates = get_db_json(api, ctx, "templates", json::object());

    // 1. Сохранение шаблона (.ltpl save <имя> [текст])
    if (action == "save") {
        if (args.size() < 2) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
            api->c_fmt(ctx, " Укажите имя шаблона.\nПример: ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, (prefix + "ltpl save инвайт Привет, {name}! Вот ваши ссылки:\n{links}").c_str(), KOTO_ENT_CODE);
            api->c_edit(ctx, 0);
            return;
        }

        std::string name = args[1];
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

        std::string text_content;
        std::vector<EntityItem> entities;

        size_t name_pos = args_str.find(args[1]);
        if (name_pos != std::string::npos) {
            size_t after_name = name_pos + args[1].length();
            if (after_name < args_str.length()) {
                text_content = koto_utils::trim(args_str.substr(after_name));
            }
        }

        if (!text_content.empty()) {
            std::string full_msg = api->msg_text(ctx) ? api->msg_text(ctx) : "";
            size_t start_byte = full_msg.find(text_content);
            if (start_byte != std::string::npos) {
                int start_u16 = utf16_len(full_msg.substr(0, start_byte));
                int ent_count = api->msg_entity_count(ctx);
                for (int i = 0; i < ent_count; ++i) {
                    int type = 0, off = 0, len = 0;
                    long long doc = 0;
                    char url_buf[512] = {0};
                    if (api->msg_entity(ctx, i, &type, &off, &len, &doc, url_buf, sizeof(url_buf))) {
                        if (off >= start_u16) {
                            entities.push_back({type, off - start_u16, len, doc, std::string(url_buf)});
                        }
                    }
                }
            }
        } else {
            const char* rt = api->reply_text(ctx);
            if (rt && rt[0]) {
                text_content = rt;
                int r_count = api->reply_entity_count(ctx);
                for (int i = 0; i < r_count; ++i) {
                    int type = 0, off = 0, len = 0;
                    long long doc = 0;
                    char url_buf[512] = {0};
                    if (api->reply_entity(ctx, i, &type, &off, &len, &doc, url_buf, sizeof(url_buf))) {
                        entities.push_back({type, off, len, doc, std::string(url_buf)});
                    }
                }
            }
        }

        if (text_content.empty()) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
            api->c_fmt(ctx, " Укажите текст шаблона в команде или ответьте на оформленное сообщение.", KOTO_ENT_BOLD);
            api->c_edit(ctx, 0);
            return;
        }

        json tpl_obj;
        tpl_obj["text"] = text_content;
        tpl_obj["entities"] = entities;
        templates[name] = tpl_obj;
        set_db_json(api, ctx, "templates", templates);

        api->c_reset(ctx);
        api->c_emoji(ctx, "💾", SAVE_EMOJI_ID);
        api->c_fmt(ctx, " Шаблон «", KOTO_ENT_BOLD);
        api->c_fmt(ctx, name.c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, "» успешно сохранён со всеми стилями и эмодзи!", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    // 2. Удаление шаблона (.ltpl del <имя>)
    if (action == "del") {
        if (args.size() < 2) return;
        std::string name = args[1];
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        api->c_reset(ctx);
        if (templates.contains(name)) {
            templates.erase(name);
            set_db_json(api, ctx, "templates", templates);
            api->c_emoji(ctx, "🗑️", TRASH_EMOJI_ID);
            api->c_fmt(ctx, (" Шаблон «" + name + "» удалён.").c_str(), KOTO_ENT_BOLD);
        } else {
            api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
            api->c_fmt(ctx, (" Шаблон «" + name + "» не найден.").c_str(), KOTO_ENT_BOLD);
        }
        api->c_edit(ctx, 0);
        return;
    }

    // 3. Список шаблонов (.ltpl list)
    if (action == "list") {
        api->c_reset(ctx);
        if (templates.empty()) {
            api->c_emoji(ctx, "📝", TPL_EMOJI_ID);
            api->c_fmt(ctx, " Список сохранённых шаблонов пуст.\nСоздайте шаблон: ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, (prefix + "ltpl save <имя> <текст>").c_str(), KOTO_ENT_CODE);
            api->c_edit(ctx, 0);
            return;
        }
        api->c_emoji(ctx, "📝", TPL_EMOJI_ID);
        api->c_fmt(ctx, " Сохранённые шаблоны текстов:\n\n", KOTO_ENT_BOLD);
        for (auto it = templates.begin(); it != templates.end(); ++it) {
            api->c_text(ctx, "• ");
            api->c_fmt(ctx, it.key().c_str(), KOTO_ENT_CODE);
            api->c_text(ctx, "\n");
        }
        api->c_edit(ctx, 0);
        return;
    }

    // 4. Просмотр шаблона (.ltpl view <имя>)
    if (action == "view") {
        if (args.size() < 2) return;
        std::string name = args[1];
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (!templates.contains(name)) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
            api->c_fmt(ctx, (" Шаблон «" + name + "» не найден.").c_str(), KOTO_ENT_BOLD);
            api->c_edit(ctx, 0);
            return;
        }

        json tpl_data = templates[name];
        std::string t_str = tpl_data.value("text", "");
        std::vector<EntityItem> ents;
        if (tpl_data.contains("entities")) {
            ents = tpl_data["entities"].get<std::vector<EntityItem>>();
        }

        api->c_reset(ctx);
        api->c_fmt(ctx, ("Предпросмотр шаблона «" + name + "»:\n\n").c_str(), KOTO_ENT_BOLD);
        int base_mark = api->c_mark(ctx);
        api->c_text(ctx, t_str.c_str());
        for (const auto& e : ents) {
            api->c_entity(ctx, e.type, base_mark + e.offset, e.length, e.doc_id, e.url.c_str());
        }
        api->c_edit(ctx, 0);
        return;
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
    api->c_fmt(ctx, " Неизвестное действие: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, action.c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, ". Доступно: save, del, list, view.");
    api->c_edit(ctx, 0);
}

// ── Обработчик команды .link ────────────────────────────────────────────────
static void cmd_link(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::string args_str = api->cmd_args(ctx) ? api->cmd_args(ctx) : "";
    std::vector<std::string> args = split_words(args_str);
    std::string prefix = get_cmd_prefix(api, ctx);

    if (args.size() < 2) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
        api->c_fmt(ctx, " Использование: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (prefix + "link [юзер] <категория> <шаблон> [текст/тег]").c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n\n💡 Пример: ");
        api->c_fmt(ctx, (prefix + "link @player арены инвайт NaVi").c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\nГде «арены» — категория чатов, «инвайт» — имя шаблона, «NaVi» — название команды.");
        api->c_edit(ctx, 0);
        return;
    }

    long long target_user_id = 0;
    std::string category_name;
    std::string template_name;
    std::string custom_text_val;
    size_t custom_text_start_arg = 0;

    std::string first_arg = args[0];
    long long resolved_uid = 0;

    if (first_arg[0] == '@') {
        resolved_uid = api->resolve(ctx, first_arg.c_str());
    } else {
        char* end = nullptr;
        long long maybe_id = std::strtoll(first_arg.c_str(), &end, 10);
        if (end && *end == '\0' && maybe_id > 0) {
            resolved_uid = maybe_id;
        }
    }

    if (resolved_uid != 0) {
        target_user_id = resolved_uid;
        if (args.size() < 3) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
            api->c_fmt(ctx, " Укажите категорию чатов и шаблон текста.\nПример: ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, (prefix + "link " + first_arg + " арены инвайт NaVi").c_str(), KOTO_ENT_CODE);
            api->c_edit(ctx, 0);
            return;
        }
        category_name = args[1];
        template_name = args[2];
        custom_text_start_arg = 3;
    } else {
        category_name = args[0];
        template_name = args[1];
        custom_text_start_arg = 2;

        long long reply_uid = api->reply_sender_id(ctx);
        if (reply_uid != 0) {
            target_user_id = reply_uid;
        } else if (api->chat_type(ctx) == 0 && api->chat_id(ctx) > 0) {
            target_user_id = api->chat_id(ctx);
        } else {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
            api->c_fmt(ctx, " Укажите получателя (@username, ID или ответом на сообщение).", KOTO_ENT_BOLD);
            api->c_edit(ctx, 0);
            return;
        }
    }

    std::transform(category_name.begin(), category_name.end(), category_name.begin(), ::tolower);
    std::transform(template_name.begin(), template_name.end(), template_name.begin(), ::tolower);

    // Извлечение custom_text и его форматирования
    std::vector<EntityItem> custom_text_entities;
    if (custom_text_start_arg < args.size()) {
        size_t pos = args_str.find(args[custom_text_start_arg]);
        if (pos != std::string::npos) {
            custom_text_val = args_str.substr(pos);
            std::string full_msg = api->msg_text(ctx) ? api->msg_text(ctx) : "";
            size_t start_byte = full_msg.find(custom_text_val);
            if (start_byte != std::string::npos) {
                int start_u16 = utf16_len(full_msg.substr(0, start_byte));
                int ent_count = api->msg_entity_count(ctx);
                for (int i = 0; i < ent_count; ++i) {
                    int type = 0, off = 0, len = 0;
                    long long doc = 0;
                    char url_buf[512] = {0};
                    if (api->msg_entity(ctx, i, &type, &off, &len, &doc, url_buf, sizeof(url_buf))) {
                        if (off >= start_u16) {
                            custom_text_entities.push_back({type, off - start_u16, len, doc, std::string(url_buf)});
                        }
                    }
                }
            }
        }
    }

    json sets = get_db_json(api, ctx, "sets", json::object());
    json templates = get_db_json(api, ctx, "templates", json::object());
    json all_cat_settings = get_db_json(api, ctx, "category_settings", json::object());
    json cat_settings = all_cat_settings.value(category_name, json::object());

    int usage_limit = cat_settings.value("limit", 0);
    int expire_hours = cat_settings.value("time", 0);
    bool request_needed = cat_settings.value("approve", false);
    int expire_date = (expire_hours > 0) ? (int)(std::time(nullptr) + (expire_hours * 3600)) : 0;

    api->c_reset(ctx);
    if (!sets.contains(category_name)) {
        api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
        api->c_fmt(ctx, (" Категория чатов «" + category_name + "» не найдена.\nСоздайте её: ").c_str(), KOTO_ENT_BOLD);
        api->c_fmt(ctx, (prefix + "lset add " + category_name).c_str(), KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }
    if (!templates.contains(template_name)) {
        api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
        api->c_fmt(ctx, (" Шаблон «" + template_name + "» не найден.\nСоздайте его: ").c_str(), KOTO_ENT_BOLD);
        api->c_fmt(ctx, (prefix + "ltpl save " + template_name + " <текст>").c_str(), KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    auto chat_ids = sets[category_name];
    if (chat_ids.empty()) {
        api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
        api->c_fmt(ctx, (" В категории «" + category_name + "» пока нет добавленных чатов.").c_str(), KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    // Сообщение ожидания
    api->c_emoji(ctx, "⏳", WAIT_EMOJI_ID);
    api->c_fmt(ctx, " Генерирую ссылки...", KOTO_ENT_ITALIC);
    api->c_edit(ctx, 0);

    json active_links = get_db_json(api, ctx, "active_links", json::object());
    if (!active_links.contains(category_name)) {
        active_links[category_name] = json::array();
    }

    std::string links_full_text;
    std::vector<EntityItem> links_full_entities;
    std::vector<std::string> failed_chats;
    int current_offset_u16 = 0;

    for (const auto& cid_val : chat_ids) {
        long long cid = cid_val.get<long long>();

        std::string j_chat = koto_utils::get_chat_full_json(api, ctx, cid);
        std::string title = "Чат " + std::to_string(cid);
        std::string username;
        try {
            auto j_obj = json::parse(j_chat);
            std::string t = j_obj.value("title", "");
            if (!t.empty()) title = t;
            username = j_obj.value("username", "");
        } catch (...) {}

        std::string link;

        std::string err_desc;
        if (!username.empty() && usage_limit == 0 && expire_date == 0 && !request_needed) {
            link = "https://t.me/" + username;
        } else {
            json exp_req;
            exp_req["chat_id"] = cid;
            if (usage_limit > 0) exp_req["usage_limit"] = usage_limit;
            if (expire_date > 0) exp_req["expire_date"] = expire_date;
            if (request_needed) exp_req["request_needed"] = true;
            if (!custom_text_val.empty()) {
                // Telegram max title length is 32 characters
                std::string t_cut;
                size_t idx = 0, n_bytes = custom_text_val.size(), cnt = 0;
                while (idx < n_bytes && cnt < 32) {
                    unsigned char c = (unsigned char)custom_text_val[idx];
                    size_t l = 1;
                    if ((c >> 5) == 0x6) l = 2;
                    else if ((c >> 4) == 0xE) l = 3;
                    else if ((c >> 3) == 0x1E) l = 4;
                    if (idx + l > n_bytes) break;
                    t_cut.append(custom_text_val, idx, l);
                    idx += l;
                    cnt++;
                }
                exp_req["title"] = t_cut;
            }

            std::vector<char> res_buf(4096, 0);
            int n = api->host_call_json(ctx, "chat.export_invite", exp_req.dump().c_str(), res_buf.data(), (int)res_buf.size());
            if (n > 0) {
                try {
                    auto res_j = json::parse(std::string(res_buf.data(), n));
                    if (res_j.value("ok", false) && res_j.contains("link")) {
                        link = res_j["link"].get<std::string>();
                        active_links[category_name].push_back({
                            {"chat_id", cid},
                            {"link", link},
                            {"tag", custom_text_val}
                        });
                    } else if (res_j.contains("error")) {
                        err_desc = res_j["error"].get<std::string>();
                    }
                } catch (...) {}
            }
        }

        std::string line_prefix = "• ";
        std::string line_text = line_prefix + title + "\n";
        int title_u16 = utf16_len(title);
        int prefix_u16 = utf16_len(line_prefix);

        if (!link.empty()) {
            links_full_entities.push_back({
                KOTO_ENT_URL,
                current_offset_u16 + prefix_u16,
                title_u16,
                0LL,
                link
            });
        } else {
            std::string reason = "ошибка создания";
            if (err_desc.find("INVITES_TOO_MUCH") != std::string::npos) reason = "превышен лимит 100 ссылок в чате";
            else if (err_desc.find("CHAT_ADMIN_REQUIRED") != std::string::npos) reason = "нет прав администратора";
            else if (!err_desc.empty()) reason = err_desc;
            failed_chats.push_back(title + " — " + reason);
        }

        links_full_text += line_text;
        current_offset_u16 += utf16_len(line_text);
    }

    set_db_json(api, ctx, "active_links", active_links);

    // Имя целевого пользователя
    char name_buf[256] = {0};
    int nn = api->user_name(ctx, target_user_id, name_buf, sizeof(name_buf));
    std::string target_name = (nn > 0 && name_buf[0]) ? std::string(name_buf) : ("Пользователь " + std::to_string(target_user_id));

    json tpl_data = templates[template_name];
    std::string tpl_text = tpl_data.value("text", "");
    std::vector<EntityItem> tpl_entities;
    if (tpl_data.contains("entities")) {
        tpl_entities = tpl_data["entities"].get<std::vector<EntityItem>>();
    }

    std::vector<std::pair<std::string, ReplacementItem>> reps;

    // 1. {name}
    EntityItem name_ent;
    name_ent.type = KOTO_ENT_URL;
    name_ent.offset = 0;
    name_ent.length = utf16_len(target_name);
    name_ent.url = "tg://user?id=" + std::to_string(target_user_id);
    reps.push_back({"{name}", {target_name, {name_ent}}});

    // 2. {links}
    reps.push_back({"{links}", {links_full_text, links_full_entities}});

    // 3. {text}
    reps.push_back({"{text}", {custom_text_val, custom_text_entities}});

    auto [final_text, final_entities] = apply_replacements(tpl_text, tpl_entities, reps);

    api->delete_msg(ctx);

    api->c_reset(ctx);
    api->c_text(ctx, final_text.c_str());
    for (const auto& e : final_entities) {
        api->c_entity(ctx, e.type, e.offset, e.length, e.doc_id, e.url.c_str());
    }
    api->c_send(ctx, api->chat_id(ctx), KOTO_NO_LINK_PREVIEW);

    if (!failed_chats.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "⚠️", ERROR_EMOJI_ID);
        api->c_fmt(ctx, " Внимание: не удалось создать ссылки для некоторых чатов:\n\n", KOTO_ENT_BOLD);
        for (const auto& w : failed_chats) {
            api->c_text(ctx, ("• " + w + "\n").c_str());
        }
        api->c_text(ctx, "\n💡 Если исчерпан лимит ссылок (макс. 100 на чат), используйте команду ");
        std::string p = get_cmd_prefix(api, ctx);
        api->c_fmt(ctx, (p + "linkreset " + category_name).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, " чтобы аннулировать старые ссылки.");
        api->c_send(ctx, api->chat_id(ctx), KOTO_NO_LINK_PREVIEW);
    }
}

static void cmd_linkreset(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::string args_str = api->cmd_args(ctx) ? api->cmd_args(ctx) : "";
    std::vector<std::string> args = split_words(args_str);
    std::string prefix = get_cmd_prefix(api, ctx);

    if (args.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
        api->c_fmt(ctx, " Использование: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (prefix + "linkreset <категория> [тег/команда]").c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n\n• Без тега — аннулирует ВСЕ ссылки в выбранной категории.\n• С тегом — аннулирует только выданные для конкретного тега (названия команды).");
        api->c_edit(ctx, 0);
        return;
    }

    std::string category = args[0];
    std::transform(category.begin(), category.end(), category.begin(), ::tolower);
    std::string target_tag = (args.size() > 1) ? args[1] : "";

    json active_links = get_db_json(api, ctx, "active_links", json::object());

    if (!active_links.contains(category) || active_links[category].empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "ℹ️", LINK_EMOJI_ID);
        api->c_fmt(ctx, (" Нет активных ссылок для категории «" + category + "».").c_str(), KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    std::vector<json> to_revoke;
    std::vector<json> to_keep;

    for (const auto& item : active_links[category]) {
        std::string tag = item.value("tag", "");
        if (!target_tag.empty()) {
            if (tag == target_tag) to_revoke.push_back(item);
            else to_keep.push_back(item);
        } else {
            to_revoke.push_back(item);
        }
    }

    if (to_revoke.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "ℹ️", LINK_EMOJI_ID);
        api->c_fmt(ctx, (" Не найдено ссылок с тегом «" + target_tag + "» в категории «" + category + "».").c_str(), KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "⏳", WAIT_EMOJI_ID);
    api->c_fmt(ctx, (" Аннулирую " + std::to_string(to_revoke.size()) + " ссылок...").c_str(), KOTO_ENT_ITALIC);
    api->c_edit(ctx, 0);

    int count = 0;
    for (const auto& item : to_revoke) {
        long long cid = item.value("chat_id", 0LL);
        std::string link = item.value("link", "");
        if (cid != 0 && !link.empty()) {
            json rev_req;
            rev_req["chat_id"] = cid;
            rev_req["link"] = link;
            rev_req["revoked"] = true;

            std::vector<char> res_buf(1024, 0);
            int n = api->host_call_json(ctx, "chat.revoke_invite", rev_req.dump().c_str(), res_buf.data(), (int)res_buf.size());
            if (n > 0) count++;
        }
    }

    active_links[category] = to_keep;
    set_db_json(api, ctx, "active_links", active_links);

    std::string tag_info = !target_tag.empty() ? (" для команды «" + target_tag + "»") : " (все в категории)";
    api->c_reset(ctx);
    api->c_emoji(ctx, "🔄", RESET_EMOJI_ID);
    api->c_fmt(ctx, (" Успешно аннулировано: " + std::to_string(count) + " ссылок" + tag_info + ".").c_str(), KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

// ── Обработчик Callback-кнопок (.lset cfg) ──────────────────────────────────
static void on_callback(koto_ctx* ctx, const koto_api* api) {
    const char* d = api->cb_data(ctx);
    if (!d) return;
    std::string data(d);

    if (data == "lset:close") {
        api->delete_msg(ctx);
        return;
    }

    // lset:tapp:<category>
    if (data.rfind("lset:tapp:", 0) == 0) {
        std::string cat = data.substr(10);
        json all_settings = get_db_json(api, ctx, "category_settings", json::object());
        if (!all_settings.contains(cat)) all_settings[cat] = json::object();
        bool cur = all_settings[cat].value("approve", false);
        all_settings[cat]["approve"] = !cur;
        set_db_json(api, ctx, "category_settings", all_settings);
        render_cfg_panel(api, ctx, cat, true, api->cb_message_id(ctx));
        return;
    }

    // lset:lim:<category> -> цикл лимитов: 0 (∞) -> 1 -> 2 -> 5 -> 10 -> 25 -> 50 -> 0
    if (data.rfind("lset:lim:", 0) == 0) {
        std::string cat = data.substr(9);
        json all_settings = get_db_json(api, ctx, "category_settings", json::object());
        if (!all_settings.contains(cat)) all_settings[cat] = json::object();
        int cur = all_settings[cat].value("limit", 0);
        static const int limits[] = {0, 1, 2, 5, 10, 25, 50};
        int next_lim = 1;
        for (size_t i = 0; i < sizeof(limits)/sizeof(limits[0]); ++i) {
            if (limits[i] == cur) {
                next_lim = limits[(i + 1) % (sizeof(limits)/sizeof(limits[0]))];
                break;
            }
        }
        all_settings[cat]["limit"] = next_lim;
        set_db_json(api, ctx, "category_settings", all_settings);
        render_cfg_panel(api, ctx, cat, true, api->cb_message_id(ctx));
        return;
    }

    // lset:tim:<category> -> цикл времени: 0 (Вечно) -> 1ч -> 6ч -> 12ч -> 24ч -> 48ч -> 72ч -> 168ч -> 0
    if (data.rfind("lset:tim:", 0) == 0) {
        std::string cat = data.substr(9);
        json all_settings = get_db_json(api, ctx, "category_settings", json::object());
        if (!all_settings.contains(cat)) all_settings[cat] = json::object();
        int cur = all_settings[cat].value("time", 0);
        static const int times[] = {0, 1, 6, 12, 24, 48, 72, 168};
        int next_tim = 1;
        for (size_t i = 0; i < sizeof(times)/sizeof(times[0]); ++i) {
            if (times[i] == cur) {
                next_tim = times[(i + 1) % (sizeof(times)/sizeof(times[0]))];
                break;
            }
        }
        all_settings[cat]["time"] = next_tim;
        set_db_json(api, ctx, "category_settings", all_settings);
        render_cfg_panel(api, ctx, cat, true, api->cb_message_id(ctx));
        return;
    }
}

// ── Регистрация команд модуля ────────────────────────────────────────────────
static const koto_command COMMANDS[] = {
    {
        "lset",
        &cmd_lset,
        KOTO_LEVEL_TRUSTED,
        "Управление категориями чатов для выдачи ссылок.",
        "<add/rm/list/cfg> [категория] [id_чата]"
    },
    {
        "linkset",
        &cmd_lset,
        KOTO_LEVEL_TRUSTED,
        "Синоним команды .lset (управление категориями чатов).",
        "<add/rm/list/cfg> [категория] [id_чата]"
    },
    {
        "ltpl",
        &cmd_ltpl,
        KOTO_LEVEL_TRUSTED,
        "Управление шаблонами сообщений с ссылками.",
        "<save/del/list/view> [имя_шаблона] [текст]"
    },
    {
        "linktpl",
        &cmd_ltpl,
        KOTO_LEVEL_TRUSTED,
        "Синоним команды .ltpl (управление шаблонами).",
        "<save/del/list/view> [имя_шаблона] [текст]"
    },
    {
        "link",
        &cmd_link,
        KOTO_LEVEL_TRUSTED,
        "Сгенерировать и отправить ссылки на чаты.",
        "[юзер] <категория> <шаблон> [текст/тег]"
    },
    {
        "linkreset",
        &cmd_linkreset,
        KOTO_LEVEL_TRUSTED,
        "Аннулировать активные ссылки по категории и тегу.",
        "<категория> [тег/команда]"
    },
    {
        "linkhelp",
        &cmd_linkhelp,
        KOTO_LEVEL_TRUSTED,
        "Подробная инструкция по выдаче ссылок, категориям и шаблонам.",
        ""
    },
    {
        "lhelp",
        &cmd_linkhelp,
        KOTO_LEVEL_TRUSTED,
        "Синоним .linkhelp (быстрая справка по модулю).",
        ""
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "link_manager",
    "Модуль для выдачи ссылок на чаты. Управление категориями, шаблонами сообщений, лимитами переходов и сроками ссылок.",
    "2.6.0",
    60,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr,
    0,
    &on_callback,
    nullptr,
    0,
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/link_manager.so"
};

} // namespace

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
