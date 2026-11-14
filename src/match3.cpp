// match3.cpp — Модуль игры «Три в ряд» (Match-3) для Kotogram (C++ ABI 3)
// Сетка 8x8, цветные фишки-кружки, индикаторы опыта и уровней, комбо-каскады (by kote).

#include "module_abi.h"
#include "nlohmann/json.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <random>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <cstring>
#include <cstdio>

namespace {
using json = nlohmann::json;

constexpr const char* VERSION = "1.0.0";
constexpr int BOARD_ROWS = 8;
constexpr int BOARD_COLS = 8;
constexpr int DEFAULT_MOVES = 25;

// Кружки для фишек
const char* const CHIP_EMOJIS[] = {
    "🔴", // 0: Red
    "🟠", // 1: Orange
    "🟡", // 2: Yellow
    "🟢", // 3: Green
    "🔵", // 4: Blue
    "🟣", // 5: Purple
    "🟤", // 6: Brown
    "⚪", // 7: White
    "⚫"  // 8: Black
};
constexpr int MAX_COLORS = sizeof(CHIP_EMOJIS) / sizeof(CHIP_EMOJIS[0]);

struct Point {
    int r = 0;
    int c = 0;
    bool operator==(const Point& o) const { return r == o.r && c == o.c; }
};

struct GameState {
    std::string id;
    long long chat_id = 0;
    long long msg_id = 0;
    long long owner_id = 0;
    std::string owner_name;

    int level = 1;
    int xp = 0;
    int needed_xp = 500;
    int score = 0;
    int moves_left = DEFAULT_MOVES;
    int high_score = 0;

    int selected_r = -1;
    int selected_c = -1;

    int last_points = 0;
    int last_combo = 0;
    int last_match_type = 0; // 0 = none, 1 = player (green), 2 = cascade (blue)

    int grid[BOARD_ROWS][BOARD_COLS]{};
    int highlight[BOARD_ROWS][BOARD_COLS]{}; // 0 = normal, 1 = player match (green), 2 = cascade match (blue)

