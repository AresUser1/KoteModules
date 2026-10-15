// tagger.cpp — Мощный модуль для тегирования участников чата (C++ ABI 3).
// Портирован из Python-модуля tagger.py (v3.4.0) с сохранением авторской системы Kote:
// • Режимы: all (все), admins (админы), random N (случайные N участников)
// • Режимы реплая: .itag (include - только упомянутые), .etag (exclude - все кроме упомянутых)
// • Настройки: диапазон задержки (delay_min-max), диапазон пачки (chunk_min-max), позиция тегов (before/after), приоритет (id/username), флаг -id
// • Вайтлисты чатов: .add, .remove, .helps (список исключений)
// • Кастомные ники: .nonick add/del/list (локальные и глобальные -g)
// • Очистка: .cleartag (удаление своих сообщений с тегами в чате)
// • Управление: .stoptag (остановка по ID чата), .tagsettings (инлайн-панель с цветными кнопками)
// • Защита владельца на всех колбэках
// Автор: Kote

#include "module_abi.h"
#include <string>
#include <vector>
#include <set>
#include <map>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <random>

// ── Премиум-эмодзи ────────────────────────────────────────────────────────────
static constexpr long long EMOJI_SETTINGS  = 6032742198179532882LL; // ⚙️
static constexpr long long EMOJI_TAG       = 5890727932011223292LL; // 🏷️
static constexpr long long EMOJI_WHITELIST = 5778299625370817409LL; // 📋
static constexpr long long EMOJI_SUCCESS   = 5774022692642492953LL; // ✅
static constexpr long long EMOJI_ERROR     = 5774077015388852135LL; // ❌
static constexpr long long EMOJI_STOP      = 6030563507299160824LL; // 🛑
static constexpr long long EMOJI_DELAY     = 5983150113483134607LL; // ⏱️
static constexpr long long EMOJI_PRIORITY  = 5890925363067886150LL; // 🏆
static constexpr long long EMOJI_POSITION  = 6042011682497106307LL; // 📍
static constexpr long long EMOJI_USERS     = 6032609071373226027LL; // 👥
static constexpr long long EMOJI_ADMINS    = 5456133934376997871LL; // 👮
static constexpr long long EMOJI_REPLY     = 5974053723322748083LL; // ↩️

// ── Активные процессы тегирования (chat_id) ──────────────────────────────────
static std::set<long long> g_active_tag_chats;

// ── Структура сохраненной сущности ────────────────────────────────────────────
struct SavedEntity {
    int type;
    int off;
    int len;
    long long docid;
    std::string url;
};

// ── Хелперы отправки ──────────────────────────────────────────────────────────
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

