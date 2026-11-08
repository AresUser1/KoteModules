// wordle.cpp
// Модуль "Отгадай слово" (Wordle) для Kotogram (ABI 3, .so)
// Автор: by kote

#include "module_abi.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <random>
#include <ctime>
#include <cctype>
#include <cstring>
#include <algorithm>

namespace {

constexpr const char* VERSION = "1.1.0";

// ── Встроенный словарь 5-буквенных существительных ─────────────────────────
const char* const DICTIONARY[] = {
    "адрес", "алмаз", "ангел", "арбуз", "базар", "балет", "банан", "баран", "берег", "билет",
    "бланк", "бокал", "борец", "бочка", "буква", "букет", "булка", "вагон", "ванна", "весна",
    "ветер", "вечер", "взрыв", "вишня", "вождь", "волна", "волос", "гараж", "герой", "голос",
    "горка", "город", "гроза", "груша", "гудок", "дверь", "диван", "длина", "добро", "дождь",
    "домик", "доска", "дрова", "забор", "завод", "закон", "замок", "запад", "запах", "звезда",
    "зверь", "зебра", "земля", "зерно", "знамя", "игрок", "икона", "искра", "кабан", "какао",
    "камыш", "канал", "капля", "карта", "каска", "катер", "кепка", "киоск", "книга", "кобра",
    "ковер", "козел", "колес", "колос", "комар", "конец", "копье", "корень", "костер", "кошка",
    "кресло", "крыло", "кубик", "кубок", "кукла", "кулак", "купец", "купол", "кусок", "лагерь",
    "лампа", "лапта", "ласка", "лебедь", "лента", "леска", "лидер", "лимон", "лодка", "ложка",
    "магия", "майка", "марка", "маска", "масло", "мачта", "месяц", "мечта", "мешок", "мишка",
    "молот", "мороз", "мусор", "мышка", "набор", "налог", "народ", "наука", "нитка", "носок",
    "ночка", "овраг", "огонь", "озеро", "океан", "олень", "опера", "опрос", "орден", "осень",
    "палка", "папка", "парта", "парус", "пачка", "пенал", "пенка", "песня", "песок", "петух",
    "пилот", "пират", "пирог", "пламя", "плита", "победа", "повар", "поезд", "покой", "полет",
    "полка", "полюс", "помпа", "порох", "поход", "почка", "почта", "право", "птица", "пункт",
    "пушка", "пьеса", "радио", "радуга", "ракета", "рамка", "ранчо", "ребус", "речка", "робот",
    "рубин", "рукав", "ручей", "ручка", "салат", "салон", "салют", "санки", "сапог", "сарай",
    "сахар", "свеча", "север", "селез", "сетка", "сироп", "сказка", "скала", "склад", "склеп",
    "склон", "скотч", "слеза", "слива", "слово", "смена", "сокол", "сосна", "спорт", "среда",
    "старт", "ствол", "стена", "степь", "столб", "струя", "сцена", "тайна", "танец", "театр",
    "тепло", "тесто", "ткань", "топор", "трава", "трасса", "тропа", "труба", "туман", "тупик",
    "турок", "тыква", "уголь", "удар", "укроп", "улица", "улыба", "утюг", "факел", "фанат",
    "ферма", "фильм", "фирма", "флейта", "фокус", "форма", "хвост", "хлеба", "холод", "хомяк",
    "хутор", "цветы", "центр", "цифра", "чайка", "чашка", "череп", "черта", "чехол", "чутье",
    "шайба", "шалаш", "шапка", "шарик", "шарф", "шахта", "шепот", "шнурок", "шпион", "шторм",
    "щенок", "экран", "юрист", "ягода", "якорь", "ящик"
};
constexpr size_t DICT_SIZE = sizeof(DICTIONARY) / sizeof(DICTIONARY[0]);

// ── Вспомогательные функции UTF-8 для кириллицы ────────────────────────────

std::string utf8_char_to_lower(unsigned char c1, unsigned char c2) {
    if (c1 == 0xD0 && c2 >= 0x90 && c2 <= 0x9F) {
        return std::string{ (char)0xD0, (char)(c2 + 0x20) };
    }
    if (c1 == 0xD0 && c2 >= 0xA0 && c2 <= 0xAF) {
        return std::string{ (char)0xD1, (char)(c2 - 0x20) };
    }
    if (c1 == 0xD0 && c2 == 0x81) {
        return std::string{ (char)0xD0, (char)0xB5 }; // Ё -> е
    }
    if (c1 == 0xD1 && c2 == 0x91) {
        return std::string{ (char)0xD0, (char)0xB5 }; // ё -> е
    }
    return std::string{ (char)c1, (char)c2 };
}

std::string utf8_char_to_upper(unsigned char c1, unsigned char c2) {
    if (c1 == 0xD0 && c2 >= 0xB0 && c2 <= 0xBF) {
        return std::string{ (char)0xD0, (char)(c2 - 0x20) };
    }
    if (c1 == 0xD1 && c2 >= 0x80 && c2 <= 0x8F) {
        return std::string{ (char)0xD0, (char)(c2 + 0x20) };
    }
    if (c1 == 0xD0 && c2 == 0xB5) {
        return std::string{ (char)0xD0, (char)0x95 }; // е -> Е
    }
    if (c1 == 0xD1 && c2 == 0x91) {
        return std::string{ (char)0xD0, (char)0x95 }; // ё -> Е
    }
    return std::string{ (char)c1, (char)c2 };
}

std::vector<std::string> utf8_split_letters(const std::string& str) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < str.size()) {
        unsigned char c = (unsigned char)str[i];
        if (c < 0x80) {
            if (!isspace(c)) {
                out.push_back(std::string(1, (char)tolower(c)));
            }
            i++;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < str.size()) {
            unsigned char c2 = (unsigned char)str[i + 1];
            out.push_back(utf8_char_to_lower(c, c2));
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < str.size()) {
            out.push_back(str.substr(i, 3));
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < str.size()) {
            out.push_back(str.substr(i, 4));
            i += 4;
        } else {
            i++;
        }
    }
    return out;
}

