// bakusearch.cpp — Помощник для ведущих мафии (Baku) для Kotogram / KoteLoader (C++ ABI 3).
// Портирован из Python-модуля bakusearch.py (v1.0.2) с сохранением 1:1 логики:
// • Команда .baku [red/blue] (в ответ на сообщение со списком живых игроков)
// • Поиск в истории чата сообщений с отметкой "голосовал" (включая "🎖 Мэр голосовал")
// • Сопоставление с живыми игроками по очищенным никам (clean_nick)
// • Фильтрация по красным (🔴) или синим (🔵)
// • Отправка итогового отчета в Избранное (Saved Messages) и удаление команды
// Автор: Kote

#include "module_abi.h"
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

// ── Очистка ника от мусора (1:1 с Python clean_nick) ─────────────────────────
static std::string clean_nick(std::string nick) {
    // 1. Удаление ](tg://user?id=\d+)
    size_t link_pos = nick.find("](tg://user?id=");
    if (link_pos != std::string::npos) {
        size_t close_pos = nick.find(')', link_pos);
        if (close_pos != std::string::npos) {
            nick.erase(link_pos, close_pos - link_pos + 1);
        }
    }

    // 2. Снятие обрамляющих '[' и ']'
    while (!nick.empty() && (nick.front() == '[' || nick.front() == ']')) nick.erase(nick.begin());
    while (!nick.empty() && (nick.back() == '[' || nick.back() == ']')) nick.pop_back();

    // 3. Удаление 🫀 (UTF-8: \xF0\x9F\xAB\x80)
    const std::string heart = "\xF0\x9F\xAB\x80";
    size_t hp = 0;
    while ((hp = nick.find(heart, hp)) != std::string::npos) {
        nick.erase(hp, heart.size());
    }

    // 4. Удаление ведущих 🔵 (\xF0\x9F\x94\xB5) или 🔴 (\xF0\x9F\x94\xB4) и пробелов
    bool changed = true;
    while (changed) {
        changed = false;
        size_t s = nick.find_first_not_of(" \t\r\n");
        if (s != std::string::npos && s > 0) { nick.erase(0, s); changed = true; }
        const std::string blue = "\xF0\x9F\x94\xB5";
        const std::string red  = "\xF0\x9F\x94\xB4";
        if (nick.rfind(blue, 0) == 0) {
            nick.erase(0, blue.size());
            changed = true;
        } else if (nick.rfind(red, 0) == 0) {
            nick.erase(0, red.size());
            changed = true;
        }
    }

    // 5. Замена типографских / неразрывных пробелов Unicode на обычный пробел
    static const std::vector<std::string> u_spaces = {
        "\xC2\xA0", "\xE2\x80\x80", "\xE2\x80\x81", "\xE2\x80\x82", "\xE2\x80\x83",
        "\xE2\x80\x84", "\xE2\x80\x85", "\xE2\x80\x86", "\xE2\x80\x87", "\xE2\x80\x88",
        "\xE2\x80\x89", "\xE2\x80\x8A", "\xE2\x80\xAF", "\xE2\x81\x9F", "\xE3\x80\x80"
    };
    for (const auto& sp : u_spaces) {
        size_t pos = 0;
        while ((pos = nick.find(sp, pos)) != std::string::npos) {
            nick.replace(pos, sp.size(), " ");
            pos += 1;
        }
    }

    // 6. Схлопывание множественных пробелов и trim
    std::string result;
    bool in_space = false;
    for (char c : nick) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!in_space && !result.empty()) {
                result += ' ';
                in_space = true;
            }
        } else {
            result += c;
            in_space = false;
        }
    }
    if (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}

