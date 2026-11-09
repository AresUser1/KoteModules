// mazegame.cpp
// Модуль "Лабиринт" (Maze Game) для Kotogram (ABI 3, .so)
// Автор: by kote (портировано из MazeGame.py)

#include "module_abi.h"
#include "nlohmann/json.hpp"
using json = nlohmann::json;

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <random>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace {

constexpr const char* VERSION = "1.0.3";
constexpr int DEFAULT_MAZE_SIZE = 15; // 15x15 лабиринт (всегда нечетный)
constexpr int VIEWPORT_SIZE = 7;      // видимая область 7x7

enum CellType {
    CELL_WALL = 0,
    CELL_PATH = 1,
    CELL_FIRE = 2,
    CELL_FINISH = 3
};

struct Pos {
    int r = 0;
    int c = 0;
    bool operator==(const Pos& o) const { return r == o.r && c == o.c; }
};

struct Game {
    std::string id;
    int64_t chat_id = 0;
    int64_t msg_id = 0;
    int64_t p1_id = 0;
    int64_t p2_id = 0;
    std::string p1_name;
    std::string p2_name;

    int rows = DEFAULT_MAZE_SIZE;
    int cols = DEFAULT_MAZE_SIZE;
    std::vector<std::vector<int>> grid; // CellType

    Pos start_pos{1, 1};
    Pos finish_pos{13, 13};
    Pos p1_pos{1, 1};
    Pos p2_pos{13, 1};

    int turn = 1; // 1 = p1, 2 = p2
    int p1_steps = 0;
    int p2_steps = 0;

    bool is_multiplayer = false;
    bool is_finished = false;
    int winner = 0; // 0 = никто, 1 = p1, 2 = p2
    std::string end_reason;
    time_t last_active = 0;
};

std::mutex g_mutex;
static int g_game_seq = 100;
std::unordered_map<std::string, Game> g_games_by_id;
std::unordered_map<int64_t, std::string> g_chat_to_game;

inline long long normalize_cid(long long cid) {
    if (cid > 1000000000LL && cid < 1000000000000LL) {
        return -1000000000000LL - cid;
    }
    return cid;
}

inline long long bare_cid(long long cid) {
    if (cid < -1000000000000LL) {
        return -cid - 1000000000000LL;
    }
    return cid;
}


void save_maze_game_db(koto_ctx* ctx, const koto_api* api, const Game& g) {
    if (!api->db_set) return;
    try {
        json j;
        j["id"] = g.id;
        j["chat_id"] = g.chat_id;
        j["msg_id"] = g.msg_id;
        j["p1_id"] = g.p1_id;
        j["p1_name"] = g.p1_name;
        j["p2_id"] = g.p2_id;
        j["p2_name"] = g.p2_name;
        j["is_multiplayer"] = g.is_multiplayer;
        j["is_finished"] = g.is_finished;
        j["turn"] = g.turn;
        j["winner"] = g.winner;
        j["end_reason"] = g.end_reason;
        j["rows"] = g.rows;
        j["cols"] = g.cols;
        j["p1_steps"] = g.p1_steps;
        j["p2_steps"] = g.p2_steps;
        j["start_pos"] = {g.start_pos.r, g.start_pos.c};
        j["finish_pos"] = {g.finish_pos.r, g.finish_pos.c};
        j["p1_pos"] = {g.p1_pos.r, g.p1_pos.c};
        j["p2_pos"] = {g.p2_pos.r, g.p2_pos.c};
        j["grid"] = g.grid;
        std::string s = j.dump();
        api->db_set(ctx, ("mzg_g:" + g.id).c_str(), s.c_str());
        if (g.chat_id != 0) {
            api->db_set(ctx, ("mzg_act:" + std::to_string(g.chat_id)).c_str(), g.id.c_str());
        }
        if (g.p1_id != 0) {
            api->db_set(ctx, ("mzg_act:" + std::to_string(g.p1_id)).c_str(), g.id.c_str());
        }
        if (g.p2_id != 0) {
            api->db_set(ctx, ("mzg_act:" + std::to_string(g.p2_id)).c_str(), g.id.c_str());
        }
    } catch (...) {}
}

bool load_maze_game_db(koto_ctx* ctx, const koto_api* api, const std::string& gid, Game& out) {
    if (!api->db_get || gid.empty()) return false;
    try {
        char buf[65536] = {0};
        int n = api->db_get(ctx, ("mzg_g:" + gid).c_str(), buf, sizeof(buf));
        if (n <= 0) return false;
        json j = json::parse(std::string(buf, n));
        out.id = j.value("id", gid);
        out.chat_id = j.value("chat_id", 0LL);
        out.msg_id = j.value("msg_id", 0LL);
        out.p1_id = j.value("p1_id", 0LL);
        out.p1_name = j.value("p1_name", "");
        out.p2_id = j.value("p2_id", 0LL);
        out.p2_name = j.value("p2_name", "");
        out.is_multiplayer = j.value("is_multiplayer", false);
        out.is_finished = j.value("is_finished", false);
        out.turn = j.value("turn", 1);
        out.winner = j.value("winner", 0);
        out.end_reason = j.value("end_reason", "");
        out.rows = j.value("rows", DEFAULT_MAZE_SIZE);
        out.cols = j.value("cols", DEFAULT_MAZE_SIZE);
        out.p1_steps = j.value("p1_steps", 0);
        out.p2_steps = j.value("p2_steps", 0);
        if (j.contains("start_pos") && j["start_pos"].is_array() && j["start_pos"].size() >= 2) {
            out.start_pos = {j["start_pos"][0], j["start_pos"][1]};
        }
        if (j.contains("finish_pos") && j["finish_pos"].is_array() && j["finish_pos"].size() >= 2) {
            out.finish_pos = {j["finish_pos"][0], j["finish_pos"][1]};
        }
        if (j.contains("p1_pos") && j["p1_pos"].is_array() && j["p1_pos"].size() >= 2) {
            out.p1_pos = {j["p1_pos"][0], j["p1_pos"][1]};
        }
        if (j.contains("p2_pos") && j["p2_pos"].is_array() && j["p2_pos"].size() >= 2) {
            out.p2_pos = {j["p2_pos"][0], j["p2_pos"][1]};
        }
        if (j.contains("grid")) {
            out.grid = j["grid"].get<std::vector<std::vector<int>>>();
        }
        return true;
    } catch (...) {
        return false;
    }
}

Game* find_maze_game(const std::string& target_id, long long cid, long long sender, koto_ctx* ctx = nullptr, const koto_api* api = nullptr) {
    if (!target_id.empty()) {
        auto it = g_games_by_id.find(target_id);
        if (it != g_games_by_id.end()) return &it->second;
        try {
            long long tid = std::stoll(target_id);
            if (tid != 0) {
                for (long long cand : {tid, normalize_cid(tid), bare_cid(tid), -tid}) {
                    auto it2 = g_chat_to_game.find(cand);
                    if (it2 != g_chat_to_game.end()) {
                        auto git = g_games_by_id.find(it2->second);
                        if (git != g_games_by_id.end()) return &git->second;
                    }
                }
                for (auto& p : g_games_by_id) {
                    if (p.second.chat_id == tid || p.second.p1_id == tid || p.second.p2_id == tid) {
                        return &p.second;
                    }
                }
            }
        } catch (...) {}

        if (ctx && api) {
            Game loaded;
            if (load_maze_game_db(ctx, api, target_id, loaded)) {
                g_games_by_id[target_id] = loaded;
                return &g_games_by_id[target_id];
            }
        }
    }
    if (cid != 0) {
        for (long long cand : {cid, normalize_cid(cid), bare_cid(cid)}) {
            auto it = g_chat_to_game.find(cand);
            if (it != g_chat_to_game.end()) {
                auto git = g_games_by_id.find(it->second);
                if (git != g_games_by_id.end()) return &git->second;
            }
        }
        if (ctx && api && api->db_get) {
            char buf[64] = {0};
            if (api->db_get(ctx, ("mzg_act:" + std::to_string(cid)).c_str(), buf, sizeof(buf)) > 0 && buf[0]) {
                Game loaded;
                if (load_maze_game_db(ctx, api, buf, loaded)) {
                    g_games_by_id[buf] = loaded;
                    g_chat_to_game[cid] = buf;
                    return &g_games_by_id[buf];
                }
            }
        }
    }
    if (sender != 0) {
        for (long long cand : {sender, normalize_cid(sender), bare_cid(sender)}) {
            auto it = g_chat_to_game.find(cand);
            if (it != g_chat_to_game.end()) {
                auto git = g_games_by_id.find(it->second);
                if (git != g_games_by_id.end()) return &git->second;
            }
        }
        if (ctx && api && api->db_get) {
            char buf[64] = {0};
            if (api->db_get(ctx, ("mzg_act:" + std::to_string(sender)).c_str(), buf, sizeof(buf)) > 0 && buf[0]) {
                Game loaded;
                if (load_maze_game_db(ctx, api, buf, loaded)) {
                    g_games_by_id[buf] = loaded;
                    g_chat_to_game[sender] = buf;
                    return &g_games_by_id[buf];
                }
            }
        }
    }
    return nullptr;
}

// ── Генератор лабиринта (DFS / Recursive Backtracker) ───────────────────────

void carve_passages(std::vector<std::vector<int>>& grid, int r, int c, int rows, int cols, std::mt19937& rng) {
    int dirs[4][2] = {{0, 2}, {2, 0}, {0, -2}, {-2, 0}};
    // Shuffle directions
    for (int i = 3; i > 0; --i) {
        std::uniform_int_distribution<int> dist(0, i);
        int j = dist(rng);
        std::swap(dirs[i], dirs[j]);
    }

    for (int i = 0; i < 4; ++i) {
        int nr = r + dirs[i][0];
        int nc = c + dirs[i][1];
        if (nr > 0 && nr < rows - 1 && nc > 0 && nc < cols - 1 && grid[nr][nc] == CELL_WALL) {
            grid[r + dirs[i][0] / 2][c + dirs[i][1] / 2] = CELL_PATH;
            grid[nr][nc] = CELL_PATH;
            carve_passages(grid, nr, nc, rows, cols, rng);
        }
    }
}

Game generate_new_maze(int64_t chat_id, int64_t p1_id, const std::string& p1_name,
                      int64_t p2_id, const std::string& p2_name, int size = DEFAULT_MAZE_SIZE) {
    if (size % 2 == 0) size++;
    if (size < 11) size = 11;
    if (size > 21) size = 21;

    std::mt19937 rng(static_cast<unsigned int>(time(nullptr) ^ (chat_id << 4)));

    Game g;
    g.id = "mzg_" + std::to_string(++g_game_seq);
    g.chat_id = chat_id;
    g.p1_id = p1_id;
    g.p1_name = p1_name;
    g.p2_id = p2_id;
    g.p2_name = p2_name;
    g.is_multiplayer = (p2_id != 0 && p2_id != p1_id);
    g.rows = size;
    g.cols = size;
    g.last_active = time(nullptr);
    g.grid.assign(size, std::vector<int>(size, CELL_WALL));

    // Carve from (1, 1)
    g.grid[1][1] = CELL_PATH;
    carve_passages(g.grid, 1, 1, size, size, rng);

    g.start_pos = {1, 1};
    g.p1_pos = {1, 1};

    // Exit candidate: дальний угол
    g.finish_pos = {size - 2, size - 2};
    g.grid[g.finish_pos.r][g.finish_pos.c] = CELL_FINISH;

    if (g.is_multiplayer) {
        // Player 2 стартует в нижнем левом углу
        g.p2_pos = {size - 2, 1};
        if (g.grid[g.p2_pos.r][g.p2_pos.c] == CELL_WALL) {
            g.grid[g.p2_pos.r][g.p2_pos.c] = CELL_PATH;
        }
        // Финиш ставим по центру для честного баланса в PvP
        g.finish_pos = {size / 2, size / 2};
        if (g.grid[g.finish_pos.r][g.finish_pos.c] == CELL_WALL) {
            g.grid[g.finish_pos.r][g.finish_pos.c] = CELL_PATH;
        }
        g.grid[g.finish_pos.r][g.finish_pos.c] = CELL_FINISH;
    }

    // Случайные огненные ловушки на месте некоторых стен (12%)
    std::uniform_int_distribution<int> pct(1, 100);
    for (int r = 2; r < size - 2; ++r) {
        for (int c = 2; c < size - 2; ++c) {
            if (g.grid[r][c] == CELL_WALL && pct(rng) <= 12) {
                g.grid[r][c] = CELL_FIRE;
            }
        }
    }

    return g;
}

// ── Отрисовка поля лабиринта (Viewport 7x7) ─────────────────────────────────

std::string render_maze_display(const Game& g) {
    std::string out;
    Pos center = (g.turn == 1) ? g.p1_pos : g.p2_pos;
    if (g.is_finished && g.winner == 2) center = g.p2_pos;

    int half = VIEWPORT_SIZE / 2;
    int r_start = center.r - half;
    int c_start = center.c - half;

    for (int r = r_start; r <= center.r + half; ++r) {
        for (int c = c_start; c <= center.c + half; ++c) {
            if (r < 0 || r >= g.rows || c < 0 || c >= g.cols) {
                out += "⬛️"; // Граница мира
                continue;
            }

            Pos cur{r, c};
            if (cur == g.p1_pos && cur == g.p2_pos && g.is_multiplayer) {
                out += "⚔️";
            } else if (cur == g.p1_pos) {
                out += "👾"; // Игрок 1
            } else if (cur == g.p2_pos && g.is_multiplayer) {
                out += "🫥"; // Игрок 2
            } else if (cur == g.finish_pos) {
                out += "🏁"; // Финиш
            } else if (g.grid[r][c] == CELL_FIRE) {
                out += "🔥"; // Ловушка
            } else if (g.grid[r][c] == CELL_PATH) {
                out += "⬜️"; // Проход
            } else {
                out += "⬛️"; // Стена
            }
        }
        out += "\n";
    }
    return out;
}

void render_game_message(koto_ctx* ctx, const koto_api* api, const Game& g) {
    api->c_reset(ctx);

    api->c_fmt(ctx, "🌀 ЛАБИРИНТ (Maze Game)\n\n", KOTO_ENT_BOLD);

    if (g.is_finished) {
        if (g.winner == 1) {
            std::string w = "🏆 ПОБЕДА ИГРОКА 1: " + g.p1_name + "!\n";
            api->c_fmt(ctx, w.c_str(), KOTO_ENT_BOLD);
        } else if (g.winner == 2) {
            std::string w = "🏆 ПОБЕДА ИГРОКА 2: " + g.p2_name + "!\n";
            api->c_fmt(ctx, w.c_str(), KOTO_ENT_BOLD);
        } else {
            api->c_fmt(ctx, "💀 ИГРА ОКОНЧЕНА!\n", KOTO_ENT_BOLD);
        }

        if (!g.end_reason.empty()) {
            std::string r = g.end_reason + "\n\n";
            api->c_fmt(ctx, r.c_str(), KOTO_ENT_ITALIC);
        }
    } else {
        if (g.is_multiplayer) {
            std::string info = "👥 Игроки: " + g.p1_name + " (👾) VS " + g.p2_name + " (🫥)\n";
            info += "👉 Сейчас ходит: " + (g.turn == 1 ? (g.p1_name + " (👾)") : (g.p2_name + " (🫥)")) + "\n";
            info += "🎯 Шаги: " + std::to_string(g.p1_steps) + " | " + std::to_string(g.p2_steps) + "\n\n";
            api->c_text(ctx, info.c_str());
        } else {
            std::string info = "👤 Игрок: " + g.p1_name + " (👾)\n";
            info += "🎯 Сделано шагов: " + std::to_string(g.p1_steps) + "\n\n";
            api->c_text(ctx, info.c_str());
        }
    }

    std::string maze_display = render_maze_display(g);
    api->c_text(ctx, maze_display.c_str());

    api->c_fmt(ctx, "\n🏁 Выход | 🔥 Огонь | ⬛️ Стена\nby kote", KOTO_ENT_ITALIC);

    // Добавляем инлайн-кнопки
    std::string gid = g.id.empty() ? std::to_string(g.chat_id) : g.id;
    if (!g.is_finished) {
        // Управление движением
        api->c_button(ctx, "⬆️", ("mzg:m:-1:0:" + gid).c_str(), 1);

        api->c_button(ctx, "⬅️", ("mzg:m:0:-1:" + gid).c_str(), 1);
        api->c_button(ctx, "⬇️", ("mzg:m:1:0:" + gid).c_str(), 0);
        api->c_button(ctx, "➡️", ("mzg:m:0:1:" + gid).c_str(), 0);

        if (api->c_button_ex) {
            api->c_button_ex(ctx, "🏳️ Сдаться", ("mzg:s:" + gid).c_str(), 1, KOTO_BTN_DANGER, 0);
        } else {
            api->c_button(ctx, "🏳️ Сдаться", ("mzg:s:" + gid).c_str(), 1);
        }
    } else {
        if (api->c_button_ex) {
            api->c_button_ex(ctx, "🔄 Новая игра", ("mzg:n:" + gid).c_str(), 1, KOTO_BTN_SUCCESS, 0);
        } else {
            api->c_button(ctx, "🔄 Новая игра", ("mzg:n:" + gid).c_str(), 1);
        }
    }
}

// ── Логика движения ────────────────────────────────────────────────────────

void apply_move(koto_ctx* ctx, const koto_api* api, Game& g, int player_num, int dr, int dc, long long mid) {
    Pos& pos = (player_num == 1) ? g.p1_pos : g.p2_pos;
    int& steps = (player_num == 1) ? g.p1_steps : g.p2_steps;

    int nr = pos.r + dr;
    int nc = pos.c + dc;

    if (nr < 0 || nr >= g.rows || nc < 0 || nc >= g.cols || g.grid[nr][nc] == CELL_WALL) {
        api->cb_answer(ctx, "🧱 Там стена! Выберите другое направление.", 0);
        return;
    }

    if (g.grid[nr][nc] == CELL_FIRE) {
        // Смерть в огне!
        g.is_finished = true;
        pos.r = nr;
        pos.c = nc;
        steps++;
        if (g.is_multiplayer) {
            g.winner = (player_num == 1) ? 2 : 1;
            std::string p_name = (player_num == 1) ? g.p1_name : g.p2_name;
            g.end_reason = "🔥 " + p_name + " сгорел(а) в огне ловушки!";
            api->cb_answer(ctx, "🔥 Вы сгорели в ловушке!", 1);
        } else {
            g.winner = 0;
            g.end_reason = "🔥 Вы угодили в огненную ловушку и сгорели!";
            api->cb_answer(ctx, "🔥 Вы сгорели в ловушке! Игра окончена.", 1);
        }
        save_maze_game_db(ctx, api, g);
    render_game_message(ctx, api, g);
        long long target_chat = (g.chat_id != 0) ? g.chat_id : api->chat_id(ctx);
    long long target_mid = (mid != 0) ? mid : g.msg_id;
    api->c_edit_message(ctx, target_chat, target_mid, 0);
        return;
    }

    // Успешный шаг
    pos.r = nr;
    pos.c = nc;
    steps++;

    if (pos == g.finish_pos) {
        // Победа! Достигли финиша
        g.is_finished = true;
        g.winner = player_num;
        std::string p_name = (player_num == 1) ? g.p1_name : g.p2_name;
        g.end_reason = "🎉 " + p_name + " первым выбрался из лабиринта за " + std::to_string(steps) + " шагов!";
        api->cb_answer(ctx, "🏁 УРА! Вы выбрались из лабиринта!", 1);
    } else {
        // Передача хода в PvP
        if (g.is_multiplayer) {
            g.turn = (g.turn == 1) ? 2 : 1;
        }
        api->cb_answer(ctx, "👣 Шаг сделан", 0);
    }

    save_maze_game_db(ctx, api, g);
    render_game_message(ctx, api, g);
    long long target_chat = (g.chat_id != 0) ? g.chat_id : api->chat_id(ctx);
    long long target_mid = (mid != 0) ? mid : g.msg_id;
    api->c_edit_message(ctx, target_chat, target_mid, 0);
}

// ── Команда старта: .maze / .mg / .лабиринт ────────────────────────────────

void cmd_maze(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);
    long long sender = api->sender_id(ctx);
    if (cid == 0) cid = sender;

    char p1_buf[64] = {0};
    if (api->user_name(ctx, sender, p1_buf, sizeof(p1_buf)) <= 0 || !p1_buf[0]) {
        snprintf(p1_buf, sizeof(p1_buf), "Игрок_%lld", (sender > 0 ? sender : -sender) % 10000);
    }

    // Проверяем, есть ли второй игрок (реплай)
    long long p2_id = 0;
    char p2_buf[64] = {0};
    if (api->reply_sender_id) {
        p2_id = api->reply_sender_id(ctx);
    }
    if (p2_id != 0 && p2_id != sender) {
        if (api->user_name(ctx, p2_id, p2_buf, sizeof(p2_buf)) <= 0 || !p2_buf[0]) {
            snprintf(p2_buf, sizeof(p2_buf), "Соперник_%lld", (p2_id > 0 ? p2_id : -p2_id) % 10000);
        }
    } else {
        p2_id = 0;
    }

    Game g = generate_new_maze(cid, sender, p1_buf, p2_id, p2_buf);

    save_maze_game_db(ctx, api, g);
    render_game_message(ctx, api, g);

    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }

    int64_t cmd_mid = api->msg_id(ctx);
    g.msg_id = (cmd_mid > 0) ? (cmd_mid + 1) : 0;

    std::lock_guard<std::mutex> lock(g_mutex);
    save_maze_game_db(ctx, api, g);
    g_games_by_id[g.id] = g;
    g_chat_to_game[cid] = g.id;
    g_chat_to_game[normalize_cid(cid)] = g.id;
    g_chat_to_game[bare_cid(cid)] = g.id;
    if (sender != 0) g_chat_to_game[sender] = g.id;
}

