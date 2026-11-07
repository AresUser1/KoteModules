// top.cpp — Модуль для просмотра топа активности пользователей по количеству сообщений в чате (C++ ABI 3).
// Портирован из Python-модуля Top (Hikka / Telethon). Автор: Kote.
// Особенности:
// • Высокоскоростное сканирование истории сообщений (get_history_json)
// • Полноценная генерация красивого графика-диаграммы (PNG в темной теме Matplotlib) с градиентом, штриховкой и золотыми плашками
// • Поддержка как групповых чатов/супергрупп/каналов, так и личных сообщений (ЛС с собеседником)
// • Детальный форматированный текстовый отчёт с кастомными премиум-эмодзи и процентами активности

#include "module_abi.h"
#include "koto_utils.hpp"
#include "font8x8.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <sstream>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <zlib.h>

// ── Кастомные премиум-эмодзи из оригинального модуля ───────────────────────────
static constexpr long long EMOJI_CHAT    = 5323538339062628165LL; // 💬
static constexpr long long EMOJI_LOADING = 5780543148782522693LL; // 🕒

// ── Графика и PNG энкодер ─────────────────────────────────────────────────────

struct Color {
    uint8_t r, g, b;
};

static inline Color color_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return {r, g, b};
}

static inline Color lerp_color(Color c1, Color c2, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    uint8_t r = (uint8_t)(c1.r + (c2.r - c1.r) * t);
    uint8_t g = (uint8_t)(c1.g + (c2.g - c1.g) * t);
    uint8_t b = (uint8_t)(c1.b + (c2.b - c1.b) * t);
    return {r, g, b};
}

class Canvas {
public:
    int width;
    int height;
    std::vector<uint8_t> pixels; // RGB 24-bit

    Canvas(int w, int h, Color bg) : width(w), height(h), pixels(w * h * 3) {
        for (int i = 0; i < w * h; ++i) {
            pixels[i * 3 + 0] = bg.r;
            pixels[i * 3 + 1] = bg.g;
            pixels[i * 3 + 2] = bg.b;
        }
    }

    inline void set_pixel(int x, int y, Color c) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        int idx = (y * width + x) * 3;
        pixels[idx + 0] = c.r;
        pixels[idx + 1] = c.g;
        pixels[idx + 2] = c.b;
    }

    void fill_rect(int x, int y, int w, int h, Color c) {
        int x2 = std::min(width, x + w);
        int y2 = std::min(height, y + h);
        int x1 = std::max(0, x);
        int y1 = std::max(0, y);
        for (int cy = y1; cy < y2; ++cy) {
            for (int cx = x1; cx < x2; ++cx) {
                set_pixel(cx, cy, c);
            }
        }
    }

    void draw_rect(int x, int y, int w, int h, Color c) {
        draw_line(x, y, x + w - 1, y, c);
        draw_line(x, y + h - 1, x + w - 1, y + h - 1, c);
        draw_line(x, y, x, y + h - 1, c);
        draw_line(x + w - 1, y, x + w - 1, y + h - 1, c);
    }

    void draw_line(int x0, int y0, int x1, int y1, Color c) {
        int dx = std::abs(x1 - x0);
        int dy = std::abs(y1 - y0);
        int sx = (x0 < x1) ? 1 : -1;
        int sy = (y0 < y1) ? 1 : -1;
        int err = dx - dy;
        while (true) {
            set_pixel(x0, y0, c);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x0 += sx;
            }
            if (e2 < dx) {
                err += dx;
                y0 += sy;
            }
        }
    }

    // Рисование диагональной штриховки (///) внутри прямоугольника (как в matplotlib set_hatch('///'))
    void draw_hatch(int x, int y, int w, int h, Color c, int step = 8) {
        int x2 = x + w;
        int y2 = y + h;
        for (int offset = -h; offset < w; offset += step) {
            int start_x = x + offset;
            int start_y = y;
            int end_x = start_x + h;
            int end_y = y2;
            // Обрезка линии по границам прямоугольника
            for (int cy = y; cy < y2; ++cy) {
                int cx = start_x + (cy - y);
                if (cx >= x && cx < x2) {
                    set_pixel(cx, cy, c);
                }
            }
        }
    }

    void draw_char(int x, int y, char ch, Color c, int scale = 1) {
        uint8_t uc = (uint8_t)ch;
        if (uc > 127) uc = '?';
        const uint8_t* glyph = font8x8_basic[uc];
        for (int r = 0; r < 8; ++r) {
            uint8_t row = glyph[r];
            for (int b = 0; b < 8; ++b) {
                if ((row >> b) & 1) {
                    if (scale == 1) {
                        set_pixel(x + b, y + r, c);
                    } else {
                        for (int dy = 0; dy < scale; ++dy) {
                            for (int dx = 0; dx < scale; ++dx) {
                                set_pixel(x + b * scale + dx, y + r * scale + dy, c);
                            }
                        }
                    }
                }
            }
        }
    }

    void draw_text(int x, int y, const std::string& str, Color c, int scale = 1) {
        int cx = x;
        for (char ch : str) {
            draw_char(cx, y, ch, c, scale);
            cx += 8 * scale;
        }
    }
};

