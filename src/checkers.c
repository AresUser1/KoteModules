// checkers.c
// Шашки с русскими правилами для Kotogram (ABI 3, .so).
// Реализация:
//  • Полноценный PvP между ЛЮБЫМИ двумя пользователями в чате (не только владелец бота).
//  • Свободный выбор цвета: любой игрок может играть за белых или за чёрных.
//  • Инлайн-доска 8x8 на кнопках Telegram.
//  • Русские правила: ход назад при бое, дальнобойные дамки, обязательный бой,
//    полный расчет цепочек взятий (серии ударов в один клик).
//  • Ничья, сдача, реванш с автоматической сменой сторон, команда .checkersend.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include "module_abi.h"

#define MAX_GAMES 32

enum GameState {
    STATE_EMPTY = 0,
    STATE_LOBBY,
    STATE_PLAYING,
    STATE_FINISHED
};

typedef struct {
    int r, c;
} Pos;

typedef struct {
    int to_r, to_c;
    int cap_count;
    Pos caps[12];
} CaptureChain;

typedef struct {
    CaptureChain chains[32];
    int count;
} ChainList;

typedef struct {
    int to_r, to_c;
} QuietMove;

typedef struct {
    QuietMove moves[32];
    int count;
} QuietList;

typedef struct {
    int enemy_r, enemy_c;
    int to_r, to_c;
} SingleJump;

typedef struct {
    char id[16];
    int state;               // GameState
    char board[8][8];        // ' ', 'w', 'b', 'W', 'B'
    long long white_id;
    long long black_id;
    char white_name[64];
    char black_name[64];
    long long chat_id;
    long long msg_id;
    char current_turn;       // 'w' или 'b'
    int sel_r, sel_c;        // Выбранная шашка (-1 если нет)
    long long draw_by;       // Кто предложил ничью (0 если никто)
    long long rematch_by;    // Кто предложил реванш (0 если никто)
    char winner;             // 'w', 'b', 'd' (ничья)
    int finish_reason;       // 0=все срублены, 1=пат (нет ходов), 2=сдача, 3=ничья
    time_t last_active;
} Game;

static Game g_games[MAX_GAMES];

// ── Вспомогательные функции доски ──────────────────────────────────────────

static inline bool in_bounds(int r, int c) {
    return r >= 0 && r < 8 && c >= 0 && c < 8;
}

static inline bool is_dark(int r, int c) {
    return (r + c) % 2 == 1;
}

static void init_board(char b[8][8]) {
    memset(b, ' ', 64);
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            if (is_dark(r, c)) {
                if (r < 3) b[r][c] = 'b';
                else if (r > 4) b[r][c] = 'w';
            }
        }
    }
}

// ── Правила и перебор ходов (Русские шашки) ────────────────────────────────

static int find_single_jumps(const char b[8][8], int r, int c, char color, bool is_king, SingleJump steps[16]) {
    int count = 0;
    const int dirs[4][2] = {{-1, -1}, {-1, 1}, {1, -1}, {1, 1}};

    if (!is_king) {
        // Простая шашка бьет вперед и назад на 1 клетку
        for (int i = 0; i < 4; ++i) {
            int er = r + dirs[i][0];
            int ec = c + dirs[i][1];
            int lr = r + 2 * dirs[i][0];
            int lc = c + 2 * dirs[i][1];
            if (in_bounds(er, ec) && in_bounds(lr, lc)) {
                char enemy = b[er][ec];
                if (enemy != ' ' && tolower(enemy) != color && b[lr][lc] == ' ') {
                    steps[count++] = (SingleJump){er, ec, lr, lc};
                }
            }
        }
    } else {
        // Дамка: скользит по диагонали, ищет вражескую шашку и приземляется за ней
        for (int i = 0; i < 4; ++i) {
            int dr = dirs[i][0];
            int dc = dirs[i][1];
            int curr_r = r + dr;
            int curr_c = c + dc;
            int er = -1, ec = -1;

            while (in_bounds(curr_r, curr_c)) {
                char cell = b[curr_r][curr_c];
                if (cell == ' ') {
                    if (er != -1) {
                        steps[count++] = (SingleJump){er, ec, curr_r, curr_c};
                    }
                } else if (tolower(cell) == color) {
                    break; // Своя фигура блокирует
                } else {
                    if (er == -1) {
                        er = curr_r;
                        ec = curr_c;
                    } else {
                        break; // Вторая фигура подряд — прыжок невозможен
                    }
                }
                curr_r += dr;
                curr_c += dc;
            }
        }
    }
    return count;
}

