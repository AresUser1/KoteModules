// picme.cpp — Кринж-модуль «Режим пикми» для Kotogram / KoteLoader (C++ ABI 3).
// Портирован из Python-модуля PicMe с сохранением премиум и кастомных эмодзи (by kote).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"

#include <string>
#include <vector>
#include <random>
#include <cstring>
#include <cstdlib>

namespace {

constexpr const char* VERSION = "1.0.1";

static const std::vector<std::string> EMOJIS = {
    "😊", "😂", "🎉", "👍", "🔥", "❤️", "🥳", "😎", "🥺", "✨", "👉👈", "💅", "🥰"
};

static const std::vector<std::string> SIGNS = {
    "!", "!!", "!!!", " ~", " :3", " UwU"
};

static bool is_picme_enabled(koto_ctx* ctx, const koto_api* api) {
    char buf[16] = {0};
    if (api->db_get && api->db_get(ctx, "picme_active", buf, sizeof(buf) - 1) > 0) {
        return strcmp(buf, "1") == 0;
    }
    return false;
}

static void set_picme_enabled(koto_ctx* ctx, const koto_api* api, bool enabled) {
    if (api->db_set) {
        api->db_set(ctx, "picme_active", enabled ? "1" : "0");
    }
}

// ── Command: .picme ───────────────────────────────────────────────────────────
void cmd_picme(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    bool current = is_picme_enabled(ctx, api);
    bool next = !current;
    set_picme_enabled(ctx, api, next);

    api->c_reset(ctx);
    if (next) {
        api->c_fmt(ctx, "😘 Режим пикми включен! ✨", KOTO_ENT_BOLD);
    } else {
        api->c_fmt(ctx, "😢 Режим пикми выключен!", KOTO_ENT_BOLD);
    }

    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

// ── UTF-16 / UTF-8 Helper Structures ─────────────────────────────────────────
struct CharSpan {
    size_t byte_pos;
    size_t byte_len;
    int u16_pos;
    int u16_len;
    bool is_space;
};

struct Token {
    bool is_space;
    size_t start_char_idx;
    size_t end_char_idx;
};

struct SavedEntity {
    int type;
    int off;
    int len;
    long long docid;
    std::string url;
};

static int utf16_count(const char* s, size_t n) {
    int units = 0;
    for (size_t i = 0; i < n;) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        int bytes = (c < 0x80) ? 1 : ((c & 0xE0) == 0xC0) ? 2 : ((c & 0xF0) == 0xE0) ? 3 : 4;
        if (i + bytes > n) bytes = static_cast<int>(n - i);
        units += (bytes == 4) ? 2 : 1;
        i += bytes;
    }
    return units;
}

static std::vector<CharSpan> parse_chars(const char* s) {
    std::vector<CharSpan> chars;
    if (!s) return chars;
    size_t n = std::strlen(s);
    int u16 = 0;
    for (size_t i = 0; i < n;) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        int blen = 1;
        if (c < 0x80) blen = 1;
        else if ((c & 0xE0) == 0xC0) blen = 2;
        else if ((c & 0xF0) == 0xE0) blen = 3;
        else if ((c & 0xF8) == 0xF0) blen = 4;

        if (i + blen > n) blen = static_cast<int>(n - i);

        int ulen = (blen == 4) ? 2 : 1;
        bool is_sp = (blen == 1 && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'));

        chars.push_back({i, static_cast<size_t>(blen), u16, ulen, is_sp});
        u16 += ulen;
        i += blen;
    }
    return chars;
}

// ── Watcher: перехват исходящих сообщений с сохранением кастомных эмодзи ──────
int on_watch(koto_ctx* ctx, const koto_api* api) {
    if (!api->is_outgoing(ctx)) return 0;
    if (!is_picme_enabled(ctx, api)) return 0;

    const char* text = api->msg_text ? api->msg_text(ctx) : nullptr;
    if (!text || text[0] == '\0') return 0;

    // Не модифицируем команды
    if (text[0] == '.' || text[0] == '/' || text[0] == '!' || text[0] == ',') {
        return 0;
    }

    auto chars = parse_chars(text);
    if (chars.empty()) return 0;

    int total_orig_u16 = chars.back().u16_pos + chars.back().u16_len;

    // Считываем оригинальные entities (включая custom emoji)
    std::vector<SavedEntity> orig_entities;
    if (api->msg_entity_count && api->msg_entity) {
        int cnt = api->msg_entity_count(ctx);
        for (int i = 0; i < cnt; ++i) {
            int type = 0, off = 0, len = 0;
            long long docid = 0;
            char url[512] = {0};
            if (api->msg_entity(ctx, i, &type, &off, &len, &docid, url, sizeof(url))) {
                if (type != KOTO_ENT_NONE && off >= 0 && len > 0) {
                    orig_entities.push_back({type, off, len, docid, url});
                }
            }
        }
    }

    // Группируем в токены (слова и пробелы)
    std::vector<Token> tokens;
    size_t i = 0;
    size_t num_words = 0;
    while (i < chars.size()) {
        bool is_sp = chars[i].is_space;
        size_t start = i;
        while (i < chars.size() && chars[i].is_space == is_sp) {
            i++;
        }
        tokens.push_back({is_sp, start, i});
        if (!is_sp) num_words++;
    }

    if (num_words == 0) return 0;

    // Инициализируем генератор случайных чисел
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> coin(0, 1);
    std::uniform_int_distribution<> emo_dist(0, static_cast<int>(EMOJIS.size()) - 1);
    std::uniform_int_distribution<> sign_dist(0, static_cast<int>(SIGNS.size()) - 1);

    std::vector<int> orig_to_new_u16(total_orig_u16, -1);
    std::string new_text;
    int cur_new_u16 = 0;

    for (const auto& tok : tokens) {
        // Записываем символы токена в новый текст с сохранением маппинга UTF-16
        for (size_t ci = tok.start_char_idx; ci < tok.end_char_idx; ++ci) {
            const auto& ch = chars[ci];
            for (int k = 0; k < ch.u16_len; ++k) {
                if (ch.u16_pos + k < total_orig_u16) {
                    orig_to_new_u16[ch.u16_pos + k] = cur_new_u16 + k;
                }
            }
            new_text.append(text + ch.byte_pos, ch.byte_len);
            cur_new_u16 += ch.u16_len;
        }

        // Если это слово (не пробел), с вероятностью 50% добавляем кринж-эмодзи
        if (!tok.is_space) {
            if (coin(gen) == 1) {
                std::string emo_str = " " + EMOJIS[emo_dist(gen)];
                new_text += emo_str;
                cur_new_u16 += utf16_count(emo_str.data(), emo_str.size());
            }
        }
    }

    // Добавляем завершающий знак
    std::string sign_str = SIGNS[sign_dist(gen)];
    new_text += sign_str;
    cur_new_u16 += utf16_count(sign_str.data(), sign_str.size());

    // Пересчитываем entities под новые позиции
    std::vector<SavedEntity> new_entities;
    for (const auto& oe : orig_entities) {
        if (oe.off < 0 || oe.off >= total_orig_u16) continue;
        int end_u16 = oe.off + oe.len - 1;
        if (end_u16 >= total_orig_u16) end_u16 = total_orig_u16 - 1;
        if (end_u16 < oe.off) continue;

        int new_start = orig_to_new_u16[oe.off];
        int new_end = orig_to_new_u16[end_u16];

        if (new_start < 0 || new_end < new_start) continue;

        int new_len = (new_end + 1) - new_start;
        new_entities.push_back({oe.type, new_start, new_len, oe.docid, oe.url});
    }

    // Собираем сообщение в композере
    api->c_reset(ctx);
    api->c_text(ctx, new_text.c_str());
    if (api->c_entity) {
        for (const auto& ne : new_entities) {
            api->c_entity(ctx, ne.type, ne.off, ne.len, ne.docid, ne.url.empty() ? nullptr : ne.url.c_str());
        }
    }
    api->c_edit(ctx, 0);

    return 0;
}

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    (void)ctx; (void)api;
}

static const koto_command COMMANDS[] = {
    {"picme", cmd_picme, KOTO_LEVEL_TRUSTED, "[ru] Включить/выключить режим пикми (by kote) [en] Toggle PicMe cringe mode (by kote).", nullptr}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "picme",
    "[ru] Кринж-модуль «Режим пикми»: добавляет милые эмодзи и восклицания к вашим сообщениям (сохраняет кастомные эмодзи) (by kote). [en] PicMe cringe mode: adds emojis to outgoing messages, preserving custom emojis (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    on_watch,
    KOTO_WATCH_OUTGOING,
    on_callback,
    nullptr, 0,
    "https://github.com/AresUser1/KoteModules/blob/main/src/picme.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