// ── Транслитерация для отрисовки имён на графике ──────────────────────────────
static std::string transliterate(const std::string& utf8) {
    std::string out;
    size_t i = 0;
    while (i < utf8.size()) {
        unsigned char c = (unsigned char)utf8[i];
        if (c < 0x80) {
            if (c >= 32 && c <= 126) out += (char)c;
            else out += ' ';
            i++;
        } else if (c == 0xD0 && i + 1 < utf8.size()) {
            unsigned char c2 = (unsigned char)utf8[i + 1];
            switch (c2) {
                case 0x90: out += "A"; break;
                case 0x91: out += "B"; break;
                case 0x92: out += "V"; break;
                case 0x93: out += "G"; break;
                case 0x94: out += "D"; break;
                case 0x95: out += "E"; break;
                case 0x81: out += "Yo"; break;
                case 0x96: out += "Zh"; break;
                case 0x97: out += "Z"; break;
                case 0x98: out += "I"; break;
                case 0x99: out += "Y"; break;
                case 0x9A: out += "K"; break;
                case 0x9B: out += "L"; break;
                case 0x9C: out += "M"; break;
                case 0x9D: out += "N"; break;
                case 0x9E: out += "O"; break;
                case 0x9F: out += "P"; break;
                case 0xA0: out += "R"; break;
                case 0xA1: out += "S"; break;
                case 0xA2: out += "T"; break;
                case 0xA3: out += "U"; break;
                case 0xA4: out += "F"; break;
                case 0xA5: out += "Kh"; break;
                case 0xA6: out += "Ts"; break;
                case 0xA7: out += "Ch"; break;
                case 0xA8: out += "Sh"; break;
                case 0xA9: out += "Shch"; break;
                case 0xAA: break;
                case 0xAB: out += "Y"; break;
                case 0xAC: break;
                case 0xAD: out += "E"; break;
                case 0xAE: out += "Yu"; break;
                case 0xAF: out += "Ya"; break;
                case 0xB0: out += "a"; break;
                case 0xB1: out += "b"; break;
                case 0xB2: out += "v"; break;
                case 0xB3: out += "g"; break;
                case 0xB4: out += "d"; break;
                case 0xB5: out += "e"; break;
                case 0xB6: out += "zh"; break;
                case 0xB7: out += "z"; break;
                case 0xB8: out += "i"; break;
                case 0xB9: out += "y"; break;
                case 0xBA: out += "k"; break;
                case 0xBB: out += "l"; break;
                case 0xBC: out += "m"; break;
                case 0xBD: out += "n"; break;
                case 0xBE: out += "o"; break;
                case 0xBF: out += "p"; break;
                default: out += " "; break;
            }
            i += 2;
        } else if (c == 0xD1 && i + 1 < utf8.size()) {
            unsigned char c2 = (unsigned char)utf8[i + 1];
            switch (c2) {
                case 0x80: out += "r"; break;
                case 0x81: out += "s"; break;
                case 0x82: out += "t"; break;
                case 0x83: out += "u"; break;
                case 0x84: out += "f"; break;
                case 0x85: out += "kh"; break;
                case 0x86: out += "ts"; break;
                case 0x87: out += "ch"; break;
                case 0x88: out += "sh"; break;
                case 0x89: out += "shch"; break;
                case 0x8A: break;
                case 0x8B: out += "y"; break;
                case 0x8C: break;
                case 0x8D: out += "e"; break;
                case 0x8E: out += "yu"; break;
                case 0x8F: out += "ya"; break;
                case 0x91: out += "yo"; break;
                default: out += " "; break;
            }
            i += 2;
        } else {
            out += " ";
            i++;
            while (i < utf8.size() && (unsigned char)utf8[i] >= 0x80 && (unsigned char)utf8[i] < 0xC0) i++;
        }
    }
    // Сжимаем лишние пробелы
    std::string clean;
    bool last_space = false;
    for (char ch : out) {
        if (ch == ' ') {
            if (!last_space && !clean.empty()) clean += ' ';
            last_space = true;
        } else {
            clean += ch;
            last_space = false;
        }
    }
    return clean;
}