static void dfs_captures(char b[8][8], int r, int c, char color, bool is_king,
                         Pos current_caps[], int cap_count, ChainList* out) {
    SingleJump steps[16];
    int step_count = find_single_jumps(b, r, c, color, is_king, steps);

    if (step_count == 0) {
        if (cap_count > 0 && out->count < 32) {
            for (int i = 0; i < out->count; ++i) {
                if (out->chains[i].to_r == r && out->chains[i].to_c == c) {
                    if (cap_count > out->chains[i].cap_count) {
                        out->chains[i].cap_count = cap_count;
                        for (int j = 0; j < cap_count; ++j) out->chains[i].caps[j] = current_caps[j];
                    }
                    return;
                }
            }
            CaptureChain* cc = &out->chains[out->count++];
            cc->to_r = r;
            cc->to_c = c;
            cc->cap_count = cap_count;
            for (int i = 0; i < cap_count; ++i) cc->caps[i] = current_caps[i];
        }
        return;
    }

    for (int i = 0; i < step_count; ++i) {
        char next_b[8][8];
        memcpy(next_b, b, 64);
        char piece = next_b[r][c];
        next_b[r][c] = ' ';
        next_b[steps[i].enemy_r][steps[i].enemy_c] = ' ';
        next_b[steps[i].to_r][steps[i].to_c] = piece;

        bool promoted = false;
        bool next_is_king = is_king;
        if (!is_king) {
            if (color == 'w' && steps[i].to_r == 0) {
                next_b[steps[i].to_r][steps[i].to_c] = 'W';
                promoted = true;
                next_is_king = true;
            } else if (color == 'b' && steps[i].to_r == 7) {
                next_b[steps[i].to_r][steps[i].to_c] = 'B';
                promoted = true;
                next_is_king = true;
            }
        }

        current_caps[cap_count] = (Pos){steps[i].enemy_r, steps[i].enemy_c};

        if (promoted) {
            // При превращении в дамку во время серии взятий останавливаемся на этой клетке
            if (out->count < 32) {
                CaptureChain* cc = &out->chains[out->count++];
                cc->to_r = steps[i].to_r;
                cc->to_c = steps[i].to_c;
                cc->cap_count = cap_count + 1;
                for (int j = 0; j <= cap_count; ++j) cc->caps[j] = current_caps[j];
            }
        } else {
            dfs_captures(next_b, steps[i].to_r, steps[i].to_c, color, next_is_king,
                         current_caps, cap_count + 1, out);
        }
    }
}

static void find_quiet_moves(const char b[8][8], int r, int c, char color, bool is_king, QuietList* out) {
    out->count = 0;
    const int dirs[4][2] = {{-1, -1}, {-1, 1}, {1, -1}, {1, 1}};

    if (!is_king) {
        int fwd = (color == 'w') ? -1 : 1;
        for (int dc = -1; dc <= 1; dc += 2) {
            int tr = r + fwd;
            int tc = c + dc;
            if (in_bounds(tr, tc) && b[tr][tc] == ' ') {
                out->moves[out->count++] = (QuietMove){tr, tc};
            }
        }
    } else {
        for (int i = 0; i < 4; ++i) {
            int tr = r + dirs[i][0];
            int tc = c + dirs[i][1];
            while (in_bounds(tr, tc) && b[tr][tc] == ' ') {
                out->moves[out->count++] = (QuietMove){tr, tc};
                tr += dirs[i][0];
                tc += dirs[i][1];
            }
        }
    }
}

static bool piece_has_captures(const char b[8][8], int r, int c) {
    char piece = b[r][c];
    if (piece == ' ') return false;
    char color = tolower(piece);
    bool is_king = (piece == 'W' || piece == 'B');
    SingleJump steps[16];
    return find_single_jumps(b, r, c, color, is_king, steps) > 0;
}

static bool board_has_any_captures(const char b[8][8], char color) {
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            if (tolower(b[r][c]) == color && piece_has_captures(b, r, c)) {
                return true;
            }
        }
    }
    return false;
}

static bool piece_has_any_moves(const char b[8][8], int r, int c) {
    if (piece_has_captures(b, r, c)) return true;
    char piece = b[r][c];
    char color = tolower(piece);
    bool is_king = (piece == 'W' || piece == 'B');
    QuietList qm;
    find_quiet_moves(b, r, c, color, is_king, &qm);
    return qm.count > 0;
}

static bool player_has_any_moves(const char b[8][8], char color) {
    if (board_has_any_captures(b, color)) return true;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            if (tolower(b[r][c]) == color) {
                QuietList qm;
                bool is_king = (b[r][c] == 'W' || b[r][c] == 'B');
                find_quiet_moves(b, r, c, color, is_king, &qm);
                if (qm.count > 0) return true;
            }
        }
    }
    return false;
}

// ── Менеджер сессий игр ────────────────────────────────────────────────────

static Game* find_game(const char* id) {
    if (!id || !*id) return NULL;
    for (int i = 0; i < MAX_GAMES; ++i) {
        if (g_games[i].state != STATE_EMPTY && strcmp(g_games[i].id, id) == 0) {
            return &g_games[i];
        }
    }
    return NULL;
}

static Game* alloc_game(void) {
    time_t oldest_time = time(NULL);
    int oldest_idx = -1;

    for (int i = 0; i < MAX_GAMES; ++i) {
        if (g_games[i].state == STATE_EMPTY) {
            memset(&g_games[i], 0, sizeof(Game));
            return &g_games[i];
        }
        if (g_games[i].last_active < oldest_time) {
            oldest_time = g_games[i].last_active;
            oldest_idx = i;
        }
    }

    // Вытесняем самую старую неактивную игру
    if (oldest_idx != -1) {
        memset(&g_games[oldest_idx], 0, sizeof(Game));
        return &g_games[oldest_idx];
    }
    return &g_games[0];
}

