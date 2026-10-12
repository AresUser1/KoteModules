// tt_dl.cpp
// Скачивание контента из TikTok (видео, GIF, фото-альбомы) через tikwm API.
// Перенос из Python-модуля tt_dl.py (SynForge). Автор: Коте.
//
// .tt <ссылка>        — скачать и переслать
// .tt caption full    — подпись: название + кредиты
// .tt caption short   — подпись: только кредиты
//
// Отправка медиа идёт прямой ссылкой через c_send_file (хост сам докачивает и
// шлёт через MTProto). JSON tikwm парсится точечно под известную схему.

#include "module_abi.h"
#include <string>
#include <vector>
#include <cstring>

static const char* API_PREFIX = "https://www.tikwm.com/api/?hd=1&url=";

// ── Процент-кодирование значения query-параметра ─────────────────────────────
static std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string to_lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

// Снять JSON-экранирование в строковом значении (\/ \" \\ \n \t \uXXXX базово).
static std::string json_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            switch (n) {
                case '/':  out.push_back('/');  break;
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case 'n':  out.push_back('\n'); break;
                case 't':  out.push_back('\t'); break;
                case 'r':  break;
                case 'u': {
                    if (i + 4 < s.size()) {
                        unsigned cp = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = s[i + 1 + k]; cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                        }
                        i += 4;
                        if (cp < 0x80) out.push_back((char)cp);
                        else if (cp < 0x800) {
                            out.push_back((char)(0xC0 | (cp >> 6)));
                            out.push_back((char)(0x80 | (cp & 0x3F)));
                        } else {
                            out.push_back((char)(0xE0 | (cp >> 12)));
                            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back((char)(0x80 | (cp & 0x3F)));
                        }
                    }
                    break;
                }
                default: out.push_back(n); break;
            }
        } else out.push_back(c);
    }
    return out;
}

// Значение строкового ключа: "key":"...."  (ищет в пределах scope).
static std::string json_str(const std::string& scope, const std::string& key) {
    std::string pat = "\"" + key + "\":\"";
    size_t p = scope.find(pat);
    if (p == std::string::npos) return "";
    p += pat.size();
    std::string raw;
    for (size_t i = p; i < scope.size(); ++i) {
        char c = scope[i];
        if (c == '\\') { raw.push_back(c); if (i + 1 < scope.size()) raw.push_back(scope[++i]); continue; }
        if (c == '"') break;
        raw.push_back(c);
    }
    return json_unescape(raw);
}
// Собрать строки массива "key":[ "...", "..." ].
static std::vector<std::string> json_str_array(const std::string& body, const std::string& key) {
    std::vector<std::string> out;
    std::string pat = "\"" + key + "\":[";
    size_t p = body.find(pat);
    if (p == std::string::npos) return out;
    p += pat.size();
    bool in_str = false;
    std::string cur;
    for (size_t i = p; i < body.size(); ++i) {
        char c = body[i];
        if (in_str) {
            if (c == '\\') { cur.push_back(c); if (i + 1 < body.size()) cur.push_back(body[++i]); continue; }
            if (c == '"') { out.push_back(json_unescape(cur)); cur.clear(); in_str = false; continue; }
            cur.push_back(c);
        } else {
            if (c == '"') in_str = true;
            else if (c == ']') break;
        }
    }
    return out;
}

// HTTP GET тела в строку (MAY BLOCK). Пусто при ошибке.
static std::string http_get_str(koto_ctx* ctx, const koto_api* api, const std::string& url) {
    std::vector<unsigned char> buf(512 * 1024);
    long long n = api->http_get(ctx, url.c_str(), buf.data(), (int)buf.size());
    if (n <= 0) return "";
    if (n > (long long)buf.size()) n = (long long)buf.size();
    return std::string((const char*)buf.data(), (size_t)n);
}

static bool caption_full(koto_ctx* ctx, const koto_api* api) {
    return api->db_get_int(ctx, "caption_full", 1) != 0;
}

// Составить подпись в текущий композер (title берётся только в full-режиме).
static void compose_caption(koto_ctx* ctx, const koto_api* api, const std::string& title, bool full) {
    api->c_reset(ctx);
    if (full && !title.empty()) {
        api->c_fmt(ctx, ("\xF0\x9F\x8E\xAC " + title).c_str(), KOTO_ENT_BOLD); // 🎬 <title>
        api->c_text(ctx, "\n\n");
    }
    api->c_fmt(ctx, "\xF0\x9F\x93\xA5 \xD0\xA1\xD0\xBA\xD0\xB0\xD1\x87\xD0\xB0\xD0\xBD\xD0\xBE \xD1\x87\xD0\xB5\xD1\x80\xD0\xB5\xD0\xB7 KoteLoader", KOTO_ENT_ITALIC); // 📥 Скачано через KoteLoader
}

