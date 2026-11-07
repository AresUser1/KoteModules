#include "module_abi.h"
#include "nlohmann/json.hpp"

#include <string>
#include <vector>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <cstdlib>

namespace {
using json = nlohmann::json;

constexpr const char* VERSION = "1.0.0";
constexpr int MAP_W = 12;
constexpr int MAP_H = 12;

// Map grid: # = Wall, . = Empty
const char MAP[MAP_H][MAP_W + 1] = {
    "############",
    "#..#.......#",
    "#..#..###..#",
    "#..........#",
    "#...##.....#",
    "#...##..#..#",
    "#.......#..#",
    "###.#####..#",
    "#..........#",
    "#..#####...#",
    "#..........#",
    "############"
};

struct DoomState {
    float x = 2.5f;
    float y = 2.5f;
    float angle = 0.0f; // Radians
    int hp = 100;
    int armor = 25;
    int ammo = 24;
    int max_ammo = 50;
    int medkits = 2;
    int kills = 0;
    int score = 0;
    int wave = 1;
    bool dead = false;
    // Monster
    float mx = 8.5f;
    float my = 8.5f;
    int m_hp = 30;
    bool m_alive = true;
};

std::string render_ascii_frame(const DoomState& st) {
    constexpr int SCREEN_W = 16;
    constexpr int SCREEN_H = 8;
    constexpr float FOV = 3.14159f / 3.0f; // 60 degrees

    std::vector<std::string> screen(SCREEN_H, std::string(SCREEN_W, ' '));
    float depths[SCREEN_W];

    // Raycast columns
    for (int col = 0; col < SCREEN_W; ++col) {
        float ray_angle = (st.angle - FOV / 2.0f) + ((float)col / (float)SCREEN_W) * FOV;
        float dist = 0.1f;
        bool hit_wall = false;
        float eye_x = std::cos(ray_angle);
        float eye_y = std::sin(ray_angle);

        while (!hit_wall && dist < 12.0f) {
            dist += 0.15f;
            int test_x = (int)(st.x + eye_x * dist);
            int test_y = (int)(st.y + eye_y * dist);

            if (test_x < 0 || test_x >= MAP_W || test_y < 0 || test_y >= MAP_H) {
                hit_wall = true;
                dist = 12.0f;
            } else if (MAP[test_y][test_x] == '#') {
                hit_wall = true;
            }
        }

        // Fish-eye correction
        dist = dist * std::cos(ray_angle - st.angle);
        depths[col] = dist;

        int ceiling = (int)((float)SCREEN_H / 2.0f - (float)SCREEN_H / dist);
        int floor = SCREEN_H - ceiling;

        char wall_char = '#';
        if (dist <= 2.5f) wall_char = '#';
        else if (dist <= 4.5f) wall_char = '=';
        else if (dist <= 7.0f) wall_char = '-';
        else wall_char = '.';

        for (int y = 0; y < SCREEN_H; ++y) {
            if (y < ceiling) screen[y][col] = ' ';
            else if (y <= floor) screen[y][col] = wall_char;
            else screen[y][col] = '.';
        }
    }

    // Render Monster if visible
    if (st.m_alive) {
        float dx = st.mx - st.x;
        float dy = st.my - st.y;
        float m_dist = std::sqrt(dx * dx + dy * dy);
        float m_angle = std::atan2(dy, dx) - st.angle;

        while (m_angle < -3.14159f) m_angle += 2.0f * 3.14159f;
        while (m_angle > 3.14159f) m_angle -= 2.0f * 3.14159f;

        if (std::abs(m_angle) < FOV / 2.0f && m_dist > 0.5f && m_dist < 10.0f) {
            int m_col = (int)(((m_angle + FOV / 2.0f) / FOV) * (float)SCREEN_W);
            if (m_col >= 0 && m_col < SCREEN_W && m_dist < depths[m_col]) {
                int mid_y = SCREEN_H / 2;
                if (mid_y >= 0 && mid_y < SCREEN_H) screen[mid_y][m_col] = 'M';
                if (mid_y - 1 >= 0) screen[mid_y - 1][m_col] = 'O';
            }
        }
    }

    std::ostringstream out;
    out << "+----------------+\n";
    for (int y = 0; y < SCREEN_H; ++y) {
        out << "|" << screen[y] << "|\n";
    }
    out << "+----------------+\n";
    return out.str();
}

std::string state_key(long long cid) {
    return "doom_state:" + std::to_string(cid);
}

void save_doom(koto_ctx* ctx, const koto_api* api, long long cid, const DoomState& st) {
    json j;
    j["x"] = st.x; j["y"] = st.y; j["angle"] = st.angle;
    j["hp"] = st.hp; j["armor"] = st.armor; j["ammo"] = st.ammo;
    j["max_ammo"] = st.max_ammo; j["medkits"] = st.medkits;
    j["kills"] = st.kills; j["score"] = st.score; j["wave"] = st.wave;
    j["dead"] = st.dead; j["mx"] = st.mx; j["my"] = st.my;
    j["m_hp"] = st.m_hp; j["m_alive"] = st.m_alive;
    api->db_set(ctx, state_key(cid).c_str(), j.dump().c_str());
}

DoomState load_doom(koto_ctx* ctx, const koto_api* api, long long cid) {
    DoomState st;
    std::vector<char> buf(8192, 0);
    int n = api->db_get(ctx, state_key(cid).c_str(), buf.data(), (int)buf.size());
    if (n > 0) {
        try {
            json j = json::parse(std::string(buf.data(), (size_t)n));
            st.x = j.value("x", 2.5f); st.y = j.value("y", 2.5f); st.angle = j.value("angle", 0.0f);
            st.hp = j.value("hp", 100); st.armor = j.value("armor", 25); st.ammo = j.value("ammo", 24);
            st.max_ammo = j.value("max_ammo", 50); st.medkits = j.value("medkits", 2);
            st.kills = j.value("kills", 0); st.score = j.value("score", 0); st.wave = j.value("wave", 1);
            st.dead = j.value("dead", false); st.mx = j.value("mx", 8.5f); st.my = j.value("my", 8.5f);
            st.m_hp = j.value("m_hp", 30); st.m_alive = j.value("m_alive", true);
        } catch (...) {}
    }
    return st;
}

void add_doom_buttons(koto_ctx* ctx, const koto_api* api, bool dead) {
    if (!api->c_button) return;
    if (dead) {
        api->c_button(ctx, "🔄 Возродиться", "dm:respawn", 1);
        return;
    }
    api->c_button(ctx, "◀ Поворот", "dm:rl", 1);
    api->c_button(ctx, "▲ Вперёд", "dm:fwd", 0);
    api->c_button(ctx, "Поворот ▶", "dm:rr", 0);

    api->c_button(ctx, "◀ Влево", "dm:sl", 1);
    api->c_button(ctx, "💥 Выстрел", "dm:fire", 0);
    api->c_button(ctx, "Вправо ▶", "dm:sr", 0);

    api->c_button(ctx, "▼ Назад", "dm:back", 1);
    api->c_button(ctx, "🔄 Перезарядка", "dm:reload", 0);
    api->c_button(ctx, "💊 Аптечка", "dm:med", 0);

    api->c_button(ctx, "🚪 Завершить игру", "dm:exit", 1);
}

void render_game_message(koto_ctx* ctx, const koto_api* api, const DoomState& st, const std::string& msg = "") {
    api->c_reset(ctx);
    api->c_fmt(ctx, "🔥 DOOM 3D (by kote)\n\n", KOTO_ENT_BOLD);

    if (st.dead) {
        api->c_fmt(ctx, "💀 ВЫ ПОГИБЛИ В БОЮ!\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "🏆 Убито демонов: ");
        api->c_fmt(ctx, std::to_string(st.kills).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n⭐ Очки: ");
        api->c_fmt(ctx, std::to_string(st.score).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n\n");
        add_doom_buttons(ctx, api, true);
        return;
    }

    std::string frame = render_ascii_frame(st);
    api->c_fmt(ctx, frame.c_str(), KOTO_ENT_PRE);

    if (!msg.empty()) {
        api->c_fmt(ctx, msg.c_str(), KOTO_ENT_ITALIC);
        api->c_text(ctx, "\n");
    }

    // HUD line
    std::string hud = "❤️ HP: " + std::to_string(st.hp) +
                      " | 🛡️ Броня: " + std::to_string(st.armor) +
                      " | 💥 Патроны: " + std::to_string(st.ammo) +
                      " | 💊 " + std::to_string(st.medkits);
    api->c_fmt(ctx, hud.c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n");
    std::string hud2 = "💀 Убийств: " + std::to_string(st.kills) + " | 🌊 Волна: " + std::to_string(st.wave);
    api->c_fmt(ctx, hud2.c_str(), KOTO_ENT_BOLD);

    add_doom_buttons(ctx, api, false);
}

// ── Command: .doom ───────────────────────────────────────────────────────────
void cmd_doom(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    DoomState st;
    save_doom(ctx, api, cid, st);

    render_game_message(ctx, api, st, "Вы вошли в комплекс Марса. Уничтожьте демонов!");
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Command: .hdoom ──────────────────────────────────────────────────────────
void cmd_hdoom(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    api->c_reset(ctx);
    api->c_fmt(ctx, "📖 DOOM 3D — Руководство (by kote)\n\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "• .doom — Начать игру в DOOM с инлайн-кнопками управления\n");
    api->c_text(ctx, "• M / O — Демон в зоне видимости рейкастера\n");
    api->c_text(ctx, "• 💥 Выстрел наносит урон прицельно по демону перед вами\n");
    api->c_text(ctx, "• 💊 Аптечка восстанавливает +35 HP\n");
    api->c_text(ctx, "• 🔄 Перезарядка пополняет магазин\n");

    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Callback Handler ────────────────────────────────────────────────────────
void on_callback(koto_ctx* ctx, const koto_api* api) {
    const char* data_raw = api->cb_data ? api->cb_data(ctx) : nullptr;
    if (!data_raw) return;
    std::string data = data_raw;
    if (data.rfind("dm:", 0) != 0) return;

    long long cid = api->chat_id(ctx);
    long long mid = api->cb_message_id(ctx);
    DoomState st = load_doom(ctx, api, cid);

    if (data == "dm:exit") {
        api->cb_answer(ctx, "Игра завершена", 0);
        api->db_del(ctx, state_key(cid).c_str());
        api->c_reset(ctx);
        api->c_fmt(ctx, "🚪 Игра DOOM завершена. Спасибо за игру! (by kote)", KOTO_ENT_BOLD);
        api->c_edit_message(ctx, cid, mid, 0);
        return;
    }

    if (data == "dm:respawn") {
        api->cb_answer(ctx, "Возрождение…", 0);
        st = DoomState();
        save_doom(ctx, api, cid, st);
        render_game_message(ctx, api, st, "Новая жизнь! Вперёд!");
        api->c_edit_message(ctx, cid, mid, 0);
        return;
    }

    if (st.dead) {
        api->cb_answer(ctx, "Вы погибли. Нажмите Возродиться.", 0);
        return;
    }

    std::string action_msg;

    if (data == "dm:rl") {
        st.angle -= 0.35f;
        api->cb_answer(ctx, "↶ Поворот влево", 0);
    } else if (data == "dm:rr") {
        st.angle += 0.35f;
        api->cb_answer(ctx, "↷ Поворот вправо", 0);
    } else if (data == "dm:fwd") {
        float nx = st.x + std::cos(st.angle) * 0.5f;
        float ny = st.y + std::sin(st.angle) * 0.5f;
        if (MAP[(int)ny][(int)nx] != '#') { st.x = nx; st.y = ny; }
        api->cb_answer(ctx, "▲ Шаг вперёд", 0);
    } else if (data == "dm:back") {
        float nx = st.x - std::cos(st.angle) * 0.5f;
        float ny = st.y - std::sin(st.angle) * 0.5f;
        if (MAP[(int)ny][(int)nx] != '#') { st.x = nx; st.y = ny; }
        api->cb_answer(ctx, "▼ Шаг назад", 0);
    } else if (data == "dm:sl") {
        float nx = st.x + std::sin(st.angle) * 0.4f;
        float ny = st.y - std::cos(st.angle) * 0.4f;
        if (MAP[(int)ny][(int)nx] != '#') { st.x = nx; st.y = ny; }
        api->cb_answer(ctx, "◀ Стрейф влево", 0);
    } else if (data == "dm:sr") {
        float nx = st.x - std::sin(st.angle) * 0.4f;
        float ny = st.y + std::cos(st.angle) * 0.4f;
        if (MAP[(int)ny][(int)nx] != '#') { st.x = nx; st.y = ny; }
        api->cb_answer(ctx, "▶ Стрейф вправо", 0);
    } else if (data == "dm:fire") {
        if (st.ammo <= 0) {
            api->cb_answer(ctx, "Щёлк! Нет патронов! Перезарядись!", 1);
            action_msg = "⚠️ Нет патронов! Жми 🔄 Перезарядка!";
        } else {
            st.ammo--;
            api->cb_answer(ctx, "💥 Бабах!", 0);
            if (st.m_alive) {
                float dx = st.mx - st.x;
                float dy = st.my - st.y;
                float dist = std::sqrt(dx * dx + dy * dy);
                float a = std::atan2(dy, dx) - st.angle;
                while (a < -3.14159f) a += 2.0f * 3.14159f;
                while (a > 3.14159f) a -= 2.0f * 3.14159f;

                if (std::abs(a) < 0.35f && dist < 8.0f) {
                    st.m_hp -= 20;
                    action_msg = "🎯 Попадание по демону! (-20 HP)";
                    if (st.m_hp <= 0) {
                        st.m_alive = false;
                        st.kills++;
                        st.score += 150;
                        action_msg = "💥 ДЕМОН УНИЧТОЖЕН! (+150 очков)";
                        // Spawn next monster
                        st.wave++;
                        st.mx = (st.wave % 2 == 0) ? 9.5f : 4.5f;
                        st.my = (st.wave % 2 == 0) ? 3.5f : 9.5f;
                        st.m_hp = 30 + st.wave * 10;
                        st.m_alive = true;
                    }
                } else {
                    action_msg = "💨 Промах!";
                }
            }
        }
    } else if (data == "dm:reload") {
        st.ammo = st.max_ammo;
        api->cb_answer(ctx, "Обойма полная!", 0);
        action_msg = "🔄 Оружие перезаряжено.";
    } else if (data == "dm:med") {
        if (st.medkits > 0) {
            st.medkits--;
            st.hp = std::min(100, st.hp + 35);
            api->cb_answer(ctx, "Аптечка использована (+35 HP)", 0);
            action_msg = "💊 Здоровье восстановлено.";
        } else {
            api->cb_answer(ctx, "Аптечек больше нет!", 1);
        }
    }

    // Monster AI movement & attack
    if (st.m_alive) {
        float dx = st.x - st.mx;
        float dy = st.y - st.my;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist > 1.2f) {
            st.mx += (dx / dist) * 0.25f;
            st.my += (dy / dist) * 0.25f;
        } else {
            // Monster attack!
            int dmg = 12;
            if (st.armor > 0) {
                st.armor = std::max(0, st.armor - 8);
                dmg = 6;
            }
            st.hp -= dmg;
            action_msg += " ⚠️ Демон атаковал вас! (-" + std::to_string(dmg) + " HP)";
            if (st.hp <= 0) {
                st.dead = true;
            }
        }
    }

    save_doom(ctx, api, cid, st);
    render_game_message(ctx, api, st, action_msg);
    api->c_edit_message(ctx, cid, mid, 0);
}

const koto_command COMMANDS[] = {
    {"doom", cmd_doom, KOTO_LEVEL_TRUSTED, "[ru] Интерактивная игра DOOM 3D (by kote) [en] Interactive DOOM 3D game (by kote).", nullptr},
    {"hdoom", cmd_hdoom, KOTO_LEVEL_TRUSTED, "[ru] Справка и управление по игре DOOM (by kote) [en] DOOM game guide (by kote).", nullptr}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "doom",
    "[ru] Интерактивная псевдо-3D игра DOOM в Telegram (by kote). [en] Interactive pseudo-3D DOOM game (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    nullptr, // on_watch
    0,
    on_callback,
    nullptr,
    0,
    "https://github.com/AresUser1/KoteModules/blob/main/src/doom.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