// ── Генератор PNG (RGB24) ─────────────────────────────────────────────────────

static void write_u32_be(std::vector<uint8_t>& out, uint32_t val) {
    out.push_back((val >> 24) & 0xFF);
    out.push_back((val >> 16) & 0xFF);
    out.push_back((val >> 8) & 0xFF);
    out.push_back(val & 0xFF);
}

static void add_chunk(std::vector<uint8_t>& out, const char type[4], const uint8_t* data, size_t len) {
    write_u32_be(out, (uint32_t)len);
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    if (len > 0 && data) {
        out.insert(out.end(), data, data + len);
    }
    uint32_t c = (uint32_t)crc32(0L, Z_NULL, 0);
    c = (uint32_t)crc32(c, out.data() + start, (uInt)(len + 4));
    write_u32_be(out, c);
}

static std::vector<uint8_t> encode_png(const Canvas& cv) {
    std::vector<uint8_t> png;
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    png.insert(png.end(), sig, sig + 8);

    // IHDR
    std::vector<uint8_t> ihdr;
    write_u32_be(ihdr, cv.width);
    write_u32_be(ihdr, cv.height);
    ihdr.push_back(8); // 8-bit depth
    ihdr.push_back(2); // Color type 2: RGB
    ihdr.push_back(0); // Compression
    ihdr.push_back(0); // Filter
    ihdr.push_back(0); // Interlace
    add_chunk(png, "IHDR", ihdr.data(), ihdr.size());

    // Scanlines с filter=0
    size_t row_len = 1 + cv.width * 3;
    std::vector<uint8_t> raw(cv.height * row_len);
    for (int y = 0; y < cv.height; ++y) {
        size_t off = y * row_len;
        raw[off] = 0; // Filter None
        std::memcpy(&raw[off + 1], &cv.pixels[y * cv.width * 3], cv.width * 3);
    }

    // Сжатие zlib
    uLongf dest_len = compressBound((uLong)raw.size());
    std::vector<uint8_t> idat(dest_len);
    if (compress(idat.data(), &dest_len, raw.data(), (uLong)raw.size()) != Z_OK) {
        return {};
    }
    idat.resize(dest_len);
    add_chunk(png, "IDAT", idat.data(), idat.size());

    // IEND
    add_chunk(png, "IEND", nullptr, 0);
    return png;
}

// ── Структура записи топа ─────────────────────────────────────────────────────

struct UserStat {
    long long user_id;
    int count;
    std::string name;
    std::string username;
};

// ── Отрисовка графика Matplotlib-стиля ─────────────────────────────────────────

