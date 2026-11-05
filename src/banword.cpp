// banword.cpp — Модуль для управления запрещёнными словами в чате (C++ ABI 3).
// Портирован из Python-модуля BanWord.py от @AuroraModules (автор оригинального модуля: dend1yya).
// Поддерживает фильтрацию запрещённых слов, действия delete/kick/mute, кастомные премиум-эмодзи и защиту владельца.

#include "module_abi.h"
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <cstdlib>

// ── Кастомные премиум-эмодзи из оригинального модуля @AuroraModules ───────────
static constexpr long long EMOJI_DOC    = 5873153278023307367LL; // 📄
static constexpr long long EMOJI_USER   = 5442879640379076105LL; // 👤
static constexpr long long EMOJI_SHIELD = 5253780051471642059LL; // 🛡
static constexpr long long EMOJI_HEART  = 5231165412275668380LL; // 🥰
static constexpr long long EMOJI_TRASH  = 5445267414562389170LL; // 🗑
static constexpr long long EMOJI_RAGE   = 5287613115180006030LL; // 🤬
static constexpr long long EMOJI_CHECK  = 5398001711786762757LL; // ✅
static constexpr long long EMOJI_CROSS  = 5388785832956016892LL; // ❌
static constexpr long long EMOJI_CHAT   = 5443038326535759644LL; // 💬
static constexpr long long EMOJI_ACTION = 5255999175174137421LL; // 🛡 (action_set)
static constexpr long long EMOJI_LIST   = 5870984130560266604LL; // 💬 (list)

// ── Вспомогательные функции ───────────────────────────────────────────────────

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

// Приведение строки UTF-8 (включая русский и английский) к нижнему регистру
static std::string utf8_tolower(const std::string& str) {
    std::string out;
    out.reserve(str.size());
    size_t i = 0;
    while (i < str.size()) {
        unsigned char c = (unsigned char)str[i];
        if (c < 0x80) {
            out += (char)std::tolower(c);
            i++;
        } else if (c == 0xD0 && i + 1 < str.size()) {
            unsigned char c2 = (unsigned char)str[i + 1];
            if (c2 >= 0x90 && c2 <= 0x9F) {
                // А-П -> а-п (0xD0 0xB0 .. 0xD0 0xBF)
                out += (char)0xD0;
                out += (char)(c2 + 0x20);
            } else if (c2 >= 0xA0 && c2 <= 0xAF) {
                // Р-Я -> р-я (0xD1 0x80 .. 0xD1 0x8F)
                out += (char)0xD1;
                out += (char)(c2 - 0x20);
            } else if (c2 == 0x81) {
                // Ё -> ё (0xD1 0x91)
                out += (char)0xD1;
                out += (char)0x91;
            } else {
                out += (char)c;
                out += (char)c2;
            }
            i += 2;
        } else if (c == 0xD1 && i + 1 < str.size()) {
            unsigned char c2 = (unsigned char)str[i + 1];
            out += (char)c;
            out += (char)c2;
            i += 2;
        } else {
            out += (char)c;
            i++;
        }
    }
    return out;
}

static std::string get_user_mention(koto_ctx* ctx, const koto_api* api, long long uid) {
    char buf[256];
    if (api->user_username && api->user_username(ctx, uid, buf, sizeof(buf)) > 0 && buf[0]) {
        return std::string("@") + buf;
    }
    if (api->user_first_name && api->user_first_name(ctx, uid, buf, sizeof(buf)) > 0 && buf[0]) {
        return buf;
    }
    if (api->user_name && api->user_name(ctx, uid, buf, sizeof(buf)) > 0 && buf[0]) {
        return buf;
    }
    return std::string("ID ") + std::to_string(uid);
}

// ── Хранилище данных (неймспейс модуля в БД) ───────────────────────────────────