std::string utf8_to_upper_letter(const std::string& let) {
    if (let.size() == 1) {
        return std::string(1, (char)toupper((unsigned char)let[0]));
    }
    if (let.size() == 2) {
        return utf8_char_to_upper((unsigned char)let[0], (unsigned char)let[1]);
    }
    return let;
}

std::string format_spaced_upper(const std::vector<std::string>& letters) {
    std::string res;
    for (size_t i = 0; i < letters.size(); ++i) {
        if (i > 0) res += " ";
        res += utf8_to_upper_letter(letters[i]);
    }
    return res;
}

bool is_all_alpha(const std::vector<std::string>& letters) {
    if (letters.empty()) return false;
    for (const auto& l : letters) {
        if (l.size() == 1) {
            if (!isalpha((unsigned char)l[0])) return false;
        } else if (l.size() == 2) {
            unsigned char c1 = (unsigned char)l[0];
            unsigned char c2 = (unsigned char)l[1];
            if (c1 != 0xD0 && c1 != 0xD1) return false;
            if (c1 == 0xD0 && !(c2 >= 0x90 && c2 <= 0xBF)) return false;
            if (c1 == 0xD1 && !(c2 >= 0x80 && c2 <= 0x91)) return false;
        } else {
            return false;
        }
    }
    return true;
}

std::string get_random_word() {
    static std::mt19937 rng((unsigned)time(nullptr));
    std::uniform_int_distribution<size_t> dist(0, DICT_SIZE - 1);
    return DICTIONARY[dist(rng)];
}

