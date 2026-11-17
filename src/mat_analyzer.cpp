// mat_analyzer.cpp
// Анализатор матов в чате за последние 10 000 сообщений (или указанный лимит).
// Порт модуля MatAnalyzer.py (by Killu / @dubai_ip) на Kotogram C++ ABI 3.
//
// Использование:
//   .mats           — проанализировать последние 10 000 сообщений в чате
//   .mats 2000      — проанализировать последние 2000 сообщений

#include "module_abi.h"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstdint>

using json = nlohmann::json;

// Корни матов: слово считается матом, если начинается с одного из них (в нижнем регистре)
static const char* const MAT_ROOTS[] = {
    "ахувается", "ахувши", "ахуев", "ахуеем", "ахуеет", "ахуеешь", "ахуел", "ахуен", "ахуеть",
    "ахуею", "бля", "выебал", "выебанный", "выебать", "выебон", "выебу", "гандон", "доеб", "доёб",
    "долбаеб", "долбаёб", "долбоеб", "долбоёб", "еба", "ёбаная", "ёбанная", "ёбанное", "ёбанные",
    "ёбанный", "ёбаное", "ёбаные", "ёбаный", "ебём", "ебёт", "ебёшь", "ебись", "ебитесь", "ебля",
    "ёбля", "ёбнулся", "ёбнуть", "ебу", "заебавший", "заебал", "заебать", "заебу", "наёб", "наебал",
    "наебать", "наебу", "нехуевенький", "нехуёвенький", "нехуево", "нехуёво", "нехуевый",
    "нехуёвый", "нехуя", "нихуевато", "нихуеватый", "нихуевая", "нихуевенький", "нихуёвенький",
    "нихуево", "нихуёво", "нихуевую", "нихуевые", "нихуевый", "нихуёвый", "нихуевым", "нихуя",
    "объебал", "объебать", "объебу", "отъебал", "отъебать", "отъебу", "переебал", "переебать",
    "переебу", "пизда", "пизде", "пиздёнка", "пиздёныш", "пиздо", "пизду", "пизды", "пиздюк",
    "пиздюли", "пиздюля", "пиздюрить", "пиздюха", "пиздючка", "пиздятина", "подъебал", "подъебать",
    "подъебу", "поебал", "поебать", "поебу", "похуе", "похуи", "похуй", "похую", "похуя", "приеб",
    "приёб", "проеб", "проёб", "разъеб", "разъёб", "сука", "суке", "суки", "сукой", "сукою", "суку",
    "сучар", "сучата", "сучёнок", "сучий", "сучище", "сучка", "уебал", "уебан", "уебать",
    "уебический", "уебище", "уёбище", "уёбка", "уебковский", "уёбок", "уебский", "уебу", "хер",
    "хуё", "хуевенький", "хуево", "хуевый", "хуеглот", "хуегрыз", "хуежор", "хуежуй", "хуем",
    "хуеплет", "хуесос", "хуец", "хуи", "хуй", "хую", "хуя", "шлюха", "шлюхе", "шлюхи", "шлюхой",
    "шлюхою", "шлюху"
};

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Декодирование одного UTF-8 codepoint с приведением кириллицы и латиницы к нижнему регистру
static uint32_t next_cp_lower(const std::string& s, size_t& i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    uint32_t cp = 0;
    if (c < 0x80) {
        cp = c;
        i += 1;
        if (cp >= 'A' && cp <= 'Z') cp = cp - 'A' + 'a';
        return cp;
    } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
        cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
        i += 2;
        if (cp >= 0x0410 && cp <= 0x042F) cp += 0x20; // А-Я -> а-я
        else if (cp == 0x0401) cp = 0x0451;           // Ё -> ё
        return cp;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
        cp = ((c & 0x0F) << 12) |
             ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
             (static_cast<unsigned char>(s[i + 2]) & 0x3F);
        i += 3;
        return cp;
    } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
        cp = ((c & 0x07) << 18) |
             ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
             ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) |
             (static_cast<unsigned char>(s[i + 3]) & 0x3F);
        i += 4;
        return cp;
    }
    i += 1;
    return 0;
}

static bool is_word_cp(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return true;
    if (cp >= '0' && cp <= '9') return true;
    if (cp == '_') return true;
    if (cp >= 0x0430 && cp <= 0x044F) return true; // а-я
    if (cp == 0x0451) return true;                 // ё
    return false;
}

