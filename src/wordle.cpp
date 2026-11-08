// wordle.cpp
// Модуль "Отгадай слово" (Wordle) для Kotogram (ABI 3, .so)
// Автор: by kote

#include "module_abi.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <random>
#include <ctime>
#include <cctype>
#include <cstring>
#include <algorithm>
#include <sstream>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace {

constexpr const char* VERSION = "1.3.1";

// ── Встроенный словарь 5-буквенных существительных ─────────────────────────
const char* const DICTIONARY[] = {
    "адрес", "алмаз", "ангел", "арбуз", "базар", "балет", "банан", "баран", "берег", "билет",
    "бланк", "бокал", "борец", "бочка", "буква", "букет", "булка", "вагон", "ванна", "весна",
    "ветер", "вечер", "взрыв", "вишня", "вождь", "волна", "волос", "гараж", "герой", "голос",
    "город", "гость", "грива", "груша", "диван", "дождь", "домик", "доска", "дрель", "дверь",
    "жажда", "живот", "жизнь", "завод", "забор", "закон", "замок", "запах", "заряд", "закат",
    "звено", "зебра", "земля", "зерно", "знамя", "игрок", "икона", "искра", "икона", "кабан",
    "канал", "капля", "карта", "каска", "катер", "киоск", "клоун", "книга", "ковер", "конец",
    "копье", "кораб", "корка", "котел", "котик", "кошка", "кража", "крест", "кукла", "кулак",
    "купец", "курок", "кусок", "лампа", "лента", "лепка", "лидер", "лимон", "линия", "лирик",
    "лодка", "ложка", "маска", "масло", "маяк ", "метро", "мишка", "молот", "моряк", "музей",
    "мышка", "набор", "налог", "народ", "напор", "носок", "ночка", "овощ ", "огонь", "океан",
    "окунь", "опера", "орехи", "орган", "осень", "остров", "отдел", "отдых", "палец", "палка",
    "парус", "пенал", "пепел", "песок", "пилот", "пирог", "плечо", "повар", "поезд", "полка",
    "полюс", "полет", "порох", "поток", "почта", "право", "прием", "приз ", "птица", "пудра",
    "пушка", "пчела", "радио", "рукав", "ручка", "рыбак", "салют", "сапог", "сахар", "север",
    "сетка", "слово", "сосна", "спорт", "ствол", "стена", "стиль", "такси", "театр", "тигр ",
    "ткань", "топор", "трава", "треск", "труба", "туман", "турок", "туфли", "тыква", "улица",
    "факел", "фирма", "фляга", "форма", "хлеб ", "холод", "хомяк", "хвост", "цвет ", "цирк ",
    "чехол", "число", "шапка", "шарик", "шахта", "шлем ", "шпага", "штора", "щенок", "экран",
    "юрист", "ягода", "якорь"
};

constexpr size_t DICT_SIZE = sizeof(DICTIONARY) / sizeof(DICTIONARY[0]);

// ── UTF-8 утилиты ──────────────────────────────────────────────────────────
std::vector<std::string> utf8_split_letters(const std::string& str) {
    std::vector<std::string> letters;
    size_t i = 0;
    while (i < str.size()) {
        unsigned char c = (unsigned char)str[i];
        size_t len = 1;
        if (c >= 0xF0) len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        if (i + len > str.size()) len = 1;
        letters.push_back(str.substr(i, len));
        i += len;
    }
    return letters;
}

std::string utf8_to_lower_letter(const std::string& l) {
    if (l.size() == 1) {
        return std::string(1, (char)tolower((unsigned char)l[0]));
    }
    if (l.size() == 2 && (unsigned char)l[0] == 0xD0) {
        unsigned char b2 = (unsigned char)l[1];
        if (b2 >= 0x90 && b2 <= 0x9F) {
            std::string res; res.push_back((char)0xD0); res.push_back((char)(b2 + 0x20)); return res;
        }
        if (b2 >= 0xA0 && b2 <= 0xAF) {
            std::string res; res.push_back((char)0xD1); res.push_back((char)(b2 - 0x20)); return res;
        }
        if (b2 == 0x81) { // Ё -> ё
            std::string res; res.push_back((char)0xD1); res.push_back((char)0x91); return res;
        }
    }
    return l;
}

std::string utf8_to_upper_letter(const std::string& l) {
    if (l.size() == 1) {
        return std::string(1, (char)toupper((unsigned char)l[0]));
    }
    if (l.size() == 2) {
        unsigned char b1 = (unsigned char)l[0];
        unsigned char b2 = (unsigned char)l[1];
        if (b1 == 0xD0 && b2 >= 0xB0 && b2 <= 0xBF) {
            std::string res; res.push_back((char)0xD0); res.push_back((char)(b2 - 0x20)); return res;
        }
        if (b1 == 0xD1 && b2 >= 0x80 && b2 <= 0x8F) {
            std::string res; res.push_back((char)0xD0); res.push_back((char)(b2 + 0x20)); return res;
        }
        if (b1 == 0xD1 && b2 == 0x91) { // ё -> Ё
            std::string res; res.push_back((char)0xD0); res.push_back((char)0x81); return res;
        }
    }
    return l;
}

bool is_all_alpha(const std::vector<std::string>& letters) {
    for (const auto& l : letters) {
        if (l.size() == 1) {
            if (!isalpha((unsigned char)l[0])) return false;
        } else if (l.size() == 2) {
            unsigned char b1 = (unsigned char)l[0];
            unsigned char b2 = (unsigned char)l[1];
            if (b1 == 0xD0 || b1 == 0xD1) {
                // Кириллица
                continue;
            }
            return false;
        } else {
            return false;
        }
    }
    return true;
}

std::string format_spaced_upper(const std::vector<std::string>& letters) {
    std::string res;
    for (size_t i = 0; i < letters.size(); ++i) {
        if (i > 0) res += " ";
        res += utf8_to_upper_letter(letters[i]);
    }
    return res;
}

std::string get_random_word() {
    static std::mt19937 rng((unsigned)std::time(nullptr));
    std::uniform_int_distribution<size_t> dist(0, DICT_SIZE - 1);
    std::string raw = DICTIONARY[dist(rng)];
    while (!raw.empty() && raw.back() == ' ') raw.pop_back();
    return raw;
}

