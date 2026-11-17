// invoice.cpp
// Создание инвойсов на Telegram Stars (XTR) через своего бота.
// Порт модуля Invoice.py (by Killu / @dubai_ip) на Kotogram C++ ABI 3.
//
// Использование:
//   .invoice <кол-во звезд>        — создать счет на оплату в звездах
//   .invoice token <BOT_TOKEN>     — быстро сохранить токен бота (или через .cfg invoice)

#include "module_abi.h"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <random>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <cstring>

using json = nlohmann::json;

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string random_hex_32() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << dis(gen) << std::setw(16) << dis(gen);
    return oss.str();
}

static void reply_or_edit(koto_ctx* ctx, const koto_api* api, int flags = 0) {
    if (api->is_outgoing && api->is_outgoing(ctx)) api->c_edit(ctx, flags);
    else if (api->c_reply) api->c_reply(ctx, flags);
    else api->c_edit(ctx, flags);
}

static std::string get_bot_token(koto_ctx* ctx, const koto_api* api) {
    char buf[512] = {0};
    if (api->cfg_get) {
        int n = api->cfg_get(ctx, "bot_token", buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            std::string t = trim(buf);
            if (!t.empty()) return t;
        }
    }
    if (api->db_get) {
        int n = api->db_get(ctx, "bot_token", buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            std::string t = trim(buf);
            if (!t.empty()) return t;
        }
    }
    // Автоматически берём токен текущего бота-помощника Kotogram, если свой не задан
    if (api->setting_get) {
        int n = api->setting_get(ctx, "bot_token", buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            std::string t = trim(buf);
            if (!t.empty()) return t;
        }
    }
    return "";
}

static const koto_setting_field SETTINGS[] = {
    {
        "bot_token",
        "Токен бота, в которого идут звезды",
        KOTO_FIELD_STR,
        "",
        nullptr
    }
};

static void cmd_invoice(koto_ctx* ctx, const koto_api* api) {
    const char* raw = api->cmd_args ? api->cmd_args(ctx) : nullptr;
    std::string args = trim(raw ? raw : "");

    // Быстрая установка токена: .invoice token <TOKEN>
    if (args.rfind("token ", 0) == 0 || args.rfind("token\t", 0) == 0) {
        std::string tok = trim(args.substr(6));
        if (tok.empty()) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "\xE2\x9D\x8C", 5980869107891838347LL);
            api->c_text(ctx, " ");
            api->c_fmt(ctx, "Укажи токен: ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, ".invoice token 123456:ABC...", KOTO_ENT_CODE);
            reply_or_edit(ctx, api);
            return;
        }
        if (api->cfg_set) api->cfg_set(ctx, "bot_token", tok.c_str());
        if (api->db_set) api->db_set(ctx, "bot_token", tok.c_str());
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9C\x85 Токен бота сохранён!", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }

    std::string token = get_bot_token(ctx, api);
    if (token.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "\xE2\x9D\x8C", 5980869107891838347LL);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, "Укажи токен бота: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".cfg invoice", KOTO_ENT_CODE);
        api->c_text(ctx, " или ");
        api->c_fmt(ctx, ".invoice token <токен>", KOTO_ENT_CODE);
        reply_or_edit(ctx, api);
        return;
    }

    bool all_digits = !args.empty();
    for (char c : args) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            all_digits = false;
            break;
        }
    }
    long long amount = 0;
    if (all_digits) {
        try { amount = std::stoll(args); } catch (...) { amount = 0; }
    }
    if (!all_digits || amount < 1) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "\xE2\x9D\x8C", 5980869107891838347LL);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, "Использование: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".invoice 50", KOTO_ENT_CODE);
        reply_or_edit(ctx, api);
        return;
    }

    if (!api->http_post) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Требуется Kotogram Host >= 92 (http_post).", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }

    // Статус ожидания
    api->c_reset(ctx);
    api->c_fmt(ctx, "\xE2\x8F\xB3 Создаю инвойс...", KOTO_ENT_ITALIC);
    reply_or_edit(ctx, api);

    std::string star_label = std::to_string(amount) + " \xE2\xAD\x90\xEF\xB8\x8F";
    json payload = {
        {"title", "оплату счета"},
        {"description", star_label},
        {"payload", random_hex_32()},
        {"currency", "XTR"},
        {"prices", json::array({{{"label", star_label}, {"amount", amount}}})}
    };

    std::string url = "https://api.telegram.org/bot" + token + "/createInvoiceLink";
    std::string body_str = payload.dump();
    std::string headers = "Content-Type: application/json\r\n";

    std::vector<unsigned char> resp_buf(64 * 1024);
    long long n = api->http_post(ctx, url.c_str(), body_str.c_str(), headers.c_str(),
                                 resp_buf.data(), static_cast<int>(resp_buf.size()));
    if (n <= 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "\xE2\x9D\x8C", 5980869107891838347LL);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, "Ошибка Bot API: ", KOTO_ENT_BOLD);
        api->c_text(ctx, "нет ответа от api.telegram.org");
        reply_or_edit(ctx, api);
        return;
    }

    std::string resp_str(reinterpret_cast<const char*>(resp_buf.data()), static_cast<size_t>(n));
    json j = json::parse(resp_str, nullptr, false);
    if (!j.is_object() || !j.value("ok", false) || !j.contains("result") || !j["result"].is_string()) {
        std::string err = (j.is_object() && j.contains("description") && j["description"].is_string())
                              ? j["description"].get<std::string>()
                              : "некорректный ответ API";
        api->c_reset(ctx);
        api->c_emoji(ctx, "\xE2\x9D\x8C", 5980869107891838347LL);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, "Ошибка Bot API: ", KOTO_ENT_BOLD);
        api->c_text(ctx, err.c_str());
        reply_or_edit(ctx, api);
        return;
    }

    std::string link = j["result"].get<std::string>();

    api->c_reset(ctx);
    api->c_fmt(ctx, ("Счет на " + std::to_string(amount)).c_str(), KOTO_ENT_BOLD);
    api->c_text(ctx, " ");
    api->c_emoji(ctx, "\xE2\xAD\x90\xEF\xB8\x8F", 5924870095925942277LL);

    if (api->c_button_ex) {
        api->c_button_ex(ctx, "Оплатить", link.c_str(), 1, KOTO_BTN_PRIMARY, 0);
    } else if (api->c_button) {
        api->c_button(ctx, "Оплатить", link.c_str(), 1);
    } else {
        api->c_text(ctx, "\n");
        api->c_url(ctx, "Оплатить", link.c_str());
    }
    reply_or_edit(ctx, api, KOTO_NO_LINK_PREVIEW);
}

static const koto_command COMMANDS[] = {
    {
        "invoice",
        &cmd_invoice,
        KOTO_LEVEL_OWNER,
        "[ru] Создать счет на оплату в Telegram Stars через своего бота. [en] Create a Telegram Stars invoice via your bot.",
        "<кол-во звезд> | token <BOT_TOKEN>"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "invoice",
    "[ru] Инвойсы на звезды Telegram Stars через своего бота (порт Invoice.py). C++ ABI 3. [en] Telegram Stars invoice creator via Bot API. C++ ABI 3.",
    "1.0.1",
    92,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    SETTINGS,
    sizeof(SETTINGS) / sizeof(SETTINGS[0]),
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/invoice.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
