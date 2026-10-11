// idprem.cpp
// Утилита для получения ID премиум-эмодзи из сообщения (C++ ABI 3).
// Перенос из Python-модуля idprem.py. Автор: Kote.
//
// .idprem (в ответ на сообщение или к своему) — показывает кастом-эмодзи и их
// document_id. Удобно для настройки других модулей (.setrpemoji, .setmodemoji).

#include "module_abi.h"
#include <string>
#include <vector>
#include <cstdint>

static const long long INFO_EMOJI_ID  = 5879813604068298387LL;
static const long long ERROR_EMOJI_ID = 5985346521103604145LL;

// ── UTF-8 -> UTF-16 (entity-офсеты Telegram считаются в UTF-16-units) ─────────
static std::vector<uint16_t> utf8_to_utf16(const std::string& s) {
    std::vector<uint16_t> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp; int len;
        if (c < 0x80)            { cp = c;        len = 1; }
        else if ((c >> 5) == 0x6){ cp = c & 0x1F; len = 2; }
        else if ((c >> 4) == 0xE){ cp = c & 0x0F; len = 3; }
        else if ((c >> 3) == 0x1E){cp = c & 0x07; len = 4; }
        else                     { cp = 0xFFFD;   len = 1; }
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

static void append_cp_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out.push_back((char)cp);
    else if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

// Вырезать [off, off+len) из UTF-16 и вернуть как UTF-8.
static std::string utf16_slice_to_utf8(const std::vector<uint16_t>& u, int off, int len) {
    std::string out;
    int end = off + len, sz = (int)u.size();
    for (int i = off; i < end && i < sz; ) {
        uint32_t cp = u[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < end && i + 1 < sz) {
            uint32_t lo = u[i + 1];
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            i += 2;
        } else i += 1;
        append_cp_utf8(out, cp);
    }
    return out;
}
static void cmd_idprem(koto_ctx* ctx, const koto_api* api) {
    // Источник: приоритет reply, иначе собственное сообщение-команда.
    const char* rt = api->reply_text(ctx);
    bool use_reply = (rt && rt[0]) || api->reply_entity_count(ctx) > 0;

    std::string text = use_reply ? std::string(rt ? rt : "")
                                 : std::string(api->msg_text(ctx) ? api->msg_text(ctx) : "");
    int ent_count = use_reply ? api->reply_entity_count(ctx) : api->msg_entity_count(ctx);

    std::vector<uint16_t> u16 = utf8_to_utf16(text);

    struct Found { std::string ch; long long id; };
    std::vector<Found> found;
    for (int i = 0; i < ent_count; ++i) {
        int type = 0, off = 0, len = 0;
        long long doc = 0;
        char url[8] = {0};
        int ok = use_reply
            ? api->reply_entity(ctx, i, &type, &off, &len, &doc, url, (int)sizeof(url))
            : api->msg_entity(ctx, i, &type, &off, &len, &doc, url, (int)sizeof(url));
        if (!ok) continue;
        if (type != KOTO_ENT_CUSTOM_EMOJI || doc == 0) continue;
        found.push_back({ utf16_slice_to_utf8(u16, off, len), doc });
    }

    api->c_reset(ctx);
    if (found.empty()) {
        api->c_emoji(ctx, "\xE2\x9D\x8C", ERROR_EMOJI_ID); // ❌
        api->c_fmt(ctx, " \xD0\x9F\xD1\x80\xD0\xB5\xD0\xBC\xD0\xB8\xD1\x83\xD0\xBC-\xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8 \xD0\xBD\xD0\xB5 \xD0\xBD\xD0\xB0\xD0\xB9\xD0\xB4\xD0\xB5\xD0\xBD\xD1\x8B.", KOTO_ENT_BOLD); // " Премиум-эмодзи не найдены."
        api->c_edit(ctx, 0);
        return;
    }

    api->c_emoji(ctx, "\xE2\x84\xB9", INFO_EMOJI_ID); // ℹ
    api->c_fmt(ctx, " \xD0\x9D\xD0\xB0\xD0\xB9\xD0\xB4\xD0\xB5\xD0\xBD\xD0\xBD\xD1\x8B\xD0\xB5 \xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8:", KOTO_ENT_BOLD); // " Найденные эмодзи:"
    api->c_text(ctx, "\n\n");
    for (const auto& f : found) {
        api->c_fmt(ctx, "\xF0\x9F\x94\xB9 \xD0\xAD\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8: ", KOTO_ENT_BOLD); // "🔹 Эмодзи: "
        api->c_text(ctx, (f.ch + "\n").c_str());
        api->c_fmt(ctx, "   ID: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, std::to_string(f.id).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n\n");
    }
    api->c_edit(ctx, 0);
}

static const koto_command COMMANDS[] = {
    {
        "idprem",
        &cmd_idprem,
        KOTO_LEVEL_TRUSTED,
        "\xD0\x9F\xD0\xBE\xD0\xBA\xD0\xB0\xD0\xB7\xD0\xB0\xD1\x82\xD1\x8C ID \xD0\xBF\xD1\x80\xD0\xB5\xD0\xBC\xD0\xB8\xD1\x83\xD0\xBC-\xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8 \xD0\xB8\xD0\xB7 \xD1\x81\xD0\xBE\xD0\xBE\xD0\xB1\xD1\x89\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F.", // "Показать ID премиум-эмодзи из сообщения."
        "[\xD0\xB2 \xD0\xBE\xD1\x82\xD0\xB2\xD0\xB5\xD1\x82 \xD0\xBD\xD0\xB0 \xD1\x81\xD0\xBE\xD0\xBE\xD0\xB1\xD1\x89\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5]" // "[в ответ на сообщение]"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "idprem",
    "\xD0\xA3\xD1\x82\xD0\xB8\xD0\xBB\xD0\xB8\xD1\x82\xD0\xB0 \xD0\xB4\xD0\xBB\xD1\x8F \xD0\xBF\xD0\xBE\xD0\xBB\xD1\x83\xD1\x87\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F ID \xD0\xBF\xD1\x80\xD0\xB5\xD0\xBC\xD0\xB8\xD1\x83\xD0\xBC-\xD1\x8D\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8 \xD0\xB8\xD0\xB7 \xD1\x81\xD0\xBE\xD0\xBE\xD0\xB1\xD1\x89\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB9 (C++ ABI 3).", // "Утилита для получения ID премиум-эмодзи из сообщений (C++ ABI 3)."
    "1.0.1",
    17,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    nullptr, 0,
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/idprem.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
