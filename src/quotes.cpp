// quotes.cpp
// Нативный C++ модуль для создания красивых цитат и стикеров из сообщений Telegram
// и генерации фейковых цитат (.q, .fq, .qcfg).
// C++ ABI 3. Автор: Kote.

#include "module_abi.h"
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <algorithm>

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

// ── Настройки модуля по умолчанию ───────────────────────────────────────────
static const koto_setting_field SETTINGS[] = {
    {
        "bg_color",
        "Цвет фона цитаты (#162330, black, red и т.д.)",
        KOTO_FIELD_STR,
        "#162330",
        nullptr
    },
    {
        "type",
        "Формат шаблона цитаты (quote или stories)",
        KOTO_FIELD_CHOICE,
        "quote",
        "quote,stories"
    },
    {
        "emoji_brand",
        "Стиль отрисовки эмодзи (apple, google, twitter, noto)",
        KOTO_FIELD_CHOICE,
        "apple",
        "apple,google,twitter,noto"
    },
    {
        "width",
        "Ширина холста цитаты",
        KOTO_FIELD_INT,
        "512",
        nullptr
    },
    {
        "height",
        "Высота холста цитаты",
        KOTO_FIELD_INT,
        "768",
        nullptr
    },
    {
        "scale",
        "Коэффициент масштабирования (scale)",
        KOTO_FIELD_INT,
        "2",
        nullptr
    },
    {
        "max_messages",
        "Максимальное число сообщений в цитате",
        KOTO_FIELD_INT,
        "15",
        nullptr
    },
    {
        "endpoint",
        "API эндпоинт генератора цитат",
        KOTO_FIELD_STR,
        "https://quote.yuri.ly/generate",
        nullptr
    }
};

static const size_t SETTINGS_COUNT = sizeof(SETTINGS) / sizeof(SETTINGS[0]);

struct QuotesConfig {
    std::string bg_color = "#162330";
    std::string type = "quote";
    std::string emoji_brand = "apple";
    int width = 512;
    int height = 768;
    int scale = 2;
    int max_messages = 15;
    std::string endpoint = "https://quote.yuri.ly/generate";
};

static const char* const FALLBACK_ENDPOINTS[] = {
    "https://quote.yuri.ly/generate",
    "https://kok.gay/gayotes/generate",
    "https://quotes.fl1yd.su/generate"
};

static QuotesConfig load_config(koto_ctx* ctx, const koto_api* api) {
    QuotesConfig c;
    if (!api || !api->cfg_get) return c;

    char buf[256];
    auto get_str = [&](const char* key, std::string& dest) {
        int n = api->cfg_get(ctx, key, buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = '\0'; dest = trim(buf); }
    };
    auto get_int = [&](const char* key, int& dest) {
        int n = api->cfg_get(ctx, key, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            try { int v = std::stoi(buf); if (v > 0) dest = v; } catch (...) {}
        }
    };

    get_str("bg_color", c.bg_color);
    get_str("type", c.type);
    get_str("emoji_brand", c.emoji_brand);
    get_int("width", c.width);
    get_int("height", c.height);
    get_int("scale", c.scale);
    get_int("max_messages", c.max_messages);
    get_str("endpoint", c.endpoint);

    if (c.endpoint.empty() || c.endpoint == "https://kok.gay/gayotes/generate") c.endpoint = "https://quote.yuri.ly/generate";
    return c;
}

// ── Преобразование KOTO_ENT_* в имя сущности для Quotes API ──────────────────
static const char* ent_type_to_api(int type) {
    switch (type) {
        case KOTO_ENT_BOLD:         return "bold";
        case KOTO_ENT_ITALIC:       return "italic";
        case KOTO_ENT_UNDERLINE:    return "underline";
        case KOTO_ENT_STRIKE:       return "strikethrough";
        case KOTO_ENT_CODE:         return "code";
        case KOTO_ENT_PRE:          return "pre";
        case KOTO_ENT_URL:          return "text_link";
        case KOTO_ENT_MENTION:      return "mention";
        case KOTO_ENT_SPOILER:      return "spoiler";
        case KOTO_ENT_CUSTOM_EMOJI: return "custom_emoji";
        case KOTO_ENT_BLOCKQUOTE:   return "blockquote";
        default:                    return nullptr;
    }
}

// Формирование JSON-объекта сообщения для API
struct QuoteMessageItem {
    long long sender_id = 0;
    std::string name;
    std::string first_name;
    std::string last_name;
    std::string username;
    std::string text;
    struct Entity {
        int type;
        int off;
        int len;
        long long docid;
        std::string url;
    };
    std::vector<Entity> entities;
};

