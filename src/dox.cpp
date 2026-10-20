// dox.cpp — Интеграция с Dox-ботами (C++ ABI 3).
// Точный (1:1) порт оригинального модуля dox.py.
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <regex>
#include <cctype>
#include <ctime>

namespace {

static constexpr long long EMO_PROCESS = 5386367538735104399LL; // ⌛️
static constexpr long long EMO_SUCCESS = 5206607081334906820LL; // ✔️
static constexpr long long EMO_FAIL    = 5210952531676504517LL; // ❌
static constexpr long long EMO_CHECK   = 5980930633298350051LL; // ✅
static constexpr long long DEFAULT_DOX_BOT_ID = 7940282943LL;   // @Pribavkakzpbot

// Вспомогательная функция для перевода UTF-8 (включая кириллицу) и ASCII в нижний регистр
static std::string utf8_tolower(const std::string& str) {
    std::string res;
    res.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        if (c < 0x80) {
            res += static_cast<char>(tolower(c));
        } else if (c == 0xD0 && i + 1 < str.size()) {
            unsigned char c2 = static_cast<unsigned char>(str[i + 1]);
            if (c2 >= 0x90 && c2 <= 0x9F) { // А-П -> а-п
                res += static_cast<char>(0xD0);
                res += static_cast<char>(c2 + 0x20);
                i++;
            } else if (c2 >= 0xA0 && c2 <= 0xAF) { // Р-Я -> р-я
                res += static_cast<char>(0xD1);
                res += static_cast<char>(c2 - 0x20);
                i++;
            } else if (c2 == 0x81) { // Ё -> ё
                res += static_cast<char>(0xD1);
                res += static_cast<char>(0x91);
                i++;
            } else {
                res += str[i];
            }
        } else {
            res += str[i];
        }
    }
    return res;
}

// Точное извлечение номера телефона (1:1 алгоритм из dox.py)
static std::string extract_phone(const std::string& text, long long target_id) {
    std::string phone_number;
    std::string target_id_str = std::to_string(target_id);

    // 1. Поиск по ключевым словам (основной метод из dox.py)
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        std::string llower = utf8_tolower(line);

        bool has_key = (
            llower.find("телефон") != std::string::npos ||
            llower.find("phone") != std::string::npos ||
            llower.find("номер") != std::string::npos ||
            line.find("📞") != std::string::npos
        );

        if (has_key) {
            // Удаляем всё кроме цифр и плюса (re.sub(r'[^\d+]', '', line))
            std::string digits_only;
            for (char c : line) {
                if (isdigit(static_cast<unsigned char>(c)) || c == '+') {
                    digits_only += c;
                }
            }
            // Ищем последовательность от 7 до 15 цифр
            std::regex reg(R"((\+?\d{7,15}))");
            std::smatch m;
            if (std::regex_search(digits_only, m, reg)) {
                phone_number = m[1].str();
                break;
            }
        }
    }

    // 2. Запасной поиск (Fallback из dox.py): Ищем любые длинные числа, кроме ID цели
    if (phone_number.empty()) {
        std::string clean_text;
        for (char c : text) {
            if (c != ' ' && c != '-' && c != '(' && c != ')') {
                clean_text += c;
            }
        }
        std::regex reg_all(R"((\d{7,15}))");
        auto words_begin = std::sregex_iterator(clean_text.begin(), clean_text.end(), reg_all);
        auto words_end = std::sregex_iterator();
        for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
            std::string num = (*i)[1].str();
            if (num != target_id_str) {
                phone_number = num;
                break;
            }
        }
    }

    return phone_number;
}