    bool is_finished = false;
    time_t last_active = 0;
};

std::mutex g_m3_mu;
static int g_game_seq = 100;
std::unordered_map<std::string, GameState> g_games_by_id;
std::unordered_map<long long, std::string> g_chat_to_game;

std::mt19937& rng() {
    static thread_local std::mt19937 gen((unsigned)time(nullptr) ^ (unsigned)std::random_device{}());
    return gen;
}

int num_colors_for_level(int level) {
    if (level <= 1) return 5; // 5 цветов на 1 уровне для легких комбо
    if (level <= 3) return 6; // + 🟣
    if (level <= 5) return 7; // + 🟤
    return 8;                 // + ⚪
}

int random_chip(int level) {
    int max_c = num_colors_for_level(level);
    std::uniform_int_distribution<int> dist(0, max_c - 1);
    return dist(rng());
}

// Проверка наличия совпадений 3+ на текущей доске
std::vector<Point> find_matches(int grid[BOARD_ROWS][BOARD_COLS]) {
    std::vector<Point> matched;
    bool marked[BOARD_ROWS][BOARD_COLS] = {};

    // Горизонтальные
    for (int r = 0; r < BOARD_ROWS; ++r) {
        int run = 1;
        for (int c = 1; c < BOARD_COLS; ++c) {
            if (grid[r][c] == grid[r][c - 1] && grid[r][c] >= 0) {
                run++;
            } else {
                if (run >= 3) {
                    for (int k = 0; k < run; ++k) marked[r][c - 1 - k] = true;
                }
                run = 1;
            }
        }
        if (run >= 3) {
            for (int k = 0; k < run; ++k) marked[r][BOARD_COLS - 1 - k] = true;
        }
    }

    // Вертикальные
    for (int c = 0; c < BOARD_COLS; ++c) {
        int run = 1;
        for (int r = 1; r < BOARD_ROWS; ++r) {
            if (grid[r][c] == grid[r - 1][c] && grid[r][c] >= 0) {
                run++;
            } else {
                if (run >= 3) {
                    for (int k = 0; k < run; ++k) marked[r - 1 - k][c] = true;
                }
                run = 1;
            }
        }
        if (run >= 3) {
            for (int k = 0; k < run; ++k) marked[BOARD_ROWS - 1 - k][c] = true;
        }
    }

    for (int r = 0; r < BOARD_ROWS; ++r) {
        for (int c = 0; c < BOARD_COLS; ++c) {
            if (marked[r][c]) matched.push_back({r, c});
        }
    }
    return matched;
}

// Генерация начальной доски без готовых троек
void init_board(GameState& g) {
    int max_c = num_colors_for_level(g.level);
    for (int r = 0; r < BOARD_ROWS; ++r) {
        for (int c = 0; c < BOARD_COLS; ++c) {
            int val = 0;
            do {
                val = random_chip(g.level);
            } while ((c >= 2 && g.grid[r][c - 1] == val && g.grid[r][c - 2] == val) ||
                     (r >= 2 && g.grid[r - 1][c] == val && g.grid[r - 2][c] == val));
            g.grid[r][c] = val;
            g.highlight[r][c] = 0;
        }
    }
}

// Подсчет нужного опыта для уровня
int calc_needed_xp(int lvl) {
    return 500 * lvl + (lvl - 1) * 250;
}

// ── Сохранение / Загрузка из DB ──────────────────────────────────────────────
void save_game_db(koto_ctx* ctx, const koto_api* api, const GameState& g) {
    if (!api->db_set || g.id.empty()) return;
    try {
        json j;
        j["id"] = g.id;
        j["chat_id"] = g.chat_id;
        j["msg_id"] = g.msg_id;
        j["owner_id"] = g.owner_id;
        j["owner_name"] = g.owner_name;
        j["level"] = g.level;
        j["xp"] = g.xp;
        j["needed_xp"] = g.needed_xp;
        j["score"] = g.score;
        j["moves_left"] = g.moves_left;
        j["high_score"] = g.high_score;
        j["last_points"] = g.last_points;
        j["last_combo"] = g.last_combo;
        j["last_match_type"] = g.last_match_type;
        j["is_finished"] = g.is_finished;

        json arr = json::array();
        for (int r = 0; r < BOARD_ROWS; ++r) {
            json row = json::array();
            for (int c = 0; c < BOARD_COLS; ++c) row.push_back(g.grid[r][c]);
            arr.push_back(row);
        }
        j["grid"] = arr;

        std::string s = j.dump();
        api->db_set(ctx, ("m3_g:" + g.id).c_str(), s.c_str());
        if (g.chat_id != 0) {
            api->db_set(ctx, ("m3_act:" + std::to_string(g.chat_id)).c_str(), g.id.c_str());
        }
        if (g.owner_id != 0) {
            api->db_set(ctx, ("m3_act:" + std::to_string(g.owner_id)).c_str(), g.id.c_str());
        }
    } catch (...) {}
}

bool load_game_db(koto_ctx* ctx, const koto_api* api, const std::string& gid, GameState& out) {
    if (!api->db_get || gid.empty()) return false;
    try {
        char buf[8192] = {0};
        int n = api->db_get(ctx, ("m3_g:" + gid).c_str(), buf, sizeof(buf));
        if (n <= 0) return false;
        json j = json::parse(std::string(buf, n));
        out.id = j.value("id", gid);
        out.chat_id = j.value("chat_id", 0LL);
        out.msg_id = j.value("msg_id", 0LL);
        out.owner_id = j.value("owner_id", 0LL);
        out.owner_name = j.value("owner_name", "");
        out.level = j.value("level", 1);
        out.xp = j.value("xp", 0);
        out.needed_xp = j.value("needed_xp", 500);
        out.score = j.value("score", 0);
        out.moves_left = j.value("moves_left", DEFAULT_MOVES);
        out.high_score = j.value("high_score", 0);
        out.last_points = j.value("last_points", 0);
        out.last_combo = j.value("last_combo", 0);
        out.last_match_type = j.value("last_match_type", 0);
        out.is_finished = j.value("is_finished", false);

        if (j.contains("grid")) {
            auto arr = j["grid"];
            for (size_t r = 0; r < arr.size() && r < (size_t)BOARD_ROWS; ++r) {
                auto row = arr[r];
                for (size_t c = 0; c < row.size() && c < (size_t)BOARD_COLS; ++c) {
                    out.grid[r][c] = row[c].get<int>();
                }
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

GameState* find_game(koto_ctx* ctx, const koto_api* api, long long cid, long long sender) {
    if (cid != 0) {
        auto it = g_chat_to_game.find(cid);
        if (it != g_chat_to_game.end()) {
            auto git = g_games_by_id.find(it->second);
            if (git != g_games_by_id.end()) return &git->second;
        }
        if (ctx && api && api->db_get) {
            char abuf[64] = {0};
            if (api->db_get(ctx, ("m3_act:" + std::to_string(cid)).c_str(), abuf, sizeof(abuf)) > 0 && abuf[0]) {
                GameState loaded;
                if (load_game_db(ctx, api, abuf, loaded)) {
                    g_games_by_id[abuf] = loaded;
                    g_chat_to_game[cid] = abuf;
                    return &g_games_by_id[abuf];
                }
            }
        }
    }
    if (sender != 0) {
        auto it = g_chat_to_game.find(sender);
        if (it != g_chat_to_game.end()) {
            auto git = g_games_by_id.find(it->second);
            if (git != g_games_by_id.end()) return &git->second;
        }
    }
    return nullptr;
}

// ── Отрисовка игрового поля и кнопок ─────────────────────────────────────────
std::string render_progress_bar(int cur, int total) {
    if (total <= 0) total = 1;
    double ratio = std::clamp((double)cur / (double)total, 0.0, 1.0);
    int filled = (int)std::round(ratio * 10.0);
    std::string bar = "[";
    for (int i = 0; i < 10; ++i) {
        if (i < filled) bar += "▰";
        else bar += "▱";
    }
    bar += "] " + std::to_string((int)(ratio * 100.0)) + "%";
    return bar;
}

void render_game(koto_ctx* ctx, const koto_api* api, const GameState& g) {
    api->c_reset(ctx);

    if (g.is_finished) {
        api->c_fmt(ctx, "💥 ИГРА ОКОНЧЕНА!\n\n", KOTO_ENT_BOLD);
        std::string res = "👤 Игрок: " + (g.owner_name.empty() ? "Игрок" : g.owner_name) + "\n";
        res += "🏆 Итоговый счёт: " + std::to_string(g.score) + " XP\n";
        res += "⭐ Достигнутый уровень: " + std::to_string(g.level) + "\n";
        if (g.score >= g.high_score && g.score > 0) {
            res += "🎉 Новый личный рекорд!\n";
        }
        res += "\nНажмите кнопку ниже, чтобы начать новую партию!\nby kote";
        api->c_text(ctx, res.c_str());

        if (api->c_button_ex) {
            api->c_button_ex(ctx, "🔄 Играть снова", "m3:new", 1, KOTO_BTN_SUCCESS, 0);
        } else {
            api->c_button(ctx, "🔄 Играть снова", "m3:new", 1);
        }
        return;
    }

    std::string title = "💎 ТРИ В РЯД (Match-3) · Уровень " + std::to_string(g.level) + "\n\n";
    api->c_fmt(ctx, title.c_str(), KOTO_ENT_BOLD);

    std::string info;
    info += "⭐ Опыт: " + std::to_string(g.xp) + " / " + std::to_string(g.needed_xp) + " XP  " + render_progress_bar(g.xp, g.needed_xp) + "\n";
    info += "🎯 Ходов осталось: " + std::to_string(g.moves_left) + " | 🏆 Рекорд: " + std::to_string(g.high_score) + "\n";

    if (g.last_points > 0) {
        info += "⚡ Последний ход: +" + std::to_string(g.last_points) + " XP";
        if (g.last_combo > 1) {
            info += " (Комбо x" + std::to_string(g.last_combo) + "! 💥)";
        }
        info += "\n";
    }

    if (g.selected_r >= 0 && g.selected_c >= 0) {
        info += "👉 Выбрана фишка [" + std::to_string(g.selected_r + 1) + "," + std::to_string(g.selected_c + 1) + "]. Нажмите соседнюю!\n";
    } else {
        info += "💡 Выберите фишку, затем соседнюю для обмена 3 в ряд\n";
    }
    api->c_text(ctx, info.c_str());

    // Отрисовка кнопок сетки 8х8
    for (int r = 0; r < BOARD_ROWS; ++r) {
        for (int c = 0; c < BOARD_COLS; ++c) {
            int new_row = (c == 0) ? 1 : 0;
            std::string cb = "m3:t:" + std::to_string(r) + ":" + std::to_string(c);

            // Определение внешнего вида кнопки
            bool is_selected = (r == g.selected_r && c == g.selected_c);
            int hl = g.highlight[r][c];

            std::string btn_txt;
            int style = KOTO_BTN_DEFAULT;

            if (is_selected) {
                btn_txt = "🔘";
                style = KOTO_BTN_PRIMARY;
            } else if (hl == 1) {
                // Прямой сбор игрока — красится в зелёный
                btn_txt = "🟩";
                style = KOTO_BTN_SUCCESS;
            } else if (hl == 2) {
                // Сам собрался (каскад/автосбор) — красится в синий
                btn_txt = "🟦";
                style = KOTO_BTN_PRIMARY;
            } else {
                int chip = g.grid[r][c];
                if (chip >= 0 && chip < MAX_COLORS) {
                    btn_txt = CHIP_EMOJIS[chip];
                } else {
                    btn_txt = "⚪";
                }
            }

            if (api->c_button_ex) {
                api->c_button_ex(ctx, btn_txt.c_str(), cb.c_str(), new_row, style, 0);
            } else {
                api->c_button(ctx, btn_txt.c_str(), cb.c_str(), new_row);
            }
        }
    }

    // Нижняя панель управления
    if (api->c_button_ex) {
        api->c_button_ex(ctx, "🔄 Заново", "m3:restart", 1, KOTO_BTN_DEFAULT, 0);
        api->c_button_ex(ctx, "💡 Подсказка", "m3:hint", 0, KOTO_BTN_DEFAULT, 0);
        api->c_button_ex(ctx, "🛑 Завершить", "m3:stop", 0, KOTO_BTN_DANGER, 0);
    } else {
        api->c_button(ctx, "🔄 Заново", "m3:restart", 1);
        api->c_button(ctx, "💡 Подсказка", "m3:hint", 0);
        api->c_button(ctx, "🛑 Завершить", "m3:stop", 0);
    }
}

// Падение фишек и заполнение новыми
void collapse_and_refill(GameState& g, const std::vector<Point>& to_remove) {
    for (const auto& pt : to_remove) {
        g.grid[pt.r][pt.c] = -1;
    }

    // Для каждого столбца сдвигаем фишки вниз
    for (int c = 0; c < BOARD_COLS; ++c) {
        int write_r = BOARD_ROWS - 1;
        for (int r = BOARD_ROWS - 1; r >= 0; --r) {
            if (g.grid[r][c] != -1) {
                g.grid[write_r][c] = g.grid[r][c];
                if (write_r != r) g.grid[r][c] = -1;
                write_r--;
            }
        }
        // Заполняем пустоты сверху новыми фишками
        while (write_r >= 0) {
            g.grid[write_r][c] = random_chip(g.level);
            write_r--;
        }
    }
}

// ── Поиск возможного хода (подсказка) ───────────────────────────────────────
bool find_possible_move(const GameState& g, Point& p1, Point& p2) {
    int temp[BOARD_ROWS][BOARD_COLS];
    std::memcpy(temp, g.grid, sizeof(temp));

    const int dr[] = {0, 1};
    const int dc[] = {1, 0};

    for (int r = 0; r < BOARD_ROWS; ++r) {
        for (int c = 0; c < BOARD_COLS; ++c) {
            for (int d = 0; d < 2; ++d) {
                int nr = r + dr[d];
                int nc = c + dc[d];
                if (nr < BOARD_ROWS && nc < BOARD_COLS) {
                    std::swap(temp[r][c], temp[nr][nc]);
                    auto m = find_matches(temp);
                    std::swap(temp[r][c], temp[nr][nc]);
                    if (!m.empty()) {
                        p1 = {r, c};
                        p2 = {nr, nc};
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// ── Обработка хода игрока ───────────────────────────────────────────────────
void process_swap(koto_ctx* ctx, const koto_api* api, GameState& g, int r1, int c1, int r2, int c2) {
    // 1. Пробный обмен
    std::swap(g.grid[r1][c1], g.grid[r2][c2]);
    auto initial_matches = find_matches(g.grid);

    if (initial_matches.empty()) {
        // Нет совпадений — отменяем обмен
        std::swap(g.grid[r1][c1], g.grid[r2][c2]);
        g.selected_r = -1;
        g.selected_c = -1;
        api->cb_answer(ctx, "❌ Нет 3 в ряд! Обмен отменён.", 0);
        return;
    }

    // Ход успешен!
    g.moves_left--;
    g.selected_r = -1;
    g.selected_c = -1;
    g.last_active = time(nullptr);

    // Очищаем предыдущую подсветку
    std::memset(g.highlight, 0, sizeof(g.highlight));

    // Сбор от прямого хода игрока — красим в ЗЕЛЁНЫЙ (1)
    for (const auto& pt : initial_matches) {
        g.highlight[pt.r][pt.c] = 1;
    }

    int gained_xp = (int)initial_matches.size() * 20;
    if (initial_matches.size() >= 4) gained_xp += 50; // Бонус за 4+
    if (initial_matches.size() >= 5) gained_xp += 150; // Бонус за 5+

    int combo = 1;

    // Падение и подсчет каскадов (автосбор)
    collapse_and_refill(g, initial_matches);

    while (true) {
        auto cascade_matches = find_matches(g.grid);
        if (cascade_matches.empty()) break;

        combo++;
        // Сам собрался (каскад) — красим в СИНИЙ (2)
        for (const auto& pt : cascade_matches) {
            g.highlight[pt.r][pt.c] = 2;
        }

        int cascade_xp = (int)cascade_matches.size() * 25 * combo;
        gained_xp += cascade_xp;

        collapse_and_refill(g, cascade_matches);
    }

    g.last_points = gained_xp;
    g.last_combo = combo;
    g.last_match_type = (combo > 1) ? 2 : 1;
    g.xp += gained_xp;
    g.score += gained_xp;

    if (g.score > g.high_score) {
        g.high_score = g.score;
    }

    // Проверка повышения уровня
    bool leveled_up = false;
    while (g.xp >= g.needed_xp) {
        g.xp -= g.needed_xp;
        g.level++;
        g.moves_left += 15; // +15 бонусных ходов за уровень!
        g.needed_xp = calc_needed_xp(g.level);
        leveled_up = true;
    }

    // Проверка окончания ходов
    if (g.moves_left <= 0) {
        g.is_finished = true;
    }

    // Уведомление игрока в toast
    std::string toast = "✨ +" + std::to_string(gained_xp) + " XP!";
    if (combo > 1) toast += " (Комбо x" + std::to_string(combo) + "!)";
    if (leveled_up) toast += " 🎉 Уровень " + std::to_string(g.level) + "! +15 ходов!";
    api->cb_answer(ctx, toast.c_str(), 0);
}

// ── Command: .m3 / .match3 ──────────────────────────────────────────────────
void cmd_match3(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);
    long long sender = api->sender_id ? api->sender_id(ctx) : 0;
    if (cid == 0) cid = sender;

    char name_buf[64] = {0};
    if (api->user_name(ctx, sender, name_buf, sizeof(name_buf)) <= 0 || !name_buf[0]) {
        snprintf(name_buf, sizeof(name_buf), "Игрок_%lld", (sender > 0 ? sender : -sender) % 10000);
    }

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? raw_args : "";

    std::lock_guard<std::mutex> lk(g_m3_mu);
    GameState g;
    g.id = "m3_" + std::to_string(++g_game_seq);
    g.chat_id = cid;
    g.owner_id = sender;
    g.owner_name = name_buf;
    g.level = 1;
    g.xp = 0;
    g.needed_xp = calc_needed_xp(1);
    g.score = 0;
    g.moves_left = DEFAULT_MOVES;
    g.last_active = time(nullptr);

    // Загрузка рекорда
    if (api->db_get) {
        char hbuf[32] = {0};
        std::string hkey = "m3_hs:" + std::to_string(sender ? sender : cid);
        if (api->db_get(ctx, hkey.c_str(), hbuf, sizeof(hbuf)) > 0 && hbuf[0]) {
            g.high_score = std::atoi(hbuf);
        }
    }

    init_board(g);
    render_game(ctx, api, g);

    if (api->is_outgoing(ctx) && api->delete_msg) {
        api->delete_msg(ctx);
    }
    api->c_send(ctx, cid, 0);

    long long cmd_mid = api->msg_id(ctx);
    g.msg_id = (cmd_mid > 0) ? (cmd_mid + 1) : 0;

    save_game_db(ctx, api, g);
    g_games_by_id[g.id] = g;
    g_chat_to_game[cid] = g.id;
    if (sender != 0) g_chat_to_game[sender] = g.id;
}

// ── Callback Handler для кнопок игры ─────────────────────────────────────────
void on_callback(koto_ctx* ctx, const koto_api* api) {
    const char* raw_data = api->cb_data(ctx);
    if (!raw_data || std::strncmp(raw_data, "m3:", 3) != 0) return;

    std::string data = raw_data;
    long long cid = api->chat_id(ctx);
    long long mid = api->cb_message_id(ctx);
    long long sender = api->sender_id(ctx);
    if (cid == 0) cid = sender;

    std::unique_lock<std::mutex> lk(g_m3_mu);
    GameState* gp = find_game(ctx, api, cid, sender);
    if (!gp) {
        api->cb_answer(ctx, "Игра не найдена. Запустите: .m3", 0);
        return;
    }

    GameState& g = *gp;
    if (mid > 0) g.msg_id = mid;

    // Защита от чужих нажатий
    if (g.owner_id != 0 && sender != g.owner_id) {
        api->cb_answer(ctx, "Это чужая игра! Запустите свою: .m3", 1);
        return;
    }

    // Рестарт / Новая игра
    if (data == "m3:restart" || data == "m3:new") {
        g.level = 1;
        g.xp = 0;
        g.needed_xp = calc_needed_xp(1);
        g.score = 0;
        g.moves_left = DEFAULT_MOVES;
        g.is_finished = false;
        g.selected_r = -1;
        g.selected_c = -1;
        g.last_points = 0;
        g.last_combo = 0;
        init_board(g);
        save_game_db(ctx, api, g);
        render_game(ctx, api, g);
        api->c_edit_message(ctx, cid, g.msg_id, 0);
        api->cb_answer(ctx, "🔄 Новая партия начата!", 0);
        return;
    }

    // Остановка
    if (data == "m3:stop") {
        g.is_finished = true;
        save_game_db(ctx, api, g);
        render_game(ctx, api, g);
        api->c_edit_message(ctx, cid, g.msg_id, 0);
        api->cb_answer(ctx, "🛑 Игра завершена", 0);
        return;
    }

    // Подсказка
    if (data == "m3:hint") {
        Point p1, p2;
        if (find_possible_move(g, p1, p2)) {
            std::string h_msg = "💡 Попробуйте поменять [" + std::to_string(p1.r + 1) + "," + std::to_string(p1.c + 1) +
                                "] и [" + std::to_string(p2.r + 1) + "," + std::to_string(p2.c + 1) + "]!";
            api->cb_answer(ctx, h_msg.c_str(), 1);
        } else {
            // Перемешиваем доску если ходов нет
            init_board(g);
            save_game_db(ctx, api, g);
            render_game(ctx, api, g);
            api->c_edit_message(ctx, cid, g.msg_id, 0);
            api->cb_answer(ctx, "Ходов не было — поле перемешано!", 0);
        }
        return;
    }

    // Нажатие на фишку: m3:t:row:col
    if (data.rfind("m3:t:", 0) == 0) {
        if (g.is_finished) {
            api->cb_answer(ctx, "Игра завершена. Нажмите «Играть снова»!", 0);
            return;
        }

        int r = -1, c = -1;
        if (sscanf(data.c_str() + 5, "%d:%d", &r, &c) != 2 || r < 0 || r >= BOARD_ROWS || c < 0 || c >= BOARD_COLS) {
            return;
        }

        // Если ничего не было выбрано — выбираем
        if (g.selected_r == -1 || g.selected_c == -1) {
            g.selected_r = r;
            g.selected_c = c;
            // Сбрасываем цвета подсветок предыдущего хода при новом клике
            std::memset(g.highlight, 0, sizeof(g.highlight));
            save_game_db(ctx, api, g);
            render_game(ctx, api, g);
            api->c_edit_message(ctx, cid, g.msg_id, 0);
            api->cb_answer(ctx, "Фишка выбрана. Нажмите соседнюю!", 0);
            return;
        }

        // Если кликнули на ту же самую — отменяем выбор
        if (g.selected_r == r && g.selected_c == c) {
            g.selected_r = -1;
            g.selected_c = -1;
            save_game_db(ctx, api, g);
            render_game(ctx, api, g);
            api->c_edit_message(ctx, cid, g.msg_id, 0);
            api->cb_answer(ctx, "Выбор снят", 0);
            return;
        }

        // Проверяем соседство (манхэттенское расстояние = 1)
        int dist = std::abs(g.selected_r - r) + std::abs(g.selected_c - c);
        if (dist == 1) {
            // Соседняя фишка — выполняем попытку обмена!
            int r1 = g.selected_r;
            int c1 = g.selected_c;
            process_swap(ctx, api, g, r1, c1, r, c);

            // Сохраняем рекорд если побит
            if (api->db_set && g.score >= g.high_score) {
                std::string hkey = "m3_hs:" + std::to_string(g.owner_id ? g.owner_id : cid);
                api->db_set(ctx, hkey.c_str(), std::to_string(g.score).c_str());
            }

            save_game_db(ctx, api, g);
            render_game(ctx, api, g);
            api->c_edit_message(ctx, cid, g.msg_id, 0);
            return;
        } else {
            // Кликнули на несоседнюю фишку — просто меняем выделение
            g.selected_r = r;
            g.selected_c = c;
            std::memset(g.highlight, 0, sizeof(g.highlight));
            save_game_db(ctx, api, g);
            render_game(ctx, api, g);
            api->c_edit_message(ctx, cid, g.msg_id, 0);
            api->cb_answer(ctx, "Выбрана новая фишка", 0);
            return;
        }
    }
}

// ── Watcher для синхронизации ID сообщения ──────────────────────────────────
int on_watch(koto_ctx* ctx, const koto_api* api) {
    const char* raw_txt = api->msg_text(ctx);
    if (!raw_txt || !*raw_txt) return 0;

    long long cur_mid = api->msg_id(ctx);
    long long cid = api->chat_id(ctx);
    long long sender = api->sender_id(ctx);
    if (cid == 0) cid = sender;

    if (std::strstr(raw_txt, "💎 ТРИ В РЯД")) {
        std::lock_guard<std::mutex> lk(g_m3_mu);
        GameState* gp = find_game(ctx, api, cid, sender);
        if (gp && !gp->is_finished && cur_mid > 0) {
            gp->msg_id = cur_mid;
            save_game_db(ctx, api, *gp);
        }
    }
    return 0;
}

const koto_command COMMANDS[] = {
    {"m3", cmd_match3, KOTO_LEVEL_ALL, "[ru] Запустить игру «Три в ряд» (Match-3) [en] Start Match-3 game.", "[restart|top]"},
    {"match3", cmd_match3, KOTO_LEVEL_ALL, "[ru] Запустить игру «Три в ряд» (Match-3) [en] Start Match-3 game.", "[restart|top]"}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "match3",
    "[ru] Игра «Три в ряд» (Match-3): сетка 8х8, цветные кружки, система уровней и опыта, подсветка собранных рядов и комбо-каскадов (by kote). [en] Match-3 puzzle game: 8x8 grid, colored circles, levels and XP, combo cascades (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    on_watch,
    KOTO_WATCH_OUTGOING,
    on_callback,
    nullptr,
    0,
    "https://github.com/AresUser1/KoteModules/blob/main/src/match3.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