static void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

static bool is_mat_word(const std::string& w) {
    if (w.size() < 6) return false; // 3 кириллических символа = минимум 6 байт
    for (const char* root : MAT_ROOTS) {
        size_t rlen = std::strlen(root);
        if (w.size() >= rlen && std::memcmp(w.data(), root, rlen) == 0) {
            return true;
        }
    }
    return false;
}

static std::string resolve_user_display(koto_ctx* ctx, const koto_api* api, long long uid) {
    char buf[256] = {0};
    if (api->user_username) {
        int n = api->user_username(ctx, uid, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            std::string u = trim(buf);
            if (!u.empty()) return (u[0] == '@' ? u : "@" + u);
        }
    }
    std::string full;
    if (api->user_first_name) {
        int n = api->user_first_name(ctx, uid, buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = '\0'; full = trim(buf); }
    }
    if (api->user_last_name) {
        int n = api->user_last_name(ctx, uid, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            std::string ln = trim(buf);
            if (!ln.empty()) full += (full.empty() ? "" : " ") + ln;
        }
    }
    if (!full.empty()) return full;
    if (api->user_name) {
        int n = api->user_name(ctx, uid, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            std::string nm = trim(buf);
            if (!nm.empty()) return nm;
        }
    }
    return "ID: " + std::to_string(uid);
}

static void reply_or_edit(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing && api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else if (api->c_reply) api->c_reply(ctx, 0);
    else api->c_edit(ctx, 0);
}

