// spammer.cpp
// Нативный C++ модуль для быстрого спама сообщениями с полной поддержкой
// форматирования, цитирования ответов, кастомных премиум-эмодзи, остановкой (.stop)
// и гибкой настройкой количества сообщений (до 10000) в приложении.
// Автор: Kote.
//
// Команды:
// .spam [количество] <текст> — начать спам (число опционально, по умолчанию из настроек)
// .stopspam [chat_id]        — остановить активный спам в чате
// .stop [chat_id]            — алиас для остановки активного спама
// .sstop [chat_id]           — быстрый алиас для остановки спама
//
// Настройки:
// default_count              — количество сообщений по умолчанию (настраивается в UI приложения и .cfg)
// max_count                  — максимальный лимит сообщений за раз (по умолчанию 10000)

#include "module_abi.h"
#include <string>
#include <vector>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <algorithm>

// --- ПРЕМИУМ ЭМОДЗИ (из оригинального модуля Spammer) ---
static const long long ROCKET_EMOJI_ID  = 5445284980978621387LL;
static const long long SUCCESS_EMOJI_ID = 5776375003280838798LL;
static const long long ERROR_EMOJI_ID   = 5778527486270770928LL;
static const long long INFO_EMOJI_ID    = 5879785854284599288LL;

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ── UTF-16 длина nbytes байт UTF-8 строки (как в Telethon/Telegram MTProto).
// Codepoint > 0xFFFF считается как 2 единицы (суррогатная пара).
static int utf16_len(const char* s, int nbytes) {
    if (!s || nbytes <= 0) return 0;
    int u = 0, i = 0;
    while (i < nbytes && s[i]) {
        unsigned char c = (unsigned char)s[i];
        int clen; unsigned cp;
        if (c < 0x80)      { clen = 1; cp = c; }
        else if (c < 0xE0) { clen = 2; cp = c & 0x1F; }
        else if (c < 0xF0) { clen = 3; cp = c & 0x0F; }
        else               { clen = 4; cp = c & 0x07; }
        for (int k = 1; k < clen && i + k < nbytes; ++k) {
            cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        }
        u += (cp > 0xFFFF) ? 2 : 1;
        i += clen;
    }
    return u;
}

// ── Декларация настроек модуля для панели приложения и команды .cfg ──────────
static const koto_setting_field SETTINGS[] = {
    {
        "default_count",
        "Количество сообщений по умолчанию",
        KOTO_FIELD_INT,
        "20",
        nullptr
    },
    {
        "max_count",
        "Максимальный лимит сообщений",
        KOTO_FIELD_INT,
        "10000",
        nullptr
    }
};

static long long get_default_count(koto_ctx* ctx, const koto_api* api) {
    long long def = 20;
    if (api && api->cfg_get) {
        char buf[32] = {0};
        int n = api->cfg_get(ctx, "default_count", buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            try {
                long long v = std::stoll(buf);
                if (v > 0) def = v;
            } catch (...) {}
        }
    }
    return def;
}

static long long get_max_count(koto_ctx* ctx, const koto_api* api) {
    long long max_c = 10000;
    if (api && api->cfg_get) {
        char buf[32] = {0};
        int n = api->cfg_get(ctx, "max_count", buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            try {
                long long v = std::stoll(buf);
                if (v > 0) max_c = v;
            } catch (...) {}
        }
    }
    return max_c;
}

struct SavedEntity {
    int type;
    int off;
    int len;
    long long docid;
    std::string url;
};

