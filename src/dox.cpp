// dox.cpp — Интеграция с Dox-ботами с автоматическим выбором кнопок и извлечением данных (C++ ABI 3).
// Портирован и расширен из Python-модуля dox.py.
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <cctype>
#include <ctime>

namespace {

static constexpr long long EMO_PROCESS = 5386367538735104399LL; // ⌛️
static constexpr long long EMO_SUCCESS = 5206607081334906820LL; // ✔️
static constexpr long long EMO_FAIL    = 5210952531676504517LL; // ❌
static constexpr long long DEFAULT_DOX_BOT_ID = 7940282943LL; // @Pribavkakzpbot

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

// Извлечение номера телефона из текста (по меткам строки или по формату)
static std::string extract_phone(const std::string& text) {
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        std::string llower;
        for (char c : line) llower += static_cast<char>(tolower(static_cast<unsigned char>(c)));

        if (llower.find("телефон") != std::string::npos ||
            llower.find("phone") != std::string::npos ||
            llower.find("номер") != std::string::npos ||
            line.find("📞") != std::string::npos) {

            std::string digits;
            for (char c : line) {
                if (isdigit(static_cast<unsigned char>(c)) || (c == '+' && digits.empty())) {
                    digits += c;
                }
            }
            if (digits.size() >= 10 && digits.size() <= 16) {
                return digits;
            }
        }
    }

    // Fallback: ищем последовательность 10-15 цифр рядом со словами телефон/номер
    size_t pos = 0;
    while (pos < text.size()) {
        if (text[pos] == '+' || isdigit(static_cast<unsigned char>(text[pos]))) {
            size_t start = pos;
            std::string cand;
            if (text[pos] == '+') { cand += '+'; pos++; }
            while (pos < text.size() && (isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == ' ' || text[pos] == '-' || text[pos] == '(' || text[pos] == ')')) {
                if (isdigit(static_cast<unsigned char>(text[pos]))) cand += text[pos];
                pos++;
            }
            if (cand.size() >= 10 && cand.size() <= 16) {
                size_t ctx_start = (start > 40) ? start - 40 : 0;
                std::string ctx_snippet = text.substr(ctx_start, pos - ctx_start + 20);
                std::string slow;
                for (char c : ctx_snippet) slow += static_cast<char>(tolower(static_cast<unsigned char>(c)));
                if (slow.find("телефон") != std::string::npos || slow.find("phone") != std::string::npos ||
                    slow.find("📞") != std::string::npos || slow.find("номер") != std::string::npos) {
                    return cand;
                }
            }
        } else {
            pos++;
        }
    }
    return "";
}