static std::string build_message_json(const QuoteMessageItem& item) {
    std::ostringstream ss;
    ss << "{";
    ss << "\"from\":{";
    ss << "\"id\":" << item.sender_id << ",";
    ss << "\"name\":\"" << json_escape(item.name.empty() ? "User" : item.name) << "\",";
    ss << "\"first_name\":\"" << json_escape(item.first_name.empty() ? (item.name.empty() ? "User" : item.name) : item.first_name) << "\",";
    if (!item.last_name.empty()) {
        ss << "\"last_name\":\"" << json_escape(item.last_name) << "\",";
    }
    if (!item.username.empty()) {
        ss << "\"username\":\"" << json_escape(item.username) << "\",";
    }
    // Если есть username, передаём прямую ссылку на аватар
    if (!item.username.empty()) {
        ss << "\"photo\":{\"url\":\"https://t.me/i/userpic/320/" << item.username << ".jpg\"}";
    } else {
        ss << "\"photo\":{}";
    }
    ss << "},";
    ss << "\"text\":\"" << json_escape(item.text) << "\",";
    ss << "\"avatar\":true,";
    ss << "\"entities\":[";
    bool first_e = true;
    for (const auto& e : item.entities) {
        const char* t_str = ent_type_to_api(e.type);
        if (!t_str) continue;
        if (!first_e) ss << ",";
        first_e = false;
        ss << "{\"type\":\"" << t_str << "\",\"offset\":" << e.off << ",\"length\":" << e.len;
        if (e.type == KOTO_ENT_URL && !e.url.empty()) {
            ss << ",\"url\":\"" << json_escape(e.url) << "\"";
        } else if (e.type == KOTO_ENT_CUSTOM_EMOJI && e.docid > 0) {
            ss << ",\"custom_emoji_id\":\"" << e.docid << "\"";
        }
        ss << "}";
    }
    ss << "]}";
    return ss.str();
}