// ── Парсер JSON-строки (распаковка \n, \", \\) ────────────────────────────────
static std::string json_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char next = s[++i];
            if (next == 'n') out += '\n';
            else if (next == 'r') out += '\r';
            else if (next == 't') out += '\t';
            else if (next == '"') out += '"';
            else if (next == '\\') out += '\\';
            else if (next == '/') out += '/';
            else {
                out += '\\';
                out += next;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

// ── Проверка строки игрока ("1. Ник") ─────────────────────────────────────────
static bool is_player_line(const std::string& line, std::string& out_raw_identifier) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= line.size() || line[i] < '0' || line[i] > '9') return false;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') i++;
    if (i < line.size() && line[i] == '.') {
        i++;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
        out_raw_identifier = line.substr(i);
        return true;
    }
    return false;
}

// ── Извлечение названия чата из JSON ──────────────────────────────────────────
static std::string extract_chat_title(const std::string& json_str) {
    size_t pos = json_str.find("\"title\":");
    if (pos == std::string::npos) return "";
    pos = json_str.find('"', pos + 8);
    if (pos == std::string::npos) return "";
    size_t end_pos = pos + 1;
    while (end_pos < json_str.size()) {
        if (json_str[end_pos] == '"' && json_str[end_pos - 1] != '\\') break;
        end_pos++;
    }
    if (end_pos < json_str.size()) {
        return json_unescape(json_str.substr(pos + 1, end_pos - pos - 1));
    }
    return "";
}