// ── Алгоритм Wordle (2 прохода) ───────────────────────────────────────────
// 2 = Зелёный (Верно)
// 1 = Синий (Есть, но не на своём месте)
// 0 = Красный (Нет)
std::vector<int> evaluate_wordle(const std::vector<std::string>& target,
                                 const std::vector<std::string>& guess) {
    size_t n = target.size();
    if (guess.size() != n) return {};

    std::vector<int> result(n, 0);
    std::vector<bool> target_used(n, false);

    // Проход 1: точные совпадения (Зелёный)
    for (size_t i = 0; i < n; ++i) {
        if (guess[i] == target[i]) {
            result[i] = 2;
            target_used[i] = true;
        }
    }

    // Проход 2: буквы на других позициях (Синий)
    for (size_t i = 0; i < n; ++i) {
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

// ── Структура игры ─────────────────────────────────────────────────────────

struct GuessRecord {
    std::string word_raw;
    std::vector<std::string> letters;
    std::vector<int> colors; // 2=зеленый, 1=синий, 0=красный
    std::string player_name;
};

struct Game {
    int64_t chat_id = 0;
    int64_t msg_id = 0;
    int64_t creator_id = 0;
    std::string creator_name;
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

std::unordered_map<int64_t, Game> g_games;
std::mutex g_mutex;

// ── Отрисовка сообщения и инлайн-кнопок ─────────────────────────────────────

void add_game_buttons(koto_ctx* ctx, const koto_api* api, const Game& g) {
    std::string cid_str = std::to_string(g.chat_id);

    // Показываем ряды кнопок со словами попыток (как игровое поле Wordle!)
    int rows_to_show = g.max_attempts;
    if (g.is_finished) {
        rows_to_show = (int)g.attempts.size();
        if (rows_to_show == 0) rows_to_show = 1;
    }

    for (int r = 0; r < rows_to_show; ++r) {
        bool has_attempt = (r < (int)g.attempts.size());

        for (int c = 0; c < g.target_len; ++c) {
            int new_row = (c == 0) ? 1 : 0;

            if (has_attempt) {
                const auto& att = g.attempts[r];
                std::string u_let = (c < (int)att.letters.size()) ? utf8_to_upper_letter(att.letters[c]) : " ";
                int col = (c < (int)att.colors.size()) ? att.colors[c] : 0;

                int btn_color = KOTO_BTN_DEFAULT;
                const char* cb = "wrd:i:0";
                if (col == 2) {
                    btn_color = KOTO_BTN_SUCCESS; // Зелёный
                    cb = "wrd:i:2";
                } else if (col == 1) {
                    btn_color = KOTO_BTN_PRIMARY; // Синий
                    cb = "wrd:i:1";
                } else {
                    btn_color = KOTO_BTN_DANGER;  // Красный
                    cb = "wrd:i:0";
                }

                if (api->c_button_ex) {
                    api->c_button_ex(ctx, u_let.c_str(), cb, new_row, btn_color, 0);
                } else {
                    std::string label = (col == 2 ? "🟩 " : (col == 1 ? "🟦 " : "🟥 ")) + u_let;
                    api->c_button(ctx, label.c_str(), cb, new_row);
                }
            } else {
                // Пустые клетки игрового поля
                if (api->c_button_ex) {
                    api->c_button_ex(ctx, "·", "wrd:i:x", new_row, KOTO_BTN_DEFAULT, 0);
                } else {
                    api->c_button(ctx, "·", "wrd:i:x", new_row);
                }
            }
        }
    }

    // Ряд кнопок управления внизу
    if (!g.is_finished) {
        api->c_button(ctx, "💡 Подсказка", ("wrd:h:" + cid_str).c_str(), 1);
        if (api->c_button_ex) {
            api->c_button_ex(ctx, "🏳️ Сдаться", ("wrd:s:" + cid_str).c_str(), 0, KOTO_BTN_DANGER, 0);
        } else {
            api->c_button(ctx, "🏳️ Сдаться", ("wrd:s:" + cid_str).c_str(), 0);
        }
    } else {
        if (api->c_button_ex) {
            api->c_button_ex(ctx, "🔄 Новая игра", ("wrd:n:" + cid_str).c_str(), 1, KOTO_BTN_SUCCESS, 0);
        } else {
            api->c_button(ctx, "🔄 Новая игра", ("wrd:n:" + cid_str).c_str(), 1);
        }
    }
}

void render_game(koto_ctx* ctx, const koto_api* api, const Game& g) {
    api->c_reset(ctx);

    api->c_fmt(ctx, "🎮 ОТГАДАЙ СЛОВО (Wordle)\n\n", KOTO_ENT_BOLD);

    if (g.is_finished) {
        if (g.is_won) {
            std::string win_str = "🎉 ПОБЕДА! Слово отгадано: " + format_spaced_upper(g.target_letters) + "\n";
            win_str += "🏆 Победитель: " + g.winner_name + " (с " + std::to_string(g.attempts.size()) + "-й попытки)!\n\n";
            api->c_fmt(ctx, win_str.c_str(), KOTO_ENT_BOLD);
        } else {
            std::string lose_str = "💀 ИГРА ОКОНЧЕНА!\nЗагаданное слово было: " + format_spaced_upper(g.target_letters) + "\n\n";
            api->c_fmt(ctx, lose_str.c_str(), KOTO_ENT_BOLD);
        }
    } else {
        std::string info = "👤 Загадал(а): " + (g.creator_name.empty() ? "KoteLoader" : g.creator_name) + "\n";
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

// ── Обработчик запуска: .wordle / .w ───────────────────────────────────────

void cmd_wordle(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);
    long long sender = api->sender_id(ctx);

    char name_buf[64] = {0};
    if (api->user_name(ctx, sender, name_buf, sizeof(name_buf)) <= 0 || !name_buf[0]) {
        snprintf(name_buf, sizeof(name_buf), "Игрок_%lld", (sender > 0 ? sender : -sender) % 10000);
    }

    const char* raw_args = api->cmd_args(ctx);
    while (raw_args && (*raw_args == ' ' || *raw_args == '\t')) raw_args++;

    std::string args = raw_args ? raw_args : "";

    // Если аргументов нет — выводим инструкцию, как загадать слово для игры
    if (args.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "⚠️ Вы не указали слово для игры!\n\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, "Использование:\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, "• .w <слово> [подсказка] — загадать слово для чата\n", KOTO_ENT_CODE);
        api->c_fmt(ctx, "• .w rnd — случайное слово от бота (соло)\n\n", KOTO_ENT_CODE);
        api->c_fmt(ctx, "Пример:\n.w кошка Домашнее животное с усами\n\n", KOTO_ENT_ITALIC);
        api->c_fmt(ctx, "by kote", KOTO_ENT_ITALIC);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    Game g;
    g.chat_id = cid;
    g.creator_id = sender;
    g.last_active = time(nullptr);
    g.max_attempts = 6;

    if (args == "rnd" || args == "rand" || args == "random" || args == "рандом" || args == "ранд") {
        // Случайное слово от бота
        g.is_custom = false;
        g.creator_name = "Бот (случайное)";
        g.target_word = get_random_word();
        g.target_letters = utf8_split_letters(g.target_word);
        g.target_len = (int)g.target_letters.size();
    } else {
        // Пользователь загадал слово
        g.creator_name = name_buf;
        g.is_custom = true;

        std::string word_part;
        std::string hint_part;

        size_t space_pos = args.find_first_of(" \t");
        if (space_pos != std::string::npos) {
            word_part = args.substr(0, space_pos);
            hint_part = args.substr(space_pos + 1);
            while (!hint_part.empty() && (hint_part.front() == ' ' || hint_part.front() == '\t')) {
                hint_part.erase(0, 1);
            }
        } else {
            word_part = args;
        }

        auto letters = utf8_split_letters(word_part);
        if (letters.size() < 3 || letters.size() > 8 || !is_all_alpha(letters)) {
            api->c_reset(ctx);
            api->c_fmt(ctx, "⚠️ Загадайте слово от 3 до 8 букв (только буквы без пробелов)!\n\nПример:\n.w пират Морской разбойник\n\nby kote", KOTO_ENT_BOLD);
            if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
            else api->c_reply(ctx, 0);
            return;
        }

        std::string lower_word;
        for (const auto& l : letters) lower_word += l;
        g.target_word = lower_word;
        g.target_letters = letters;
        g.target_len = (int)letters.size();
        g.hint = hint_part;
    }

    render_game(ctx, api, g);

    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }

    // Запоминаем msg_id
    g.msg_id = api->msg_id(ctx);

    std::lock_guard<std::mutex> lock(g_mutex);
    g_games[cid] = g;
}

// ── Обработка попытки (guess) ──────────────────────────────────────────────

bool process_guess(koto_ctx* ctx, const koto_api* api, int64_t cid, int64_t sender, const std::string& sender_name, const std::string& guess_text) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_games.find(cid);
    if (it == g_games.end() || it->second.is_finished) {
        return false;
    }

    Game& g = it->second;

    // Автор загадки не может сам отгадывать своё слово
    if (g.is_custom && sender == g.creator_id) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "😜 Вы сами загадали это слово! Пусть отгадывают другие участники.", KOTO_ENT_ITALIC);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return true;
    }

    auto guess_letters = utf8_split_letters(guess_text);
    if ((int)guess_letters.size() != g.target_len) {
        std::string err = "⚠️ В слове должно быть " + std::to_string(g.target_len) + " букв (вы ввели " + std::to_string(guess_letters.size()) + ")!";
        api->c_reset(ctx);
        api->c_fmt(ctx, err.c_str(), KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return true;
    }

    if (!is_all_alpha(guess_letters)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "⚠️ Допустимы только буквы!", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return true;
    }

    std::string raw_guess;
    for (const auto& l : guess_letters) raw_guess += l;

    // Проверяем, не было ли уже такой попытки
    for (const auto& att : g.attempts) {
        if (att.word_raw == raw_guess) {
            api->c_reset(ctx);
            api->c_fmt(ctx, "⚠️ Это слово уже пробовали в текущей игре!", KOTO_ENT_ITALIC);
            if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
            else api->c_reply(ctx, 0);
            return true;
        }
    }

    auto colors = evaluate_wordle(g.target_letters, guess_letters);

    GuessRecord rec;
    rec.word_raw = raw_guess;
    rec.letters = guess_letters;
    rec.colors = colors;
    rec.player_name = sender_name;
    g.attempts.push_back(rec);
    g.last_active = time(nullptr);

    // Проверка победы: все цвета == 2 (зелёный)
    bool won = true;
    for (int c : colors) {
        if (c != 2) {
            won = false;
            break;
        }
    }

    if (won) {
        g.is_finished = true;
        g.is_won = true;
        g.winner_name = sender_name;
    } else if ((int)g.attempts.size() >= g.max_attempts) {
        g.is_finished = true;
        g.is_won = false;
    }

    // Удаляем сообщение команды .g <слово>, чтобы не засорять чат
    if (api->is_outgoing(ctx) && api->delete_msg) {
        api->delete_msg(ctx);
    }

    // Обновляем игровое табло
    render_game(ctx, api, g);

    if (g.msg_id != 0) {
        api->c_edit_message(ctx, g.chat_id, g.msg_id, 0);
    } else {
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    }

    return true;
}

void cmd_guess(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);
    long long sender = api->sender_id(ctx);

    char name_buf[64] = {0};
    if (api->user_name(ctx, sender, name_buf, sizeof(name_buf)) <= 0 || !name_buf[0]) {
        snprintf(name_buf, sizeof(name_buf), "Игрок_%lld", (sender > 0 ? sender : -sender) % 10000);
    }

    const char* raw_args = api->cmd_args(ctx);
    while (raw_args && (*raw_args == ' ' || *raw_args == '\t')) raw_args++;

    if (!raw_args || !*raw_args) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "Использование: .g <слово>", KOTO_ENT_CODE);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    if (!process_guess(ctx, api, cid, sender, name_buf, raw_args)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "⚠️ В этом чате нет активной игры в Wordle.\nЗапустите новую командой: .w <слово>", KOTO_ENT_ITALIC);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    }
}