// ── Оценка Wordle: 2 = зелёный (на месте), 1 = синий (не на месте), 0 = серый/красный (нет)
std::vector<int> evaluate_wordle(const std::vector<std::string>& target,
                                 const std::vector<std::string>& guess) {
    size_t n = target.size();
    std::vector<int> result(n, 0);
    std::vector<bool> target_used(n, false);

    // Проход 1: буквы на своих местах (зелёные)
    for (size_t i = 0; i < n && i < guess.size(); ++i) {
        if (target[i] == guess[i]) {
            result[i] = 2;
            target_used[i] = true;
        }
    }

    // Проход 2: буквы не на своих позициях (синие)
    for (size_t i = 0; i < n && i < guess.size(); ++i) {
        if (result[i] == 2) continue;
        for (size_t j = 0; j < n; ++j) {
            if (!target_used[j] && target[j] == guess[i]) {
                result[i] = 1;
                target_used[j] = true;
                break;
            }
        }
    }

    return result;
}

// ── Структуры игры ─────────────────────────────────────────────────────────
struct GuessRecord {
    std::string word_raw;
    std::vector<std::string> letters;
    std::vector<int> colors; // 2=зеленый, 1=синий, 0=нет
    std::string player_name;
};

struct Game {
    std::string id;
    int64_t chat_id = 0;
    int64_t msg_id = 0;
    int64_t creator_id = 0;
    std::string creator_name;
    int64_t target_opponent_id = 0;
    std::string target_opponent_name;
    bool is_lobby = false;
    std::string target_word;
    std::vector<std::string> target_letters;
    int target_len = 5;
    std::string hint;
    bool hint_revealed = false;
    bool is_custom = false;
    int max_attempts = 6;
    std::vector<GuessRecord> attempts;
    bool is_finished = false;
    bool is_won = false;
    std::string winner_name;
    time_t last_active = 0;
};

struct StagedWord {
    std::string word;
    std::vector<std::string> letters;
    std::string hint;
    time_t created_at = 0;
};

std::atomic<uint64_t> g_game_seq{100};
std::unordered_map<std::string, Game> g_games_by_id;
std::unordered_map<int64_t, std::string> g_chat_to_game;
std::unordered_map<int64_t, StagedWord> g_staged_words;
std::mutex g_mutex;

inline int64_t normalize_cid(int64_t cid) {
    if (cid <= -1000000000000LL) return -(cid + 1000000000000LL);
    if (cid < 0) return -cid;
    return cid;
}

inline int64_t bare_cid(int64_t cid) {
    return normalize_cid(cid);
}


void save_wordle_game_db(koto_ctx* ctx, const koto_api* api, const Game& g) {
    if (!api->db_set) return;
    try {
        json j;
        j["id"] = g.id;
        j["chat_id"] = g.chat_id;
        j["msg_id"] = g.msg_id;
        j["creator_id"] = g.creator_id;
        j["creator_name"] = g.creator_name;
        j["target_opponent_id"] = g.target_opponent_id;
        j["target_opponent_name"] = g.target_opponent_name;
        j["target_word"] = g.target_word;
        j["target_letters"] = g.target_letters;
        j["target_len"] = g.target_len;
        j["max_attempts"] = g.max_attempts;
        j["hint"] = g.hint;
        j["hint_revealed"] = g.hint_revealed;
        j["is_custom"] = g.is_custom;
        j["is_finished"] = g.is_finished;
        j["is_won"] = g.is_won;
        j["is_lobby"] = g.is_lobby;
        j["winner_name"] = g.winner_name;
        j["last_active"] = (long long)g.last_active;
        json atts = json::array();
        for (const auto& a : g.attempts) {
            atts.push_back({
                {"word_raw", a.word_raw},
                {"letters", a.letters},
                {"colors", a.colors},
                {"player_name", a.player_name}
            });
        }
        j["attempts"] = atts;
        std::string s = j.dump();
        std::string key = "wrd_g:" + g.id;
        api->db_set(ctx, key.c_str(), s.c_str());
        if (g.chat_id != 0) {
            api->db_set(ctx, ("wrd_active:" + std::to_string(g.chat_id)).c_str(), g.id.c_str());
        }
    } catch (...) {}
}