// Рекурсивный поиск callback_data кнопки Telegram в 1D и 2D массивах JSON
static void search_tg_button(const nlohmann::json& node, std::string& out_data) {
    if (!out_data.empty()) return;

    if (node.is_object()) {
        if (node.contains("text") && node["text"].is_string()) {
            std::string t = node["text"].get<std::string>();
            std::string tl;
            for (char c : t) tl += static_cast<char>(tolower(static_cast<unsigned char>(c)));

            if (tl.find("telegram") != std::string::npos ||
                tl.find("телеграм") != std::string::npos ||
                tl == "тг" || tl == "tg" || tl.find("тг") != std::string::npos) {

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

// ── .setdoxbot [@bot] ─────────────────────────────────────────────────────────

static void cmd_setdoxbot(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) return;

    const char* raw = api->cmd_args(ctx);
    std::string bot_username = raw ? koto_utils::trim(raw) : "";

    char pbuf[32] = ".";
    api->setting_get(ctx, "prefix", pbuf, sizeof(pbuf));
    std::string prefix = pbuf;

    long long cur_chat = api->chat_id(ctx);

    // Если аргумент пустой — привязываем текущий чат как Dox-бота
    if (bot_username.empty()) {
        if (cur_chat != 0) {
            koto_utils::db_set_int64(api, ctx, "dox_bot_id", cur_chat);
            api->c_reset(ctx);
            api->c_emoji(ctx, "✔️", EMO_SUCCESS);
            api->c_fmt(ctx, " Текущий чат (ID ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, std::to_string(cur_chat).c_str(), KOTO_ENT_CODE);
            api->c_fmt(ctx, ") сохранён как Dox-бот.", KOTO_ENT_BOLD);
            send_result(ctx, api);
            return;
        }

        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMO_FAIL);
        api->c_fmt(ctx, " Ошибка: Укажите юзернейм вашего Dox-бота.", KOTO_ENT_BOLD);
        api->c_text(ctx, "\nПример: ");
        api->c_fmt(ctx, (prefix + "setdoxbot @Pribavkakzpbot").c_str(), KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

    if (bot_username[0] != '@') {
        bot_username = "@" + bot_username;
    }

    std::string lower_bot = bot_username;
    for (char& c : lower_bot) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

    if (lower_bot == "@koteuserbotdoxbot") {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMO_FAIL);
        api->c_fmt(ctx, " Ошибка: Нельзя установить официального бота.", KOTO_ENT_BOLD);
        api->c_text(ctx, "\nСоздайте своего собственного бота через @KoteUserBotDoxbot.");
        send_result(ctx, api);
        return;
    }

    api->db_set(ctx, "dox_bot_username", bot_username.c_str());

    // Резолвим и сохраняем ID бота
    long long bot_uid = 0;
    if (api->resolve) {
        std::string uname = bot_username.substr(1);
        bot_uid = api->resolve(ctx, uname.c_str());
        if (bot_uid != 0) {
            koto_utils::db_set_int64(api, ctx, "dox_bot_id", bot_uid);
        }
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "✔️", EMO_SUCCESS);
    api->c_fmt(ctx, " Dox-бот успешно настроен: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, bot_username.c_str(), KOTO_ENT_CODE);
    send_result(ctx, api);
}

// ── .dox [@user/ID] ───────────────────────────────────────────────────────────

static void cmd_dox(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) return;

    char bot_buf[256] = {0};
    int n = api->db_get(ctx, "dox_bot_username", bot_buf, sizeof(bot_buf) - 1);
    std::string dox_bot = (n > 0) ? bot_buf : "";

    if (dox_bot.empty()) {
        dox_bot = "@Pribavkakzpbot"; // Дефолтный бот
        api->db_set(ctx, "dox_bot_username", dox_bot.c_str());
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
        api->c_text(ctx, "\nУкажите @юзернейм, ID или ответьте на сообщение.");
        send_result(ctx, api);
        return;
    }

    // Резолв целевого бота
    long long bot_uid = koto_utils::db_get_int64(api, ctx, "dox_bot_id", 0);
    if (bot_uid == 0 && api->resolve) {
        std::string uname = dox_bot;
        if (uname[0] == '@') uname = uname.substr(1);
        bot_uid = api->resolve(ctx, uname.c_str());
        if (bot_uid != 0) {
            koto_utils::db_set_int64(api, ctx, "dox_bot_id", bot_uid);
        }
    }
    if (bot_uid == 0) {
        if (dox_bot == "@Pribavkakzpbot" || dox_bot == "Pribavkakzpbot" || dox_bot.empty()) {
            bot_uid = DEFAULT_DOX_BOT_ID;
            koto_utils::db_set_int64(api, ctx, "dox_bot_id", bot_uid);
        }
    }

    if (bot_uid == 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMO_FAIL);
        api->c_fmt(ctx, " Ошибка: Dox-бот не настроен или недоступен.", KOTO_ENT_BOLD);
        api->c_text(ctx, "\nИспользуйте .setdoxbot для привязки бота.");
        send_result(ctx, api);
        return;
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

    // 1. Отображаем нейтральный статус поиска (НЕ палим юзернейм бота в чат!)
    api->c_reset(ctx);
    api->c_emoji(ctx, "⌛️", EMO_PROCESS);
    api->c_fmt(ctx, " Начинаю поиск по ID: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, std::to_string(target_id).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\nЗапрос направлен в базу данных...");
    send_result(ctx, api);

    // 2. Отправляем ID ТОЛЬКО боту в диалог с ботом
    api->c_reset(ctx);
    api->c_text(ctx, std::to_string(target_id).c_str());
    api->c_send(ctx, bot_uid, 0);
}

// ── Watcher: автоматический выбор кнопки Telegram и получение досье ───────────

static int on_watch(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) return 0;

    long long state = koto_utils::db_get_int64(api, ctx, "dox_state", 0);
    if (state == 0) return 0;

    long long start_t = koto_utils::db_get_int64(api, ctx, "dox_start_t", 0);
    if (time(nullptr) - start_t > 90) {
        koto_utils::db_set_int64(api, ctx, "dox_state", 0);
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

    // Признаки сообщений от поискового бота
    bool is_dox_content = (
        !tg_btn_data.empty() ||
        text.find("Выберите направление для поиска") != std::string::npos ||
        text.find("Обнаружен идентификатор") != std::string::npos ||
        text.find("💬 ID:") != std::string::npos ||
        text.find("📞 Телефон:") != std::string::npos ||
        text.find("История изменения имени") != std::string::npos ||
        text.find("Подарочные связи") != std::string::npos ||
        text.find("Интересовались этим") != std::string::npos ||
        text.find("лимит запросов") != std::string::npos
    );

    // Проверяем, от Dox-бота ли сообщение
    bool from_dox_bot = (bot_id != 0 && (sender == bot_id || chat_id == bot_id));
    if (!from_dox_bot && is_dox_content) {
        // Автоматически запоминаем ID бота
        from_dox_bot = true;
        bot_id = sender ? sender : chat_id;
        koto_utils::db_set_int64(api, ctx, "dox_bot_id", bot_id);
    }

    if (!from_dox_bot) return 0;

    long long origin_chat = koto_utils::db_get_int64(api, ctx, "dox_origin_chat", 0);
    long long origin_msg = koto_utils::db_get_int64(api, ctx, "dox_origin_msg", 0);

    // ── Проверка на лимит запросов ──
    std::string text_lower;
    for (char c : text) text_lower += static_cast<char>(tolower(static_cast<unsigned char>(c)));

    if (text_lower.find("лимит запросов") != std::string::npos) {
        api->c_reset(ctx);
        api->c_text(ctx, "⚠️ ");
        api->c_fmt(ctx, "Лимит запросов исчерпан.\n", KOTO_ENT_BOLD);
        api->c_text(ctx, text.c_str());

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
        // Если кнопки не в текущем сообщении, проверяем последние сообщения бота
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
            // Кликаем инлайн кнопку Telegram!
            if (api->click_button) {
                api->click_button(ctx, bot_id ? bot_id : chat_id, msg_id, tg_btn_data.c_str());
            }
            koto_utils::db_set_int64(api, ctx, "dox_state", 2); // Ожидаем ответное досье
            return 1;
        }

        // Если это уже сразу результат без меню кнопок, обрабатываем сразу
        bool looks_like_result = (text.find("Телефон") != std::string::npos ||
                                  text.find("📞") != std::string::npos ||
                                  text.find("💬 ID:") != std::string::npos ||
                                  text.find("История изменения") != std::string::npos);
        if (!looks_like_result) {
            return 0;
        }
    }

    // ── ШАГ 2: Бот прислал досье ──
    bool is_result = (text.find("Телефон") != std::string::npos ||
                      text.find("phone") != std::string::npos ||
                      text.find("📞") != std::string::npos ||
                      text.find("История изменения") != std::string::npos ||
                      text.find("Интересовались") != std::string::npos ||
                      text.find("💬 ID:") != std::string::npos ||
                      text.find("Группы") != std::string::npos ||
                      text.find("Подарочные связи") != std::string::npos ||
                      text.find("не найден") != std::string::npos);

    if (is_result) {
        std::string phone = extract_phone(text);

        api->c_reset(ctx);
        api->c_emoji(ctx, "✔️", EMO_SUCCESS);
        api->c_fmt(ctx, " Результат поиска KoteDox:\n\n", KOTO_ENT_BOLD);

        if (!phone.empty()) {
            api->c_text(ctx, "📞 ");
            api->c_fmt(ctx, "Телефон: ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, (phone + "\n\n").c_str(), KOTO_ENT_CODE);
        } else {
            api->c_text(ctx, "📞 ");
            api->c_fmt(ctx, "Телефон: ", KOTO_ENT_BOLD);
            api->c_text(ctx, "не найден в базе\n\n");
        }

        // Полный текст досье (с защитой от превышения лимита сообщения Telegram 4096 символов)
        std::string out_dox = text;
        if (out_dox.size() > 3700) {
            out_dox = out_dox.substr(0, 3700) + "\n\n... (сообщение сокращено по лимиту длины Telegram)";
        }
        api->c_text(ctx, out_dox.c_str());

        // Редактируем исходящее сообщение
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
        "Устанавливает вашего персонального Dox-бота (или текущий диалог).",
        "[@username_бота]"
    },
    {
        "dox",
        &cmd_dox,
        KOTO_LEVEL_OWNER,
        "Ищет информацию о пользователе через Dox-бота с автовыбором Telegram.",
        "[@user/ID/ответ]"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "dox",
    "Интеграция с Dox-ботами: автовыбор Telegram и сбор досье (C++ ABI 3).",
    "1.0.8",
    28, // click_button и msg_buttons_json с versionCode 28+
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