// ── .q ──────────────────────────────────────────────────────────────────────
static void cmd_quote(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    if (!api->http_post) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Сетевой API HTTP POST недоступен на этом ядре Kotogram.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    const char* r_text = api->reply_text ? api->reply_text(ctx) : nullptr;
    long long r_mid = api->reply_msg_id ? api->reply_msg_id(ctx) : 0;
    if (r_mid <= 0 && (!r_text || !r_text[0])) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Ответьте на сообщение (реплай), чтобы создать цитату.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    QuotesConfig cfg = load_config(ctx, api);

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? trim(raw) : "";

    int count = 1;
    bool as_file = false;
    std::string bg_color = cfg.bg_color;

    if (!args.empty()) {
        std::istringstream iss(args);
        std::string token;
        while (iss >> token) {
            if (token == "!file" || token == "file" || token == "png") {
                as_file = true;
            } else {
                bool is_num = true;
                for (char c : token) { if (!std::isdigit((unsigned char)c)) { is_num = false; break; } }
                if (is_num) {
                    try {
                        int val = std::stoi(token);
                        if (val > 0) count = std::min(val, cfg.max_messages);
                    } catch (...) {}
                } else {
                    bg_color = token;
                }
            }
        }
    }

    // Сообщаем статус пользователю
    api->c_reset(ctx);
    api->c_fmt(ctx, "🎨 Рендеринг цитаты...", KOTO_ENT_ITALIC);
    api->c_edit(ctx, 0);

    std::vector<QuoteMessageItem> items;

    // Собираем сообщение из реплая
    QuoteMessageItem rep_item;
    rep_item.sender_id = api->reply_sender_id ? api->reply_sender_id(ctx) : 0;
    rep_item.text = r_text ? r_text : "";

    if (rep_item.sender_id > 0) {
        char n_buf[128] = {0};
        if (api->user_name && api->user_name(ctx, rep_item.sender_id, n_buf, sizeof(n_buf) - 1) > 0) {
            rep_item.name = n_buf;
        }
        char fn_buf[128] = {0};
        if (api->user_first_name && api->user_first_name(ctx, rep_item.sender_id, fn_buf, sizeof(fn_buf) - 1) > 0) {
            rep_item.first_name = fn_buf;
        }
        char ln_buf[128] = {0};
        if (api->user_last_name && api->user_last_name(ctx, rep_item.sender_id, ln_buf, sizeof(ln_buf) - 1) > 0) {
            rep_item.last_name = ln_buf;
        }
        char un_buf[128] = {0};
        if (api->user_username && api->user_username(ctx, rep_item.sender_id, un_buf, sizeof(un_buf) - 1) > 0) {
            rep_item.username = un_buf;
        }
        if (rep_item.name.empty()) {
            rep_item.name = rep_item.first_name;
            if (!rep_item.last_name.empty()) rep_item.name += " " + rep_item.last_name;
        }
    }

    int n_ents = api->reply_entity_count ? api->reply_entity_count(ctx) : 0;
    for (int i = 0; i < n_ents; ++i) {
        int type = 0, off = 0, len = 0;
        long long docid = 0;
        char url[512] = {0};
        if (api->reply_entity && api->reply_entity(ctx, i, &type, &off, &len, &docid, url, sizeof(url))) {
            QuoteMessageItem::Entity e;
            e.type = type;
            e.off = off;
            e.len = len;
            e.docid = docid;
            e.url = url;
            rep_item.entities.push_back(e);
        }
    }

    items.push_back(rep_item);

    // Формируем JSON запрос к API
    std::string ext = as_file ? "png" : "webp";

    std::ostringstream payload;
    payload << "{";
    payload << "\"backgroundColor\":\"" << json_escape(bg_color) << "\",";
    payload << "\"width\":" << cfg.width << ",";
    payload << "\"height\":" << cfg.height << ",";
    payload << "\"scale\":" << cfg.scale << ",";
    payload << "\"emojiBrand\":\"" << json_escape(cfg.emoji_brand) << "\",";
    payload << "\"format\":\"" << ext << "\",";
    payload << "\"type\":\"" << json_escape(cfg.type) << "\",";
    payload << "\"messages\":[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) payload << ",";
        payload << build_message_json(items[i]);
    }
    payload << "]}";

    std::vector<std::string> endpoints;
    if (!cfg.endpoint.empty()) endpoints.push_back(cfg.endpoint);
    for (const char* fb : FALLBACK_ENDPOINTS) {
        if (std::find(endpoints.begin(), endpoints.end(), fb) == endpoints.end()) {
            endpoints.push_back(fb);
        }
    }

    std::vector<unsigned char> resp(1024 * 1024); // 1 MB буфер под стикер/картинку
    long long got = 0;
    bool is_valid_img = false;

    for (const auto& ep : endpoints) {
        std::string req_url = ep + "." + ext;
        got = api->http_post(ctx, req_url.c_str(), payload.str().c_str(), "Content-Type: application/json", resp.data(), (int)resp.size());

        if (got >= 8) {
            if ((got >= 12 && memcmp(resp.data(), "RIFF", 4) == 0 && memcmp(resp.data() + 8, "WEBP", 4) == 0) ||
                (memcmp(resp.data(), "\x89PNG\r\n\x1a\n", 8) == 0)) {
                is_valid_img = true;
                break;
            }
        }
    }

    if (!is_valid_img) {
        api->c_reset(ctx);
        if (got > 0) {
            std::string err_text(reinterpret_cast<char*>(resp.data()), std::min((size_t)got, (size_t)256));
            api->c_fmt(ctx, "❌ Ошибка сервиса цитат: ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, err_text.c_str(), KOTO_ENT_CODE);
        } else {
            api->c_fmt(ctx, "❌ Ошибка сетевого запроса к генератору цитат.", KOTO_ENT_BOLD);
        }
        api->c_edit(ctx, 0);
        return;
    }

    // Удаляем статусное сообщение
    api->delete_msg(ctx);

    // Отправляем готовую цитату в чат
    long long cid = api->chat_id(ctx);
    std::string fname = as_file ? "Quote.png" : "Quote.webp";
    api->c_reset(ctx);
    api->c_send_bytes(ctx, cid, fname.c_str(), resp.data(), (int)got, 0);
}