bool load_wordle_game_db(koto_ctx* ctx, const koto_api* api, const std::string& gid, Game& out) {
    if (!api->db_get || gid.empty()) return false;
    try {
        char buf[16384] = {0};
        std::string key = "wrd_g:" + gid;
        int n = api->db_get(ctx, key.c_str(), buf, sizeof(buf));
        if (n <= 0) return false;
        json j = json::parse(std::string(buf, n));
        out.id = j.value("id", gid);
        out.chat_id = j.value("chat_id", 0LL);
        out.msg_id = j.value("msg_id", 0LL);
        out.creator_id = j.value("creator_id", 0LL);
        out.creator_name = j.value("creator_name", "");
        out.target_opponent_id = j.value("target_opponent_id", 0LL);
        out.target_opponent_name = j.value("target_opponent_name", "");
        out.target_word = j.value("target_word", "");
        out.target_letters = j.value("target_letters", std::vector<std::string>{});
        out.target_len = j.value("target_len", 5);
        out.max_attempts = j.value("max_attempts", 6);
        out.hint = j.value("hint", "");
        out.hint_revealed = j.value("hint_revealed", false);
        out.is_custom = j.value("is_custom", false);
        out.is_finished = j.value("is_finished", false);
        out.is_won = j.value("is_won", false);
        out.is_lobby = j.value("is_lobby", false);
        out.winner_name = j.value("winner_name", "");
        out.last_active = (time_t)j.value("last_active", 0LL);
        out.attempts.clear();
        if (j.contains("attempts")) {
            for (const auto& aj : j["attempts"]) {
                GuessRecord gr;
                gr.word_raw = aj.value("word_raw", "");
                gr.letters = aj.value("letters", std::vector<std::string>{});
                gr.colors = aj.value("colors", std::vector<int>{});
                gr.player_name = aj.value("player_name", "");
                out.attempts.push_back(gr);
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

Game* find_wordle_game(const std::string& target_id, int64_t cid, int64_t sender, koto_ctx* ctx = nullptr, const koto_api* api = nullptr) {
    // 1. По точному ID игры (GID)
    if (!target_id.empty()) {
        auto it = g_games_by_id.find(target_id);
        if (it != g_games_by_id.end()) return &it->second;

        // Числовой target_id (chat_id или user_id из inline кнопки)
        try {
            int64_t num_id = std::stoll(target_id);
            if (num_id != 0) {
                for (int64_t cand : {num_id, normalize_cid(num_id), bare_cid(num_id), -num_id}) {
                    auto it_c = g_chat_to_game.find(cand);
                    if (it_c != g_chat_to_game.end()) {
                        auto git = g_games_by_id.find(it_c->second);
                        if (git != g_games_by_id.end()) return &git->second;
                    }
                }
                for (auto& p : g_games_by_id) {
                    if (p.second.chat_id == num_id || p.second.creator_id == num_id || p.second.target_opponent_id == num_id) {
                        return &p.second;
                    }
                }
            }
        } catch (...) {}

        // Fallback: загрузка из DB
        if (ctx && api) {
            Game loaded;
            if (load_wordle_game_db(ctx, api, target_id, loaded)) {
                g_games_by_id[target_id] = loaded;
                return &g_games_by_id[target_id];
            }
        }
    }
    // 2. По cid в карте
    if (cid != 0) {
        for (int64_t cand : {cid, normalize_cid(cid), bare_cid(cid)}) {
            auto it = g_chat_to_game.find(cand);
            if (it != g_chat_to_game.end()) {
                auto git = g_games_by_id.find(it->second);
                if (git != g_games_by_id.end()) return &git->second;
            }
        }
        if (ctx && api && api->db_get) {
            char abuf[64] = {0};
            if (api->db_get(ctx, ("wrd_active:" + std::to_string(cid)).c_str(), abuf, sizeof(abuf)) > 0 && abuf[0]) {
                Game loaded;
                if (load_wordle_game_db(ctx, api, abuf, loaded)) {
                    g_games_by_id[abuf] = loaded;
                    g_chat_to_game[cid] = abuf;
                    return &g_games_by_id[abuf];
                }
            }
        }
    }
    // 3. По sender
    if (sender != 0) {
        auto it = g_chat_to_game.find(sender);
        if (it != g_chat_to_game.end()) {
            auto git = g_games_by_id.find(it->second);
            if (git != g_games_by_id.end()) return &git->second;
        }
    }
    return nullptr;
}

// Персистентное хранение для .wset
bool get_staged_word(koto_ctx* ctx, const koto_api* api, int64_t sender, StagedWord& out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_staged_words.find(sender);
    if (it != g_staged_words.end() && !it->second.word.empty()) {
        out = it->second;
        return true;
    }
    if (api->db_get) {
        std::string key = "wstg_" + std::to_string(sender);
        char buf[512] = {0};
        int len = api->db_get(ctx, key.c_str(), buf, sizeof(buf));
        if (len > 0) {
            std::string raw(buf);
            size_t sep = raw.find('\t');
            std::string w = (sep != std::string::npos) ? raw.substr(0, sep) : raw;
            std::string h = (sep != std::string::npos) ? raw.substr(sep + 1) : "";
            auto lets = utf8_split_letters(w);
            if (!lets.empty()) {
                out.word = w;
                out.letters = lets;
                out.hint = h;
                out.created_at = time(nullptr);
                g_staged_words[sender] = out;
                return true;
            }
        }
    }
    return false;
}

void clear_staged_word(koto_ctx* ctx, const koto_api* api, int64_t sender) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_staged_words.erase(sender);
    if (api->db_del) {
        std::string key = "wstg_" + std::to_string(sender);
        api->db_del(ctx, key.c_str());
    }
}

void save_staged_word(koto_ctx* ctx, const koto_api* api, int64_t sender, const StagedWord& stg) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_staged_words[sender] = stg;
    if (api->db_set) {
        std::string key = "wstg_" + std::to_string(sender);
        std::string val = stg.word + "\t" + stg.hint;
        api->db_set(ctx, key.c_str(), val.c_str());
    }
}

inline void add_button_color(koto_ctx* ctx, const koto_api* api, const char* text, const char* data, int is_first, int color) {
    if (api->c_button_ex) {
        api->c_button_ex(ctx, text, data, is_first, color, 0);
    } else {
        api->c_button(ctx, text, data, is_first);
    }
}

// ── Отрисовка клавиатуры и табло ──────────────────────────────────────────
void add_game_buttons(koto_ctx* ctx, const koto_api* api, const Game& g) {
    if (g.is_lobby) {
        // Кнопки лобби
        std::string j_cb = "wrd:j:" + g.id;
        std::string c_cb = "wrd:c:" + g.id;
        add_button_color(ctx, api, "🎮 Присоединиться", j_cb.c_str(), 1, KOTO_BTN_SUCCESS);
        add_button_color(ctx, api, "🏳️ Отменить", c_cb.c_str(), 0, KOTO_BTN_DANGER);
        return;
    }

    int rows_to_show = g.max_attempts;
    if (g.is_finished) {
        rows_to_show = (int)g.attempts.size();
        if (rows_to_show == 0) rows_to_show = 1;
    }

    for (int r = 0; r < rows_to_show; ++r) {
        bool has_attempt = (r < (int)g.attempts.size());
        for (int c = 0; c < g.target_len; ++c) {
            int is_first = (c == 0) ? 1 : 0;
            if (has_attempt && c < (int)g.attempts[r].letters.size()) {
                const auto& letter = g.attempts[r].letters[c];
                int col = (c < (int)g.attempts[r].colors.size()) ? g.attempts[r].colors[c] : 0;
                int btn_color = KOTO_BTN_DANGER;
                if (col == 2) btn_color = KOTO_BTN_SUCCESS;
                else if (col == 1) btn_color = KOTO_BTN_PRIMARY;
                std::string cb_data = "wrd:i:" + std::to_string(col);
                add_button_color(ctx, api, utf8_to_upper_letter(letter).c_str(), cb_data.c_str(), is_first, btn_color);
            } else {
                add_button_color(ctx, api, "·", "wrd:i:-1", is_first, KOTO_BTN_DEFAULT);
            }
        }
    }

    if (!g.is_finished) {
        std::string h_cb = "wrd:h:" + g.id;
        std::string s_cb = "wrd:s:" + g.id;
        add_button_color(ctx, api, "💡 Подсказка", h_cb.c_str(), 1, KOTO_BTN_PRIMARY);
        add_button_color(ctx, api, "❓ Как ходить", "wrd:help", 0, KOTO_BTN_DEFAULT);
        add_button_color(ctx, api, "🏳️ Сдаться", s_cb.c_str(), 0, KOTO_BTN_DANGER);
    } else {
        std::string n_cb = "wrd:n:" + g.id;
        add_button_color(ctx, api, "🔄 Новая игра", n_cb.c_str(), 1, KOTO_BTN_SUCCESS);
    }
}

void render_game(koto_ctx* ctx, const koto_api* api, const Game& g) {
    api->c_reset(ctx);
    api->c_fmt(ctx, "🎮 ОТГАДАЙ СЛОВО (Wordle)\n\n", KOTO_ENT_BOLD);

    if (g.is_lobby) {
        api->c_fmt(ctx, "👥 ЛОББИ ИГРЫ (Ожидание оппонента)\n\n", KOTO_ENT_BOLD);
        std::string info = "👤 Загадал(а): " + (g.creator_name.empty() ? "KoteLoader" : g.creator_name) + "\n";
        info += "📏 Длина слова: " + std::to_string(g.target_len) + "\n";
        info += "🎯 Число попыток: " + std::to_string(g.max_attempts) + "\n\n";
        api->c_text(ctx, info.c_str());
        api->c_fmt(ctx, "Нажмите кнопку «Присоединиться», чтобы принять вызов!\nby kote", KOTO_ENT_ITALIC);
        add_game_buttons(ctx, api, g);
        return;
    }

    if (g.is_finished) {
        if (g.is_won) {
            std::string win_str = "🎉 ПОБЕДА! Слово отгадано: " + format_spaced_upper(g.target_letters) + "\n";
            win_str += "👑 Победитель: " + g.winner_name + " (с " + std::to_string(g.attempts.size()) + "-й попытки)!\n\n";
            api->c_fmt(ctx, win_str.c_str(), KOTO_ENT_BOLD);
        } else {
            std::string lose_str = "💥 Игра окончена!\nЗагаданное слово было: " + format_spaced_upper(g.target_letters) + "\n\n";
            api->c_fmt(ctx, lose_str.c_str(), KOTO_ENT_BOLD);
        }
    } else {
        std::string info = "👤 Загадал(а): " + (g.creator_name.empty() ? "KoteLoader" : g.creator_name) + "\n";
        if (g.target_opponent_id != 0 && !g.target_opponent_name.empty()) {
            info += "🎯 Отгадывает: " + g.target_opponent_name + "\n";
        }
        info += "📏 Букв в слове: " + std::to_string(g.target_len) + "\n";
        info += "🎯 Попытка: " + std::to_string(g.attempts.size()) + " / " + std::to_string(g.max_attempts) + "\n\n";
        api->c_text(ctx, info.c_str());

        if (!g.hint.empty()) {
            if (g.attempts.size() >= 2 || g.hint_revealed) {
                std::string h_str = "💡 Подсказка: " + g.hint + "\n\n";
                api->c_fmt(ctx, h_str.c_str(), KOTO_ENT_ITALIC);
            } else {
                api->c_fmt(ctx, "🔒 Подсказка откроется после 2-й попытки (или нажмите кнопку)\n\n", KOTO_ENT_ITALIC);
            }
        } else if (!g.is_custom) {
            if (g.attempts.size() >= 4 && g.target_letters.size() > 1) {
                std::string h_str = "💡 Подсказка: Последняя буква — «" + utf8_to_upper_letter(g.target_letters.back()) + "»\n\n";
                api->c_fmt(ctx, h_str.c_str(), KOTO_ENT_ITALIC);
            } else if (g.attempts.size() >= 2 && !g.target_letters.empty()) {
                std::string h_str = "💡 Подсказка: Первая буква — «" + utf8_to_upper_letter(g.target_letters.front()) + "»\n\n";
                api->c_fmt(ctx, h_str.c_str(), KOTO_ENT_ITALIC);
            }
        }

        api->c_fmt(ctx, "Отправляйте догадки: .g <слово> (или ответом на это сообщение)\n", KOTO_ENT_CODE);
    }

    api->c_fmt(ctx, "🟩 Верно | 🟦 Есть не там | 🟥 Нет\nby kote", KOTO_ENT_ITALIC);
    add_game_buttons(ctx, api, g);
}

// ── Вспомогательный парсер попыток (-a <число>) ───────────────────────────
int extract_attempts_arg(std::string& args) {
    int attempts = 6;
    for (const char* flag : {"-a ", "/a ", "-p ", "/p "}) {
        size_t pos = args.find(flag);
        if (pos != std::string::npos) {
            int val = 0;
            if (sscanf(args.c_str() + pos + strlen(flag), "%d", &val) == 1 && val >= 3 && val <= 10) {
                attempts = val;
            }
            size_t end_pos = args.find(' ', pos + strlen(flag));
            if (end_pos != std::string::npos) args.erase(pos, end_pos - pos + 1);
            else args.erase(pos);
            break;
        }
    }
    // Также проверяем отдельное число в аргументах (.w rnd 8 или .w 7)
    std::istringstream iss(args);
    std::string token, filtered;
    while (iss >> token) {
        bool is_num = true;
        for (char c : token) if (!isdigit((unsigned char)c)) { is_num = false; break; }
        if (is_num && token.size() <= 2) {
            int val = std::stoi(token);
            if (val >= 3 && val <= 10) {
                attempts = val;
                continue;
            }
        }
        if (!filtered.empty()) filtered += " ";
        filtered += token;
    }
    args = filtered;
    while (!args.empty() && (args.back() == ' ' || args.back() == '\t')) args.pop_back();
    return attempts;
}

// ── Команда: .wset ─────────────────────────────────────────────────────────
void cmd_wset(koto_ctx* ctx, const koto_api* api) {
    long long sender = api->sender_id(ctx);
    const char* raw_args = api->cmd_args(ctx);
    while (raw_args && (*raw_args == ' ' || *raw_args == '\t')) raw_args++;

    if (!raw_args || !*raw_args) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "⚙️ Загадывание секретного слова заранее\n\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, "Использование:\n.wset <слово> [подсказка] [-a 6]\n\n", KOTO_ENT_CODE);
        api->c_fmt(ctx, "Пример с подсказкой:\n.wset комок круглый предмет\n\n", KOTO_ENT_ITALIC);
        api->c_fmt(ctx, "Затем просто напишите .w в любом чате или группе для начала игры!\nby kote", KOTO_ENT_ITALIC);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    std::string args = raw_args;
    extract_attempts_arg(args);

    std::string word_part;
    std::string hint_part;

    size_t space_pos = args.find_first_of(" \t");
    if (space_pos != std::string::npos) {
        word_part = args.substr(0, space_pos);
        hint_part = args.substr(space_pos + 1);
        while (!hint_part.empty() && (hint_part.front() == ' ' || hint_part.front() == '\t')) hint_part.erase(0, 1);
    } else {
        word_part = args;
    }

    auto letters = utf8_split_letters(word_part);
    if (letters.size() < 3 || letters.size() > 8 || !is_all_alpha(letters)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Загадывайте слово от 3 до 8 букв (только буквы без символов)!\n\nПример:\n.wset комок круглый предмет\n\nby kote", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    std::string lower_word;
    for (const auto& l : letters) lower_word += utf8_to_lower_letter(l);

    StagedWord stg;
    stg.word = lower_word;
    stg.letters = utf8_split_letters(lower_word);
    stg.hint = hint_part;
    stg.created_at = time(nullptr);
    save_staged_word(ctx, api, sender, stg);

    api->c_reset(ctx);
    api->c_fmt(ctx, "✅ Слово успешно сохранено в секрете!\n\n", KOTO_ENT_BOLD);
    std::string info = "📏 Букв: " + std::to_string(stg.letters.size()) + "\n";
    if (!stg.hint.empty()) info += "💡 Подсказка: " + stg.hint + "\n";
    info += "\nТеперь перейдите в нужный чат/группу и напишите: .w\nНикто не увидит слово при старте!\nby kote";
    api->c_text(ctx, info.c_str());

    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Запуск игры: .w / .wordle ──────────────────────────────────────────────
void cmd_wordle(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);
    long long sender = api->sender_id(ctx);
    if (cid == 0) cid = sender;

    char name_buf[64] = {0};
    if (api->user_name(ctx, sender, name_buf, sizeof(name_buf)) <= 0 || !name_buf[0]) {
        snprintf(name_buf, sizeof(name_buf), "Игрок_%lld", (sender > 0 ? sender : -sender) % 10000);
    }

    const char* raw_args = api->cmd_args(ctx);
    while (raw_args && (*raw_args == ' ' || *raw_args == '\t')) raw_args++;
    std::string args = raw_args ? raw_args : "";

    int custom_attempts = extract_attempts_arg(args);

    Game g;
    g.id = "wrd_" + std::to_string(++g_game_seq);
    g.chat_id = cid;
    g.creator_id = sender;
    g.last_active = time(nullptr);
    g.max_attempts = custom_attempts;

    // Проверяем оппонента (ответ на сообщение)
    long long r_sender = 0;
    if (api->reply_sender_id) r_sender = api->reply_sender_id(ctx);
    if (r_sender != 0 && r_sender != sender) {
        g.target_opponent_id = r_sender;
        char opp_buf[64] = {0};
        if (api->user_name(ctx, r_sender, opp_buf, sizeof(opp_buf)) > 0 && opp_buf[0]) {
            g.target_opponent_name = opp_buf;
        } else {
            g.target_opponent_name = "Игрок_" + std::to_string(r_sender % 10000);
        }
    }

    // Проверяем наличие заготовленного слова через .wset
    StagedWord stg;
    bool has_staged = get_staged_word(ctx, api, sender, stg);

    if (args.empty() || args == "go" || args == "start") {
        if (has_staged) {
            g.creator_name = name_buf;
            g.is_custom = true;
            g.target_word = stg.word;
            g.target_letters = stg.letters;
            g.target_len = (int)stg.letters.size();
            g.hint = stg.hint;
            clear_staged_word(ctx, api, sender);
        } else {
            // Если в группе и нет заготовки — предлагаем запустить случайное
            g.is_custom = false;
            g.creator_name = "Бот (случайное)";
            g.target_word = get_random_word();
            g.target_letters = utf8_split_letters(g.target_word);
            g.target_len = (int)g.target_letters.size();
        }
    } else if (args == "rnd" || args == "rand" || args == "random" || args == "рандом" || args == "случ") {
        g.is_custom = false;
        g.creator_name = "Бот (случайное)";
        g.target_word = get_random_word();
        g.target_letters = utf8_split_letters(g.target_word);
        g.target_len = (int)g.target_letters.size();
    } else {
        // Прямое слово в аргументе: .w <слово> [подсказка]
        g.creator_name = name_buf;
        g.is_custom = true;

        std::string word_part;
        std::string hint_part;
        size_t space_pos = args.find_first_of(" \t");
        if (space_pos != std::string::npos) {
            word_part = args.substr(0, space_pos);
            hint_part = args.substr(space_pos + 1);
            while (!hint_part.empty() && (hint_part.front() == ' ' || hint_part.front() == '\t')) hint_part.erase(0, 1);
        } else {
            word_part = args;
        }

        auto letters = utf8_split_letters(word_part);
        if (letters.size() < 3 || letters.size() > 8 || !is_all_alpha(letters)) {
            api->c_reset(ctx);
            api->c_fmt(ctx, "❌ Загадывайте слово от 3 до 8 букв!\n\nИли запустите случайное: .w rnd\nby kote", KOTO_ENT_BOLD);
            if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
            else api->c_reply(ctx, 0);
            return;
        }

        std::string lower_word;
        for (const auto& l : letters) lower_word += utf8_to_lower_letter(l);
        g.target_word = lower_word;
        g.target_letters = utf8_split_letters(lower_word);
        g.target_len = (int)g.target_letters.size();
        g.hint = hint_part;
    }

    // Если запущено в группе/канале (cid < 0) и соперник не указан — включаем режим лобби
    if (cid < 0 && g.target_opponent_id == 0) {
        g.is_lobby = true;
    }

    render_game(ctx, api, g);

    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }

    int64_t cmd_mid = api->msg_id(ctx);
    g.msg_id = (cmd_mid > 0) ? (cmd_mid + 1) : 0;

    std::lock_guard<std::mutex> lock(g_mutex);
    save_wordle_game_db(ctx, api, g);
    g_games_by_id[g.id] = g;
    g_chat_to_game[cid] = g.id;
    g_chat_to_game[normalize_cid(cid)] = g.id;
    g_chat_to_game[bare_cid(cid)] = g.id;
    if (sender != 0) g_chat_to_game[sender] = g.id;
}

// ── Обработка попытки (guess) ──────────────────────────────────────────────
bool process_guess(koto_ctx* ctx, const koto_api* api, int64_t cid, int64_t sender,
                   const std::string& sender_name, const std::string& guess_text) {
    std::unique_lock<std::mutex> lock(g_mutex);
    Game* gp = find_wordle_game("", cid, sender);
    if (!gp || gp->is_finished) return false;

    Game& g = *gp;

    // Если игра в лобби — нельзя угадывать без присоединения
    if (g.is_lobby) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "⏳ Игра ждет оппонента! Нажмите «Присоединиться» на табло.", KOTO_ENT_ITALIC);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return true;
    }

    // Защита оппонента: если игра привязана к конкретному игроку, посторонние не могут ходить
    if (g.target_opponent_id != 0 && sender != g.target_opponent_id) {
        // В группах просто игнорируем посторонних без спама в чат
        return true;
    }

    // Автор загадки не может сам отгадывать своё слово
    if (g.is_custom && sender == g.creator_id) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Вы сами загадали это слово! Ждите ответа соперника.", KOTO_ENT_ITALIC);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return true;
    }

    auto raw_letters = utf8_split_letters(guess_text);
    if ((int)raw_letters.size() != g.target_len) {
        std::string err = "❌ В слове должно быть " + std::to_string(g.target_len) + " букв (вы ввели " + std::to_string(raw_letters.size()) + ")!";
        api->c_reset(ctx);
        api->c_fmt(ctx, err.c_str(), KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return true;
    }

    if (!is_all_alpha(raw_letters)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Используйте только буквы!", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return true;
    }

    std::vector<std::string> guess_letters;
    std::string lower_guess;
    for (const auto& l : raw_letters) {
        std::string low = utf8_to_lower_letter(l);
        guess_letters.push_back(low);
        lower_guess += low;
    }

    // Проверяем повторы
    for (const auto& att : g.attempts) {
        if (att.word_raw == lower_guess) {
            api->c_reset(ctx);
            api->c_fmt(ctx, "⚠️ Это слово уже проверялось в текущей игре!", KOTO_ENT_ITALIC);
            if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
            else api->c_reply(ctx, 0);
            return true;
        }
    }

    auto colors = evaluate_wordle(g.target_letters, guess_letters);

    GuessRecord rec;
    rec.word_raw = lower_guess;
    rec.letters = guess_letters;
    rec.colors = colors;
    rec.player_name = sender_name;
    g.attempts.push_back(rec);
    g.last_active = time(nullptr);

    // Проверка победы (все 2)
    bool won = true;
    for (int c : colors) {
        if (c != 2) { won = false; break; }
    }

    if (won) {
        g.is_finished = true;
        g.is_won = true;
        g.winner_name = sender_name;
    } else if ((int)g.attempts.size() >= g.max_attempts) {
        g.is_finished = true;
        g.is_won = false;
    }

    // Удаляем команду .g <слово>, чтобы не засорять чат
    if (api->is_outgoing(ctx) && api->delete_msg) {
        api->delete_msg(ctx);
    }

    save_wordle_game_db(ctx, api, g);
    render_game(ctx, api, g);

    int64_t target_chat = (cid != 0) ? cid : g.chat_id;
    if (g.msg_id != 0) {
        api->c_edit_message(ctx, target_chat, g.msg_id, 0);
    } else {
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    }

    return true;
}

