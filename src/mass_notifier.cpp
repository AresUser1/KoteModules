// mass_notifier.cpp — Рассылка сообщений участникам чата в ЛС (C++ ABI 3).
// Портирован из Python-модуля mass_notifier.py (v1.0.3).
// Автор: Kote

#include "module_abi.h"
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <algorithm>

static constexpr long long SUCCESS_EMOJI_ID = 5774022692642492953LL; // ✅
static constexpr long long ERROR_EMOJI_ID   = 5985346521103604145LL; // ❌

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

static std::string trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, (last - first + 1));
}

// ── UTF-16 длина nbytes байт UTF-8 строки
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

struct SavedEntity {
    int type;
    int off;
    int len;
    long long docid;
    std::string url;
};

// ── Парсинг sender_id / from_id из истории JSON ─────────────────────────────
static void collect_users_from_history_json(const std::string& json, long long me_id, std::set<long long>& out_users) {
    if (json.empty() || json == "[]") return;

    size_t pos = 0;
    while ((pos = json.find('{', pos)) != std::string::npos) {
        size_t end_pos = json.find('}', pos);
        if (end_pos == std::string::npos) break;
        std::string obj = json.substr(pos, end_pos - pos + 1);

        // Ищем sender_id или from_id
        long long uid = 0;
        size_t id_pos = obj.find("\"sender_id\":");
        if (id_pos == std::string::npos) {
            id_pos = obj.find("\"from_id\":");
        }
        if (id_pos != std::string::npos) {
            size_t colon = obj.find(':', id_pos);
            if (colon != std::string::npos) {
                size_t vstart = obj.find_first_not_of(" \t\r\n", colon + 1);
                if (vstart != std::string::npos) {
                    char* end = nullptr;
                    uid = strtoll(obj.c_str() + vstart, &end, 10);
                }
            }
        }

        // Проверяем, что это не бот
        bool is_bot = false;
        size_t bot_pos = obj.find("\"is_bot\":");
        if (bot_pos != std::string::npos) {
            size_t bcolon = obj.find(':', bot_pos);
            if (bcolon != std::string::npos) {
                size_t bval = obj.find_first_not_of(" \t\r\n", bcolon + 1);
                if (bval != std::string::npos && obj.compare(bval, 4, "true") == 0) {
                    is_bot = true;
                }
            }
        }

        if (uid > 0 && uid != me_id && !is_bot) {
            out_users.insert(uid);
        }
        pos = end_pos + 1;
    }
}