static void cmd_spam(koto_ctx* ctx, const koto_api* api) {
    // --- 1. Парсинг аргументов команды ---
    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? trim(raw) : "";

    long long default_cnt = get_default_count(ctx, api);
    long long max_cnt = get_max_count(ctx, api);
    long long count = default_cnt;
    std::string user_text = "";

    // Проверяем наличие reply-сообщения
    const char* rep = (api->reply_text) ? api->reply_text(ctx) : nullptr;
    std::string rep_text = rep ? rep : "";
    bool has_reply = !rep_text.empty();

    if (!args.empty()) {
        size_t sp = args.find_first_of(" \t\n");
        std::string first_token = (sp == std::string::npos) ? args : args.substr(0, sp);

        bool is_num = !first_token.empty();
        for (char c : first_token) {
            if (!std::isdigit((unsigned char)c)) {
                is_num = false;
                break;
            }
        }

        if (is_num) {
            try {
                long long parsed = std::stoll(first_token);
                if (parsed > 0) {
                    if (sp != std::string::npos) {
                        // Число + пробел + текст: e.g. "10 Привет" -> count=10, user_text="Привет"
                        count = parsed;
                        user_text = trim(args.substr(sp + 1));
                    } else {
                        // Только число без пробела: e.g. "50"
                        if (has_reply) {
                            // Ответ на сообщение -> count=50, текст берется из ответа
                            count = parsed;
                        } else {
                            // Не ответ -> спамим текст "50" default_cnt раз
                            count = default_cnt;
                            user_text = first_token;
                        }
                    }
                }
            } catch (...) {
                count = default_cnt;
                user_text = args;
            }
        } else {
            // Текст без числа в начале
            user_text = args;
            count = default_cnt;
        }
    }

    // Применяем лимиты
    if (count <= 0) count = default_cnt;
    if (count > max_cnt) count = max_cnt;

    // --- 2. Определение текста и стилей (entities, премиум-эмодзи) для спама ---
    const char* mt = api->msg_text ? api->msg_text(ctx) : nullptr;
    std::string full_msg = mt ? mt : "";

    // Определяем UTF-16 смещение user_text внутри полного текста команды
    int user_text_u16_start = 0;
    int user_text_u16_len = 0;
    if (!user_text.empty() && !full_msg.empty()) {
        size_t bpos = full_msg.rfind(user_text);
        if (bpos != std::string::npos) {
            user_text_u16_start = utf16_len(full_msg.c_str(), (int)bpos);
        }
        user_text_u16_len = utf16_len(user_text.c_str(), (int)user_text.size());
    }

    // Извлекаем сущности (жирный, курсив, премиум-эмодзи, цитаты) из сообщения пользователя
    std::vector<SavedEntity> user_entities;
    int n_msg_ents = api->msg_entity_count ? api->msg_entity_count(ctx) : 0;
    int range_start = user_text_u16_start;
    int range_end = user_text_u16_start + user_text_u16_len;

    for (int i = 0; i < n_msg_ents; ++i) {
        int type = 0, off = 0, len = 0;
        long long docid = 0;
        char url[512] = {0};
        if (!api->msg_entity(ctx, i, &type, &off, &len, &docid, url, sizeof(url))) continue;
        if (type == KOTO_ENT_NONE) continue;

        int ent_start = off;
        int ent_end = off + len;
        int clamped_start = std::max(ent_start, range_start);
        int clamped_end = std::min(ent_end, range_end);

        if (clamped_end > clamped_start) {
            SavedEntity se;
            se.type = type;
            se.off = clamped_start - range_start;
            se.len = clamped_end - clamped_start;
            se.docid = docid;
            se.url = url;
            user_entities.push_back(se);
        }
    }

    // Извлекаем сущности из отвеченного сообщения (reply)
    std::vector<SavedEntity> reply_entities;
    int n_rep_ents = api->reply_entity_count ? api->reply_entity_count(ctx) : 0;
    for (int i = 0; i < n_rep_ents; ++i) {
        int type = 0, off = 0, len = 0;
        long long docid = 0;
        char url[512] = {0};
        if (!api->reply_entity(ctx, i, &type, &off, &len, &docid, url, sizeof(url))) continue;
        if (type == KOTO_ENT_NONE) continue;
        SavedEntity se;
        se.type = type;
        se.off = off;
        se.len = len;
        se.docid = docid;
        se.url = url;
        reply_entities.push_back(se);
    }

    std::string text_to_spam;
    std::vector<SavedEntity> entities_to_spam;

    if (has_reply) {
        if (!user_text.empty()) {
            // СЛУЧАЙ 1: Ответ + свой текст (делаем цитату)
            text_to_spam = rep_text + "\n" + user_text;
            int rep_u16_len = utf16_len(rep_text.c_str(), (int)rep_text.size());

            // Добавляем стиль "цитата" для всего отвеченного текста
            if (rep_u16_len > 0) {
                SavedEntity bq;
                bq.type = KOTO_ENT_BLOCKQUOTE;
                bq.off = 0;
                bq.len = rep_u16_len;
                bq.docid = 0;
                entities_to_spam.push_back(bq);
            }

            // Добавляем оригинальные стили из ответа (включая премиум-эмодзи)
            entities_to_spam.insert(entities_to_spam.end(), reply_entities.begin(), reply_entities.end());

            // Добавляем стили из нового текста пользователя (смещаем их)
            int user_entities_offset = rep_u16_len + 1; // +1 за перевод строки '\n'
            for (auto e : user_entities) {
                e.off += user_entities_offset;
                entities_to_spam.push_back(e);
            }
        } else {
            // СЛУЧАЙ 2: Ответ без своего текста (просто спамим ответом)
            text_to_spam = rep_text;
            entities_to_spam = reply_entities;
        }
    } else {
        // СЛУЧАЙ 3: Не ответ (исходное поведение)
        text_to_spam = user_text;
        entities_to_spam = user_entities;
    }

    // --- 3. Проверка, есть ли что спамить ---
    if (text_to_spam.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
        api->c_fmt(ctx, " Не найден текст для спама. Напишите текст или ответьте на сообщение.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    long long cid = api->chat_id(ctx);

    // --- 4. Запуск спама ---
    // 1. Удаляем сообщение команды
    api->delete_msg(ctx);

    // 2. Отправляем подтверждение старта с премиум-эмодзи ракеты и подсказкой .stop
    api->c_reset(ctx);
    api->c_emoji(ctx, "🚀", ROCKET_EMOJI_ID);
    api->c_fmt(ctx, " Спам запущен! ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, "Количество: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, std::to_string(count).c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, " (Остановить: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, ".stop", KOTO_ENT_CODE);
    api->c_fmt(ctx, ")", KOTO_ENT_BOLD);
    api->c_send(ctx, cid, 0);

    // 3. Записываем активность спама для возможности отмены через .stop / .stopspam
    if (api->db_set) {
        api->db_set(ctx, ("active_" + std::to_string(cid)).c_str(), "1");
        api->db_set(ctx, "last_active_chat", std::to_string(cid).c_str());
        api->db_del(ctx, ("stop_" + std::to_string(cid)).c_str());
    }

    // 4. Формируем тело сообщения со всеми сохраненными премиум-эмодзи и сущностями
    api->c_reset(ctx);
    api->c_text(ctx, text_to_spam.c_str());
    if (!entities_to_spam.empty()) {
        for (const auto& e : entities_to_spam) {
            api->c_entity(ctx, e.type, e.off, e.len, e.docid, e.url.empty() ? nullptr : e.url.c_str());
        }
    } else {
        // Если сырых сущностей не было, но пользователь написал Markdown:
        api->c_reset(ctx);
        api->c_markdown(ctx, text_to_spam.c_str());
    }

    // 5. Очередь на отправку сообщений
    for (long long i = 0; i < count; ++i) {
        api->c_send(ctx, cid, 0);
    }
}

static void cmd_stopspam(koto_ctx* ctx, const koto_api* api) {
    if (!api) return;
    long long cid = api->chat_id(ctx);

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? trim(raw) : "";
    if (!args.empty()) {
        try {
            long long maybe_id = std::stoll(args);
            if (maybe_id != 0) cid = maybe_id;
        } catch (...) {}
    }

    char active_buf[32] = {0};
    int n = (api->db_get) ? api->db_get(ctx, ("active_" + std::to_string(cid)).c_str(), active_buf, sizeof(active_buf) - 1) : -1;
    bool is_active = (n > 0 && active_buf[0] == '1');

    if (!is_active) {
        // Проверяем fallback на последний активный чат спама
        char any_buf[64] = {0};
        int any_n = (api->db_get) ? api->db_get(ctx, "last_active_chat", any_buf, sizeof(any_buf) - 1) : -1;
        if (any_n > 0) {
            try {
                long long last_cid = std::stoll(any_buf);
                if (last_cid != 0) {
                    char last_act[32] = {0};
                    int ln = api->db_get(ctx, ("active_" + std::to_string(last_cid)).c_str(), last_act, sizeof(last_act) - 1);
                    if (ln > 0 && last_act[0] == '1') {
                        cid = last_cid;
                        is_active = true;
                    }
                }
            } catch (...) {}
        }
    }

    if (!is_active) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "ℹ️", INFO_EMOJI_ID);
        api->c_fmt(ctx, " Активных задач спама нет.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    // Сигнал на остановку спама в чате cid
    if (api->db_set) {
        api->db_set(ctx, ("stop_" + std::to_string(cid)).c_str(), "1");
        api->db_del(ctx, ("active_" + std::to_string(cid)).c_str());
        api->db_del(ctx, "last_active_chat");
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "🛑", ERROR_EMOJI_ID);
    api->c_fmt(ctx, (" Спам в чате " + std::to_string(cid) + " успешно остановлен!").c_str(), KOTO_ENT_BOLD);
    api->c_edit(ctx, 0);
}

static const koto_command COMMANDS[] = {
    {
        "spam",
        &cmd_spam,
        KOTO_LEVEL_TRUSTED,
        "Начать спам сообщениями (с сохранением разметки, цитат и премиум-эмодзи).",
        "[количество] <текст>"
    },
    {
        "stopspam",
        &cmd_stopspam,
        KOTO_LEVEL_TRUSTED,
        "Остановить текущую задачу спама в чате.",
        "[chat_id]"
    },
    {
        "stop",
        &cmd_stopspam,
        KOTO_LEVEL_TRUSTED,
        "Остановить текущую задачу спама в чате.",
        "[chat_id]"
    },
    {
        "sstop",
        &cmd_stopspam,
        KOTO_LEVEL_TRUSTED,
        "Остановить текущую задачу спама в чате.",
        "[chat_id]"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "spammer",
    "Быстрый спам сообщениями с поддержкой разметки, цитат, премиум-эмодзи, остановкой (.stop) и настройкой количества сообщений (до 10000). C++ ABI 3.",
    "1.3.0",
    24,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    SETTINGS,
    sizeof(SETTINGS) / sizeof(SETTINGS[0]),
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/spammer.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