void cmd_guess(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);
    long long sender = api->sender_id(ctx);
    if (cid == 0) cid = sender;

    char name_buf[64] = {0};
    if (api->user_name(ctx, sender, name_buf, sizeof(name_buf)) <= 0 || !name_buf[0]) {
        snprintf(name_buf, sizeof(name_buf), "Игрок_%lld", (sender > 0 ? sender : -sender) % 10000);
    }

    const char* raw_args = api->cmd_args(ctx);
    while (raw_args && (*raw_args == ' ' || *raw_args == '\t')) raw_args++;

    if (!raw_args || !*raw_args) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❓ Использование: .g <слово>\n\nНапример: .g комок\nby kote", KOTO_ENT_ITALIC);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    std::string guess_text = raw_args;
    while (!guess_text.empty() && (guess_text.back() == ' ' || guess_text.back() == '\t')) guess_text.pop_back();

    if (!process_guess(ctx, api, cid, sender, name_buf, guess_text)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Активная игра Wordle не найдена в этом чате!\nЗапустите: .w rnd", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    }
}

void cmd_whint(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);
    long long sender = api->sender_id(ctx);
    if (cid == 0) cid = sender;

    std::lock_guard<std::mutex> lock(g_mutex);
    Game* gp = find_wordle_game("", cid, sender);
    if (!gp || gp->is_finished) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Нет активной игры в этом чате.", KOTO_ENT_ITALIC);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    Game& g = *gp;
    std::string h = g.hint;
    if (h.empty()) {
        if (!g.target_letters.empty()) {
            h = "Первая буква слова — «" + utf8_to_upper_letter(g.target_letters.front()) + "»";
        } else {
            h = "Подсказка отсутствует.";
        }
    }

    api->c_reset(ctx);
    api->c_fmt(ctx, ("💡 Подсказка: " + h).c_str(), KOTO_ENT_BOLD);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

