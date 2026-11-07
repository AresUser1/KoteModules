// freegpt.cpp — Бесплатный ChatGPT и генерация изображений БЕЗ API и БЕЗ ботов (C++ ABI 3).
// Портирован из Python-модуля FreeGPT (by kote).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdlib>

namespace {

constexpr const char* VERSION = "1.0.0";

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

static std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (unsigned char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else if (c == ' ') {
            escaped << '+';
        } else {
            escaped << '%' << std::setw(2) << std::uppercase << int(c);
        }
    }
    return escaped.str();
}

// ── Command: .gf <вопрос> ─────────────────────────────────────────────────────
void cmd_gf(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::string query;
    const char* raw_args = api->cmd_args(ctx);
    if (raw_args && raw_args[0]) query = trim(raw_args);

    if (query.empty() && api->reply_text) {
        const char* r_text = api->reply_text(ctx);
        if (r_text && r_text[0]) query = trim(r_text);
    }

    if (query.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Нужно указать вопрос: .gf <вопрос> или в ответ на сообщение", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    // Сообщение о начале генерации
    api->c_reset(ctx);
    api->c_fmt(ctx, "🔄 Спрашиваю ChatGPT…", KOTO_ENT_BOLD);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);

    // Модель из настроек
    char model_buf[64] = {0};
    std::string model = "gpt-4o";
    if (api->setting_get && api->setting_get(ctx, "model", model_buf, sizeof(model_buf) - 1) > 0) {
        if (model_buf[0]) model = model_buf;
    }

    // URL для Pollinations free AI API
    std::string enc = url_encode(query);
    std::string url = "https://text.pollinations.ai/" + enc + "?model=openai";

    std::vector<unsigned char> resp(65536, 0);
    long long n = api->http_get ? api->http_get(ctx, url.c_str(), resp.data(), (int)resp.size() - 1) : -1;

    if (n <= 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "🚫 Ошибка получения ответа от нейросети. Попробуйте позже.", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    std::string answer(reinterpret_cast<char*>(resp.data()), (size_t)n);

    // Формируем результат
    api->c_reset(ctx);
    api->c_fmt(ctx, "👨‍💻 Вопрос: ", KOTO_ENT_BOLD);
    api->c_text(ctx, (query + "\n\n").c_str());

    api->c_fmt(ctx, "🤖 Ответ:\n", KOTO_ENT_BOLD);
    api->c_markdown(ctx, answer.c_str());

    api->c_text(ctx, "\n\n🖥 ");
    api->c_fmt(ctx, "Модель: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, model.c_str(), KOTO_ENT_CODE);

    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Command: .gfi <промпт> ────────────────────────────────────────────────────
void cmd_gfi(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    std::string prompt;
    const char* raw_args = api->cmd_args(ctx);
    if (raw_args && raw_args[0]) prompt = trim(raw_args);

    if (prompt.empty() && api->reply_text) {
        const char* r_text = api->reply_text(ctx);
        if (r_text && r_text[0]) prompt = trim(r_text);
    }

    if (prompt.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Нужно указать промпт для генерации: .gfi <промпт>", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    // Сообщение о начале генерации
    api->c_reset(ctx);
    api->c_fmt(ctx, "🔄 Генерирую изображение (Flux AI)…", KOTO_ENT_BOLD);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);

    std::string img_model = "flux";
    char m_buf[64] = {0};
    if (api->setting_get && api->setting_get(ctx, "image_model", m_buf, sizeof(m_buf) - 1) > 0) {
        if (m_buf[0]) img_model = m_buf;
    }

    std::string enc = url_encode(prompt);
    std::string url = "https://image.pollinations.ai/prompt/" + enc + "?model=" + img_model + "&width=1024&height=1024&nologo=true";

    // Скачиваем байты изображения
    std::vector<unsigned char> img_bytes(4 * 1024 * 1024, 0);
    long long n = api->http_get ? api->http_get(ctx, url.c_str(), img_bytes.data(), (int)img_bytes.size()) : -1;

    long long cid = api->chat_id(ctx);

    if (n > 1000) {
        // Формируем подпись
        api->c_reset(ctx);
        api->c_fmt(ctx, "🖼 Промпт: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, prompt.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n🖥 ");
        api->c_fmt(ctx, "Модель: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, img_model.c_str(), KOTO_ENT_CODE);

        // Отправляем изображение в чат
        if (api->c_send_bytes) {
            api->c_send_bytes(ctx, cid, "freegpt_image.jpg", img_bytes.data(), (int)n, 0);
            if (api->is_outgoing(ctx) && api->delete_msg) {
                api->delete_msg(ctx);
            }
            return;
        }
    }

    // Если не удалось скачать изображение
    api->c_reset(ctx);
    api->c_fmt(ctx, "🚫 Ошибка при генерации изображения. Попробуйте другой промпт.", KOTO_ENT_BOLD);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    (void)ctx; (void)api;
}

static const koto_setting_field SETTINGS[] = {
    {
        "model",
        "Текстовая модель (по умолчанию gpt-4o)",
        KOTO_FIELD_STR,
        "gpt-4o",
        nullptr
    },
    {
        "image_model",
        "Модель генерации изображений (flux, turbo)",
        KOTO_FIELD_STR,
        "flux",
        nullptr
    }
};

static const koto_command COMMANDS[] = {
    {"gf", cmd_gf, KOTO_LEVEL_TRUSTED, "[ru] Задать вопрос ChatGPT (БЕЗ API, БЕЗ ботов) (by kote) [en] Ask ChatGPT (FREE, no API key, no bots).", "<вопрос>"},
    {"gfi", cmd_gfi, KOTO_LEVEL_TRUSTED, "[ru] Сгенерировать изображение через Flux AI (by kote) [en] Generate AI image via Flux (FREE).", "<промпт>"}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "freegpt",
    "[ru] Бесплатный ChatGPT и генерация изображений Flux БЕЗ API и БЕЗ ботов (by kote). [en] Free ChatGPT & Flux AI image generation without API keys or bots (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    nullptr, 0,
    on_callback,
    SETTINGS,
    static_cast<int>(sizeof(SETTINGS) / sizeof(SETTINGS[0])),
    "https://github.com/AresUser1/KoteModules/blob/main/src/freegpt.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