static void cmd_mats(koto_ctx* ctx, const koto_api* api) {
    if (!api->get_history_json) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9D\x8C Требуется Kotogram Host >= 27 (get_history_json).", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }

    int limit_total = 10000;
    const char* raw_args = api->cmd_args ? api->cmd_args(ctx) : nullptr;
    std::string args = trim(raw_args ? raw_args : "");
    if (!args.empty()) {
        try {
            int parsed = std::stoi(args);
            if (parsed >= 50 && parsed <= 20000) limit_total = parsed;
        } catch (...) {}
    }

    // Моментальный статус
    api->c_reset(ctx);
    api->c_emoji(ctx, "\xF0\x9F\x94\x8D", 5258274739041883702LL);
    api->c_text(ctx, " Анализирую маты...");
    reply_or_edit(ctx, api);

    long long cid = api->chat_id(ctx);
    long long my_uid = api->sender_id ? api->sender_id(ctx) : 0;
    if (my_uid == 0 && api->get_me) my_uid = api->get_me(ctx);

    std::unordered_map<std::string, int> word_counts;
    std::unordered_map<long long, int> user_counts;
    long long my_mats = 0;
    long long total_mats = 0;

    int fetched_total = 0;
    int next_progress = 1000;
    int offset_id = 0;
    std::vector<char> buf(512 * 1024);

    while (fetched_total < limit_total) {
        int batch_lim = std::min(100, limit_total - fetched_total);
        int n = api->get_history_json(ctx, cid, offset_id, batch_lim, 0,
                                      buf.data(), static_cast<int>(buf.size()) - 1);
        if (n <= 0) break;
        if (n >= static_cast<int>(buf.size())) n = static_cast<int>(buf.size()) - 1;
        buf[n] = '\0';

        json arr = json::parse(buf.data(), buf.data() + n, nullptr, false);
        if (!arr.is_array() || arr.empty()) break;

        int min_id = offset_id;
        for (const auto& m : arr) {
            if (!m.is_object()) continue;
            int mid = m.value("id", 0);
            if (mid > 0 && (min_id == 0 || mid < min_id)) min_id = mid;

            std::string txt = m.value("text", "");
            long long sid = m.value("sender_id", 0LL);

            if (!txt.empty()) {
                size_t i = 0;
                std::string cur;
                int found_in_msg = 0;
                while (i < txt.size()) {
                    uint32_t cp = next_cp_lower(txt, i);
                    if (is_word_cp(cp)) {
                        append_utf8(cur, cp);
                    } else {
                        if (!cur.empty()) {
                            if (is_mat_word(cur)) {
                                word_counts[cur]++;
                                found_in_msg++;
                            }
                            cur.clear();
                        }
                    }
                }
                if (!cur.empty() && is_mat_word(cur)) {
                    word_counts[cur]++;
                    found_in_msg++;
                }
                if (found_in_msg > 0) {
                    total_mats += found_in_msg;
                    if (sid != 0) {
                        user_counts[sid] += found_in_msg;
                        if (sid == my_uid) my_mats += found_in_msg;
                    }
                }
            }
        }

        fetched_total += static_cast<int>(arr.size());
        if (min_id == 0 || min_id == offset_id) {
            break;
        }
        offset_id = min_id;

        if (fetched_total >= next_progress && fetched_total < limit_total) {
            next_progress += 1000;
            api->c_reset(ctx);
            api->c_emoji(ctx, "\xF0\x9F\x94\x8D", 5258274739041883702LL);
            api->c_text(ctx, (" Анализирую маты... (" + std::to_string(fetched_total) + "/" + std::to_string(limit_total) + ")").c_str());
            reply_or_edit(ctx, api);
        }
    }

    if (total_mats == 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "\xF0\x9F\x98\x8E", 5348437144945856263LL);
        api->c_text(ctx, (" В этом чате чисто! За последние " + std::to_string(fetched_total) + " сообщений матов не найдено.").c_str());
        reply_or_edit(ctx, api);
        return;
    }

    std::vector<std::pair<std::string, int>> top_words(word_counts.begin(), word_counts.end());
    std::sort(top_words.begin(), top_words.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });

    std::vector<std::pair<long long, int>> top_users(user_counts.begin(), user_counts.end());
    std::sort(top_users.begin(), top_users.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });

    double pct = std::round((static_cast<double>(my_mats) * 100.0 / static_cast<double>(total_mats)) * 100.0) / 100.0;
    std::ostringstream pct_ss;
    pct_ss << std::fixed << std::setprecision(2) << pct;
    std::string pct_str = pct_ss.str();
    while (pct_str.size() > 1 && pct_str.back() == '0') pct_str.pop_back();
    if (!pct_str.empty() && pct_str.back() == '.') pct_str.pop_back();

    api->c_reset(ctx);
    api->c_emoji(ctx, "\xF0\x9F\xA4\xAC", 5373123633415723713LL);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, ("Статистика матов в чате (проверено " + std::to_string(fetched_total) + " сообщ.):").c_str(), KOTO_ENT_BOLD);
    api->c_text(ctx, "\n\n");

    api->c_fmt(ctx, "Всего матов: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, std::to_string(total_mats).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n");

    api->c_fmt(ctx, "Ваши маты: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, std::to_string(my_mats).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, (" (" + pct_str + "%)\n").c_str());

    api->c_fmt(ctx, "Уникальных матов: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, std::to_string(word_counts.size()).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n\n");

    api->c_fmt(ctx, "Топ-10 матов:\n", KOTO_ENT_BOLD);
    size_t wlim = std::min<size_t>(10, top_words.size());
    for (size_t i = 0; i < wlim; ++i) {
        api->c_text(ctx, (std::to_string(i + 1) + ". " + top_words[i].first + ": ").c_str());
        api->c_fmt(ctx, std::to_string(top_words[i].second).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n");
    }

    api->c_text(ctx, "\n");
    api->c_fmt(ctx, "Топ-матюкальщики:\n", KOTO_ENT_BOLD);
    size_t ulim = std::min<size_t>(10, top_users.size());
    for (size_t i = 0; i < ulim; ++i) {
        std::string uname = resolve_user_display(ctx, api, top_users[i].first);
        api->c_text(ctx, (std::to_string(i + 1) + ". " + uname + ": ").c_str());
        api->c_fmt(ctx, std::to_string(top_users[i].second).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n");
    }

    reply_or_edit(ctx, api);
}

static const koto_command COMMANDS[] = {
    {
        "mats",
        &cmd_mats,
        KOTO_LEVEL_TRUSTED,
        "[ru] Статистика матов в чате за последние N сообщений (по умолч. 10000). [en] Profanity statistics in chat.",
        "[лимит]"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "mat_analyzer",
    "[ru] Анализирует маты в чате за последние 10к сообщений (порт MatAnalyzer.py). C++ ABI 3. [en] Chat profanity analyzer. C++ ABI 3.",
    "1.0.1",
    27,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    nullptr, 0,
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/mat_analyzer.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