void cmd_wclear(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);
    long long sender = api->sender_id(ctx);
    if (cid == 0) cid = sender;

    std::lock_guard<std::mutex> lock(g_mutex);
    Game* gp = find_wordle_game("", cid, sender);
    if (!gp || gp->is_finished) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Нет активной игры в этом чате.", KOTO_ENT_ITALIC);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    gp->is_finished = true;
    api->c_reset(ctx);
    api->c_fmt(ctx, "🛑 Игра Wordle завершена досрочно.", KOTO_ENT_ITALIC);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Watcher: ловит прямые ответы на табло и команды .g ────────────────────
int on_watch(koto_ctx* ctx, const koto_api* api) {
    const char* raw_txt = api->msg_text(ctx);
    if (!raw_txt || !*raw_txt) return 0;

    long long r_mid = api->reply_msg_id ? api->reply_msg_id(ctx) : 0;
    long long cur_mid = api->msg_id(ctx);
    long long cid = api->chat_id(ctx);
    long long sender = api->sender_id(ctx);
    if (cid == 0) cid = sender;

    // Синхронизация ID сообщения игры при отправке табло
    if (std::strstr(raw_txt, "🎮 ОТГАДАЙ СЛОВО")) {
        std::lock_guard<std::mutex> lock(g_mutex);
        Game* gp = find_wordle_game("", cid, sender);
        if (gp && !gp->is_finished && cur_mid > 0) {
            gp->msg_id = cur_mid;
        }
        return 0;
    }

    std::unique_lock<std::mutex> lock(g_mutex);
    Game* gp = find_wordle_game("", cid, sender);
    if (!gp || gp->is_finished || gp->is_lobby) return 0;

    Game& g = *gp;
    if (r_mid > 0) g.msg_id = r_mid;

    const char* p = raw_txt;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (!*p) return 0;

    std::string text_trimmed = p;
    while (!text_trimmed.empty() && (text_trimmed.back() == ' ' || text_trimmed.back() == '\t' || text_trimmed.back() == '\n' || text_trimmed.back() == '\r')) {
        text_trimmed.pop_back();
    }

    std::string guess_word;
    bool is_explicit_guess = false;

    std::string lower_start = text_trimmed;
    for (char& ch : lower_start) ch = (char)tolower((unsigned char)ch);

    const char* prefixes[] = {
        ".guess ", "/guess ", "!guess ", "guess ",
        ".g ", "/g ", "!g ", "g ",
        ".guess\n", "/guess\n", "!guess\n", "guess\n",
        ".g\n", "/g\n", "!g\n", "g\n"
    };

    for (const char* pfx : prefixes) {
        size_t pfx_len = std::strlen(pfx);
        if (lower_start.rfind(pfx, 0) == 0) {
            guess_word = text_trimmed.substr(pfx_len);
            while (!guess_word.empty() && (guess_word.front() == ' ' || guess_word.front() == '\t')) guess_word.erase(0, 1);
            is_explicit_guess = true;
            break;
        }
    }

    // Если это ответ именно на сообщение с игрой
    if (!is_explicit_guess && r_mid != 0 && (g.msg_id == r_mid || std::abs(g.msg_id - r_mid) <= 15)) {
        auto letters = utf8_split_letters(text_trimmed);
        if ((int)letters.size() == g.target_len && is_all_alpha(letters)) {
            guess_word = text_trimmed;
            is_explicit_guess = true;
            g.msg_id = r_mid;
        }
    }

    if (!is_explicit_guess || guess_word.empty()) return 0;

    lock.unlock();

    char name_buf[64] = {0};
    if (api->user_name(ctx, sender, name_buf, sizeof(name_buf)) <= 0 || !name_buf[0]) {
        snprintf(name_buf, sizeof(name_buf), "Игрок_%lld", (sender > 0 ? sender : -sender) % 10000);
    }

    process_guess(ctx, api, cid, sender, name_buf, guess_word);
    return 1;
}