static std::vector<std::string> load_banned_words(koto_ctx* ctx, const koto_api* api) {
    std::vector<std::string> res;
    char buf[65536];
    int len = api->db_get(ctx, "banned_words", buf, sizeof(buf) - 1);
    if (len <= 0) return res;
    buf[len] = '\0';

    std::stringstream ss(buf);
    std::string line;
    while (std::getline(ss, line)) {
        line = trim(line);
        if (!line.empty()) {
            res.push_back(line);
        }
    }
    return res;
}

static void save_banned_words(koto_ctx* ctx, const koto_api* api, const std::vector<std::string>& words) {
    std::string s;
    for (size_t i = 0; i < words.size(); ++i) {
        s += words[i];
        s += "\n";
    }
    api->db_set(ctx, "banned_words", s.c_str());
}

static std::vector<long long> load_enabled_chats(koto_ctx* ctx, const koto_api* api) {
    std::vector<long long> res;
    char buf[16384];
    int len = api->db_get(ctx, "enabled_chats", buf, sizeof(buf) - 1);
    if (len <= 0) return res;
    buf[len] = '\0';

    std::stringstream ss(buf);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (!item.empty()) {
            char* end = nullptr;
            long long id = strtoll(item.c_str(), &end, 10);
            if (end != item.c_str() && id != 0) {
                res.push_back(id);
            }
        }
    }
    return res;
}

static void save_enabled_chats(koto_ctx* ctx, const koto_api* api, const std::vector<long long>& chats) {
    std::string s;
    for (size_t i = 0; i < chats.size(); ++i) {
        if (i > 0) s += ",";
        s += std::to_string(chats[i]);
    }
    api->db_set(ctx, "enabled_chats", s.c_str());
}

static bool is_chat_enabled(koto_ctx* ctx, const koto_api* api, long long cid) {
    auto chats = load_enabled_chats(ctx, api);
    for (long long id : chats) {
        if (id == cid) return true;
    }
    return false;
}

static std::string load_action(koto_ctx* ctx, const koto_api* api) {
    char buf[32];
    int len = api->db_get(ctx, "action", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        std::string a = trim(buf);
        if (a == "kick" || a == "mute" || a == "delete") return a;
    }
    if (api->cfg_get) {
        len = api->cfg_get(ctx, "BAN_ACTION", buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            std::string a = trim(buf);
            if (a == "kick" || a == "mute" || a == "delete") return a;
        }
    }
    return "delete";
}

static void save_action(koto_ctx* ctx, const koto_api* api, const std::string& act) {
    api->db_set(ctx, "action", act.c_str());
    if (api->cfg_set) {
        api->cfg_set(ctx, "BAN_ACTION", act.c_str());
    }
}

// ── Обработчики команд ────────────────────────────────────────────────────────