// ── .lsall ──────────────────────────────────────────────────────────────────
static void cmd_lsall(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    long long me_id = api->get_me ? api->get_me(ctx) : 0;

    int chat_type = api->chat_type(ctx);
    if (chat_type != KOTO_CHAT_GROUP && chat_type != KOTO_CHAT_CHANNEL) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
        api->c_fmt(ctx, " Эта команда работает только в группах и супергруппах.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    // Сообщение о сборе аудитории
    api->c_reset(ctx);
    api->c_text(ctx, "⏳ Собираю аудиторию для рассылки...");
    send_result(ctx, api);

    // Извлекаем аргументы
    const char* raw_args = api->cmd_args(ctx);
    std::string custom_text = raw_args ? trim(raw_args) : "";

    // Проверяем reply
    long long reply_id = (api->reply_msg_id) ? api->reply_msg_id(ctx) : 0;
    const char* reply_text = (api->reply_text) ? api->reply_text(ctx) : nullptr;
    std::string rep_text_str = reply_text ? reply_text : "";

    // Сбор участников через историю чата
    std::set<long long> target_users;
    if (api->get_history_json) {
        char hbuf[65536];
        // 1-я пачка
        int hlen = api->get_history_json(ctx, cid, 0, 100, 0, hbuf, sizeof(hbuf) - 1);
        if (hlen > 0) {
            hbuf[hlen] = '\0';
            collect_users_from_history_json(hbuf, me_id, target_users);
        }
        // 2-я пачка (глубже в историю)
        hlen = api->get_history_json(ctx, cid, 0, 100, 100, hbuf, sizeof(hbuf) - 1);
        if (hlen > 0) {
            hbuf[hlen] = '\0';
            collect_users_from_history_json(hbuf, me_id, target_users);
        }
    }

    if (target_users.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "🤷‍♂️ Некому отправлять сообщение (участники не найдены в недавней истории чата).");
        send_result(ctx, api);
        return;
    }

    // Проверяем, есть ли что отправлять
    if (custom_text.empty() && reply_id == 0 && rep_text_str.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", ERROR_EMOJI_ID);
        api->c_fmt(ctx, " Ошибка: Укажи текст команды или сделай реплай на готовое сообщение.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    // Извлекаем entities из сообщения с командой, если передан custom_text
    std::vector<SavedEntity> custom_entities;
    if (!custom_text.empty() && api->msg_entity_count && api->msg_text) {
        const char* full_msg = api->msg_text(ctx);
        if (full_msg) {
            std::string full_str = full_msg;
            size_t text_pos = full_str.find(custom_text);
            if (text_pos != std::string::npos) {
                int prefix_u16_start = utf16_len(full_str.c_str(), (int)text_pos);
                int total_ents = api->msg_entity_count(ctx);
                for (int i = 0; i < total_ents; ++i) {
                    int type = 0, off = 0, len = 0;
                    long long docid = 0;
                    char url[512] = {0};
                    if (api->msg_entity(ctx, i, &type, &off, &len, &docid, url, sizeof(url))) {
                        if (type != KOTO_ENT_NONE && off >= prefix_u16_start) {
                            SavedEntity se;
                            se.type = type;
                            se.off = off - prefix_u16_start;
                            se.len = len;
                            se.docid = docid;
                            se.url = url;
                            custom_entities.push_back(se);
                        }
                    }
                }
            }
        }
    }

    // Извлекаем entities из reply, если custom_text пустой
    std::vector<SavedEntity> reply_entities;
    if (custom_text.empty() && !rep_text_str.empty() && api->reply_entity_count) {
        int r_cnt = api->reply_entity_count(ctx);
        for (int i = 0; i < r_cnt; ++i) {
            int type = 0, off = 0, len = 0;
            long long docid = 0;
            char url[512] = {0};
            if (api->reply_entity(ctx, i, &type, &off, &len, &docid, url, sizeof(url))) {
                if (type != KOTO_ENT_NONE) {
                    SavedEntity se;
                    se.type = type;
                    se.off = off;
                    se.len = len;
                    se.docid = docid;
                    se.url = url;
                    reply_entities.push_back(se);
                }
            }
        }
    }

    // Рассылаем каждому пользователю в ЛС
    int sent_count = 0;
    int error_count = 0;

    // Получаем настройку задержки из UI приложения
    int delay_sec = 0;
    if (api->cfg_get) {
        char buf[32] = {0};
        if (api->cfg_get(ctx, "delay_sec", buf, sizeof(buf) - 1) > 0) {
            delay_sec = atoi(buf);
        }
    }

    size_t uidx = 0;
    for (long long uid : target_users) {
        uidx++;
        if (!custom_text.empty()) {
            api->c_reset(ctx);
            api->c_text(ctx, custom_text.c_str());
            if (!custom_entities.empty()) {
                for (const auto& e : custom_entities) {
                    api->c_entity(ctx, e.type, e.off, e.len, e.docid, e.url.empty() ? nullptr : e.url.c_str());
                }
            }
            api->c_send(ctx, uid, 0);
            sent_count++;
        } else if (reply_id > 0 && api->forward_messages) {
            int mid = (int)reply_id;
            api->forward_messages(ctx, cid, &mid, 1, uid);
            sent_count++;
        } else if (!rep_text_str.empty()) {
            api->c_reset(ctx);
            api->c_text(ctx, rep_text_str.c_str());
            if (!reply_entities.empty()) {
                for (const auto& e : reply_entities) {
                    api->c_entity(ctx, e.type, e.off, e.len, e.docid, e.url.empty() ? nullptr : e.url.c_str());
                }
            }
            api->c_send(ctx, uid, 0);
            sent_count++;
        } else {
            error_count++;
        }

        if (delay_sec > 0 && delay_sec <= 30 && uidx + 1 < target_users.size()) {
            struct timespec ts;
            ts.tv_sec = delay_sec;
            ts.tv_nsec = 0;
            nanosleep(&ts, nullptr);
        }
    }

    // Отчет о завершении рассылки
    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", SUCCESS_EMOJI_ID);
    api->c_fmt(ctx, " Рассылка завершена!\n", KOTO_ENT_BOLD);
    api->c_fmt(ctx, ("Отправлено: " + std::to_string(sent_count) + "\n").c_str(), KOTO_ENT_BOLD);
    api->c_fmt(ctx, ("Ошибок (ЛС закрыта/блок): " + std::to_string(error_count)).c_str(), KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// ── Колбэк (защита владельца) ───────────────────────────────────────────────
static void on_callback(koto_ctx* ctx, const koto_api* api) {
    (void)ctx; (void)api;
}

static const koto_command COMMANDS[] = {
    {
        "lsall",
        &cmd_lsall,
        KOTO_LEVEL_TRUSTED,
        "Рассылка сообщений участникам чата в ЛС.",
        "<текст / реплай на сообщение>"
    }
};

static const koto_setting_field SETTINGS[] = {
    {
        "delay_sec",
        "Задержка между отправками в ЛС (сек)",
        KOTO_FIELD_INT,
        "1",
        nullptr
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "mass_notifier",
    "Модуль для рассылки сообщений участникам чата в ЛС (C++ ABI 3).",
    "1.0.6", 60,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    &on_callback,
    SETTINGS,
    sizeof(SETTINGS) / sizeof(SETTINGS[0]),
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/mass_notifier.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