// ── Callback Handler для inline-кнопок ────────────────────────────────────
void on_callback(koto_ctx* ctx, const koto_api* api) {
    const char* raw_data = api->cb_data(ctx);
    if (!raw_data || std::strncmp(raw_data, "wrd:", 4) != 0) return;

    std::string data = raw_data;
    long long cid = api->chat_id(ctx);
    long long mid = api->cb_message_id(ctx);
    long long sender = api->sender_id(ctx);
    if (cid == 0) cid = sender;

    char sender_name[64] = {0};
    if (api->user_name(ctx, sender, sender_name, sizeof(sender_name)) <= 0 || !sender_name[0]) {
        snprintf(sender_name, sizeof(sender_name), "Игрок_%lld", (sender > 0 ? sender : -sender) % 10000);
    }

    // Подсказка правил
    if (data.rfind("wrd:help", 0) == 0) {
        api->cb_answer(ctx, "🎮 Отправляйте слова командой: .g <слово>\nили просто ответом на табло игры!\n\n🟩 Зеленый — буква на месте\n🟦 Синий — есть не там\n🟥 Красный — нет в слове", 1);
        return;
    }

    // Клик по букве табло
    if (data.rfind("wrd:i:", 0) == 0) {
        char col_code = (data.size() > 6) ? data[6] : ' ';
        if (col_code == '2') {
            api->cb_answer(ctx, "🟩 Буква на своём месте!", 0);
        } else if (col_code == '1') {
            api->cb_answer(ctx, "🟦 Буква есть в слове, но на другой позиции!", 0);
        } else if (col_code == '0') {
            api->cb_answer(ctx, "🟥 Этой буквы нет в слове!", 0);
        } else {
            api->cb_answer(ctx, "🎯 Отправляйте слова: .g <слово>", 0);
        }
        return;
    }

    // Парсим target_id игры
    std::string target_id;
    if (data.rfind("wrd:h:", 0) == 0) target_id = data.substr(6);
    else if (data.rfind("wrd:s:", 0) == 0) target_id = data.substr(6);
    else if (data.rfind("wrd:n:", 0) == 0) target_id = data.substr(6);
    else if (data.rfind("wrd:j:", 0) == 0) target_id = data.substr(6);
    else if (data.rfind("wrd:c:", 0) == 0) target_id = data.substr(6);

    std::unique_lock<std::mutex> lock(g_mutex);
    Game* gp = find_wordle_game(target_id, cid, sender, ctx, api);
    if (!gp) {
        api->cb_answer(ctx, "❌ Игра не найдена или устарела!", 0);
        return;
    }

    Game& g = *gp;
    if (mid != 0) g.msg_id = mid;

    // Присоединиться к лобби: wrd:j:<gid>
    if (data.rfind("wrd:j:", 0) == 0) {
        if (!g.is_lobby) {
            api->cb_answer(ctx, "⚠️ В эту игру уже играют!", 0);
            return;
        }
        if (g.is_custom && sender == g.creator_id) {
            api->cb_answer(ctx, "❌ Вы сами загадали это слово! Ждите другого игрока.", 1);
            return;
        }

        g.target_opponent_id = sender;
        g.target_opponent_name = sender_name;
        g.is_lobby = false;
        g.last_active = time(nullptr);

        api->cb_answer(ctx, "🎮 Вы вошли в игру! Отправляйте догадки: .g <слово>", 0);
        render_game(ctx, api, g);
        api->c_edit_message(ctx, 0, mid, 0);
        return;
    }

    // Отмена лобби: wrd:c:<gid>
    if (data.rfind("wrd:c:", 0) == 0) {
        if (!g.is_lobby) {
            api->cb_answer(ctx, "Игра уже началась!", 0);
            return;
        }
        if (sender != g.creator_id && sender != api->get_me(ctx)) {
            api->cb_answer(ctx, "❌ Только создатель может отменить лобби!", 1);
            return;
        }
        g.is_finished = true;
        api->cb_answer(ctx, "Лобби отменено.", 0);
        api->c_reset(ctx);
        api->c_fmt(ctx, "🏳️ Лобби игры Wordle отменено создателем.", KOTO_ENT_ITALIC);
        api->c_edit_message(ctx, 0, mid, 0);
        return;
    }

    // Подсказка: wrd:h:<gid>
    if (data.rfind("wrd:h:", 0) == 0) {
        if (g.attempts.size() >= 2 || !g.hint.empty() || g.is_finished) {
            std::string h = g.hint;
            if (h.empty()) {
                if (g.target_letters.size() > 1 && g.attempts.size() >= 4) {
                    h = "Первая: «" + utf8_to_upper_letter(g.target_letters.front()) +
                        "», последняя: «" + utf8_to_upper_letter(g.target_letters.back()) + "»";
                } else if (!g.target_letters.empty()) {
                    h = "Первая буква: «" + utf8_to_upper_letter(g.target_letters.front()) + "»";
                } else {
                    h = "Подсказка пока недоступна.";
                }
            }
            g.hint_revealed = true;
            api->cb_answer(ctx, ("💡 " + h).c_str(), 1);
        } else {
            api->cb_answer(ctx, "🔒 Подсказка станет доступна после 2-й попытки!", 1);
        }
        return;
    }

    // Сдаться: wrd:s:<gid>
    if (data.rfind("wrd:s:", 0) == 0) {
        if (g.is_finished) {
            api->cb_answer(ctx, "Игра уже завершена!", 0);
            return;
        }
        if (g.target_opponent_id != 0 && sender != g.target_opponent_id && sender != g.creator_id) {
            api->cb_answer(ctx, "⚠️ Вы не участвуете в этой игре!", 1);
            return;
        }

        g.is_finished = true;
        g.is_won = false;
        api->cb_answer(ctx, "🏳️ Вы сдались!", 0);
        render_game(ctx, api, g);
        api->c_edit_message(ctx, 0, mid, 0);
        return;
    }

    // Новая игра: wrd:n:<gid>
    if (data.rfind("wrd:n:", 0) == 0) {
        Game new_g;
        new_g.id = "wrd_" + std::to_string(++g_game_seq);
        new_g.chat_id = cid;
        new_g.creator_id = sender;
        new_g.creator_name = "Бот (случайное)";
        new_g.is_custom = false;
        new_g.target_word = get_random_word();
        new_g.target_letters = utf8_split_letters(new_g.target_word);
        new_g.target_len = (int)new_g.target_letters.size();
        new_g.max_attempts = g.max_attempts;
        new_g.last_active = time(nullptr);

        if (cid < 0) {
            new_g.is_lobby = true;
        }

        g_games_by_id[new_g.id] = new_g;
        g_chat_to_game[cid] = new_g.id;
        g_chat_to_game[normalize_cid(cid)] = new_g.id;
        g_chat_to_game[bare_cid(cid)] = new_g.id;
        if (sender != 0) g_chat_to_game[sender] = new_g.id;

        api->cb_answer(ctx, "🔄 Новая игра запущена!", 0);
        render_game(ctx, api, new_g);
        // Отправляем новым сообщением, сохраняя экран победы в истории чата!
        api->c_send(ctx, cid, 0);
        return;
    }
}