static void generate_id(char out[16]) {
    static unsigned counter = 1;
    unsigned r = ((unsigned)rand() ^ (unsigned)time(NULL) ^ (counter++ * 104729)) & 0xFFFFFF;
    snprintf(out, 16, "%06x", r);
}

// ── Отрисовка UI: текст и кнопки ──────────────────────────────────────────

static const char* piece_emoji(char p) {
    switch (p) {
        case 'w': return "⚪";
        case 'W': return "⬜";
        case 'b': return "⚫";
        case 'B': return "⬛";
        default:  return " ";
    }
}

static void render_game(koto_ctx* ctx, const koto_api* api, Game* g) {
    g->last_active = time(NULL);
    api->c_reset(ctx);

    if (g->state == STATE_LOBBY) {
        api->c_text(ctx, "♟ ");
        api->c_fmt(ctx, "Шашки — Вызов на игру!\n\n", KOTO_ENT_BOLD);

        char buf[256];
        snprintf(buf, sizeof(buf), "⚪ Белые: %s\n⚫ Чёрные: %s\n\n",
                 g->white_id ? g->white_name : "[Свободно]",
                 g->black_id ? g->black_name : "[Свободно]");
        api->c_text(ctx, buf);

        if (!g->white_id && !g->black_id) {
            api->c_text(ctx, "Выберите сторону, чтобы создать партию:\n(Играть может любой участник чата)");
            char d_w[32], d_b[32], d_c[32];
            snprintf(d_w, sizeof(d_w), "chk_p:w:%s", g->id);
            snprintf(d_b, sizeof(d_b), "chk_p:b:%s", g->id);
            snprintf(d_c, sizeof(d_c), "chk_c:%s", g->id);
            api->c_button(ctx, "⚪ Играть за белых", d_w, 1);
            api->c_button(ctx, "⚫ Играть за чёрных", d_b, 0);
            api->c_button(ctx, "❌ Отмена", d_c, 1);
        } else if (g->white_id && !g->black_id) {
            snprintf(buf, sizeof(buf), "%s ждёт соперника за чёрных!\nНажмите кнопку ниже, чтобы начать игру:", g->white_name);
            api->c_text(ctx, buf);
            char d_b[32], d_c[32];
            snprintf(d_b, sizeof(d_b), "chk_j:b:%s", g->id);
            snprintf(d_c, sizeof(d_c), "chk_c:%s", g->id);
            api->c_button(ctx, "⚫ Принять вызов (за чёрных)", d_b, 1);
            api->c_button(ctx, "❌ Отменить вызов", d_c, 1);
        } else if (!g->white_id && g->black_id) {
            snprintf(buf, sizeof(buf), "%s ждёт соперника за белых!\nНажмите кнопку ниже, чтобы начать игру:", g->black_name);
            api->c_text(ctx, buf);
            char d_w[32], d_c[32];
            snprintf(d_w, sizeof(d_w), "chk_j:w:%s", g->id);
            snprintf(d_c, sizeof(d_c), "chk_c:%s", g->id);
            api->c_button(ctx, "⚪ Принять вызов (за белых)", d_w, 1);
            api->c_button(ctx, "❌ Отменить вызов", d_c, 1);
        }
        return;
    }

    int wc = 0, bc = 0;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            char p = g->board[r][c];
            if (p == 'w' || p == 'W') wc++;
            else if (p == 'b' || p == 'B') bc++;
        }
    }

    if (g->state == STATE_FINISHED) {
        api->c_text(ctx, "♟ ");
        api->c_fmt(ctx, "Шашки — Партия завершена!\n\n", KOTO_ENT_BOLD);

        char header[256];
        snprintf(header, sizeof(header), "⚫ %s [%d] vs ⚪ %s [%d]\n\n",
                 g->black_name, bc, g->white_name, wc);
        api->c_text(ctx, header);

        if (g->winner == 'd') {
            api->c_fmt(ctx, "🤝 Ничья по согласию сторон!", KOTO_ENT_BOLD);
        } else {
            const char* w_emoji = (g->winner == 'w') ? "⚪" : "⚫";
            const char* w_name = (g->winner == 'w') ? g->white_name : g->black_name;
            char win_buf[192];
            const char* reason_str = "";
            if (g->finish_reason == 1) reason_str = " (у соперника нет ходов)";
            else if (g->finish_reason == 2) reason_str = " (соперник сдался)";
            else if (g->finish_reason == 0) reason_str = " (все шашки срублены)";

            snprintf(win_buf, sizeof(win_buf), "🏆 %s %s победил!%s", w_emoji, w_name, reason_str);
            api->c_fmt(ctx, win_buf, KOTO_ENT_BOLD);
        }

        char d_rem[32], d_cls[32];
        snprintf(d_rem, sizeof(d_rem), "chk_r:%s", g->id);
        snprintf(d_cls, sizeof(d_cls), "chk_cls:%s", g->id);

        if (g->rematch_by == 0) {
            api->c_button(ctx, "🔄 Реванш (смена сторон)", d_rem, 1);
        } else {
            char d_acc[32], d_rej[32];
            snprintf(d_acc, sizeof(d_acc), "chk_ra:%s", g->id);
            snprintf(d_rej, sizeof(d_rej), "chk_rr:%s", g->id);
            api->c_button(ctx, "✅ Принять реванш", d_acc, 1);
            api->c_button(ctx, "❌ Отклонить", d_rej, 0);
        }
        api->c_button(ctx, "✖ Закрыть", d_cls, 1);
        return;
    }

    // STATE_PLAYING
    api->c_text(ctx, "♟ ");
    api->c_fmt(ctx, "Шашки\n\n", KOTO_ENT_BOLD);

    char header[256];
    snprintf(header, sizeof(header), "⚫ %s [%d]  vs  ⚪ %s [%d]\n\n",
             g->black_name, bc, g->white_name, wc);
    api->c_text(ctx, header);

    const char* cur_e = (g->current_turn == 'w') ? "⚪" : "⚫";
    const char* cur_n = (g->current_turn == 'w') ? g->white_name : g->black_name;
    char turn_str[192];
    snprintf(turn_str, sizeof(turn_str), "Ход: %s %s", cur_e, cur_n);
    api->c_fmt(ctx, turn_str, KOTO_ENT_BOLD);

    if (g->draw_by != 0) {
        const char* dr_name = (g->draw_by == g->white_id) ? g->white_name : g->black_name;
        char draw_msg[128];
        snprintf(draw_msg, sizeof(draw_msg), "\n🤝 %s предлагает ничью!", dr_name);
        api->c_fmt(ctx, draw_msg, KOTO_ENT_ITALIC);
    }

    // Расчет доступных направлений для выбранной шашки
    bool has_force_cap = board_has_any_captures(g->board, g->current_turn);
    ChainList cl = {0};
    QuietList qm = {0};

    if (g->sel_r >= 0 && g->sel_c >= 0) {
        char p = g->board[g->sel_r][g->sel_c];
        if (p != ' ' && tolower(p) == g->current_turn) {
            bool is_king = (p == 'W' || p == 'B');
            if (has_force_cap) {
                Pos caps[12];
                dfs_captures(g->board, g->sel_r, g->sel_c, g->current_turn, is_king, caps, 0, &cl);
            } else {
                find_quiet_moves(g->board, g->sel_r, g->sel_c, g->current_turn, is_king, &qm);
            }
        }
    }

    // Сетка 8x8 кнопок
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            int new_row = (c == 0) ? 1 : 0;
            char cell = g->board[r][c];

            if (!is_dark(r, c)) {
                api->c_button(ctx, " ", "chk_x", new_row);
                continue;
            }

            // Проверка, является ли клетка точкой назначения
            bool is_dest = false;
            if (cl.count > 0) {
                for (int i = 0; i < cl.count; ++i) {
                    if (cl.chains[i].to_r == r && cl.chains[i].to_c == c) {
                        is_dest = true;
                        break;
                    }
                }
            } else if (qm.count > 0) {
                for (int i = 0; i < qm.count; ++i) {
                    if (qm.moves[i].to_r == r && qm.moves[i].to_c == c) {
                        is_dest = true;
                        break;
                    }
                }
            }

            if (is_dest) {
                char cb[32];
                snprintf(cb, sizeof(cb), "chkm:%s:%d:%d", g->id, r, c);
                api->c_button(ctx, "·", cb, new_row);
                continue;
            }

            if (r == g->sel_r && c == g->sel_c) {
                char cb[32];
                snprintf(cb, sizeof(cb), "chkm:%s:%d:%d", g->id, r, c);
                api->c_button(ctx, piece_emoji(cell), cb, new_row);
                continue;
            }

            if (cell != ' ' && tolower(cell) == g->current_turn) {
                // Если обязательный бой — кнопка активна только если эта шашка может рубить
                bool can_tap = has_force_cap ? piece_has_captures(g->board, r, c)
                                             : piece_has_any_moves(g->board, r, c);
                if (can_tap) {
                    char cb[32];
                    snprintf(cb, sizeof(cb), "chkm:%s:%d:%d", g->id, r, c);
                    api->c_button(ctx, piece_emoji(cell), cb, new_row);
                    continue;
                }
            }

            if (cell != ' ') {
                api->c_button(ctx, piece_emoji(cell), "chk_x", new_row);
            } else {
                api->c_button(ctx, " ", "chk_x", new_row);
            }
        }
    }

    // Нижняя панель действий
    if (g->draw_by != 0) {
        char d_acc[32], d_rej[32];
        snprintf(d_acc, sizeof(d_acc), "chk_da:%s", g->id);
        snprintf(d_rej, sizeof(d_rej), "chk_dr:%s", g->id);
        api->c_button(ctx, "✅ Принять ничью", d_acc, 1);
        api->c_button(ctx, "❌ Отклонить", d_rej, 0);
    } else {
        char d_draw[32], d_surr[32];
        snprintf(d_draw, sizeof(d_draw), "chk_d:%s", g->id);
        snprintf(d_surr, sizeof(d_surr), "chk_s:%s", g->id);
        api->c_button(ctx, "🤝 Ничья", d_draw, 1);
        api->c_button(ctx, "🏳 Сдаться", d_surr, 0);
    }
}