static std::vector<uint8_t> render_top_chart(const std::vector<UserStat>& stats, const std::string& chat_title, int total_scanned) {
    if (stats.empty()) return {};

    int count = (int)stats.size();
    int item_height = 36;
    int top_margin = 75;
    int bottom_margin = 55;
    int left_margin = 250;
    int right_margin = 110;

    int w = 960;
    int h = std::max(480, top_margin + bottom_margin + count * item_height);

    // Тёмный фон matplotlib dark_background
    Color bg_color = color_rgb(18, 18, 28);
    Canvas cv(w, h, bg_color);

    // Заголовок
    cv.draw_text(w / 2 - 130, 20, "TOP USERS BY MESSAGE COUNT", color_rgb(255, 255, 255), 2);

    std::string sub = transliterate(chat_title);
    if (sub.size() > 40) sub = sub.substr(0, 37) + "...";
    sub += " | Total scanned: " + std::to_string(total_scanned);
    cv.draw_text(w / 2 - (int)sub.size() * 4, 46, sub, color_rgb(160, 160, 200), 1);

    int max_val = 1;
    for (const auto& u : stats) {
        if (u.count > max_val) max_val = u.count;
    }

    int bar_area_w = w - left_margin - right_margin;
    int axis_y = h - bottom_margin;

    // Сетка и оси
    Color axis_color = color_rgb(138, 43, 226); // #8A2BE2
    Color grid_color = color_rgb(42, 42, 60);

    // Вертикальные линии сетки
    for (int step = 1; step <= 4; ++step) {
        int gx = left_margin + (bar_area_w * step) / 4;
        cv.draw_line(gx, top_margin - 10, gx, axis_y, grid_color);
        int val_mark = (max_val * step) / 4;
        std::string mark_str = std::to_string(val_mark);
        cv.draw_text(gx - (int)mark_str.size() * 4, axis_y + 8, mark_str, color_rgb(130, 130, 160), 1);
    }

    // Левая и нижняя границы осей
    cv.draw_line(left_margin, top_margin - 10, left_margin, axis_y, axis_color);
    cv.draw_line(left_margin, axis_y, w - right_margin, axis_y, axis_color);

    // Подпись оси X
    cv.draw_text(w / 2 - 48, axis_y + 26, "Message count", color_rgb(255, 255, 255), 1);

    // Отрисовка полос (bars)
    for (int i = 0; i < count; ++i) {
        const auto& item = stats[i];
        int by = top_margin + i * item_height;
        int bh = 22;

        int bw = (int)((double)item.count / max_val * bar_area_w);
        if (bw < 4) bw = 4;

        // Определение цвета бара: топ 3 — золотые плашки, остальные — градиент #8A2BE2 -> #4B0082
        Color bar_color;
        if (i == 0) {
            bar_color = color_rgb(255, 215, 0); // #FFD700 Gold
        } else if (i == 1) {
            bar_color = color_rgb(240, 195, 0); // Gold-bright
        } else if (i == 2) {
            bar_color = color_rgb(212, 175, 55); // Gold-amber
        } else {
            float t = (float)(i - 3) / std::max(1, count - 4);
            bar_color = lerp_color(color_rgb(138, 43, 226), color_rgb(75, 0, 130), t);
        }

        // Заливка бара
        cv.fill_rect(left_margin, by, bw, bh, bar_color);

        // Штриховка '///' как в matplotlib
        Color hatch_color = (i < 3) ? color_rgb(180, 140, 0) : color_rgb(45, 0, 80);
        cv.draw_hatch(left_margin, by, bw, bh, hatch_color, 8);

        // Контур бара
        cv.draw_rect(left_margin, by, bw, bh, color_rgb(0, 0, 0));

        // Имя пользователя слева от оси
        std::string uname = transliterate(!item.username.empty() ? ("@" + item.username) : item.name);
        if (uname.empty()) uname = "User " + std::to_string(item.user_id);
        if (uname.size() > 24) uname = uname.substr(0, 21) + "...";

        int text_x = left_margin - 12 - (int)uname.size() * 8;
        Color name_color = (i < 3) ? color_rgb(255, 225, 120) : color_rgb(220, 220, 235);
        cv.draw_text(text_x, by + 7, uname, name_color, 1);

        // Бейдж #1, #2, #3 и число сообщений справа от бара
        int val_x = left_margin + bw + 12;
        if (i < 3) {
            std::string rank_str = "#" + std::to_string(i + 1);
            cv.draw_text(val_x, by + 7, rank_str, color_rgb(255, 215, 0), 1);
            val_x += 28;
            cv.draw_text(val_x, by + 7, std::to_string(item.count), color_rgb(255, 255, 255), 1);
        } else {
            cv.draw_text(val_x, by + 7, std::to_string(item.count), color_rgb(200, 200, 220), 1);
        }
    }

    return encode_png(cv);
}