// ── Описание модуля и команд ───────────────────────────────────────────────
const koto_command COMMANDS[] = {
    {"wordle", cmd_wordle, KOTO_LEVEL_ALL, "[ru] Игра Отгадай слово (.w, .w rnd, .w <слово> [-a 6]) [en] Wordle game (.w, .w rnd, .w <word>)", "[слово]"},
    {"w", cmd_wordle, KOTO_LEVEL_ALL, "[ru] Быстрый алиас для .wordle [en] Fast alias for .wordle", "[слово]"},
    {"word", cmd_wordle, KOTO_LEVEL_ALL, "[ru] Быстрый алиас для .wordle [en] Fast alias for .wordle", "[слово]"},
    {"wset", cmd_wset, KOTO_LEVEL_ALL, "[ru] Загадать секретное слово для игры (.wset <слово> [подсказка] [-a 6]) [en] Stage custom secret word", "<слово> [подсказка]"},
    {"wordleset", cmd_wset, KOTO_LEVEL_ALL, "[ru] Быстрый алиас для .wset [en] Fast alias for .wset", "<слово> [подсказка]"},
    {"guess", cmd_guess, KOTO_LEVEL_ALL, "[ru] Отправить догадку в текущую игру Wordle (.g <слово>) [en] Submit Wordle guess", "<слово>"},
    {"g", cmd_guess, KOTO_LEVEL_ALL, "[ru] Быстрый алиас для .guess (.g <слово>) [en] Fast alias for .guess", "<слово>"},
    {"whint", cmd_whint, KOTO_LEVEL_ALL, "[ru] Показать подсказку к текущему слову [en] Show Wordle hint", nullptr},
    {"hint", cmd_whint, KOTO_LEVEL_ALL, "[ru] Алиас для .whint [en] Alias for .whint", nullptr},
    {"wclear", cmd_wclear, KOTO_LEVEL_ALL, "[ru] Завершить активную игру Wordle в чате [en] Stop active Wordle game", nullptr},
    {"wstop", cmd_wclear, KOTO_LEVEL_ALL, "[ru] Алиас для .wclear [en] Alias for .wclear", nullptr}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "wordle",
    "[ru] Игра 'Отгадай слово' (Wordle): цветные инлайн-кнопки, лобби, защита соперника, подсказки, настраиваемые попытки (by kote). [en] Wordle word guessing game with colored buttons, lobby, opponent lock, hints and custom attempts (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    on_watch,
    KOTO_WATCH_INCOMING | KOTO_WATCH_OUTGOING,
    on_callback,
    nullptr,
    0,
    "https://github.com/AresUser1/KoteModules/blob/main/src/wordle.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}