// ── Обработчики команд ────────────────────────────────────────────────────

static void cmd_checkers(koto_ctx* ctx, const koto_api* api) {
    api->c_reset(ctx);
    api->c_text(ctx, "♟ ");
    api->c_fmt(ctx, "Шашки (Инлайн-игра через бота)\n\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "Для игры в шашки с интерактивной доской на кнопках требуется бот-помощник (via-bot).\n\n");
    api->c_text(ctx, "1. Укажите токен бота в ");
    api->c_fmt(ctx, "Настройки → Токен бота", KOTO_ENT_BOLD);
    api->c_text(ctx, " в приложении KoteLoader.\n");
    api->c_text(ctx, "2. После настройки запустите игру командой ");
    api->c_fmt(ctx, ".шашки", KOTO_ENT_CODE);
    api->c_text(ctx, " или через инлайн-режим ");
    api->c_fmt(ctx, "@имя_бота checkers", KOTO_ENT_CODE);
    api->c_text(ctx, " в любом чате!");

    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, KOTO_FMT_PLAIN);
    } else {
        api->c_reply(ctx, KOTO_FMT_PLAIN);
    }
}

static void cmd_checkersend(koto_ctx* ctx, const koto_api* api) {
    const char* args = api->cmd_args(ctx);
    while (args && (*args == ' ' || *args == '\t')) args++;

    api->c_reset(ctx);

    if (args && *args) {
        char target_id[16];
        int k = 0;
        for (const char* p = args; *p && k < 15 && *p != ' '; ++p) target_id[k++] = *p;
        target_id[k] = '\0';

        Game* g = find_game(target_id);
        if (!g) {
            api->c_fmt(ctx, "❌ Игра с таким ID не найдена.", KOTO_ENT_BOLD);
            if (api->is_outgoing(ctx)) api->c_edit(ctx, KOTO_FMT_PLAIN);
            else api->c_reply(ctx, KOTO_FMT_PLAIN);
            return;
        }

        g->state = STATE_EMPTY;
        api->c_fmt(ctx, "✅ Игра завершена администратором.", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, KOTO_FMT_PLAIN);
        else api->c_reply(ctx, KOTO_FMT_PLAIN);
        return;
    }

    // Список активных игр
    int count = 0;
    api->c_fmt(ctx, "♟ Активные партии шашек:\n\n", KOTO_ENT_BOLD);

    for (int i = 0; i < MAX_GAMES; ++i) {
        if (g_games[i].state != STATE_EMPTY) {
            char line[256];
            const char* st = (g_games[i].state == STATE_LOBBY) ? "Ожидание" :
                             (g_games[i].state == STATE_PLAYING) ? "Идет игра" : "Завершена";
            snprintf(line, sizeof(line), "• `%s` | %s vs %s [%s]\n",
                     g_games[i].id,
                     g_games[i].white_id ? g_games[i].white_name : "?",
                     g_games[i].black_id ? g_games[i].black_name : "?",
                     st);
            api->c_text(ctx, line);
            count++;
        }
    }

    if (count == 0) {
        api->c_reset(ctx);
        api->c_text(ctx, "♟ Нет активных партий.");
    } else {
        api->c_text(ctx, "\nИспользуйте: .checkersend <id> для закрытия.");
    }

    if (api->is_outgoing(ctx)) api->c_edit(ctx, KOTO_FMT_PLAIN);
    else api->c_reply(ctx, KOTO_FMT_PLAIN);
}