void cmd_mazestop(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);
    long long sender = api->sender_id(ctx);
    if (cid == 0) cid = sender;

    std::lock_guard<std::mutex> lock(g_mutex);
    Game* gp = find_maze_game("", cid, sender);
    if (!gp || gp->is_finished) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "⚠️ Нет активной игры в Лабиринт.", KOTO_ENT_ITALIC);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    Game& g = *gp;
    g.is_finished = true;
    g.end_reason = "🏳️ Игра была остановлена.";

    save_maze_game_db(ctx, api, g);
    render_game_message(ctx, api, g);

    if (g.msg_id != 0) {
        int64_t target_chat = (g.chat_id != 0 && g.chat_id != cid) ? g.chat_id : 0;
        api->c_edit_message(ctx, target_chat, g.msg_id, 0);
    } else {
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    }
}

// ── Callback Handler для inline-кнопок ─────────────────────────────────────

void on_callback(koto_ctx* ctx, const koto_api* api) {
    const char* raw_data = api->cb_data(ctx);
    if (!raw_data || std::strncmp(raw_data, "mzg:", 4) != 0) return;

    std::string data = raw_data;
    long long cid = api->chat_id(ctx);
    long long mid = api->cb_message_id(ctx);
    long long sender = api->sender_id(ctx);

    std::string target_id;
    int move_dr = 0, move_dc = 0;
    bool is_move = false;

    if (data.rfind("mzg:m:", 0) == 0) {
        is_move = true;
        size_t p1 = 6;
        size_t p2 = data.find(':', p1);
        if (p2 != std::string::npos) {
            size_t p3 = data.find(':', p2 + 1);
            if (p3 != std::string::npos) {
                try {
                    move_dr = std::stoi(data.substr(p1, p2 - p1));
                    move_dc = std::stoi(data.substr(p2 + 1, p3 - (p2 + 1)));
                    target_id = data.substr(p3 + 1);
                } catch (...) {}
            }
        }
    } else if (data.rfind("mzg:s:", 0) == 0) {
        target_id = data.substr(6);
    } else if (data.rfind("mzg:n:", 0) == 0) {
        target_id = data.substr(6);
    }

    std::unique_lock<std::mutex> lock(g_mutex);
    Game* gp = find_maze_game(target_id, cid, sender, ctx, api);
    if (!gp) {
        api->cb_answer(ctx, "Игра не найдена", 0);
        return;
    }

    Game& g = *gp;
    if (mid != 0) g.msg_id = mid;

    // Сдаться: mzg:s:<cid>
    if (data.rfind("mzg:s:", 0) == 0) {
        if (g.is_finished) {
            api->cb_answer(ctx, "Игра уже завершена", 0);
            return;
        }

        if (g.is_multiplayer) {
            if (sender == g.p1_id) {
                g.is_finished = true;
                g.winner = 2;
                g.end_reason = "🏳️ " + g.p1_name + " сдался(ась)!";
            } else if (sender == g.p2_id) {
                g.is_finished = true;
                g.winner = 1;
                g.end_reason = "🏳️ " + g.p2_name + " сдался(ась)!";
            } else {
                api->cb_answer(ctx, "Вы не участвуете в этой игре!", 1);
                return;
            }
        } else {
            if (sender != g.p1_id && sender != 0) {
                api->cb_answer(ctx, "Вы не участвуете в этой игре!", 1);
                return;
            }
            g.is_finished = true;
            g.winner = 0;
            g.end_reason = "🏳️ Вы сдались и покинули лабиринт.";
        }

        api->cb_answer(ctx, "🏳️ Сдались!", 0);
        save_maze_game_db(ctx, api, g);
    render_game_message(ctx, api, g);
        long long target_chat = (g.chat_id != 0) ? g.chat_id : api->chat_id(ctx);
    long long target_mid = (mid != 0) ? mid : g.msg_id;
    api->c_edit_message(ctx, target_chat, target_mid, 0);
        return;
    }

    // Новая игра: mzg:n:<cid>
    if (data.rfind("mzg:n:", 0) == 0) {
        std::string old_id = g.id;
        Game new_g = generate_new_maze(g.chat_id, g.p1_id, g.p1_name, g.p2_id, g.p2_name, g.rows);
        new_g.msg_id = mid;
        g = new_g;
        g_games_by_id[g.id] = g;
        g_chat_to_game[g.chat_id] = g.id;
        g_chat_to_game[normalize_cid(g.chat_id)] = g.id;
        g_chat_to_game[bare_cid(g.chat_id)] = g.id;
        if (g.p1_id != 0) g_chat_to_game[g.p1_id] = g.id;

        api->cb_answer(ctx, "🔄 Лабиринт перестроен!", 0);
        save_maze_game_db(ctx, api, g);
    render_game_message(ctx, api, g);
        long long target_chat = (g.chat_id != 0) ? g.chat_id : api->chat_id(ctx);
    long long target_mid = (mid != 0) ? mid : g.msg_id;
    api->c_edit_message(ctx, target_chat, target_mid, 0);
        return;
    }

    // Ход: mzg:m:<dr>:<dc>:<cid>
    if (data.rfind("mzg:m:", 0) == 0) {
        if (g.is_finished) {
            api->cb_answer(ctx, "Игра уже окончена", 0);
            return;
        }

        int dr = move_dr, dc = move_dc;

        int player_num = 1;
        if (g.is_multiplayer) {
            if (sender == g.p1_id) player_num = 1;
            else if (sender == g.p2_id) player_num = 2;
            else {
                api->cb_answer(ctx, "Вы не участвуете в этой игре!", 1);
                return;
            }

            if (player_num != g.turn) {
                api->cb_answer(ctx, "⏳ Сейчас не ваш ход!", 1);
                return;
            }
        } else {
            if (sender != g.p1_id && sender != 0) {
                // Если кто-то другой жмет кнопки в одиночной игре
                api->cb_answer(ctx, "Это одиночная игра другого игрока!", 1);
                return;
            }
        }

        apply_move(ctx, api, g, player_num, dr, dc, mid);
        return;
    }
}