// .bwadd <слово> — Добавляет запрещённое слово
static void cmd_bwadd(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string word = raw ? trim(raw) : "";
    if (word.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "💬", EMOJI_CHAT);
        api->c_fmt(ctx, " Слово не указано.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    auto words = load_banned_words(ctx, api);
    std::string word_lower = utf8_tolower(word);
    for (const auto& w : words) {
        if (utf8_tolower(w) == word_lower) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "⚠️", 0);
            api->c_fmt(ctx, " Слово уже в списке запрещённых: ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, w.c_str(), KOTO_ENT_CODE);
            send_result(ctx, api);
            return;
        }
    }

    words.push_back(word);
    save_banned_words(ctx, api, words);

    api->c_reset(ctx);
    api->c_emoji(ctx, "📄", EMOJI_DOC);
    api->c_fmt(ctx, " Запрещённое слово добавлено: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, word.c_str(), KOTO_ENT_CODE);
    send_result(ctx, api);
}

// .bwdel <слово> — Удаляет запрещённое слово
static void cmd_bwdel(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string word = raw ? trim(raw) : "";
    if (word.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "💬", EMOJI_CHAT);
        api->c_fmt(ctx, " Слово не указано.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    auto words = load_banned_words(ctx, api);
    std::string word_lower = utf8_tolower(word);
    auto it = std::find_if(words.begin(), words.end(), [&](const std::string& w) {
        return utf8_tolower(w) == word_lower;
    });

    if (it == words.end()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_CROSS);
        api->c_fmt(ctx, " Слово не найдено в списке запрещённых: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, word.c_str(), KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

    std::string removed_word = *it;
    words.erase(it);
    save_banned_words(ctx, api, words);

    api->c_reset(ctx);
    api->c_emoji(ctx, "🗑", EMOJI_TRASH);
    api->c_fmt(ctx, " Запрещённое слово удалено: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, removed_word.c_str(), KOTO_ENT_CODE);
    send_result(ctx, api);
}

// .bwon — Включает банворды в текущем чате
static void cmd_bwon(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    auto chats = load_enabled_chats(ctx, api);
    if (std::find(chats.begin(), chats.end(), cid) == chats.end()) {
        chats.push_back(cid);
        save_enabled_chats(ctx, api, chats);
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", EMOJI_CHECK);
    api->c_fmt(ctx, " Банворды включены в этом чате.", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// .bwoff — Отключает банворды в текущем чате
static void cmd_bwoff(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    auto chats = load_enabled_chats(ctx, api);
    auto it = std::find(chats.begin(), chats.end(), cid);
    if (it != chats.end()) {
        chats.erase(it);
        save_enabled_chats(ctx, api, chats);
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "❌", EMOJI_CROSS);
    api->c_fmt(ctx, " Банворды выключены в этом чате.", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// .bword <kick|mute|delete> — Устанавливает действие при нахождении запрещённого слова
static void cmd_bword(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string arg = raw ? trim(raw) : "";
    std::string action = utf8_tolower(arg);

    if (action != "kick" && action != "mute" && action != "delete") {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", 5980953710157632545LL);
        api->c_fmt(ctx, " Действие не указано. Используйте: kick, mute, delete.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    save_action(ctx, api, action);

    api->c_reset(ctx);
    api->c_emoji(ctx, "🛡", EMOJI_ACTION);
    api->c_fmt(ctx, " Действие установлено: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, action.c_str(), KOTO_ENT_CODE);
    send_result(ctx, api);
}

// .bwlist — Выводит список запрещённых слов
static void cmd_bwlist(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    auto words = load_banned_words(ctx, api);
    if (words.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "🤬", EMOJI_RAGE);
        api->c_fmt(ctx, " Список запрещённых слов пуст.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "💬", EMOJI_LIST);
    api->c_fmt(ctx, " Список запрещённых слов:\n", KOTO_ENT_BOLD);
    for (const auto& w : words) {
        api->c_text(ctx, "• ");
        api->c_fmt(ctx, w.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n");
    }
    send_result(ctx, api);
}

// ── Watcher входящих сообщений ────────────────────────────────────────────────

static int on_watch(koto_ctx* ctx, const koto_api* api) {
    // Пропускаем исходящие сообщения
    if (api->is_outgoing(ctx)) return 0;

    long long sid = api->sender_id(ctx);
    long long me = api->get_me(ctx);

    // Защита аккаунта владельца и доверенных лиц
    if (sid == me || sid == 0) return 0;
    if (api->check_permission && api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return 0;

    // Проверяем, включен ли модуль в данном чате
    long long cid = api->chat_id(ctx);
    if (!is_chat_enabled(ctx, api, cid)) return 0;

    const char* text = api->msg_text(ctx);
    if (!text || !text[0]) return 0;

    auto words = load_banned_words(ctx, api);
    if (words.empty()) return 0;

    std::string text_lower = utf8_tolower(text);

    // Поиск совпадений с запрещёнными словами
    bool matched = false;
    for (const auto& w : words) {
        std::string w_lower = utf8_tolower(w);
        if (!w_lower.empty() && text_lower.find(w_lower) != std::string::npos) {
            matched = true;
            break;
        }
    }

    if (!matched) return 0;

    std::string action = load_action(ctx, api);
    long long mid = api->msg_id(ctx);

    if (action == "delete") {
        if (api->delete_message) {
            api->delete_message(ctx, cid, mid);
        }
    } else if (action == "kick") {
        if (api->delete_message) {
            api->delete_message(ctx, cid, mid);
        }
        if (api->kick_user) {
            api->kick_user(ctx, cid, sid);
        }

        std::string username = get_user_mention(ctx, api, sid);
        api->c_reset(ctx);
        api->c_emoji(ctx, "👤", EMOJI_USER);
        api->c_text(ctx, " | Пользователь ");
        api->c_fmt(ctx, username.c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, " использовал запрещённое слово и был кикнут. ");
        api->c_emoji(ctx, "🛡", EMOJI_SHIELD);
        api->c_text(ctx, "\n");
        api->c_emoji(ctx, "🥰", EMOJI_HEART);
        api->c_fmt(ctx, "\nProtected by @AuroraModules", KOTO_ENT_ITALIC);
        api->c_send(ctx, cid, 0);
    } else if (action == "mute") {
        if (api->delete_message) {
            api->delete_message(ctx, cid, mid);
        }
        if (api->mute_user) {
            // Мут на 1 час (3600 секунд) как в оригинальном модуле
            api->mute_user(ctx, cid, sid, 3600);
        }

        std::string username = get_user_mention(ctx, api, sid);
        api->c_reset(ctx);
        api->c_emoji(ctx, "👤", EMOJI_USER);
        api->c_text(ctx, " | Пользователь ");
        api->c_fmt(ctx, username.c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, " использовал запрещённое слово и был замучен на 1 час. ");
        api->c_emoji(ctx, "🛡", EMOJI_SHIELD);
        api->c_text(ctx, "\n");
        api->c_emoji(ctx, "🥰", EMOJI_HEART);
        api->c_fmt(ctx, "\nProtected by @AuroraModules", KOTO_ENT_ITALIC);
        api->c_send(ctx, cid, 0);
    }

    return 1; // Сообщение обработано
}

// ── Реестр команд ─────────────────────────────────────────────────────────────

static const koto_command COMMANDS[] = {
    {
        "bwadd",
        &cmd_bwadd,
        KOTO_LEVEL_TRUSTED,
        "Добавляет запрещённое слово.",
        "<слово>"
    },
    {
        "bwdel",
        &cmd_bwdel,
        KOTO_LEVEL_TRUSTED,
        "Удаляет запрещённое слово.",
        "<слово>"
    },
    {
        "bwon",
        &cmd_bwon,
        KOTO_LEVEL_TRUSTED,
        "Включает банворды в чате.",
        nullptr
    },
    {
        "bwoff",
        &cmd_bwoff,
        KOTO_LEVEL_TRUSTED,
        "Отключает банворды в чате.",
        nullptr
    },
    {
        "bword",
        &cmd_bword,
        KOTO_LEVEL_TRUSTED,
        "Устанавливает действие при нахождении запрещённого слова (kick, mute, delete).",
        "<kick|mute|delete>"
    },
    {
        "bwlist",
        &cmd_bwlist,
        KOTO_LEVEL_TRUSTED,
        "Выводит список запрещённых слов.",
        nullptr
    }
};

// ── Схема настроек модуля для панели .cfg ─────────────────────────────────────

static const koto_setting_field SETTINGS[] = {
    {
        "BAN_ACTION",
        "Действие при нахождении запрещённого слова (kick, mute, delete)",
        KOTO_FIELD_CHOICE,
        "delete",
        "delete,kick,mute"
    }
};

// ── Метаданные модуля ─────────────────────────────────────────────────────────

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "BanWord",
    "Модуль для управления запрещёнными словами в чате.",
    "1.0.0",
    60,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    &on_watch,
    KOTO_WATCH_INCOMING,
    nullptr, // on_callback
    SETTINGS,
    sizeof(SETTINGS) / sizeof(SETTINGS[0]),
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/banword.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