// ── Подсказка: .hint / .whint ──────────────────────────────────────────────

void cmd_hint(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_games.find(cid);
    if (it == g_games.end() || it->second.is_finished) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "⚠️ Нет активной игры.", KOTO_ENT_ITALIC);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    Game& g = it->second;
    if (g.attempts.size() >= 2 || !g.hint.empty() || g.is_finished) {
        std::string h = g.hint;
        if (h.empty()) {
            if (g.target_letters.size() > 1 && g.attempts.size() >= 4) {
                h = "Первая буква: «" + utf8_to_upper_letter(g.target_letters.front()) +
                    "», последняя буква: «" + utf8_to_upper_letter(g.target_letters.back()) + "»";
            } else if (!g.target_letters.empty()) {
                h = "Первая буква: «" + utf8_to_upper_letter(g.target_letters.front()) + "»";
            } else {
                h = "Подсказка пока недоступна.";
            }
        }
        g.hint_revealed = true;
        api->c_reset(ctx);
        std::string out = "💡 Подсказка: " + h;
        api->c_fmt(ctx, out.c_str(), KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    } else {
        api->c_reset(ctx);
        api->c_fmt(ctx, "🔒 Подсказка откроется после 2 неудачных попыток!", KOTO_ENT_ITALIC);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    }
}