// ── Watcher: синхронизация ─────────────────────────────────────────────────

int on_watch(koto_ctx* ctx, const koto_api* api) {
    long long r_mid = api->reply_msg_id ? api->reply_msg_id(ctx) : 0;
    if (r_mid == 0) return 0;

    long long cid = api->chat_id(ctx);
    long long sender = api->sender_id(ctx);

    std::lock_guard<std::mutex> lock(g_mutex);
    Game* gp = find_maze_game("", cid, sender);
    if (gp && !gp->is_finished) {
        if (gp->msg_id == 0 || std::abs(gp->msg_id - r_mid) <= 15) {
            gp->msg_id = r_mid;
        }
    }
    return 0;
}

// ── Декларация команд и модуля ─────────────────────────────────────────────

const koto_command COMMANDS[] = {
    {"maze", cmd_maze, KOTO_LEVEL_ALL, "[ru] Игра Лабиринт (одному или реплаем на игрока) [en] Maze game (solo or reply to player)", "[размер]"},
    {"mg", cmd_maze, KOTO_LEVEL_ALL, "[ru] Быстрый алиас для .maze [en] Fast alias for .maze", "[размер]"},
    {"mazegame", cmd_maze, KOTO_LEVEL_ALL, "[ru] Быстрый алиас для .maze [en] Fast alias for .maze", "[размер]"},
    {"лабиринт", cmd_maze, KOTO_LEVEL_ALL, "[ru] Быстрый алиас для .maze [en] Fast alias for .maze", "[размер]"},
    {"mazestop", cmd_mazestop, KOTO_LEVEL_ALL, "[ru] Завершить текущую игру в лабиринт [en] Stop active maze game", nullptr}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "mazegame",
    "[ru] Игра Лабиринт: поиск выхода, огненные ловушки, одиночная игра и PvP на двоих с инлайн-кнопками (by kote). [en] Maze game: find the exit, fire traps, solo and 2-player PvP with inline buttons (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    on_watch,
    KOTO_WATCH_INCOMING | KOTO_WATCH_OUTGOING,
    on_callback,
    nullptr, // settings
    0,
    "https://github.com/AresUser1/KoteModules/blob/main/src/mazegame.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