// ── Обработчик Inline Callback Кнопок (on_callback) ───────────────────────

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    const char* data = api->cb_data(ctx);
    if (!data || strncmp(data, "chk", 3) != 0) return;

    if (strcmp(data, "chk_x") == 0) {
        api->cb_answer(ctx, "", 0);
        return;
    }

    long long mid = api->cb_message_id(ctx);
    long long chat = api->chat_id(ctx);
    long long sender = api->sender_id(ctx);

    char name[64];
    if (api->user_name(ctx, sender, name, sizeof(name)) <= 0 || !name[0]) {
        snprintf(name, sizeof(name), "Игрок_%lld", sender % 10000);
    }

    // ── 1. Выбор цвета в лобби: chk_p:w:<id> или chk_p:b:<id> ──
    if (strncmp(data, "chk_p:", 6) == 0) {
        char side = data[6]; // 'w' или 'b'
        const char* gid = data + 8;
        Game* g = find_game(gid);
        if (!g || g->state != STATE_LOBBY) {
            api->cb_answer(ctx, "❌ Игра не найдена или уже началась", 1);
            return;
        }

        if (side == 'w') {
            g->white_id = sender;
            strncpy(g->white_name, name, sizeof(g->white_name) - 1);
            api->cb_answer(ctx, "⚪ Вы заняли белых! Ждём соперника.", 0);
        } else {
            g->black_id = sender;
            strncpy(g->black_name, name, sizeof(g->black_name) - 1);
            api->cb_answer(ctx, "⚫ Вы заняли чёрных! Ждём соперника.", 0);
        }

        render_game(ctx, api, g);
        api->c_edit_message(ctx, chat, mid, KOTO_FMT_PLAIN);
        return;
    }

    // ── 2. Присоединение соперника: chk_j:w:<id> или chk_j:b:<id> ──
    if (strncmp(data, "chk_j:", 6) == 0) {
        char side = data[6];
        const char* gid = data + 8;
        Game* g = find_game(gid);
        if (!g || g->state != STATE_LOBBY) {
            api->cb_answer(ctx, "❌ Игра не найдена или уже началась", 1);
            return;
        }

        // Защита от игры с самим собой
        if ((side == 'b' && sender == g->white_id) || (side == 'w' && sender == g->black_id)) {
            api->cb_answer(ctx, "❌ Нельзя играть с самим собой!", 1);
            return;
        }

        if (side == 'w') {
            g->white_id = sender;
            strncpy(g->white_name, name, sizeof(g->white_name) - 1);
        } else {
            g->black_id = sender;
            strncpy(g->black_name, name, sizeof(g->black_name) - 1);
        }

        // Игра стартует!
        g->state = STATE_PLAYING;
        g->current_turn = 'w';
        g->sel_r = -1;
        g->sel_c = -1;
        g->last_active = time(NULL);

        api->cb_answer(ctx, "♟ Партия началась! Белые ходят первыми.", 0);
        render_game(ctx, api, g);
        api->c_edit_message(ctx, chat, mid, KOTO_FMT_PLAIN);
        return;
    }

    // ── 3. Отмена вызова в лобби: chk_c:<id> ──
    if (strncmp(data, "chk_c:", 6) == 0) {
        const char* gid = data + 6;
        Game* g = find_game(gid);
        if (!g || g->state != STATE_LOBBY) {
            api->cb_answer(ctx, "❌ Игра не найдена", 1);
            return;
        }

        if (g->white_id && sender != g->white_id && g->black_id && sender != g->black_id && sender != api->get_me(ctx)) {
            api->cb_answer(ctx, "❌ Только создатель может отменить вызов", 1);
            return;
        }

        g->state = STATE_EMPTY;
        api->cb_answer(ctx, "Вызов отменён.", 0);
        api->c_reset(ctx);
        api->c_text(ctx, "♟ Вызов на игру в шашки отменён.");
        api->c_edit_message(ctx, chat, mid, KOTO_FMT_PLAIN);
        return;
    }

    // ── 4. Ход по доске: chkm:<id>:<r>:<c> ──
    if (strncmp(data, "chkm:", 5) == 0) {
        char gid[16];
        int r = -1, c = -1;
        const char* p1 = strchr(data + 5, ':');
        if (!p1) return;
        int id_len = (int)(p1 - (data + 5));
        if (id_len >= 15) id_len = 15;
        memcpy(gid, data + 5, id_len);
        gid[id_len] = '\0';

        const char* p2 = strchr(p1 + 1, ':');
        if (!p2) return;
        r = atoi(p1 + 1);
        c = atoi(p2 + 1);

        Game* g = find_game(gid);
        if (!g || g->state != STATE_PLAYING) {
            api->cb_answer(ctx, "❌ Игра уже завершена", 0);
            return;
        }

        // Проверка очереди хода
        if (g->current_turn == 'w' && sender != g->white_id) {
            api->cb_answer(ctx, "⏳ Сейчас ход белых ⚪", 0);
            return;
        }
        if (g->current_turn == 'b' && sender != g->black_id) {
            api->cb_answer(ctx, "⏳ Сейчас ход чёрных ⚫", 0);
            return;
        }

        char cell = g->board[r][c];
        bool has_force_cap = board_has_any_captures(g->board, g->current_turn);

        // Клик по своей фигуре — переключение выбора
        if (cell != ' ' && tolower(cell) == g->current_turn) {
            bool can_select = has_force_cap ? piece_has_captures(g->board, r, c)
                                            : piece_has_any_moves(g->board, r, c);
            if (can_select) {
                g->sel_r = r;
                g->sel_c = c;
                api->cb_answer(ctx, "Фигура выбрана. Нажмите на · для хода", 0);
                render_game(ctx, api, g);
                api->c_edit_message(ctx, chat, mid, KOTO_FMT_PLAIN);
                return;
            } else {
                if (has_force_cap) {
                    api->cb_answer(ctx, "⚠️ Обязательный бой! Рубите доступной шашкой.", 1);
                } else {
                    api->cb_answer(ctx, "У этой фигуры нет доступных ходов.", 0);
                }
                return;
            }
        }

        // Если фигура уже была выбрана — проверка, является ли (r, c) ходом
        if (g->sel_r >= 0 && g->sel_c >= 0) {
            int fr = g->sel_r;
            int fc = g->sel_c;
            char piece = g->board[fr][fc];
            bool is_king = (piece == 'W' || piece == 'B');

            if (has_force_cap) {
                Pos caps[12];
                ChainList cl = {0};
                dfs_captures(g->board, fr, fc, g->current_turn, is_king, caps, 0, &cl);

                for (int i = 0; i < cl.count; ++i) {
                    if (cl.chains[i].to_r == r && cl.chains[i].to_c == c) {
                        // Выполняем серию взятий!
                        g->board[fr][fc] = ' ';
                        for (int k = 0; k < cl.chains[i].cap_count; ++k) {
                            g->board[cl.chains[i].caps[k].r][cl.chains[i].caps[k].c] = ' ';
                        }
                        g->board[r][c] = piece;

                        // Превращение в дамку
                        if (!is_king) {
                            if (g->current_turn == 'w' && r == 0) g->board[r][c] = 'W';
                            else if (g->current_turn == 'b' && r == 7) g->board[r][c] = 'B';
                        }

                        g->sel_r = -1;
                        g->sel_c = -1;
                        g->draw_by = 0;

                        // Проверка на победу (все срублены)
                        char opp = (g->current_turn == 'w') ? 'b' : 'w';
                        int opp_cnt = 0;
                        for (int rr = 0; rr < 8; ++rr) {
                            for (int cc = 0; cc < 8; ++cc) {
                                if (tolower(g->board[rr][cc]) == opp) opp_cnt++;
                            }
                        }

                        if (opp_cnt == 0) {
                            g->state = STATE_FINISHED;
                            g->winner = g->current_turn;
                            g->finish_reason = 0;
                            api->cb_answer(ctx, "🏆 Победа! Все шашки соперника срублены.", 1);
                        } else {
                            // Передача хода и проверка на пат
                            g->current_turn = opp;
                            if (!player_has_any_moves(g->board, opp)) {
                                g->state = STATE_FINISHED;
                                g->winner = (opp == 'w') ? 'b' : 'w';
                                g->finish_reason = 1;
                                api->cb_answer(ctx, "🚫 Пат! У соперника нет ходов. Победа!", 1);
                            } else {
                                api->cb_answer(ctx, "Удар выполнен! Ход передан.", 0);
                            }
                        }

                        render_game(ctx, api, g);
                        api->c_edit_message(ctx, chat, mid, KOTO_FMT_PLAIN);
                        return;
                    }
                }
            } else {
                QuietList qm = {0};
                find_quiet_moves(g->board, fr, fc, g->current_turn, is_king, &qm);

                for (int i = 0; i < qm.count; ++i) {
                    if (qm.moves[i].to_r == r && qm.moves[i].to_c == c) {
                        // Тихий ход
                        g->board[fr][fc] = ' ';
                        g->board[r][c] = piece;

                        if (!is_king) {
                            if (g->current_turn == 'w' && r == 0) g->board[r][c] = 'W';
                            else if (g->current_turn == 'b' && r == 7) g->board[r][c] = 'B';
                        }

                        g->sel_r = -1;
                        g->sel_c = -1;
                        g->draw_by = 0;

                        char opp = (g->current_turn == 'w') ? 'b' : 'w';
                        g->current_turn = opp;

                        if (!player_has_any_moves(g->board, opp)) {
                            g->state = STATE_FINISHED;
                            g->winner = (opp == 'w') ? 'b' : 'w';
                            g->finish_reason = 1;
                            api->cb_answer(ctx, "🚫 Пат! У соперника нет ходов. Победа!", 1);
                        } else {
                            api->cb_answer(ctx, "Ход сделан.", 0);
                        }

                        render_game(ctx, api, g);
                        api->c_edit_message(ctx, chat, mid, KOTO_FMT_PLAIN);
                        return;
                    }
                }
            }
        }

        // Клик в пустоту — сброс выбора
        g->sel_r = -1;
        g->sel_c = -1;
        api->cb_answer(ctx, "", 0);
        render_game(ctx, api, g);
        api->c_edit_message(ctx, chat, mid, KOTO_FMT_PLAIN);
        return;
    }

    // ── 5. Сдача: chk_s:<id> ──
    if (strncmp(data, "chk_s:", 6) == 0) {
        const char* gid = data + 6;
        Game* g = find_game(gid);
        if (!g || g->state != STATE_PLAYING) return;

        if (sender != g->white_id && sender != g->black_id) {
            api->cb_answer(ctx, "❌ Вы не участник этой партии", 1);
            return;
        }

        char surr_color = (sender == g->white_id) ? 'w' : 'b';
        g->winner = (surr_color == 'w') ? 'b' : 'w';
        g->state = STATE_FINISHED;
        g->finish_reason = 2;

        api->cb_answer(ctx, "Вы сдались.", 1);
        render_game(ctx, api, g);
        api->c_edit_message(ctx, chat, mid, KOTO_FMT_PLAIN);
        return;
    }

    // ── 6. Предложение ничьей: chk_d:<id> ──
    if (strncmp(data, "chk_d:", 6) == 0) {
        const char* gid = data + 6;
        Game* g = find_game(gid);
        if (!g || g->state != STATE_PLAYING) return;

        if (sender != g->white_id && sender != g->black_id) {
            api->cb_answer(ctx, "❌ Вы не участник этой партии", 1);
            return;
        }

        if (g->draw_by != 0) {
            api->cb_answer(ctx, "Ничья уже предложена", 0);
            return;
        }

        g->draw_by = sender;
        api->cb_answer(ctx, "🤝 Предложение ничьей отправлено сопернику!", 0);
        render_game(ctx, api, g);
        api->c_edit_message(ctx, chat, mid, KOTO_FMT_PLAIN);
        return;
    }

    // ── 7. Принятие ничьей: chk_da:<id> ──
    if (strncmp(data, "chk_da:", 7) == 0) {
        const char* gid = data + 7;
        Game* g = find_game(gid);
        if (!g || g->state != STATE_PLAYING) return;

        if (sender != g->white_id && sender != g->black_id) {
            api->cb_answer(ctx, "❌ Вы не участник", 1);
            return;
        }
        if (g->draw_by == 0) return;
        if (sender == g->draw_by) {
            api->cb_answer(ctx, "⏳ Ожидайте ответа соперника", 1);
            return;
        }

        g->state = STATE_FINISHED;
        g->winner = 'd';
        g->finish_reason = 3;
        api->cb_answer(ctx, "🤝 Ничья согласована!", 1);
        render_game(ctx, api, g);
        api->c_edit_message(ctx, chat, mid, KOTO_FMT_PLAIN);
        return;
    }

    // ── 8. Отклонение ничьей: chk_dr:<id> ──
    if (strncmp(data, "chk_dr:", 7) == 0) {
        const char* gid = data + 7;
        Game* g = find_game(gid);
        if (!g || g->state != STATE_PLAYING) return;

        if (sender != g->white_id && sender != g->black_id) {
            api->cb_answer(ctx, "❌ Вы не участник", 1);
            return;
        }

        bool was_offerer = (sender == g->draw_by);
        g->draw_by = 0;
        if (was_offerer) api->cb_answer(ctx, "Предложение ничьей отменено.", 0);
        else api->cb_answer(ctx, "Ничья отклонена. Партия продолжается!", 0);

        render_game(ctx, api, g);
        api->c_edit_message(ctx, chat, mid, KOTO_FMT_PLAIN);
        return;
    }

    // ── 9. Реванш: chk_r:<id> ──
    if (strncmp(data, "chk_r:", 6) == 0) {
        const char* gid = data + 6;
        Game* g = find_game(gid);
        if (!g || g->state != STATE_FINISHED) return;

        if (sender != g->white_id && sender != g->black_id) {
            api->cb_answer(ctx, "❌ Вы не играли в этой партии", 1);
            return;
        }

        g->rematch_by = sender;
        api->cb_answer(ctx, "🔄 Реванш предложен сопернику!", 0);
        render_game(ctx, api, g);
        api->c_edit_message(ctx, chat, mid, KOTO_FMT_PLAIN);
        return;
    }

    // ── 10. Принятие реванша (смена сторон!): chk_ra:<id> ──
    if (strncmp(data, "chk_ra:", 7) == 0) {
        const char* gid = data + 7;
        Game* g = find_game(gid);
        if (!g || g->state != STATE_FINISHED) return;

        if (sender != g->white_id && sender != g->black_id) {
            api->cb_answer(ctx, "❌ Вы не участник", 1);
            return;
        }
        if (sender == g->rematch_by) {
            api->cb_answer(ctx, "⏳ Ожидайте ответа соперника", 1);
            return;
        }

        // Меняем стороны местами!
        long long tmp_id = g->white_id;
        g->white_id = g->black_id;
        g->black_id = tmp_id;

        char tmp_n[64];
        strncpy(tmp_n, g->white_name, sizeof(tmp_n) - 1);
        strncpy(g->white_name, g->black_name, sizeof(g->white_name) - 1);
        strncpy(g->black_name, tmp_n, sizeof(g->black_name) - 1);

        init_board(g->board);
        g->state = STATE_PLAYING;
        g->current_turn = 'w';
        g->sel_r = -1;
        g->sel_c = -1;
        g->draw_by = 0;
        g->rematch_by = 0;
        g->winner = 0;
        g->finish_reason = 0;

        api->cb_answer(ctx, "🔄 Реванш принят! Стороны поменялись.", 0);
        render_game(ctx, api, g);
        api->c_edit_message(ctx, chat, mid, KOTO_FMT_PLAIN);
        return;
    }

    // ── 11. Отклонение реванша: chk_rr:<id> ──
    if (strncmp(data, "chk_rr:", 7) == 0) {
        const char* gid = data + 7;
        Game* g = find_game(gid);
        if (!g) return;

        g->rematch_by = 0;
        api->cb_answer(ctx, "Реванш отклонён.", 0);
        render_game(ctx, api, g);
        api->c_edit_message(ctx, chat, mid, KOTO_FMT_PLAIN);
        return;
    }

    // ── 12. Закрытие завершенной партии: chk_cls:<id> ──
    if (strncmp(data, "chk_cls:", 8) == 0) {
        const char* gid = data + 8;
        Game* g = find_game(gid);
        if (g) g->state = STATE_EMPTY;
        api->cb_answer(ctx, "Партия закрыта.", 0);
        api->c_reset(ctx);
        api->c_text(ctx, "♟ Партия закрыта.");
        api->c_edit_message(ctx, chat, mid, KOTO_FMT_PLAIN);
        return;
    }
}

// ── Декларация команд и модуля ─────────────────────────────────────────────

static const koto_command COMMANDS[] = {
    {
        "checkers",
        &cmd_checkers,
        KOTO_LEVEL_ALL,
        "Начать игру в русские шашки (инлайн)",
        "[white|black]"
    },
    {
        "шашки",
        &cmd_checkers,
        KOTO_LEVEL_ALL,
        "Начать игру в русские шашки (псевдоним)",
        "[white|black]"
    },
    {
        "checkersend",
        &cmd_checkersend,
        KOTO_LEVEL_ALL,
        "Список активных партий или завершение игры по ID",
        "[id]"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "checkers",
    "Русские шашки: полноценный PvP между любыми игроками, свободный выбор белых/чёрных, серии ударов и дамки",
    "2.1.0",
    0,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    NULL, 0,
    &on_callback,
    NULL, 0,
    NULL
};

const koto_module* koto_module_register(void) {
    return &MODULE;
}