// ── Остановка / сдача: .wstop ──────────────────────────────────────────────

void cmd_stop(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_games.find(cid);
    if (it == g_games.end() || it->second.is_finished) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "⚠️ Нет активной игры в Wordle.", KOTO_ENT_ITALIC);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    Game& g = it->second;
    g.is_finished = true;
    g.is_won = false;

    render_game(ctx, api, g);

    if (g.msg_id != 0) {
        api->c_edit_message(ctx, g.chat_id, g.msg_id, 0);
    } else {
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    }
}

// ── Watcher: перехват ответов реплаем на игровое табло ──────────────────────

int on_watch(koto_ctx* ctx, const koto_api* api) {
    const char* txt = api->msg_text(ctx);
    if (!txt || !*txt) return 0;

    // Не перехватываем команды
    if (*txt == '.' || *txt == '/' || *txt == '!') return 0;

    long long r_mid = api->reply_msg_id ? api->reply_msg_id(ctx) : 0;
    long long cid = api->chat_id(ctx);

    std::unique_lock<std::mutex> lock(g_mutex);
    auto it = g_games.find(cid);
    if (it == g_games.end() || it->second.is_finished) return 0;

    // Если это реплай на сообщение игры Wordle
    if (r_mid != 0 && r_mid == it->second.msg_id) {
        lock.unlock();
        long long sender = api->sender_id(ctx);
        char name_buf[64] = {0};
        if (api->user_name(ctx, sender, name_buf, sizeof(name_buf)) <= 0 || !name_buf[0]) {
            snprintf(name_buf, sizeof(name_buf), "Игрок_%lld", (sender > 0 ? sender : -sender) % 10000);
        }
        process_guess(ctx, api, cid, sender, name_buf, txt);
        return 1;
    }

    return 0;
}