// Рекурсивный поиск callback_data кнопки Telegram в структуре кнопок
static void search_tg_button(const nlohmann::json& node, std::string& out_data) {
    if (!out_data.empty()) return;

    if (node.is_object()) {
        if (node.contains("text") && node["text"].is_string()) {
            std::string t = node["text"].get<std::string>();
            std::string tl = utf8_tolower(t);

            if (tl.find("telegram") != std::string::npos ||
                tl.find("телеграм") != std::string::npos ||
                tl == "тг" || tl == "tg") {

                if (node.contains("data") && node["data"].is_string()) {
                    out_data = node["data"].get<std::string>();
                    return;
                }
                if (node.contains("callback_data") && node["callback_data"].is_string()) {
                    out_data = node["callback_data"].get<std::string>();
                    return;
                }
            }
        }
        for (auto it = node.begin(); it != node.end(); ++it) {
            search_tg_button(it.value(), out_data);
            if (!out_data.empty()) return;
        }
    } else if (node.is_array()) {
        for (auto& elem : node) {
            search_tg_button(elem, out_data);
            if (!out_data.empty()) return;
        }
    }
}

static std::string find_telegram_button(const std::string& buttons_json) {
    if (buttons_json.empty() || buttons_json == "[]") return "";
    try {
        auto j = nlohmann::json::parse(buttons_json);
        std::string data;
        search_tg_button(j, data);
        return data;
    } catch (...) {}
    return "";
}

// ── .setdoxbot <@bot> (1:1 из dox.py) ──────────────────────────────────────────

static void cmd_setdoxbot(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) return;

    const char* raw = api->cmd_args(ctx);
    std::string bot_username = raw ? koto_utils::trim(raw) : "";

    char pbuf[32] = ".";
    api->setting_get(ctx, "prefix", pbuf, sizeof(pbuf));
    std::string prefix = pbuf;

    if (bot_username.empty() || bot_username[0] != '@') {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMO_FAIL);
        api->c_fmt(ctx, " Ошибка: Укажите юзернейм вашего Dox-бота.", KOTO_ENT_BOLD);
        api->c_text(ctx, "\nПример: ");
        api->c_fmt(ctx, (prefix + "setdoxbot @MyDoxBot").c_str(), KOTO_ENT_CODE);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0); else api->c_reply(ctx, 0);
        return;
    }

    std::string lower_bot = bot_username;
    for (char& c : lower_bot) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

    if (lower_bot == "@koteuserbotdoxbot") {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMO_FAIL);
        api->c_fmt(ctx, " Ошибка: Нельзя установить официального бота.", KOTO_ENT_BOLD);
        api->c_text(ctx, "\nСоздайте своего собственного через @KoteUserBotDoxbot.");
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0); else api->c_reply(ctx, 0);
        return;
    }

    api->db_set(ctx, "dox_bot_username", bot_username.c_str());

    long long bot_uid = 0;
    if (api->resolve) {
        std::string uname = bot_username.substr(1);
        bot_uid = api->resolve(ctx, uname.c_str());
        if (bot_uid != 0) {
            koto_utils::db_set_int64(api, ctx, "dox_bot_id", bot_uid);
        }
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", EMO_CHECK);
    api->c_fmt(ctx, " Dox-бот успешно установлен на: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, bot_username.c_str(), KOTO_ENT_CODE);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0); else api->c_reply(ctx, 0);
}

// ── .dox [@user/ID] (1:1 из dox.py) ───────────────────────────────────────────

static void cmd_dox(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) return;

    char bot_buf[256] = {0};
    int n = api->db_get(ctx, "dox_bot_username", bot_buf, sizeof(bot_buf) - 1);
    std::string dox_bot_username = (n > 0) ? bot_buf : "";

    char pbuf[32] = ".";
    api->setting_get(ctx, "prefix", pbuf, sizeof(pbuf));
    std::string prefix = pbuf;

    if (dox_bot_username.empty()) {
        dox_bot_username = "@Pribavkakzpbot";
        api->db_set(ctx, "dox_bot_username", dox_bot_username.c_str());
    }

    long long target_id = 0;
    if (api->reply_sender_id) target_id = api->reply_sender_id(ctx);

    const char* raw = api->cmd_args(ctx);
    std::string arg = raw ? koto_utils::trim(raw) : "";
    if (target_id == 0 && !arg.empty()) {
        char* end = nullptr;
        long long val = strtoll(arg.c_str(), &end, 10);
        if (*end == '\0' && val > 0) target_id = val;
        else if (arg[0] == '@' && api->resolve) {
            target_id = api->resolve(ctx, arg.c_str() + 1);
        }
    }

    if (target_id == 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMO_FAIL);
        api->c_fmt(ctx, " Ошибка: Не удалось найти пользователя.", KOTO_ENT_BOLD);
        api->c_text(ctx, "\nУкажите @/ID или ответьте на сообщение.");
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0); else api->c_reply(ctx, 0);
        return;
    }

    // Резолв целевого бота
    long long bot_uid = koto_utils::db_get_int64(api, ctx, "dox_bot_id", 0);
    if (bot_uid == 0 && api->resolve) {
        std::string uname = dox_bot_username;
        if (uname[0] == '@') uname = uname.substr(1);
        bot_uid = api->resolve(ctx, uname.c_str());
        if (bot_uid != 0) {
            koto_utils::db_set_int64(api, ctx, "dox_bot_id", bot_uid);
        }
    }
    if (bot_uid == 0) {
        bot_uid = DEFAULT_DOX_BOT_ID;
        koto_utils::db_set_int64(api, ctx, "dox_bot_id", bot_uid);
    }

    long long cur_chat = api->chat_id(ctx);
    if (cur_chat == 0 && api->get_me) cur_chat = api->get_me(ctx);
    long long cur_msg = api->msg_id ? api->msg_id(ctx) : 0;

    // Сохраняем состояние ожидания
    koto_utils::db_set_int64(api, ctx, "dox_target", target_id);
    koto_utils::db_set_int64(api, ctx, "dox_origin_chat", cur_chat);
    koto_utils::db_set_int64(api, ctx, "dox_origin_msg", cur_msg);
    koto_utils::db_set_int64(api, ctx, "dox_state", 1); // 1 = ждём меню кнопок
    koto_utils::db_set_int64(api, ctx, "dox_start_t", time(nullptr));

    // 1. Точное сообщение из dox.py:
    // self._build_emoji("process") + " Начинаю поиск по ID: " + target_id
    api->c_reset(ctx);
    api->c_emoji(ctx, "⌛️", EMO_PROCESS);
    api->c_fmt(ctx, " Начинаю поиск по ID: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, std::to_string(target_id).c_str(), KOTO_ENT_CODE);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0); else api->c_reply(ctx, 0);

    // 2. Отправляем ID цели боту
    api->c_reset(ctx);
    api->c_text(ctx, std::to_string(target_id).c_str());
    api->c_send(ctx, bot_uid, 0);
}