static void edit_err(koto_ctx* ctx, const koto_api* api, const char* msg_utf8) {
    api->c_reset(ctx);
    api->c_fmt(ctx, msg_utf8, KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

static void cmd_tt(koto_ctx* ctx, const koto_api* api) {
    const char* raw = api->cmd_args(ctx);
    std::string args = trim(raw ? raw : "");

    // ── Переключение режима подписи ──────────────────────────────────────────
    if (args.rfind("caption", 0) == 0) {
        std::string rest = trim(args.substr(7));
        std::string mode = to_lower(rest);
        if (mode == "full") {
            api->db_set_int(ctx, "caption_full", 1);
            api->c_reset(ctx);
            api->c_fmt(ctx, "\xE2\x9C\x85 \xD0\xA0\xD0\xB5\xD0\xB6\xD0\xB8\xD0\xBC \xD0\xBF\xD0\xBE\xD0\xB4\xD0\xBF\xD0\xB8\xD1\x81\xD0\xB8: ", KOTO_ENT_BOLD); // ✅ Режим подписи:
            api->c_text(ctx, "\xD0\xBF\xD0\xBE\xD0\xBB\xD0\xBD\xD1\x8B\xD0\xB9 (\xD0\xBD\xD0\xB0\xD0\xB7\xD0\xB2\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB5 + \xD0\xBA\xD1\x80\xD0\xB5\xD0\xB4\xD0\xB8\xD1\x82\xD1\x8B)"); // полный (название + кредиты)
            api->c_edit(ctx, 0);
            return;
        } else if (mode == "short") {
            api->db_set_int(ctx, "caption_full", 0);
            api->c_reset(ctx);
            api->c_fmt(ctx, "\xE2\x9C\x85 \xD0\xA0\xD0\xB5\xD0\xB6\xD0\xB8\xD0\xBC \xD0\xBF\xD0\xBE\xD0\xB4\xD0\xBF\xD0\xB8\xD1\x81\xD0\xB8: ", KOTO_ENT_BOLD); // ✅ Режим подписи:
            api->c_text(ctx, "\xD0\xBA\xD1\x80\xD0\xB0\xD1\x82\xD0\xBA\xD0\xB8\xD0\xB9 (\xD1\x82\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xBA\xD0\xBE \xD0\xBA\xD1\x80\xD0\xB5\xD0\xB4\xD0\xB8\xD1\x82\xD1\x8B)"); // краткий (только кредиты)
            api->c_edit(ctx, 0);
            return;
        } else {
            std::string cur = caption_full(ctx, api) ? "full" : "short";
            api->c_reset(ctx);
            api->c_fmt(ctx, ("\xE2\x84\xB9\xEF\xB8\x8F \xD0\xA2\xD0\xB5\xD0\xBA\xD1\x83\xD1\x89\xD0\xB8\xD0\xB9 \xD1\x80\xD0\xB5\xD0\xB6\xD0\xB8\xD0\xBC: " + cur + "\n\xD0\x98\xD1\x81\xD0\xBF\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD1\x83\xD0\xB9: .tt caption full / short").c_str(), KOTO_ENT_ITALIC); // ℹ️ Текущий режим: ... Используй: ...
            api->c_edit(ctx, 0);
            return;
        }
    }

    if (args.empty()) {
        edit_err(ctx, api, "\xE2\x9D\x8C \xD0\xA3\xD0\xBA\xD0\xB0\xD0\xB6\xD0\xB8\xD1\x82\xD0\xB5 \xD1\x81\xD1\x81\xD1\x8B\xD0\xBB\xD0\xBA\xD1\x83 \xD0\xBD\xD0\xB0 TikTok."); // ❌ Укажите ссылку на TikTok.
        return;
    }

    // Первое слово = ссылка.
    std::string link = args;
    size_t sp = link.find_first_of(" \t\n");
    if (sp != std::string::npos) link = link.substr(0, sp);

    // 1. Моментальный статус "Получаю информацию..."
    api->c_reset(ctx);
    api->c_fmt(ctx, "\xF0\x9F\x94\x8D \xD0\x9F\xD0\xBE\xD0\xBB\xD1\x83\xD1\x87\xD0\xB0\xD1\x8E \xD0\xB8\xD0\xBD\xD1\x84\xD0\xBE\xD1\x80\xD0\xBC\xD0\xB0\xD1\x86\xD0\xB8\xD1\x8E...", KOTO_ENT_ITALIC); // 🔍 Получаю информацию...
    api->c_edit(ctx, 0);

    std::string body = http_get_str(ctx, api, std::string(API_PREFIX) + url_encode(link));
    if (body.empty() || body.find("\"code\":0") == std::string::npos) {
        edit_err(ctx, api, "\xE2\x9D\x8C \xD0\x9E\xD1\x88\xD0\xB8\xD0\xB1\xD0\xBA\xD0\xB0 API: \xD0\xBD\xD0\xB5 \xD1\x83\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xBE\xD1\x81\xD1\x8C \xD0\xBF\xD0\xBE\xD0\xBB\xD1\x83\xD1\x87\xD0\xB8\xD1\x82\xD1\x8C \xD0\xB4\xD0\xB0\xD0\xBD\xD0\xBD\xD1\x8B\xD0\xB5."); // ❌ Ошибка API: не удалось получить данные.
        return;
    }

    // 2. Статус "Скачиваю..."
    api->c_reset(ctx);
    api->c_fmt(ctx, "\xF0\x9F\x93\xA5 \xD0\xA1\xD0\xBA\xD0\xB0\xD1\x87\xD0\xB8\xD0\xB2\xD0\xB0\xD1\x8E...", KOTO_ENT_ITALIC); // 📥 Скачиваю...
    api->c_edit(ctx, 0);

    long long cid = api->chat_id(ctx);
    bool full = caption_full(ctx, api);

    // Заголовок (data.title идёт до music_info).
    size_t dpos = body.find("\"data\"");
    size_t mpos = body.find("\"music_info\"");
    std::string data_scope = (dpos == std::string::npos)
        ? body
        : (mpos != std::string::npos && mpos > dpos ? body.substr(dpos, mpos - dpos) : body.substr(dpos));
    std::string title = json_str(data_scope, "title");

    // ── Фото-альбом ───────────────────────────────────────────────────────────
    std::vector<std::string> images = json_str_array(body, "images");
    if (!images.empty()) {
        compose_caption(ctx, api, title, full);   // подпись = композер (на 1-м фото)
        if (api->c_send_album) {
            std::vector<const char*> ptrs;
            ptrs.reserve(images.size());
            for (const auto& img : images) ptrs.push_back(img.c_str());
            api->c_send_album(ctx, cid, ptrs.data(), (int)ptrs.size(), 0);
        } else {
            bool first = true;
            for (const auto& img : images) {
                if (first) { compose_caption(ctx, api, title, full); first = false; }
                else api->c_reset(ctx);
                api->c_send_file(ctx, cid, img.c_str(), 0);
            }
        }
        api->delete_msg(ctx);
        return;
    }

    // ── Видео / GIF ─────────────────────────────────────────────────────────
    std::string play = json_str(data_scope, "play");
    if (play.empty()) play = json_str(data_scope, "hdplay");
    if (play.empty()) play = json_str(data_scope, "wmplay");
    if (play.empty()) {
        edit_err(ctx, api, "\xE2\x9D\x8C \xD0\x9D\xD0\xB5 \xD0\xBD\xD0\xB0\xD0\xB9\xD0\xB4\xD0\xB5\xD0\xBD \xD0\xBF\xD1\x80\xD1\x8F\xD0\xBC\xD0\xBE\xD0\xB9 URL \xD0\xB2\xD0\xB8\xD0\xB4\xD0\xB5\xD0\xBE."); // ❌ Не найден прямой URL видео.
        return;
    }

    compose_caption(ctx, api, title, full);
    api->c_send_file(ctx, cid, play.c_str(), 0);
    api->delete_msg(ctx);
}

static const koto_command COMMANDS[] = {
    {
        "tt",
        &cmd_tt,
        KOTO_LEVEL_TRUSTED,
        "\xD0\xA1\xD0\xBA\xD0\xB0\xD1\x87\xD0\xB0\xD1\x82\xD1\x8C \xD0\xBA\xD0\xBE\xD0\xBD\xD1\x82\xD0\xB5\xD0\xBD\xD1\x82 \xD0\xB8\xD0\xB7 TikTok (\xD0\xB2\xD0\xB8\xD0\xB4\xD0\xB5\xD0\xBE, GIF, \xD1\x84\xD0\xBE\xD1\x82\xD0\xBE).", // "Скачать контент из TikTok (видео, GIF, фото)."
        "<\xD1\x81\xD1\x81\xD1\x8B\xD0\xBB\xD0\xBA\xD0\xB0> | caption [full|short]" // "<ссылка> | caption [full|short]"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "tt_dl",
    "\xD0\xA1\xD0\xBA\xD0\xB0\xD1\x87\xD0\xB8\xD0\xB2\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB5 \xD0\xB2\xD0\xB8\xD0\xB4\xD0\xB5\xD0\xBE \xD0\xB8\xD0\xB7 TikTok (\xD0\xB2\xD0\xB8\xD0\xB4\xD0\xB5\xD0\xBE, \xD0\xB3\xD0\xB8\xD1\x84\xD0\xBA\xD0\xB8, \xD1\x84\xD0\xBE\xD1\x82\xD0\xBE-\xD0\xB0\xD0\xBB\xD1\x8C\xD0\xB1\xD0\xBE\xD0\xBC\xD1\x8B). C++ ABI 3.", // "Скачивание видео из TikTok (видео, гифки, фото-альбомы). C++ ABI 3."
    "1.4.0",
    24,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    nullptr, 0,
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/tt_dl.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