// ── Callback Handler для inline-кнопок ─────────────────────────────────────

void on_callback(koto_ctx* ctx, const koto_api* api) {
    const char* raw_data = api->cb_data(ctx);
    if (!raw_data || std::strncmp(raw_data, "wrd:", 4) != 0) return;

    std::string data = raw_data;
    long long cid = api->chat_id(ctx);
    long long mid = api->cb_message_id(ctx);

    // Буквы и клетки поля Wordle
    if (data.rfind("wrd:i:", 0) == 0) {
        char col_code = data[6];
        if (col_code == '2') {
            api->cb_answer(ctx, "🟩 Буква на своём месте! (Верно)", 0);
        } else if (col_code == '1') {
            api->cb_answer(ctx, "🟦 Буква есть в слове, но на другой позиции!", 0);
        } else if (col_code == '0') {
            api->cb_answer(ctx, "🟥 Этой буквы нет в слове!", 0);
        } else {
            api->cb_answer(ctx, "Отправляйте догадки: .g <слово> или реплаем", 0);
        }
        return;
    }

    // Подсказка
    if (data.rfind("wrd:h:", 0) == 0) {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_games.find(cid);
        if (it != g_games.end()) {
            Game& g = it->second;
            if (g.attempts.size() >= 2 || !g.hint.empty() || g.is_finished) {
                std::string h = g.hint;
                if (h.empty()) {
                    if (g.target_letters.size() > 1 && g.attempts.size() >= 4) {
                        h = "Первая буква: «" + utf8_to_upper_letter(g.target_letters.front()) +
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
        } else {
            api->cb_answer(ctx, "Игра не найдена", 0);
        }
        return;
    }

    // Сдаться
    if (data.rfind("wrd:s:", 0) == 0) {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_games.find(cid);
        if (it != g_games.end() && !it->second.is_finished) {
            Game& g = it->second;
            g.is_finished = true;
            g.is_won = false;
            api->cb_answer(ctx, "🏳️ Вы сдались!", 0);
            render_game(ctx, api, g);
            api->c_edit_message(ctx, cid, mid, 0);
        } else {
            api->cb_answer(ctx, "Игра уже завершена", 0);
        }
        return;
    }

    // Новая игра
    if (data.rfind("wrd:n:", 0) == 0) {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_games.find(cid);
        if (it != g_games.end() && it->second.is_custom) {
            api->cb_answer(ctx, "Чтобы начать новую игру, напишите: .w <слово> [подсказка]", 1);
            return;
        }

        Game g;
        g.chat_id = cid;
        g.msg_id = mid;
        g.creator_name = "Бот (случайное)";
        g.last_active = time(nullptr);
        g.max_attempts = 6;
        g.is_custom = false;
        g.target_word = get_random_word();
        g.target_letters = utf8_split_letters(g.target_word);
        g.target_len = (int)g.target_letters.size();

        g_games[cid] = g;

        api->cb_answer(ctx, "🔄 Новая игра начата!", 0);
        render_game(ctx, api, g);
        api->c_edit_message(ctx, cid, mid, 0);
        return;
    }
}

// ── Декларация команд и модуля ─────────────────────────────────────────────

const koto_command COMMANDS[] = {
    {"wordle", cmd_wordle, KOTO_LEVEL_ALL, "[ru] Игра Отгадай слово: .w <слово> [подсказка] или .w rnd [en] Wordle game: .w <word> [hint] or .w rnd", "[word] [hint]"},
    {"w", cmd_wordle, KOTO_LEVEL_ALL, "[ru] Быстрый алиас для .wordle [en] Fast alias for .wordle", "[word] [hint]"},
    {"word", cmd_wordle, KOTO_LEVEL_ALL, "[ru] Быстрый алиас для .wordle [en] Fast alias for .wordle", "[word] [hint]"},
    {"g", cmd_guess, KOTO_LEVEL_ALL, "[ru] Предложить слово в игре Wordle [en] Submit a guess in Wordle", "<слово>"},
    {"guess", cmd_guess, KOTO_LEVEL_ALL, "[ru] Предложить слово в игре Wordle [en] Submit a guess in Wordle", "<слово>"},
    {"whint", cmd_hint, KOTO_LEVEL_ALL, "[ru] Показать подсказку к текущему слову [en] Show hint for current word", nullptr},
    {"hint", cmd_hint, KOTO_LEVEL_ALL, "[ru] Показать подсказку к текущему слову [en] Show hint for current word", nullptr},
    {"wstop", cmd_stop, KOTO_LEVEL_ALL, "[ru] Сдаться и открыть загаданное слово [en] Surrender and reveal hidden word", nullptr}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "wordle",
    "[ru] Отгадай слово (Wordle) с цветным полем из инлайн-кнопок и мультиплеером (by kote). [en] Wordle guessing game with colored inline buttons board and multiplayer (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    on_watch,
    KOTO_WATCH_INCOMING,
    on_callback,
    nullptr, // settings
    0,
    "https://github.com/AresUser1/KoteModules/blob/main/src/wordle.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