// ── Watcher: реакция на ответы бота (1:1 логика dox.py) ───────────────────────

static int on_watch(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) return 0;

    long long state = koto_utils::db_get_int64(api, ctx, "dox_state", 0);
    if (state == 0) return 0;

    long long start_t = koto_utils::db_get_int64(api, ctx, "dox_start_t", 0);
    long long origin_chat = koto_utils::db_get_int64(api, ctx, "dox_origin_chat", 0);
    long long origin_msg = koto_utils::db_get_int64(api, ctx, "dox_origin_msg", 0);
    long long target_id = koto_utils::db_get_int64(api, ctx, "dox_target", 0);

    // Таймаут 30 секунд (как в dox.py: timeout=30)
    if (time(nullptr) - start_t > 30) {
        koto_utils::db_set_int64(api, ctx, "dox_state", 0);
        char bot_buf[256] = {0};
        api->db_get(ctx, "dox_bot_username", bot_buf, sizeof(bot_buf) - 1);
        std::string dox_bot_username = bot_buf[0] ? bot_buf : "@DoxBot";

        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMO_FAIL);
        api->c_fmt(ctx, " Ошибка: Dox-бот ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, dox_bot_username.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, " не ответил в течение 30 секунд.");

        if (origin_chat != 0 && origin_msg != 0 && api->c_edit_message) {
            api->c_edit_message(ctx, origin_chat, origin_msg, 0);
        } else if (origin_chat != 0) {
            api->c_send(ctx, origin_chat, 0);
        }
        return 0;
    }

    long long sender = api->sender_id(ctx);
    long long chat_id = api->chat_id(ctx);
    long long bot_id = koto_utils::db_get_int64(api, ctx, "dox_bot_id", 0);
    if (bot_id == 0) bot_id = DEFAULT_DOX_BOT_ID;

    const char* raw_txt = api->msg_text(ctx);
    std::string text = raw_txt ? raw_txt : "";
    long long msg_id = api->msg_id ? api->msg_id(ctx) : 0;

    // Извлекаем инлайн-кнопки
    char bbuf[8192] = {0};
    int blen = api->msg_buttons_json ? api->msg_buttons_json(ctx, bbuf, sizeof(bbuf) - 1) : 0;
    std::string bjson = (blen > 0) ? bbuf : "";
    std::string tg_btn_data = find_telegram_button(bjson);

    bool is_dox_content = (
        !tg_btn_data.empty() ||
        text.find("Выберите направление для поиска") != std::string::npos ||
        text.find("Обнаружен идентификатор") != std::string::npos ||
        text.find("💬 ID:") != std::string::npos ||
        text.find("📞 Телефон:") != std::string::npos ||
        text.find("История изменения имени") != std::string::npos ||
        text.find("лимит запросов") != std::string::npos
    );

    bool from_dox_bot = (bot_id != 0 && (sender == bot_id || chat_id == bot_id));
    if (!from_dox_bot && is_dox_content) {
        from_dox_bot = true;
        bot_id = sender ? sender : chat_id;
        koto_utils::db_set_int64(api, ctx, "dox_bot_id", bot_id);
    }

    if (!from_dox_bot) return 0;

    // ── Проверка на лимит запросов (check_for_limit из dox.py) ──
    std::string text_lower = utf8_tolower(text);

    if (text_lower.find("лимит запросов") != std::string::npos) {
        std::string time_str = "неизвестно";
        std::regex reg_time(R"(через\s+([\d\s:]+))");
        std::smatch tm;
        if (std::regex_search(text, tm, reg_time)) {
            time_str = koto_utils::trim(tm[1].str());
        }

        api->c_reset(ctx);
        api->c_fmt(ctx, "⚠️ Лимит запросов исчерпан.", KOTO_ENT_BOLD);
        api->c_text(ctx, ("\nНовые запросы будут доступны через " + time_str + ".").c_str());

        if (origin_chat != 0 && origin_msg != 0 && api->c_edit_message) {
            api->c_edit_message(ctx, origin_chat, origin_msg, 0);
        } else if (origin_chat != 0) {
            api->c_send(ctx, origin_chat, 0);
        }
        koto_utils::db_set_int64(api, ctx, "dox_state", 0);
        return 1;
    }

    // ── ШАГ 1: Бот прислал меню с кнопками ("Выберите направление для поиска") ──
    if (state == 1) {
        // Проверяем кнопки в истории, если текущее сообщение без кнопок
        if (tg_btn_data.empty() && api->get_history_json) {
            char hbuf[16384] = {0};
            int hlen = api->get_history_json(ctx, bot_id ? bot_id : chat_id, 0, 3, 0, hbuf, sizeof(hbuf) - 1);
            if (hlen > 0) {
                try {
                    auto hist = nlohmann::json::parse(hbuf);
                    for (auto& m : hist) {
                        if (m.contains("buttons")) {
                            std::string cand = find_telegram_button(m["buttons"].dump());
                            if (!cand.empty()) {
                                tg_btn_data = cand;
                                if (m.contains("id")) msg_id = m["id"].get<long long>();
                                break;
                            }
                        }
                    }
                } catch (...) {}
            }
        }

        if (!tg_btn_data.empty()) {
            if (api->click_button) {
                api->click_button(ctx, bot_id ? bot_id : chat_id, msg_id, tg_btn_data.c_str());
            }
            koto_utils::db_set_int64(api, ctx, "dox_state", 2); // Ожидаем ответное досье с телефоном
            return 1;
        }

        // Если бот вернул результат сразу (без кнопок)
        bool has_immediate_result = (text.find("Телефон") != std::string::npos ||
                                     text.find("📞") != std::string::npos ||
                                     text.find("💬 ID:") != std::string::npos);
        if (!has_immediate_result) {
            // Если пришли кнопки, но кнопки 'Telegram' нет
            if (bjson.size() > 5) {
                api->c_reset(ctx);
                api->c_emoji(ctx, "❌", EMO_FAIL);
                api->c_fmt(ctx, " Ошибка: Dox-бот не предложил кнопку 'Telegram' для поиска.", KOTO_ENT_BOLD);
                if (origin_chat != 0 && origin_msg != 0 && api->c_edit_message) {
                    api->c_edit_message(ctx, origin_chat, origin_msg, 0);
                } else if (origin_chat != 0) {
                    api->c_send(ctx, origin_chat, 0);
                }
                koto_utils::db_set_int64(api, ctx, "dox_state", 0);
                return 1;
            }
            return 0;
        }
    }

    // ── ШАГ 2: Бот прислал результат поиска (извлечение номера 1:1 как в dox.py) ──
    std::string phone = extract_phone(text, target_id);

    // Если номер найден:
    // self._build_emoji("success") + " Номер телефона: " + phone_number
    if (!phone.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "✔️", EMO_SUCCESS);
        api->c_fmt(ctx, " Номер телефона: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, phone.c_str(), KOTO_ENT_CODE);

        if (origin_chat != 0 && origin_msg != 0 && api->c_edit_message) {
            api->c_edit_message(ctx, origin_chat, origin_msg, 0);
        } else if (origin_chat != 0) {
            api->c_send(ctx, origin_chat, 0);
        }
        koto_utils::db_set_int64(api, ctx, "dox_state", 0);
        return 1;
    }

    // Если номер не найден в результате поиска:
    // self._build_emoji("fail") + " Номер телефона не найден в результате поиска."
    bool is_conclusive = (
        state == 2 ||
        text.find("не найден") != std::string::npos ||
        text.find("История изменения") != std::string::npos ||
        text.find("💬 ID:") != std::string::npos ||
        text.find("Интересовались") != std::string::npos
    );

    if (is_conclusive) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMO_FAIL);
        api->c_fmt(ctx, " Номер телефона не найден в результате поиска.", KOTO_ENT_BOLD);

        if (origin_chat != 0 && origin_msg != 0 && api->c_edit_message) {
            api->c_edit_message(ctx, origin_chat, origin_msg, 0);
        } else if (origin_chat != 0) {
            api->c_send(ctx, origin_chat, 0);
        }
        koto_utils::db_set_int64(api, ctx, "dox_state", 0);
        return 1;
    }

    return 0;
}

} // namespace

static const koto_command COMMANDS[] = {
    {
        "setdoxbot",
        &cmd_setdoxbot,
        KOTO_LEVEL_OWNER,
        "Устанавливает вашего Dox-бота (1:1 как в dox.py).",
        "<@username_бота>"
    },
    {
        "dox",
        &cmd_dox,
        KOTO_LEVEL_OWNER,
        "Ищет информацию о пользователе (1:1 как в dox.py).",
        "[@user/ID]"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "dox",
    "Интеграция с Dox-ботами: автовыбор Telegram и поиск номера (C++ ABI 3).",
    "1.0.9",
    28,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    &on_watch,
    KOTO_WATCH_INCOMING,
    nullptr,
    nullptr, 0,
    nullptr
};

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register(void) {
    return &MODULE;
}