// ── Вспомогательные функции ───────────────────────────────────────────────────

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

static std::string trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\n\r");
    return s.substr(first, last - first + 1);
}

// ── Обработчик команды .top ───────────────────────────────────────────────────

static void cmd_top(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    if (cid == 0) return;

    // Первым делом информируем о начале подсчёта сообщений
    api->c_reset(ctx);
    api->c_emoji(ctx, "🕒", EMOJI_LOADING);
    api->c_fmt(ctx, " Подсчет сообщений начался, пожалуйста подождите, это может занять некоторое время...", KOTO_ENT_BOLD);
    send_result(ctx, api);

    int ctype = api->chat_type(ctx);
    long long me = api->get_me ? api->get_me(ctx) : 0;

    // Считываем аргументы пользователя (лимит сканирования или топ)
    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";
    int user_limit = 0;
    if (!args.empty()) {
        user_limit = std::atoi(args.c_str());
    }

    int max_scan_messages = 2000;
    int top_n = 20;

    if (user_limit > 30) {
        max_scan_messages = std::min(5000, user_limit);
    } else if (user_limit > 0) {
        top_n = user_limit;
    }

    // ── 1. Режим ЛИЧНОГО ЧАТА (Private) ───────────────────────────────────────
    if (ctype == KOTO_CHAT_PRIVATE || (cid > 0 && ctype == KOTO_CHAT_UNKNOWN)) {
        long long target_uid = cid;

        int my_count = 0;
        int their_count = 0;
        int total_scanned = 0;

        if (api->get_history_json) {
            int offset_id = 0;
            std::vector<char> hbuf(131072, 0);

            while (total_scanned < max_scan_messages) {
                int hlen = api->get_history_json(ctx, cid, offset_id, 100, 0, hbuf.data(), (int)hbuf.size() - 1);
                if (hlen <= 2) break;

                std::string jstr(hbuf.data(), hlen);
                size_t pos = 0;
                int in_chunk = 0;
                long long min_id = 0;

                while ((pos = jstr.find("{\"id\":", pos)) != std::string::npos) {
                    size_t end_pos = jstr.find('}', pos);
                    if (end_pos == std::string::npos) break;
                    std::string obj = jstr.substr(pos, end_pos - pos + 1);

                    long long mid = 0;
                    size_t id_p = obj.find("\"id\":");
                    if (id_p != std::string::npos) mid = std::atoll(obj.c_str() + id_p + 5);

                    long long sid = 0;
                    size_t sid_p = obj.find("\"sender_id\":");
                    if (sid_p != std::string::npos) sid = std::atoll(obj.c_str() + sid_p + 12);

                    bool is_out = false;
                    size_t out_p = obj.find("\"out\":true");
                    if (out_p != std::string::npos) is_out = true;

                    if (mid > 0) {
                        if (min_id == 0 || mid < min_id) min_id = mid;
                        in_chunk++;
                        total_scanned++;

                        if (is_out || (me > 0 && sid == me)) {
                            my_count++;
                        } else {
                            their_count++;
                        }
                    }
                    pos = end_pos + 1;
                }

                if (in_chunk == 0 || min_id == 0 || min_id == offset_id || in_chunk < 100) break;
                offset_id = (int)min_id;
            }
        }

        std::string my_name = koto_utils::get_user_first_name(api, ctx, me);
        if (my_name.empty()) my_name = "Вы";

        std::string target_name = koto_utils::get_user_first_name(api, ctx, target_uid);
        if (target_name.empty()) target_name = koto_utils::get_user_name(api, ctx, target_uid);
        if (target_name.empty()) target_name = "Собеседник";

        std::vector<UserStat> stats;
        stats.push_back({me, my_count, my_name, koto_utils::get_user_username(api, ctx, me)});
        stats.push_back({target_uid, their_count, target_name, koto_utils::get_user_username(api, ctx, target_uid)});

        std::sort(stats.begin(), stats.end(), [](const UserStat& a, const UserStat& b) {
            return a.count > b.count;
        });

        auto png = render_top_chart(stats, "Диалог с " + target_name, total_scanned);

        // Формирование текстового отчёта
        api->c_reset(ctx);
        api->c_emoji(ctx, "💬", EMOJI_CHAT);
        api->c_fmt(ctx, " Количество сообщений в личном чате с ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, target_name.c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, ":\n\n");

        int sum = my_count + their_count;
        for (size_t i = 0; i < stats.size(); ++i) {
            const auto& st = stats[i];
            float pct = (sum > 0) ? ((float)st.count / sum * 100.0f) : 0.0f;
            char pct_buf[32];
            snprintf(pct_buf, sizeof(pct_buf), "%.1f%%", pct);

            if (i == 0) api->c_text(ctx, "🥇 ");
            else if (i == 1) api->c_text(ctx, "🥈 ");

            api->c_fmt(ctx, (std::to_string(i + 1) + ". ").c_str(), KOTO_ENT_BOLD);
            api->c_fmt(ctx, st.name.c_str(), KOTO_ENT_BOLD);
            api->c_text(ctx, " — ");
            api->c_fmt(ctx, std::to_string(st.count).c_str(), KOTO_ENT_CODE);
            api->c_text(ctx, " сообщ. (");
            api->c_fmt(ctx, pct_buf, KOTO_ENT_ITALIC);
            api->c_text(ctx, ")\n");
        }

        api->c_text(ctx, "\n📊 Всего сообщений в выборке: ");
        api->c_fmt(ctx, std::to_string(total_scanned).c_str(), KOTO_ENT_CODE);

        if (!png.empty() && api->c_send_bytes) {
            api->delete_msg(ctx);
            api->c_send_bytes(ctx, cid, "top_chat.png", png.data(), (int)png.size(), 0);
        } else {
            send_result(ctx, api);
        }
        return;
    }

    // ── 2. Режим ГРУППОВОГО ЧАТА (Group / Supergroup / Channel) ───────────────

    // Название чата
    std::string chat_title = "Чат";
    std::string full_json = koto_utils::get_chat_full_json(api, ctx, cid);
    try {
        if (!full_json.empty() && full_json != "{}") {
            auto j = nlohmann::json::parse(full_json);
            if (j.contains("title")) chat_title = j["title"].get<std::string>();
        }
    } catch (...) {}

    std::map<long long, int> message_counts;
    int total_scanned = 0;

    if (api->get_history_json) {
        int offset_id = 0;
        std::vector<char> hbuf(131072, 0);

        while (total_scanned < max_scan_messages) {
            int hlen = api->get_history_json(ctx, cid, offset_id, 100, 0, hbuf.data(), (int)hbuf.size() - 1);
            if (hlen <= 2) break;

            std::string jstr(hbuf.data(), hlen);
            size_t pos = 0;
            int in_chunk = 0;
            long long min_id = 0;

            while ((pos = jstr.find("{\"id\":", pos)) != std::string::npos) {
                size_t end_pos = jstr.find('}', pos);
                if (end_pos == std::string::npos) break;
                std::string obj = jstr.substr(pos, end_pos - pos + 1);

                long long mid = 0;
                size_t id_p = obj.find("\"id\":");
                if (id_p != std::string::npos) mid = std::atoll(obj.c_str() + id_p + 5);

                long long sid = 0;
                size_t sid_p = obj.find("\"sender_id\":");
                if (sid_p != std::string::npos) sid = std::atoll(obj.c_str() + sid_p + 12);

                if (mid > 0) {
                    if (min_id == 0 || mid < min_id) min_id = mid;
                    in_chunk++;
                    total_scanned++;

                    if (sid > 0) {
                        message_counts[sid]++;
                    }
                }
                pos = end_pos + 1;
            }

            if (in_chunk == 0 || min_id == 0 || min_id == offset_id || in_chunk < 100) break;
            offset_id = (int)min_id;
        }
    }

    if (message_counts.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "💬", EMOJI_CHAT);
        api->c_fmt(ctx, " Не удалось проанализировать сообщения или история чата пуста.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    // Сортировка по количеству сообщений
    std::vector<std::pair<long long, int>> sorted_counts(message_counts.begin(), message_counts.end());
    std::sort(sorted_counts.begin(), sorted_counts.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    std::vector<UserStat> stats;
    int limit_count = std::min((int)sorted_counts.size(), top_n);
    for (int i = 0; i < limit_count; ++i) {
        long long uid = sorted_counts[i].first;
        int count = sorted_counts[i].second;

        std::string fname = koto_utils::get_user_first_name(api, ctx, uid);
        if (fname.empty()) fname = koto_utils::get_user_name(api, ctx, uid);
        if (fname.empty()) fname = "ID " + std::to_string(uid);

        std::string uname = koto_utils::get_user_username(api, ctx, uid);
        stats.push_back({uid, count, fname, uname});
    }

    // Генерация PNG диаграммы
    auto png = render_top_chart(stats, chat_title, total_scanned);

    // Составление текстового сообщения
    api->c_reset(ctx);
    api->c_emoji(ctx, "💬", EMOJI_CHAT);
    api->c_fmt(ctx, " Топ пользователей в ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, chat_title.c_str(), KOTO_ENT_BOLD);
    api->c_text(ctx, ":\n\n");

    for (size_t i = 0; i < stats.size(); ++i) {
        const auto& st = stats[i];
        float pct = (total_scanned > 0) ? ((float)st.count / total_scanned * 100.0f) : 0.0f;
        char pct_buf[32];
        snprintf(pct_buf, sizeof(pct_buf), "%.1f%%", pct);

        if (i == 0) api->c_text(ctx, "🥇 ");
        else if (i == 1) api->c_text(ctx, "🥈 ");
        else if (i == 2) api->c_text(ctx, "🥉 ");
        else api->c_text(ctx, "▫️ ");

        api->c_fmt(ctx, (std::to_string(i + 1) + ". ").c_str(), KOTO_ENT_BOLD);

        if (!st.username.empty()) {
            std::string user_tag = "@" + st.username;
            api->c_fmt(ctx, user_tag.c_str(), KOTO_ENT_BOLD);
        } else {
            std::string url = "tg://user?id=" + std::to_string(st.user_id);
            api->c_url(ctx, st.name.c_str(), url.c_str());
        }

        api->c_text(ctx, " — ");
        api->c_fmt(ctx, std::to_string(st.count).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, " сообщ. (");
        api->c_fmt(ctx, pct_buf, KOTO_ENT_ITALIC);
        api->c_text(ctx, ")\n");
    }

    api->c_text(ctx, "\n📊 Всего проанализировано: ");
    api->c_fmt(ctx, std::to_string(total_scanned).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " сообщ. (уникальных авторов: ");
    api->c_fmt(ctx, std::to_string(message_counts.size()).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, ")");

    if (!png.empty() && api->c_send_bytes) {
        api->delete_msg(ctx);
        api->c_send_bytes(ctx, cid, "top_chart.png", png.data(), (int)png.size(), 0);
    } else {
        send_result(ctx, api);
    }
}

// ── Реестр команд ─────────────────────────────────────────────────────────────

static const koto_command COMMANDS[] = {
    {
        "top",
        &cmd_top,
        KOTO_LEVEL_TRUSTED,
        "Посмотреть топ пользователей по количеству сообщений в чате (график + статистика).",
        "[число]"
    }
};

// ── Схема настроек модуля для панели .cfg ─────────────────────────────────────

static const koto_setting_field SETTINGS[] = {
    {
        "SCAN_LIMIT",
        "Максимум сообщений для сканирования топа (100-5000)",
        KOTO_FIELD_INT,
        "2000",
        nullptr
    },
    {
        "TOP_LIMIT",
        "Количество пользователей в топе (5-30)",
        KOTO_FIELD_INT,
        "20",
        nullptr
    }
};

// ── Метаданные модуля ─────────────────────────────────────────────────────────

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "top",
    "Модуль для просмотра топа активности пользователей по количеству сообщений в чате (график + статистика).",
    "1.1.0",
    60,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, // on_watch
    0,
    nullptr, // on_callback
    SETTINGS,
    sizeof(SETTINGS) / sizeof(SETTINGS[0]),
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/top.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
