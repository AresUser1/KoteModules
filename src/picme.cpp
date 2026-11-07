// picme.cpp — Кринж-модуль «Режим пикми» для Kotogram / KoteLoader (C++ ABI 3).
// Портирован из Python-модуля PicMe (by kote).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"

#include <string>
#include <vector>
#include <sstream>
#include <random>
#include <cstring>
#include <cstdlib>

namespace {

constexpr const char* VERSION = "1.0.0";

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

// ── Watcher: перехват исходящих сообщений ─────────────────────────────────────
int on_watch(koto_ctx* ctx, const koto_api* api) {
    if (!api->is_outgoing(ctx)) return 0;
    if (!is_picme_enabled(ctx, api)) return 0;

    const char* text = api->msg_text ? api->msg_text(ctx) : nullptr;
    if (!text || text[0] == '\0') return 0;

    // Не модифицируем команды (начинающиеся с точки или слэша)
    if (text[0] == '.' || text[0] == '/' || text[0] == '!' || text[0] == ',') {
        return 0;
    }

    // Разбиваем на слова
    std::string s(text);
    std::istringstream iss(s);
    std::string word;
    std::vector<std::string> words;
    while (iss >> word) {
        words.push_back(word);
    }
    if (words.empty()) return 0;

    // Инициализируем генератор случайных чисел
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> coin(0, 1);
    std::uniform_int_distribution<> emo_dist(0, (int)EMOJIS.size() - 1);
    std::uniform_int_distribution<> sign_dist(0, (int)SIGNS.size() - 1);

    std::string result;
    for (size_t i = 0; i < words.size(); ++i) {
        if (i > 0) result += " ";
        result += words[i];
        if (coin(gen) == 1) {
            result += " " + EMOJIS[emo_dist(gen)];
        }
    }
    result += SIGNS[sign_dist(gen)];

    // Редактируем сообщение
    api->c_reset(ctx);
    api->c_text(ctx, result.c_str());
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
    "[ru] Кринж-модуль «Режим пикми»: добавляет милые эмодзи и восклицания к вашим сообщениям (by kote). [en] PicMe cringe mode: adds emojis to outgoing messages (by kote).",
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