// ── .fq (Fake Quote) ────────────────────────────────────────────────────────
static void cmd_fake_quote(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    if (!api->http_post) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Сетевой API HTTP POST недоступен на этом ядре Kotogram.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? trim(raw) : "";

    const char* r_text = api->reply_text ? api->reply_text(ctx) : nullptr;
    long long r_sender = api->reply_sender_id ? api->reply_sender_id(ctx) : 0;

    if (args.empty() && r_sender <= 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Использование:\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".fq @username Текст сообщения\n.fq Текст (в ответ на чьё-то сообщение)\n.fq @user1 текст1; @user2 текст2", KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    QuotesConfig cfg = load_config(ctx, api);

    api->c_reset(ctx);
    api->c_fmt(ctx, "🎭 Генерирую фейк-цитату...", KOTO_ENT_ITALIC);
    api->c_edit(ctx, 0);

    std::vector<QuoteMessageItem> items;

    if (r_sender > 0 && (args.empty() || args[0] != '@')) {
        // Случай 1: реплай на человека, а в аргументах — текст, который он якобы сказал
        QuoteMessageItem item;
        item.sender_id = r_sender;
        item.text = args.empty() ? (r_text ? r_text : "...") : args;

        char n_buf[128] = {0};
        if (api->user_name && api->user_name(ctx, r_sender, n_buf, sizeof(n_buf) - 1) > 0) item.name = n_buf;
        char fn_buf[128] = {0};
        if (api->user_first_name && api->user_first_name(ctx, r_sender, fn_buf, sizeof(fn_buf) - 1) > 0) item.first_name = fn_buf;
        char ln_buf[128] = {0};
        if (api->user_last_name && api->user_last_name(ctx, r_sender, ln_buf, sizeof(ln_buf) - 1) > 0) item.last_name = ln_buf;
        char un_buf[128] = {0};
        if (api->user_username && api->user_username(ctx, r_sender, un_buf, sizeof(un_buf) - 1) > 0) item.username = un_buf;
        if (item.name.empty()) item.name = "User";

        items.push_back(item);
    } else {
        // Случай 2: разбор сегментов через точку с запятой ";"
        std::istringstream ss_all(args);
        std::string segment;
        while (std::getline(ss_all, segment, ';')) {
            segment = trim(segment);
            if (segment.empty()) continue;

            size_t sp = segment.find_first_of(" \t");
            std::string user_token = (sp == std::string::npos) ? segment : segment.substr(0, sp);
            std::string text_token = (sp == std::string::npos) ? "" : trim(segment.substr(sp + 1));

            QuoteMessageItem item;
            item.text = text_token.empty() ? "..." : text_token;

            long long uid = 0;
            std::string un_lookup;
            if (user_token.size() > 1 && user_token[0] == '@') {
                un_lookup = user_token.substr(1);
            } else {
                try { uid = std::stoll(user_token); } catch (...) { un_lookup = user_token; }
            }

            if (uid == 0 && !un_lookup.empty() && api->resolve) {
                uid = api->resolve(ctx, un_lookup.c_str());
            }

            item.sender_id = uid > 0 ? uid : 12345678LL;
            item.username = un_lookup;

            if (uid > 0) {
                char n_buf[128] = {0};
                if (api->user_name && api->user_name(ctx, uid, n_buf, sizeof(n_buf) - 1) > 0) item.name = n_buf;
                char fn_buf[128] = {0};
                if (api->user_first_name && api->user_first_name(ctx, uid, fn_buf, sizeof(fn_buf) - 1) > 0) item.first_name = fn_buf;
                char ln_buf[128] = {0};
                if (api->user_last_name && api->user_last_name(ctx, uid, ln_buf, sizeof(ln_buf) - 1) > 0) item.last_name = ln_buf;
                char un_buf[128] = {0};
                if (api->user_username && api->user_username(ctx, uid, un_buf, sizeof(un_buf) - 1) > 0) item.username = un_buf;
            }

            if (item.name.empty()) {
                item.name = !un_lookup.empty() ? un_lookup : ("ID " + std::to_string(uid));
                item.first_name = item.name;
            }

            items.push_back(item);
        }
    }

    if (items.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Не удалось определить участников для фейк-цитаты.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    std::ostringstream payload;
    payload << "{";
    payload << "\"backgroundColor\":\"" << json_escape(cfg.bg_color) << "\",";
    payload << "\"width\":" << cfg.width << ",";
    payload << "\"height\":" << cfg.height << ",";
    payload << "\"scale\":" << cfg.scale << ",";
    payload << "\"emojiBrand\":\"" << json_escape(cfg.emoji_brand) << "\",";
    payload << "\"format\":\"webp\",";
    payload << "\"type\":\"" << json_escape(cfg.type) << "\",";
    payload << "\"messages\":[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) payload << ",";
        payload << build_message_json(items[i]);
    }
    payload << "]}";

    std::vector<std::string> endpoints;
    if (!cfg.endpoint.empty()) endpoints.push_back(cfg.endpoint);
    for (const char* fb : FALLBACK_ENDPOINTS) {
        if (std::find(endpoints.begin(), endpoints.end(), fb) == endpoints.end()) {
            endpoints.push_back(fb);
        }
    }

    std::vector<unsigned char> resp(1024 * 1024);
    long long got = 0;
    bool is_valid_img = false;

    for (const auto& ep : endpoints) {
        std::string req_url = ep + ".webp";
        got = api->http_post(ctx, req_url.c_str(), payload.str().c_str(), "Content-Type: application/json", resp.data(), (int)resp.size());

        if (got >= 12 && memcmp(resp.data(), "RIFF", 4) == 0 && memcmp(resp.data() + 8, "WEBP", 4) == 0) {
            is_valid_img = true;
            break;
        }
    }

    if (!is_valid_img) {
        api->c_reset(ctx);
        if (got > 0) {
            std::string err_text(reinterpret_cast<char*>(resp.data()), std::min((size_t)got, (size_t)256));
            api->c_fmt(ctx, "❌ Ошибка сервиса цитат: ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, err_text.c_str(), KOTO_ENT_CODE);
        } else {
            api->c_fmt(ctx, "❌ Ошибка сетевого запроса к генератору цитат.", KOTO_ENT_BOLD);
        }
        api->c_edit(ctx, 0);
        return;
    }

    api->delete_msg(ctx);

    long long cid = api->chat_id(ctx);
    api->c_reset(ctx);
    api->c_send_bytes(ctx, cid, "FakeQuote.webp", resp.data(), (int)got, 0);
}

// ── .qcfg ───────────────────────────────────────────────────────────────────
static void cmd_quotes_cfg(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? trim(raw) : "";

    QuotesConfig cfg = load_config(ctx, api);

    if (args.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "⚙️ Настройки модуля Quotes:\n\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "• Цвет фона: ");
        api->c_fmt(ctx, cfg.bg_color.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n• Формат: ");
        api->c_fmt(ctx, cfg.type.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n• Стиль эмодзи: ");
        api->c_fmt(ctx, cfg.emoji_brand.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n• Размер: ");
        api->c_fmt(ctx, (std::to_string(cfg.width) + "x" + std::to_string(cfg.height)).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n• Scale: ");
        api->c_fmt(ctx, std::to_string(cfg.scale).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n• Макс. сообщений: ");
        api->c_fmt(ctx, std::to_string(cfg.max_messages).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n• Эндпоинт: ");
        api->c_fmt(ctx, cfg.endpoint.c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, "\n\nИзменить: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".qcfg bg_color black", KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    size_t sp = args.find_first_of(" \t");
    std::string key = (sp == std::string::npos) ? args : args.substr(0, sp);
    std::string val = (sp == std::string::npos) ? "" : trim(args.substr(sp + 1));

    for (char& c : key) c = (char)std::tolower((unsigned char)c);

    if (val.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Укажите значение: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (".qcfg " + key + " <значение>").c_str(), KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    if (key == "bg_color" || key == "type" || key == "emoji_brand" || key == "width" ||
        key == "height" || key == "scale" || key == "max_messages" || key == "endpoint") {
        if (api->cfg_set) {
            api->cfg_set(ctx, key.c_str(), val.c_str());
        }
        api->c_reset(ctx);
        api->c_fmt(ctx, "✅ Параметр ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, key.c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, " установлен в ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, val.c_str(), KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
    } else {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Неизвестный параметр. Доступны: bg_color, type, emoji_brand, width, height, scale, max_messages, endpoint.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
    }
}

static const koto_command COMMANDS[] = {
    {
        "q",
        &cmd_quote,
        KOTO_LEVEL_TRUSTED,
        "Создать цитату-стикер из реплая (.q [count] [color] [!file]).",
        "[число] [цвет] [!file]"
    },
    {
        "fq",
        &cmd_fake_quote,
        KOTO_LEVEL_TRUSTED,
        "Создать фейковую цитату (.fq @user текст или .fq текст в реплай).",
        "<@user/текст>"
    },
    {
        "qcfg",
        &cmd_quotes_cfg,
        KOTO_LEVEL_TRUSTED,
        "Настройки генератора цитат (цвет, размер, эмодзи, эндпоинт).",
        "[параметр] [значение]"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "quotes",
    "Создание стикеров-цитат и фейк-цитат из сообщений Telegram через сервис Quotes API. C++ ABI 3.",
    "1.0.1",
    24,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    SETTINGS,
    SETTINGS_COUNT,
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/quotes.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