// ── Длина UTF-16 для UTF-8 строки ─────────────────────────────────────────────
[[maybe_unused]] static int utf16_len(const char* s, int nbytes) {
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

// ── Конфигурация теггера ──────────────────────────────────────────────────────
struct TaggerConfig {
    std::string priority;   // "id" или "username"
    std::string position;   // "before" или "after"
    bool tag_admins;
    int chunk_min;
    int chunk_max;
    int delay_min;
    int delay_max;
    std::string reply_mode; // "off", "include", "exclude"
};

static TaggerConfig get_config(koto_ctx* ctx, const koto_api* api) {
    TaggerConfig c;
    c.priority = "id";
    c.position = "before";
    c.tag_admins = true;
    c.chunk_min = 5;
    c.chunk_max = 5;
    c.delay_min = 3;
    c.delay_max = 3;
    c.reply_mode = "off";

    char buf[128];
    // 1. Приоритет тега (id или username)
    if (api->cfg_get && api->cfg_get(ctx, "priority", buf, sizeof(buf) - 1) > 0) c.priority = buf;
    else if (api->db_get(ctx, "cfg_priority", buf, sizeof(buf) - 1) > 0) c.priority = buf;

    // 2. Позиция текста (before или after)
    if (api->cfg_get && api->cfg_get(ctx, "position", buf, sizeof(buf) - 1) > 0) c.position = buf;
    else if (api->db_get(ctx, "cfg_position", buf, sizeof(buf) - 1) > 0) c.position = buf;

    // 3. Режим реплая (off, include, exclude)
    if (api->cfg_get && api->cfg_get(ctx, "reply_mode", buf, sizeof(buf) - 1) > 0) c.reply_mode = buf;
    else if (api->db_get(ctx, "cfg_reply_mode", buf, sizeof(buf) - 1) > 0) c.reply_mode = buf;

    // 4. Тегать админов (bool)
    if (api->cfg_get && api->cfg_get(ctx, "tag_admins", buf, sizeof(buf) - 1) > 0) {
        c.tag_admins = (strcmp(buf, "0") != 0 && strcmp(buf, "false") != 0);
    } else if (api->db_get(ctx, "cfg_tag_admins", buf, sizeof(buf) - 1) > 0) {
        c.tag_admins = (strcmp(buf, "0") != 0 && strcmp(buf, "false") != 0);
    }

    // 5. Диапазон участников
    if (api->cfg_get && api->cfg_get(ctx, "chunk_min", buf, sizeof(buf) - 1) > 0) c.chunk_min = atoi(buf);
    else c.chunk_min = (int)api->db_get_int(ctx, "cfg_chunk_min", 5);

    if (api->cfg_get && api->cfg_get(ctx, "chunk_max", buf, sizeof(buf) - 1) > 0) c.chunk_max = atoi(buf);
    else c.chunk_max = (int)api->db_get_int(ctx, "cfg_chunk_max", 5);

    // 6. Диапазон задержки
    if (api->cfg_get && api->cfg_get(ctx, "delay_min", buf, sizeof(buf) - 1) > 0) c.delay_min = atoi(buf);
    else c.delay_min = (int)api->db_get_int(ctx, "cfg_delay_min", 3);

    if (api->cfg_get && api->cfg_get(ctx, "delay_max", buf, sizeof(buf) - 1) > 0) c.delay_max = atoi(buf);
    else c.delay_max = (int)api->db_get_int(ctx, "cfg_delay_max", 3);

    return c;
}

static void save_config(koto_ctx* ctx, const koto_api* api, const TaggerConfig& c) {
    if (api->cfg_set) {
        api->cfg_set(ctx, "priority", c.priority.c_str());
        api->cfg_set(ctx, "position", c.position.c_str());
        api->cfg_set(ctx, "reply_mode", c.reply_mode.c_str());
        api->cfg_set(ctx, "tag_admins", c.tag_admins ? "1" : "0");
        api->cfg_set(ctx, "chunk_min", std::to_string(c.chunk_min).c_str());
        api->cfg_set(ctx, "chunk_max", std::to_string(c.chunk_max).c_str());
        api->cfg_set(ctx, "delay_min", std::to_string(c.delay_min).c_str());
        api->cfg_set(ctx, "delay_max", std::to_string(c.delay_max).c_str());
    }
    api->db_set(ctx, "cfg_priority", c.priority.c_str());
    api->db_set(ctx, "cfg_position", c.position.c_str());
    api->db_set(ctx, "cfg_reply_mode", c.reply_mode.c_str());
    api->db_set(ctx, "cfg_tag_admins", c.tag_admins ? "1" : "0");
    api->db_set_int(ctx, "cfg_chunk_min", c.chunk_min);
    api->db_set_int(ctx, "cfg_chunk_max", c.chunk_max);
    api->db_set_int(ctx, "cfg_delay_min", c.delay_min);
    api->db_set_int(ctx, "cfg_delay_max", c.delay_max);
}

// ── Вайтлисты чатов (список ID исключаемых пользователей) ──────────────────────
static std::set<long long> get_chat_whitelist(koto_ctx* ctx, const koto_api* api, long long cid) {
    std::set<long long> wl;
    std::string key = "wl_" + std::to_string(cid);
    char buf[16384];
    int len = api->db_get(ctx, key.c_str(), buf, sizeof(buf) - 1);
    if (len <= 0) return wl;
    buf[len] = '\0';

    std::istringstream iss(buf);
    std::string token;
    while (iss >> token) {
        char* end = nullptr;
        long long uid = strtoll(token.c_str(), &end, 10);
        if (uid != 0) wl.insert(uid);
    }
    return wl;
}

static void save_chat_whitelist(koto_ctx* ctx, const koto_api* api, long long cid, const std::set<long long>& wl) {
    std::string key = "wl_" + std::to_string(cid);
    std::string val;
    for (long long uid : wl) {
        val += std::to_string(uid) + " ";
    }
    api->db_set(ctx, key.c_str(), val.c_str());
}

// ── Кастомные ники (.nonick) ──────────────────────────────────────────────────
static std::string get_user_custom_nick(koto_ctx* ctx, const koto_api* api, long long cid, long long uid) {
    char buf[256];
    // Сначала локальный ник чата
    std::string local_key = "nick_" + std::to_string(cid) + "_" + std::to_string(uid);
    int len = api->db_get(ctx, local_key.c_str(), buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return std::string(buf);
    }
    // Затем глобальный ник (chat_id = 0)
    std::string global_key = "nick_0_" + std::to_string(uid);
    len = api->db_get(ctx, global_key.c_str(), buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return std::string(buf);
    }
    return "";
}

static void set_user_custom_nick(koto_ctx* ctx, const koto_api* api, long long cid, long long uid, const std::string& name) {
    std::string key = "nick_" + std::to_string(cid) + "_" + std::to_string(uid);
    api->db_set(ctx, key.c_str(), name.c_str());

    // Добавляем в индекс списка
    std::string list_key = "nicks_idx_" + std::to_string(cid);
    char buf[16384];
    int len = api->db_get(ctx, list_key.c_str(), buf, sizeof(buf) - 1);
    std::string cur = (len > 0) ? std::string(buf, len) : "";
    std::string uid_token = std::to_string(uid) + " ";
    if (cur.find(uid_token) == std::string::npos) {
        cur += uid_token;
        api->db_set(ctx, list_key.c_str(), cur.c_str());
    }
}

static void del_user_custom_nick(koto_ctx* ctx, const koto_api* api, long long cid, long long uid) {
    std::string key = "nick_" + std::to_string(cid) + "_" + std::to_string(uid);
    api->db_del(ctx, key.c_str());
}

static std::map<long long, std::string> list_user_custom_nicks(koto_ctx* ctx, const koto_api* api, long long cid) {
    std::map<long long, std::string> out;
    std::string list_key = "nicks_idx_" + std::to_string(cid);
    char buf[16384];
    int len = api->db_get(ctx, list_key.c_str(), buf, sizeof(buf) - 1);
    if (len <= 0) return out;
    buf[len] = '\0';

    std::istringstream iss(buf);
    std::string token;
    while (iss >> token) {
        long long uid = strtoll(token.c_str(), nullptr, 10);
        if (uid != 0) {
            std::string nick = get_user_custom_nick(ctx, api, cid, uid);
            if (!nick.empty()) {
                out[uid] = nick;
            }
        }
    }
    return out;
}

// ── Формирование отображаемого имени для тега ─────────────────────────────────
static std::string get_tag_display_name(koto_ctx* ctx, const koto_api* api, long long cid, long long uid, bool force_id, const std::string& priority) {
    if (force_id) {
        return std::to_string(uid);
    }
    std::string custom = get_user_custom_nick(ctx, api, cid, uid);
    if (!custom.empty()) {
        if (custom == "none") {
            char name_buf[256] = {0};
            if (api->user_name && api->user_name(ctx, uid, name_buf, sizeof(name_buf)) > 0 && name_buf[0]) {
                return name_buf;
            }
        } else {
            return custom;
        }
    }
    if (priority == "username") {
        char un_buf[128] = {0};
        if (api->user_username && api->user_username(ctx, uid, un_buf, sizeof(un_buf)) > 0 && un_buf[0]) {
            return "@" + std::string(un_buf);
        }
    }
    char n_buf[256] = {0};
    if (api->user_first_name && api->user_first_name(ctx, uid, n_buf, sizeof(n_buf)) > 0 && n_buf[0]) {
        return n_buf;
    }
    if (api->user_name && api->user_name(ctx, uid, n_buf, sizeof(n_buf)) > 0 && n_buf[0]) {
        return n_buf;
    }
    return "User " + std::to_string(uid);
}

// ── Извлечение участников из реплая (reply_mode = include / exclude) ──────────
static std::set<long long> extract_users_from_reply(koto_ctx* ctx, const koto_api* api) {
    std::set<long long> ids;
    long long r_sender = api->reply_sender_id ? api->reply_sender_id(ctx) : 0;
    if (r_sender != 0) ids.insert(r_sender);

    if (api->reply_entity_count) {
        int cnt = api->reply_entity_count(ctx);
        for (int i = 0; i < cnt; ++i) {
            int type = 0, off = 0, len = 0;
            long long docid = 0;
            char url[512] = {0};
            if (api->reply_entity(ctx, i, &type, &off, &len, &docid, url, sizeof(url))) {
                if (type == KOTO_ENT_URL && url[0]) {
                    const char* prefix = "tg://user?id=";
                    if (strncmp(url, prefix, strlen(prefix)) == 0) {
                        long long uid = strtoll(url + strlen(prefix), nullptr, 10);
                        if (uid != 0) ids.insert(uid);
                    }
                }
            }
        }
    }

    const char* r_text = api->reply_text ? api->reply_text(ctx) : nullptr;
    if (r_text && api->resolve) {
        std::string txt = r_text;
        size_t p = 0;
        while ((p = txt.find('@', p)) != std::string::npos) {
            size_t end = p + 1;
            while (end < txt.size() && (isalnum((unsigned char)txt[end]) || txt[end] == '_')) end++;
            if (end > p + 1) {
                std::string uname = txt.substr(p + 1, end - p - 1);
                long long uid = api->resolve(ctx, uname.c_str());
                if (uid != 0) ids.insert(uid);
            }
            p = end;
        }
    }
    return ids;
}

// ── Сбор аудитории для тега ───────────────────────────────────────────────────
static std::vector<long long> collect_tag_targets(koto_ctx* ctx, const koto_api* api, long long cid, const std::string& mode, int random_limit, const std::string& force_reply_mode) {
    std::vector<long long> users;
    std::set<long long> seen;
    long long me_id = api->get_me ? api->get_me(ctx) : 0;

    auto config = get_config(ctx, api);
    std::string rmode = force_reply_mode.empty() ? config.reply_mode : force_reply_mode;

    std::set<long long> reply_uids;
    if (rmode != "off") {
        reply_uids = extract_users_from_reply(ctx, api);
    }

    if (rmode == "include" && !reply_uids.empty()) {
        for (long long uid : reply_uids) {
            if (uid != me_id && seen.insert(uid).second) {
                users.push_back(uid);
            }
        }
    } else {
        // Сбор из недавней истории чата
        if (api->get_history_json) {
            char hbuf[65536];
            for (int offset = 0; offset <= 200; offset += 100) {
                int hlen = api->get_history_json(ctx, cid, 0, 100, offset, hbuf, sizeof(hbuf) - 1);
                if (hlen <= 0) break;
                hbuf[hlen] = '\0';
                std::string j(hbuf);

                size_t p = 0;
                while ((p = j.find("\"sender_id\":", p)) != std::string::npos) {
                    size_t colon = j.find(':', p);
                    if (colon != std::string::npos) {
                        size_t vstart = j.find_first_not_of(" \t\r\n", colon + 1);
                        if (vstart != std::string::npos) {
                            char* end = nullptr;
                            long long uid = strtoll(j.c_str() + vstart, &end, 10);
                            if (uid > 0 && uid != me_id) {
                                if (rmode == "exclude" && reply_uids.find(uid) != reply_uids.end()) {
                                    // Пропускаем исключенных
                                } else if (seen.insert(uid).second) {
                                    users.push_back(uid);
                                }
                            }
                        }
                    }
                    p += 12;
                }
            }
        }
    }

    // Исключение по вайтлисту чата
    auto wl = get_chat_whitelist(ctx, api, cid);
    if (!wl.empty()) {
        std::vector<long long> filtered;
        for (long long uid : users) {
            if (wl.find(uid) == wl.end()) {
                filtered.push_back(uid);
            }
        }
        users = filtered;
    }

    // Перемешивание и ограничение
    if (!users.empty()) {
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(users.begin(), users.end(), g);

        if (mode == "random" && random_limit > 0 && (int)users.size() > random_limit) {
            users.resize(random_limit);
        }
    }

    return users;
}

// ── Построение настроечной панели для .tagsettings ─────────────────────────────
static void build_settings_panel(koto_ctx* ctx, const koto_api* api) {
    auto config = get_config(ctx, api);

    std::string delay_str = (config.delay_min != config.delay_max) ?
        (std::to_string(config.delay_min) + "-" + std::to_string(config.delay_max)) : std::to_string(config.delay_min);

    std::string users_str = (config.chunk_min != config.chunk_max) ?
        (std::to_string(config.chunk_min) + "-" + std::to_string(config.chunk_max)) : std::to_string(config.chunk_min);

    std::string rmode_icon;
    if (config.reply_mode == "off") rmode_icon = "🚫 Выкл";
    else if (config.reply_mode == "include") rmode_icon = "🎯 Include";
    else if (config.reply_mode == "exclude") rmode_icon = "🚮 Exclude";
    else rmode_icon = config.reply_mode;

    std::string pos_icon = (config.position == "before") ? "Начало" : "Конец";
    std::string adm_icon = config.tag_admins ? "✅ Вкл" : "❌ Выкл";

    api->c_reset(ctx);
    api->c_emoji(ctx, "🏷️", EMOJI_TAG);
    api->c_fmt(ctx, " Настройки Теггера\n\n", KOTO_ENT_BOLD);

    api->c_emoji(ctx, "⏱️", EMOJI_DELAY);
    api->c_text(ctx, (" Задержка: " + delay_str + " сек.\n").c_str());

    api->c_emoji(ctx, "👥", EMOJI_USERS);
    api->c_text(ctx, (" Юзеров за раз: " + users_str + " чел.\n").c_str());

    api->c_emoji(ctx, "🏆", EMOJI_PRIORITY);
    api->c_text(ctx, (" Имя: " + config.priority + "\n").c_str());

    api->c_emoji(ctx, "📍", EMOJI_POSITION);
    api->c_text(ctx, (" Позиция тегов: " + pos_icon + "\n").c_str());

    api->c_emoji(ctx, "👮", EMOJI_ADMINS);
    api->c_text(ctx, (" Тегать админов: " + adm_icon + "\n").c_str());

    api->c_emoji(ctx, "↩️", EMOJI_REPLY);
    api->c_text(ctx, (" Режим ответа: " + rmode_icon + "\n\n").c_str());

    api->c_fmt(ctx, "ℹ️ Справка по режимам ответа:\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "• Include: тегает только упомянутых в реплае\n");
    api->c_text(ctx, "• Exclude: тегает всех кроме упомянутых в реплае\n");
    api->c_text(ctx, "• Выкл: тегает всех независимо от реплая\n");

    // Кнопки
    if (api->c_button_ex) {
        api->c_button_ex(ctx, "⏱️ Задержка", "ts_set_delay", 1, KOTO_BTN_PRIMARY, 0);
        api->c_button_ex(ctx, "👥 Кол-во", "ts_set_users", 0, KOTO_BTN_PRIMARY, 0);

        api->c_button_ex(ctx, ("🏆 " + config.priority).c_str(), "ts_toggle_priority", 1, KOTO_BTN_DEFAULT, 0);
        api->c_button_ex(ctx, ("📍 " + pos_icon).c_str(), "ts_toggle_position", 0, KOTO_BTN_DEFAULT, 0);

        api->c_button_ex(ctx, ("👮 " + adm_icon).c_str(), "ts_toggle_admins", 1, config.tag_admins ? KOTO_BTN_SUCCESS : KOTO_BTN_DEFAULT, 0);
        api->c_button_ex(ctx, ("↩️ " + rmode_icon).c_str(), "ts_toggle_reply", 0, (config.reply_mode != "off") ? KOTO_BTN_PRIMARY : KOTO_BTN_DEFAULT, 0);

        api->c_button_ex(ctx, "🔄 Обновить", "ts_back", 1, KOTO_BTN_DEFAULT, 0);
        api->c_button_ex(ctx, "❌ Закрыть", "close_panel", 0, KOTO_BTN_DANGER, 0);
    } else {
        api->c_button(ctx, "⏱️ Задержка", "ts_set_delay", 1);
        api->c_button(ctx, "👥 Кол-во", "ts_set_users", 0);
        api->c_button(ctx, ("🏆 " + config.priority).c_str(), "ts_toggle_priority", 1);
        api->c_button(ctx, ("📍 " + pos_icon).c_str(), "ts_toggle_position", 0);
        api->c_button(ctx, ("👮 " + adm_icon).c_str(), "ts_toggle_admins", 1);
        api->c_button(ctx, ("↩️ " + rmode_icon).c_str(), "ts_toggle_reply", 0);
        api->c_button(ctx, "🔄 Обновить", "ts_back", 1);
        api->c_button(ctx, "❌ Закрыть", "close_panel", 0);
    }
}

// ── Основная процедура тегирования ────────────────────────────────────────────
static void execute_tagging(koto_ctx* ctx, const koto_api* api, long long target_chat_id, const std::string& mode, int random_limit, const std::string& template_text, const std::string& force_reply_mode, bool force_id) {
    if (g_active_tag_chats.find(target_chat_id) != g_active_tag_chats.end()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "🛑", EMOJI_STOP);
        api->c_fmt(ctx, (" Тег в чате " + std::to_string(target_chat_id) + " уже запущен. Остановить: .stoptag").c_str(), KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    auto targets = collect_tag_targets(ctx, api, target_chat_id, mode, random_limit, force_reply_mode);
    if (targets.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Нет пользователей для тега (чат пуст или все в вайтлисте).", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    // Сохранение форматирования и Премиум-эмодзи (авторский метод Kote: прем + тег)
    std::string final_template = template_text;
    std::vector<SavedEntity> template_entities;

    if (!template_text.empty() && api->msg_text && api->msg_entity_count && api->msg_entity) {
        const char* raw_msg = api->msg_text(ctx);
        std::string full_msg = raw_msg ? raw_msg : "";
        int template_u16_start = 0;
        size_t bpos = full_msg.find(template_text);
        if (bpos != std::string::npos) {
            template_u16_start = utf16_len(full_msg.c_str(), (int)bpos);
        }
        int template_u16_len = utf16_len(template_text.c_str(), (int)template_text.size());

        int n_ents = api->msg_entity_count(ctx);
        for (int i = 0; i < n_ents; ++i) {
            int type = 0, off = 0, len = 0;
            long long docid = 0;
            char url[512] = {0};
            if (!api->msg_entity(ctx, i, &type, &off, &len, &docid, url, sizeof(url))) continue;
            if (type == KOTO_ENT_NONE) continue;
            if (off >= template_u16_start && (off + len) <= (template_u16_start + template_u16_len)) {
                SavedEntity se;
                se.type = type;
                se.off = off - template_u16_start;
                se.len = len;
                se.docid = docid;
                se.url = url;
                template_entities.push_back(se);
            }
        }
    } else if (final_template.empty() && api->reply_text && api->reply_entity_count && api->reply_entity) {
        const char* rep_t = api->reply_text(ctx);
        if (rep_t && rep_t[0]) {
            final_template = rep_t;
            int n_rep = api->reply_entity_count(ctx);
            for (int i = 0; i < n_rep; ++i) {
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
                template_entities.push_back(se);
            }
        }
    }

    auto config = get_config(ctx, api);
    g_active_tag_chats.insert(target_chat_id);

    // Удаляем исходное сообщение с командой
    if (api->delete_msg) {
        api->delete_msg(ctx);
    }

    int c_min = std::max(1, config.chunk_min);
    int c_max = std::max(c_min, config.chunk_max);

    // Разбиваем на пачки
    size_t idx = 0;
    while (idx < targets.size() && g_active_tag_chats.find(target_chat_id) != g_active_tag_chats.end()) {
        int current_chunk_size = c_min;
        if (c_max > c_min) {
            current_chunk_size = c_min + (rand() % (c_max - c_min + 1));
        }

        api->c_reset(ctx);

        if (config.position == "after" && !final_template.empty()) {
            int base_off = api->c_mark(ctx);
            api->c_text(ctx, final_template.c_str());
            if (!template_entities.empty()) {
                for (const auto& se : template_entities) {
                    api->c_entity(ctx, se.type, base_off + se.off, se.len, se.docid, se.url.empty() ? nullptr : se.url.c_str());
                }
            }
            api->c_text(ctx, "\n");
        }

        bool first_user = true;
        for (int i = 0; i < current_chunk_size && idx < targets.size(); ++i, ++idx) {
            long long uid = targets[idx];
            std::string dname = get_tag_display_name(ctx, api, target_chat_id, uid, force_id, config.priority);
            std::string link = "tg://user?id=" + std::to_string(uid);

            if (!first_user) {
                api->c_text(ctx, " ");
            }
            int u_off = api->c_mark(ctx);
            api->c_text(ctx, dname.c_str());
            int u_len = api->c_mark(ctx) - u_off;
            api->c_entity(ctx, KOTO_ENT_URL, u_off, u_len, 0, link.c_str());
            first_user = false;
        }

        if (config.position == "before" && !final_template.empty()) {
            api->c_text(ctx, "\n");
            int base_off = api->c_mark(ctx);
            api->c_text(ctx, final_template.c_str());
            if (!template_entities.empty()) {
                for (const auto& se : template_entities) {
                    api->c_entity(ctx, se.type, base_off + se.off, se.len, se.docid, se.url.empty() ? nullptr : se.url.c_str());
                }
            }
        }

        api->c_send(ctx, target_chat_id, 0);

        if (idx < targets.size() && g_active_tag_chats.find(target_chat_id) != g_active_tag_chats.end()) {
            int d_min = std::max(1, config.delay_min);
            int d_max = std::max(d_min, config.delay_max);
            int wait_sec = d_min;
            if (d_max > d_min) {
                wait_sec = d_min + (rand() % (d_max - d_min + 1));
            }
            if (wait_sec > 0 && wait_sec <= 60) {
                struct timespec ts;
                ts.tv_sec = wait_sec;
                ts.tv_nsec = 0;
                nanosleep(&ts, nullptr);
            }
        }
    }

    g_active_tag_chats.erase(target_chat_id);
}

// ── .tag ────────────────────────────────────────────────────────────────────
static void cmd_tag(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string full_args = raw ? trim(raw) : "";

    bool force_id = false;
    size_t id_pos = full_args.find("-id");
    if (id_pos != std::string::npos) {
        force_id = true;
        full_args.erase(id_pos, 3);
        full_args = trim(full_args);
    }

    long long target_chat = api->chat_id(ctx);
    std::string mode = "all";
    int random_limit = 0;

    std::istringstream iss(full_args);
    std::string first_word;
    std::string template_text;

    if (iss >> first_word) {
        char* end = nullptr;
        long long maybe_cid = strtoll(first_word.c_str(), &end, 10);
        if (end != first_word.c_str() && std::abs(maybe_cid) >= 100000) {
            target_chat = maybe_cid;
            std::string next_word;
            if (iss >> next_word) {
                if (next_word == "admins") {
                    mode = "admins";
                } else if (next_word == "random") {
                    mode = "random";
                    iss >> random_limit;
                } else {
                    std::string rest;
                    std::getline(iss, rest);
                    template_text = next_word + rest;
                }
            }
        } else if (first_word == "admins") {
            mode = "admins";
        } else if (first_word == "random") {
            mode = "random";
            iss >> random_limit;
        } else {
            std::string rest;
            std::getline(iss, rest);
            template_text = first_word + rest;
        }
        if (template_text.empty()) {
            std::string rest;
            std::getline(iss, rest);
            template_text = trim(rest);
        }
    }

    execute_tagging(ctx, api, target_chat, mode, random_limit, template_text, "", force_id);
}

// ── .itag ───────────────────────────────────────────────────────────────────
static void cmd_itag(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string full_args = raw ? trim(raw) : "";

    bool force_id = false;
    size_t id_pos = full_args.find("-id");
    if (id_pos != std::string::npos) {
        force_id = true;
        full_args.erase(id_pos, 3);
        full_args = trim(full_args);
    }

    execute_tagging(ctx, api, api->chat_id(ctx), "all", 0, full_args, "include", force_id);
}

// ── .etag ───────────────────────────────────────────────────────────────────
static void cmd_etag(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string full_args = raw ? trim(raw) : "";

    bool force_id = false;
    size_t id_pos = full_args.find("-id");
    if (id_pos != std::string::npos) {
        force_id = true;
        full_args.erase(id_pos, 3);
        full_args = trim(full_args);
    }

    execute_tagging(ctx, api, api->chat_id(ctx), "all", 0, full_args, "exclude", force_id);
}

// ── .stoptag ────────────────────────────────────────────────────────────────
static void cmd_stoptag(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? trim(raw) : "";

    long long target = api->chat_id(ctx);
    if (!args.empty()) {
        char* end = nullptr;
        long long maybe_id = strtoll(args.c_str(), &end, 10);
        if (maybe_id != 0) target = maybe_id;
    }

    if (g_active_tag_chats.find(target) == g_active_tag_chats.end()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, (" В чате " + std::to_string(target) + " тег не запущен.").c_str(), KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    g_active_tag_chats.erase(target);
    api->c_reset(ctx);
    api->c_emoji(ctx, "🛑", EMOJI_STOP);
    api->c_fmt(ctx, (" Тегирование остановлено (" + std::to_string(target) + ")!").c_str(), KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// ── .cleartag ───────────────────────────────────────────────────────────────
static void cmd_cleartag(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    long long me = api->get_me ? api->get_me(ctx) : 0;
    int count = 0;

    if (api->get_history_json) {
        char hbuf[65536];
        int hlen = api->get_history_json(ctx, cid, 0, 100, 0, hbuf, sizeof(hbuf) - 1);
        if (hlen > 0) {
            hbuf[hlen] = '\0';
            std::string j(hbuf);
            size_t p = 0;
            while ((p = j.find("\"id\":", p)) != std::string::npos) {
                size_t id_colon = j.find(':', p);
                long long mid = strtoll(j.c_str() + id_colon + 1, nullptr, 10);

                size_t s_pos = j.find("\"sender_id\":", p);
                if (s_pos != std::string::npos && s_pos - p < 150) {
                    size_t s_colon = j.find(':', s_pos);
                    long long sid = strtoll(j.c_str() + s_colon + 1, nullptr, 10);
                    if (sid == me && mid > 0) {
                        if (api->delete_message) {
                            api->delete_message(ctx, cid, mid);
                            count++;
                        }
                    }
                }
                p += 5;
            }
        }
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", EMOJI_SUCCESS);
    api->c_fmt(ctx, (" Удалено сообщений с тегами: " + std::to_string(count)).c_str(), KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// ── .tagdelay ───────────────────────────────────────────────────────────────
static void cmd_tagdelay(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? trim(raw) : "";
    if (args.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Укажите задержку в секундах: .tagdelay <min> [max]", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    std::istringstream iss(args);
    int val1 = 0, val2 = 0;
    iss >> val1;
    if (!(iss >> val2)) val2 = val1;

    auto config = get_config(ctx, api);
    config.delay_min = std::max(0, std::min(val1, val2));
    config.delay_max = std::max(config.delay_min, std::max(val1, val2));
    save_config(ctx, api, config);

    api->c_reset(ctx);
    api->c_emoji(ctx, "⏱️", EMOJI_DELAY);
    api->c_fmt(ctx, (" Задержка установлена: " + std::to_string(config.delay_min) + "-" + std::to_string(config.delay_max) + " сек.").c_str(), KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// ── .tagusers ───────────────────────────────────────────────────────────────
static void cmd_tagusers(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? trim(raw) : "";
    if (args.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Укажите количество: .tagusers <min> [max]", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    std::istringstream iss(args);
    int val1 = 0, val2 = 0;
    iss >> val1;
    if (!(iss >> val2)) val2 = val1;

    auto config = get_config(ctx, api);
    config.chunk_min = std::max(1, std::min(val1, val2));
    config.chunk_max = std::max(config.chunk_min, std::max(val1, val2));
    save_config(ctx, api, config);

    api->c_reset(ctx);
    api->c_emoji(ctx, "👥", EMOJI_USERS);
    api->c_fmt(ctx, (" Юзеров за раз: " + std::to_string(config.chunk_min) + "-" + std::to_string(config.chunk_max)).c_str(), KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// ── .add / .remove / .helps (Вайтлист чата) ──────────────────────────────────
static void cmd_add(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long target = api->reply_sender_id ? api->reply_sender_id(ctx) : 0;
    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? trim(raw) : "";

    if (target == 0 && !args.empty()) {
        if (args[0] == '@' && api->resolve) {
            target = api->resolve(ctx, args.substr(1).c_str());
        } else {
            char* end = nullptr;
            target = strtoll(args.c_str(), &end, 10);
            if (end == args.c_str() && api->resolve) target = api->resolve(ctx, args.c_str());
        }
    }

    if (target == 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Пользователь не найден.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    auto wl = get_chat_whitelist(ctx, api, api->chat_id(ctx));
    wl.insert(target);
    save_chat_whitelist(ctx, api, api->chat_id(ctx), wl);

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", EMOJI_SUCCESS);
    api->c_fmt(ctx, (" Пользователь " + std::to_string(target) + " добавлен в вайтлист.").c_str(), KOTO_ENT_BOLD);
    send_result(ctx, api);
}

static void cmd_remove(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long target = api->reply_sender_id ? api->reply_sender_id(ctx) : 0;
    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? trim(raw) : "";

    if (target == 0 && !args.empty()) {
        if (args[0] == '@' && api->resolve) {
            target = api->resolve(ctx, args.substr(1).c_str());
        } else {
            char* end = nullptr;
            target = strtoll(args.c_str(), &end, 10);
            if (end == args.c_str() && api->resolve) target = api->resolve(ctx, args.c_str());
        }
    }

    if (target == 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Пользователь не найден.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    auto wl = get_chat_whitelist(ctx, api, api->chat_id(ctx));
    wl.erase(target);
    save_chat_whitelist(ctx, api, api->chat_id(ctx), wl);

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", EMOJI_SUCCESS);
    api->c_fmt(ctx, (" Пользователь " + std::to_string(target) + " убран из вайтлиста.").c_str(), KOTO_ENT_BOLD);
    send_result(ctx, api);
}

static void cmd_helps(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    auto wl = get_chat_whitelist(ctx, api, api->chat_id(ctx));

    api->c_reset(ctx);
    api->c_emoji(ctx, "📋", EMOJI_WHITELIST);
    api->c_fmt(ctx, (" В вайтлисте чата: " + std::to_string(wl.size()) + " чел.").c_str(), KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// ── .nonick ─────────────────────────────────────────────────────────────────
static void cmd_nonick(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args_str = raw ? trim(raw) : "";

    if (args_str.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "ℹ️", EMOJI_SETTINGS);
        api->c_fmt(ctx, " Использование: .nonick <add/del/list> [@user] [Name] [-g]", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    bool is_global = (args_str.find("-g") != std::string::npos);
    if (is_global) {
        size_t gp = args_str.find("-g");
        args_str.erase(gp, 2);
        args_str = trim(args_str);
    }
    long long cid = is_global ? 0 : api->chat_id(ctx);

    std::istringstream iss(args_str);
    std::string action;
    iss >> action;
    for (auto& c : action) c = (char)std::tolower((unsigned char)c);

    if (action == "list") {
        auto nicks = list_user_custom_nicks(ctx, api, cid);
        api->c_reset(ctx);
        api->c_emoji(ctx, "📋", EMOJI_WHITELIST);
        api->c_fmt(ctx, is_global ? " Ники (Global):\n\n" : " Ники (Local):\n\n", KOTO_ENT_BOLD);
        if (nicks.empty()) {
            api->c_fmt(ctx, "Нет установленных ников.", KOTO_ENT_ITALIC);
        } else {
            for (const auto& kv : nicks) {
                api->c_fmt(ctx, (std::to_string(kv.first) + ": ").c_str(), KOTO_ENT_CODE);
                api->c_text(ctx, (kv.second + "\n").c_str());
            }
        }
        send_result(ctx, api);
        return;
    }

    long long target = api->reply_sender_id ? api->reply_sender_id(ctx) : 0;
    std::string target_token;
    std::string nick_name;

    if (target == 0) {
        iss >> target_token;
        if (!target_token.empty()) {
            if (target_token[0] == '@' && api->resolve) {
                target = api->resolve(ctx, target_token.substr(1).c_str());
            } else {
                char* end = nullptr;
                target = strtoll(target_token.c_str(), &end, 10);
                if (end == target_token.c_str() && api->resolve) target = api->resolve(ctx, target_token.c_str());
            }
        }
    }
    std::string rest;
    std::getline(iss, rest);
    nick_name = trim(rest);

    if (target == 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMOJI_ERROR);
        api->c_fmt(ctx, " Пользователь не найден.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    if (action == "del") {
        del_user_custom_nick(ctx, api, cid, target);
        api->c_reset(ctx);
        api->c_emoji(ctx, "✅", EMOJI_SUCCESS);
        api->c_fmt(ctx, " Кастомный ник удален.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    if (action == "add") {
        if (nick_name.empty()) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", EMOJI_ERROR);
            api->c_fmt(ctx, " Укажите желаемый ник.", KOTO_ENT_BOLD);
            send_result(ctx, api);
            return;
        }
        set_user_custom_nick(ctx, api, cid, target, nick_name);
        api->c_reset(ctx);
        api->c_emoji(ctx, "✅", EMOJI_SUCCESS);
        api->c_fmt(ctx, (" Ник для " + std::to_string(target) + " установлен: " + nick_name).c_str(), KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "❌", EMOJI_ERROR);
    api->c_fmt(ctx, " Неизвестное действие. Используйте: add, del или list.", KOTO_ENT_BOLD);
    send_result(ctx, api);
}

// ── .tagsettings ────────────────────────────────────────────────────────────
static void cmd_tagsettings(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    build_settings_panel(ctx, api);
    send_result(ctx, api);
}

// ── Колбэки инлайн-панели (с защитой владельца) ─────────────────────────────
static void on_callback(koto_ctx* ctx, const koto_api* api) {
    long long sender = api->sender_id ? api->sender_id(ctx) : 0;
    long long me = api->get_me ? api->get_me(ctx) : 0;

    bool allowed = (me != 0 && sender == me);
    if (!allowed && api->check_permission) {
        allowed = (api->check_permission(ctx, KOTO_LEVEL_TRUSTED) != 0);
    }

    if (!allowed) {
        if (api->cb_answer) api->cb_answer(ctx, "⛔ Эта панель только для владельца!", 1);
        return;
    }

    const char* raw_data = api->cb_data ? api->cb_data(ctx) : nullptr;
    if (!raw_data) return;
    std::string data(raw_data);

    if (data == "close_panel") {
        api->c_reset(ctx);
        api->c_fmt(ctx, "Панель закрыта.", KOTO_ENT_ITALIC);
        api->c_edit_message(ctx, api->chat_id(ctx), api->cb_message_id(ctx), 0);
        if (api->cb_answer) api->cb_answer(ctx, "Панель закрыта.", 0);
        return;
    }

    auto config = get_config(ctx, api);

    if (data == "ts_toggle_priority") {
        config.priority = (config.priority == "id") ? "username" : "id";
        save_config(ctx, api, config);
        build_settings_panel(ctx, api);
        api->c_edit_message(ctx, api->chat_id(ctx), api->cb_message_id(ctx), 0);
        if (api->cb_answer) api->cb_answer(ctx, ("Приоритет: " + config.priority).c_str(), 0);
        return;
    }

    if (data == "ts_toggle_position") {
        config.position = (config.position == "before") ? "after" : "before";
        save_config(ctx, api, config);
        build_settings_panel(ctx, api);
        api->c_edit_message(ctx, api->chat_id(ctx), api->cb_message_id(ctx), 0);
        if (api->cb_answer) api->cb_answer(ctx, ("Позиция: " + config.position).c_str(), 0);
        return;
    }

    if (data == "ts_toggle_admins") {
        config.tag_admins = !config.tag_admins;
        save_config(ctx, api, config);
        build_settings_panel(ctx, api);
        api->c_edit_message(ctx, api->chat_id(ctx), api->cb_message_id(ctx), 0);
        if (api->cb_answer) api->cb_answer(ctx, config.tag_admins ? "Админы: Вкл" : "Админы: Выкл", 0);
        return;
    }

    if (data == "ts_toggle_reply") {
        if (config.reply_mode == "off") config.reply_mode = "include";
        else if (config.reply_mode == "include") config.reply_mode = "exclude";
        else config.reply_mode = "off";

        save_config(ctx, api, config);
        build_settings_panel(ctx, api);
        api->c_edit_message(ctx, api->chat_id(ctx), api->cb_message_id(ctx), 0);
        if (api->cb_answer) api->cb_answer(ctx, ("Режим: " + config.reply_mode).c_str(), 0);
        return;
    }

    if (data == "ts_back") {
        build_settings_panel(ctx, api);
        api->c_edit_message(ctx, api->chat_id(ctx), api->cb_message_id(ctx), 0);
        if (api->cb_answer) api->cb_answer(ctx, "Панель обновлена", 0);
        return;
    }

    if (data == "ts_set_delay") {
        if (api->cb_answer) api->cb_answer(ctx, "Используйте: .tagdelay <min> [max]", 1);
        return;
    }

    if (data == "ts_set_users") {
        if (api->cb_answer) api->cb_answer(ctx, "Используйте: .tagusers <min> [max]", 1);
        return;
    }
}

// ── Список команд ────────────────────────────────────────────────────────────
static const koto_command COMMANDS[] = {
    {
        "tag",
        &cmd_tag,
        KOTO_LEVEL_TRUSTED,
        "Запустить тегирование (всех, админов или случайных участников).",
        "[ID] [admins/random N] [-id] [текст]"
    },
    {
        "itag",
        &cmd_itag,
        KOTO_LEVEL_TRUSTED,
        "Принудительный include-тег (только упомянутые в реплае).",
        "[-id] [текст]"
    },
    {
        "etag",
        &cmd_etag,
        KOTO_LEVEL_TRUSTED,
        "Принудительный exclude-тег (все кроме упомянутых в реплае).",
        "[-id] [текст]"
    },
    {
        "stoptag",
        &cmd_stoptag,
        KOTO_LEVEL_TRUSTED,
        "Остановить процесс тегирования в чате.",
        "[ID]"
    },
    {
        "cleartag",
        &cmd_cleartag,
        KOTO_LEVEL_TRUSTED,
        "Удалить недавние свои сообщения с тегами в чате.",
        nullptr
    },
    {
        "tagdelay",
        &cmd_tagdelay,
        KOTO_LEVEL_TRUSTED,
        "Установить диапазон задержки между тегами.",
        "<min> [max]"
    },
    {
        "tagusers",
        &cmd_tagusers,
        KOTO_LEVEL_TRUSTED,
        "Установить диапазон количества юзеров за раз.",
        "<min> [max]"
    },
    {
        "add",
        &cmd_add,
        KOTO_LEVEL_TRUSTED,
        "Добавить пользователя в вайтлист чата (не тегать).",
        "[@user/ID]"
    },
    {
        "remove",
        &cmd_remove,
        KOTO_LEVEL_TRUSTED,
        "Убрать пользователя из вайтлиста чата.",
        "[@user/ID]"
    },
    {
        "helps",
        &cmd_helps,
        KOTO_LEVEL_TRUSTED,
        "Показать количество участников в вайтлисте чата.",
        nullptr
    },
    {
        "nonick",
        &cmd_nonick,
        KOTO_LEVEL_TRUSTED,
        "Управление кастомными никами теггера (add/del/list, -g).",
        "<add/del/list> [@user] [Name] [-g]"
    },
    {
        "tagsettings",
        &cmd_tagsettings,
        KOTO_LEVEL_TRUSTED,
        "Инлайн-панель настроек теггера.",
        nullptr
    }
};

static const koto_setting_field SETTINGS[] = {
    {
        "priority",
        "Приоритет отображения тега",
        KOTO_FIELD_CHOICE,
        "id",
        "id,username"
    },
    {
        "position",
        "Позиция текста сообщения",
        KOTO_FIELD_CHOICE,
        "before",
        "before,after"
    },
    {
        "tag_admins",
        "Тегать администраторов чата",
        KOTO_FIELD_BOOL,
        "1",
        nullptr
    },
    {
        "reply_mode",
        "Режим обработки реплая",
        KOTO_FIELD_CHOICE,
        "off",
        "off,include,exclude"
    },
    {
        "chunk_min",
        "Мин. участников в пачке",
        KOTO_FIELD_INT,
        "5",
        nullptr
    },
    {
        "chunk_max",
        "Макс. участников в пачке",
        KOTO_FIELD_INT,
        "5",
        nullptr
    },
    {
        "delay_min",
        "Мин. задержка (сек)",
        KOTO_FIELD_INT,
        "3",
        nullptr
    },
    {
        "delay_max",
        "Макс. задержка (сек)",
        KOTO_FIELD_INT,
        "3",
        nullptr
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "tagger",
    "Продвинутый теггер участников чата с вайтлистами, кастомными никами и инлайн-панелью (C++ ABI 3).",
    "3.4.0",
    26,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    &on_callback,
    SETTINGS,
    sizeof(SETTINGS) / sizeof(SETTINGS[0]),
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/tagger.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