// ── Обработчик команды .baku [red/blue] ────────────────────────────────────────
static void cmd_baku(koto_ctx* ctx, const koto_api* api) {
    // 1. Удаляем вызванное сообщение
    api->delete_msg(ctx);

    long long me_id = api->get_me(ctx);

    // 2. Проверяем наличие reply
    long long reply_id = api->reply_msg_id ? api->reply_msg_id(ctx) : 0;
    const char* reply_text_ptr = api->reply_text(ctx);
    if (!reply_text_ptr || !*reply_text_ptr) {
        api->c_reset(ctx);
        api->c_text(ctx, "⚠️ ");
        api->c_fmt(ctx, "BakuSearch Ошибка:\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "Эту команду нужно использовать в ответ на сообщение со списком живых игроков.");
        api->c_send(ctx, me_id, 0);
        return;
    }

    std::string reply_text = reply_text_ptr;

    // 3. Парсим строки игроков
    std::vector<std::string> player_lines;
    std::vector<std::string> player_identifiers;
    std::stringstream ss(reply_text);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string raw_id;
        if (is_player_line(line, raw_id)) {
            player_lines.push_back(line);
            player_identifiers.push_back(raw_id);
        }
    }

    if (player_lines.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "⚠️ ");
        api->c_fmt(ctx, "BakuSearch Ошибка:\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "В отвеченном сообщении не найдены строки игроков (вида ");
        api->c_fmt(ctx, "1. Ник", KOTO_ENT_CODE);
        api->c_text(ctx, ").");
        api->c_send(ctx, me_id, 0);
        return;
    }

    // 4. Проверяем "Всего: (\d+)"
    size_t total_pos = reply_text.find("Всего:");
    if (total_pos == std::string::npos) total_pos = reply_text.find("Всего :");
    if (total_pos != std::string::npos) {
        size_t d_pos = total_pos;
        while (d_pos < reply_text.size() && (reply_text[d_pos] < '0' || reply_text[d_pos] > '9')) d_pos++;
        if (d_pos < reply_text.size() && reply_text[d_pos] >= '0' && reply_text[d_pos] <= '9') {
            size_t end_d = d_pos;
            while (end_d < reply_text.size() && reply_text[end_d] >= '0' && reply_text[end_d] <= '9') end_d++;
            int total_players = std::atoi(reply_text.substr(d_pos, end_d - d_pos).c_str());
            if (total_players > 0 && (int)player_lines.size() != total_players) {
                api->c_reset(ctx);
                api->c_text(ctx, "⚠️ ");
                api->c_fmt(ctx, "BakuSearch Ошибка:\n", KOTO_ENT_BOLD);
                std::string err = "Указано " + std::to_string(total_players) + " игроков, но найдено " +
                                  std::to_string(player_lines.size()) + " строк.";
                api->c_text(ctx, err.c_str());
                api->c_send(ctx, me_id, 0);
                return;
            }
        }
    }

    // 5. Режим фильтрации (red / blue)
    const char* args_ptr = api->cmd_args(ctx);
    std::string mode = args_ptr ? args_ptr : "";
    for (char& c : mode) c = (char)std::tolower((unsigned char)c);
    {
        size_t s = mode.find_first_not_of(" \t\r\n");
        size_t e = mode.find_last_not_of(" \t\r\n");
        if (s != std::string::npos) mode = mode.substr(s, e - s + 1);
        else mode.clear();
    }

    // 6. Запрашиваем историю чата от reply_id вверх (новее reply_id)
    long long chat_id = api->chat_id(ctx);
    long long trigger_id = api->msg_id(ctx);

    std::vector<char> hist_buf(131072, 0); // 128 KB под 200 сообщений
    int hist_len = 0;
    if (api->get_history_json) {
        if (reply_id > 0) {
            hist_len = api->get_history_json(ctx, chat_id, (int)reply_id, 200, -200, hist_buf.data(), (int)hist_buf.size() - 1);
        } else {
            hist_len = api->get_history_json(ctx, chat_id, (int)trigger_id, 200, 0, hist_buf.data(), (int)hist_buf.size() - 1);
        }
    }
    if (hist_len <= 2) {
        api->c_reset(ctx);
        api->c_text(ctx, "⚠️ ");
        api->c_fmt(ctx, "BakuSearch Ошибка:\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "Голоса не найдены в истории чата (проверено 200 сообщ. вверх).");
        api->c_send(ctx, me_id, 0);
        return;
    }

    std::string hist_json(hist_buf.data(), hist_len);

    // 7. Извлекаем сообщения и записи голосов
    std::vector<std::string> votes_log;
    size_t obj_pos = 0;
    while ((obj_pos = hist_json.find("{\"id\":", obj_pos)) != std::string::npos) {
        size_t end_obj = hist_json.find('}', obj_pos);
        if (end_obj == std::string::npos) break;
        std::string obj_str = hist_json.substr(obj_pos, end_obj - obj_pos + 1);

        // id
        long long mid = 0;
        size_t id_p = obj_str.find("\"id\":");
        if (id_p != std::string::npos) {
            mid = std::atoll(obj_str.c_str() + id_p + 5);
        }
        if (trigger_id != 0 && mid == trigger_id) {
            obj_pos = end_obj + 1;
            continue;
        }
        if (reply_id != 0 && mid < reply_id) {
            obj_pos = end_obj + 1;
            continue;
        }

        // text
        size_t txt_p = obj_str.find("\"text\":\"");
        if (txt_p != std::string::npos) {
            size_t val_start = txt_p + 8;
            size_t val_end = val_start;
            while (val_end < obj_str.size()) {
                if (obj_str[val_end] == '"' && obj_str[val_end - 1] != '\\') break;
                val_end++;
            }
            if (val_end < obj_str.size()) {
                std::string msg_text = json_unescape(obj_str.substr(val_start, val_end - val_start));
                if (msg_text.find("голосовал") != std::string::npos) {
                    votes_log.push_back(msg_text);
                }
            }
        }
        obj_pos = end_obj + 1;
    }

    if (votes_log.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "⚠️ ");
        api->c_fmt(ctx, "BakuSearch Ошибка:\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "Голоса не найдены в истории чата (проверено 200 сообщ. вверх).");
        api->c_send(ctx, me_id, 0);
        return;
    }

    // 8. Собираем уникальные ники проголосовавших
    std::set<std::string> voted_players_nicks;
    for (const auto& vote_text : votes_log) {
        if (vote_text.find("Мэр голосовал") != std::string::npos) {
            voted_players_nicks.insert(clean_nick("🎖 Мэр"));
            continue;
        }

        // Ищем строку с "голосовал"
        std::stringstream vss(vote_text);
        std::string vline;
        while (std::getline(vss, vline)) {
            size_t g_pos = vline.find("голосовал");
            if (g_pos != std::string::npos) {
                std::string voted_nick = vline.substr(0, g_pos);
                std::string cleaned = clean_nick(voted_nick);
                if (!cleaned.empty()) {
                    voted_players_nicks.insert(cleaned);
                }
            }
        }
    }

    // 9. Сверяем со списком живых игроков
    std::vector<std::string> not_voted;
    for (size_t i = 0; i < player_lines.size(); ++i) {
        std::string cleaned_identifier = clean_nick(player_identifiers[i]);
        if (voted_players_nicks.count(cleaned_identifier) == 0) {
            not_voted.push_back(player_lines[i]);
        }
    }

    // 10. Фильтр red / blue
    const std::string red_emoji  = "\xF0\x9F\x94\xB4"; // 🔴
    const std::string blue_emoji = "\xF0\x9F\x94\xB5"; // 🔵
    if (mode == "red") {
        std::vector<std::string> filtered;
        for (const auto& p : not_voted) {
            if (p.find(red_emoji) != std::string::npos) filtered.push_back(p);
        }
        not_voted = std::move(filtered);
    } else if (mode == "blue") {
        std::vector<std::string> filtered;
        for (const auto& p : not_voted) {
            if (p.find(blue_emoji) != std::string::npos) filtered.push_back(p);
        }
        not_voted = std::move(filtered);
    }

    // 11. Получаем имя чата
    char chat_json_buf[4096];
    int chat_json_len = api->chat_full_json ? api->chat_full_json(ctx, chat_id, chat_json_buf, sizeof(chat_json_buf)) : 0;
    std::string chat_title;
    if (chat_json_len > 0) {
        chat_title = extract_chat_title(std::string(chat_json_buf, chat_json_len));
    }
    if (chat_title.empty()) {
        chat_title = "ЛС (" + std::to_string(chat_id) + ")";
    }

    // 12. Отправляем результат в Избранное (Saved Messages)
    api->c_reset(ctx);
    if (!not_voted.empty()) {
        std::string title_line = "Отчёт из чата \"" + chat_title + "\"";
        api->c_fmt(ctx, title_line.c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, "\n\n");
        api->c_fmt(ctx, "Не голосовали:", KOTO_ENT_BOLD);
        api->c_text(ctx, "\n");
        for (size_t i = 0; i < not_voted.size(); ++i) {
            api->c_text(ctx, (not_voted[i] + (i + 1 < not_voted.size() ? "\n" : "")).c_str());
        }
    } else {
        api->c_text(ctx, "✅ ");
        api->c_fmt(ctx, "BakuSearch: Все проголосовали", KOTO_ENT_BOLD);
    }
    api->c_send(ctx, me_id, 0);
}

// ── Декларация команд и регистрация модуля ────────────────────────────────────
static const koto_command BAKUSEARCH_COMMANDS[] = {
    {
        "baku",
        cmd_baku,
        KOTO_LEVEL_TRUSTED,
        "Поиск не проголосовавших игроков",
        "[red/blue]"
    }
};

static const koto_module BAKUSEARCH_MODULE = {
    KOTO_MODULE_ABI,
    "bakusearch",
    "Помощник для ведущих мафии (Baku). Поиск не проголосовавших игроков.",
    "1.0.3",
    27, // min_host_version 27 (0.3.7+)
    0,  // flags
    BAKUSEARCH_COMMANDS,
    sizeof(BAKUSEARCH_COMMANDS) / sizeof(BAKUSEARCH_COMMANDS[0]),
    nullptr, // on_watch
    0,       // watch_flags
    nullptr, // on_callback
    nullptr, // settings
    0,       // settings_count
    "https://t.me/KoteModulesMint"
};

extern "C" const koto_module* koto_module_register(void) {
    return &BAKUSEARCH_MODULE;
}
