// discordo.cpp
// Показывает вашу текущую активность в Discord через Lanyard API с генерацией PNG-баннера.
// Порт модуля discordo.py (by SunnexGB / @H_SunMods) на Kotogram C++ ABI 3.
//
// Команды:
//   .dsnow                      — показать текущую активность в Discord (+ PNG баннер)
//   .dsguide                    — подробный гайд по настройке (Discord ID + Lanyard)
//   .ctheme {"bg":"#1e1f22",...}— настроить кастомные цвета темы (текстом, реплаем или .json файлом)

#include "module_abi.h"
#include "nlohmann/json.hpp"
#include "font8x8.hpp"
#include <zlib.h>
#include <string>
#include <vector>
#include <cmath>
#include <ctime>
#include <cstring>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <algorithm>

using json = nlohmann::json;

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string to_lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

static std::string get_prefix(koto_ctx* ctx, const koto_api* api) {
    if (api->setting_get) {
        char buf[32] = {0};
        int n = api->setting_get(ctx, "prefix", buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            std::string p = trim(buf);
            if (!p.empty()) return p;
        }
    }
    return ".";
}

static std::string cfg_str(koto_ctx* ctx, const koto_api* api, const char* key, const char* def = "") {
    char buf[1024] = {0};
    if (api->cfg_get) {
        int n = api->cfg_get(ctx, key, buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = '\0'; std::string v = trim(buf); if (!v.empty()) return v; }
    }
    if (api->db_get) {
        int n = api->db_get(ctx, key, buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = '\0'; std::string v = trim(buf); if (!v.empty()) return v; }
    }
    return def;
}

static void reply_or_edit(koto_ctx* ctx, const koto_api* api, int flags = 0) {
    if (api->is_outgoing && api->is_outgoing(ctx)) api->c_edit(ctx, flags);
    else if (api->c_reply) api->c_reply(ctx, flags);
    else api->c_edit(ctx, flags);
}

// ── Мини-рендерер PNG баннера (zlib, без внешних зависимостей) ────────────────
struct RGBA { uint8_t r, g, b, a; };

static bool parse_hex_color(const std::string& hex, RGBA& out) {
    std::string s = trim(hex);
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    if (s.size() != 6 && s.size() != 8) return false;
    auto hval = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int v[8];
    for (size_t i = 0; i < s.size(); ++i) {
        v[i] = hval(s[i]);
        if (v[i] < 0) return false;
    }
    out.r = static_cast<uint8_t>((v[0] << 4) | v[1]);
    out.g = static_cast<uint8_t>((v[2] << 4) | v[3]);
    out.b = static_cast<uint8_t>((v[4] << 4) | v[5]);
    out.a = (s.size() == 8) ? static_cast<uint8_t>((v[6] << 4) | v[7]) : 255;
    return true;
}

struct Canvas {
    int w, h;
    std::vector<uint8_t> px; // RGBA
    Canvas(int W, int H) : w(W), h(H), px(W * H * 4, 0) {}

    void blend_pixel(int x, int y, RGBA c) {
        if (x < 0 || y < 0 || x >= w || y >= h || c.a == 0) return;
        size_t idx = static_cast<size_t>((y * w + x) * 4);
        if (c.a == 255) {
            px[idx] = c.r; px[idx + 1] = c.g; px[idx + 2] = c.b; px[idx + 3] = 255;
        } else {
            int a = c.a, ia = 255 - a;
            px[idx]     = static_cast<uint8_t>((c.r * a + px[idx] * ia) / 255);
            px[idx + 1] = static_cast<uint8_t>((c.g * a + px[idx + 1] * ia) / 255);
            px[idx + 2] = static_cast<uint8_t>((c.b * a + px[idx + 2] * ia) / 255);
            px[idx + 3] = 255;
        }
    }

    void fill_rect(int x0, int y0, int rw, int rh, RGBA c) {
        for (int y = y0; y < y0 + rh; ++y)
            for (int x = x0; x < x0 + rw; ++x)
                blend_pixel(x, y, c);
    }

    void fill_round_rect(int x0, int y0, int rw, int rh, int r, RGBA c) {
        for (int y = y0; y < y0 + rh; ++y) {
            for (int x = x0; x < x0 + rw; ++x) {
                int dx = 0, dy = 0;
                if (x < x0 + r) dx = (x0 + r) - x;
                else if (x >= x0 + rw - r) dx = x - (x0 + rw - r - 1);
                if (y < y0 + r) dy = (y0 + r) - y;
                else if (y >= y0 + rh - r) dy = y - (y0 + rh - r - 1);
                if (dx * dx + dy * dy <= r * r) blend_pixel(x, y, c);
            }
        }
    }

    void fill_circle(int cx, int cy, int r, RGBA c) {
        for (int y = cy - r; y <= cy + r; ++y)
            for (int x = cx - r; x <= cx + r; ++x)
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r)
                    blend_pixel(x, y, c);
    }

    // Транслитерация кириллицы для встроенного 8x8 ASCII-шрифта на баннере
    static std::string to_ascii_label(const std::string& utf8) {
        static const char* const cyr_upper[33] = {
            "A","B","V","G","D","E","ZH","Z","I","Y","K","L","M","N","O","P",
            "R","S","T","U","F","KH","TS","CH","SH","SCH","","Y","","E","YU","YA","YO"
        };
        static const char* const cyr_lower[33] = {
            "a","b","v","g","d","e","zh","z","i","y","k","l","m","n","o","p",
            "r","s","t","u","f","kh","ts","ch","sh","sch","","y","","e","yu","ya","yo"
        };
        std::string out;
        for (size_t i = 0; i < utf8.size(); ) {
            unsigned char c = static_cast<unsigned char>(utf8[i]);
            if (c < 0x80) {
                out.push_back(c >= 32 ? static_cast<char>(c) : ' ');
                i += 1;
            } else if ((c & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
                uint32_t cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3F);
                i += 2;
                if (cp >= 0x0410 && cp <= 0x042F) out += cyr_upper[cp - 0x0410];
                else if (cp == 0x0401) out += cyr_upper[32];
                else if (cp >= 0x0430 && cp <= 0x044F) out += cyr_lower[cp - 0x0430];
                else if (cp == 0x0451) out += cyr_lower[32];
                else out.push_back(' ');
            } else if ((c & 0xF0) == 0xE0 && i + 2 < utf8.size()) { i += 3; }
            else if ((c & 0xF8) == 0xF0 && i + 3 < utf8.size()) { i += 4; }
            else { i += 1; }
        }
        return out;
    }

    void draw_text(int x, int y, const std::string& utf8, int scale, RGBA color, int max_chars = 36) {
        std::string s = to_ascii_label(utf8);
        if (static_cast<int>(s.size()) > max_chars) {
            s = s.substr(0, static_cast<size_t>(max_chars - 3)) + "...";
        }
        int cx = x;
        for (char ch : s) {
            unsigned char uc = static_cast<unsigned char>(ch);
            if (uc >= 128) uc = '?';
            for (int row = 0; row < 8; ++row) {
                uint8_t bits = static_cast<uint8_t>(font8x8_basic[uc][row]);
                for (int col = 0; col < 8; ++col) {
                    if (bits & (1u << col)) {
                        fill_rect(cx + col * scale, y + row * scale, scale, scale, color);
                    }
                }
            }
            cx += 8 * scale;
        }
    }

    std::vector<uint8_t> encode_png() const {
        auto put_be32 = [](std::vector<uint8_t>& out, uint32_t v) {
            out.push_back((v >> 24) & 0xFF);
            out.push_back((v >> 16) & 0xFF);
            out.push_back((v >> 8) & 0xFF);
            out.push_back(v & 0xFF);
        };
        auto add_chunk = [&](std::vector<uint8_t>& out, const char type[4], const uint8_t* data, size_t len) {
            put_be32(out, static_cast<uint32_t>(len));
            size_t tpos = out.size();
            out.insert(out.end(), type, type + 4);
            if (len > 0) out.insert(out.end(), data, data + len);
            uLong crc = crc32(0L, Z_NULL, 0);
            crc = crc32(crc, out.data() + tpos, static_cast<uInt>(len + 4));
            put_be32(out, static_cast<uint32_t>(crc));
        };

        std::vector<uint8_t> png = {137, 80, 78, 71, 13, 10, 26, 10};
        std::vector<uint8_t> ihdr;
        put_be32(ihdr, static_cast<uint32_t>(w));
        put_be32(ihdr, static_cast<uint32_t>(h));
        ihdr.push_back(8); // bit depth
        ihdr.push_back(6); // RGBA
        ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
        add_chunk(png, "IHDR", ihdr.data(), ihdr.size());

        std::vector<uint8_t> raw;
        raw.reserve(static_cast<size_t>(h) * (1 + static_cast<size_t>(w) * 4));
        for (int y = 0; y < h; ++y) {
            raw.push_back(0); // filter none
            const uint8_t* row = px.data() + static_cast<size_t>(y) * w * 4;
            raw.insert(raw.end(), row, row + static_cast<size_t>(w) * 4);
        }

        uLongf comp_len = compressBound(static_cast<uLong>(raw.size()));
        std::vector<uint8_t> comp(comp_len);
        compress2(comp.data(), &comp_len, raw.data(), static_cast<uLong>(raw.size()), 6);
        add_chunk(png, "IDAT", comp.data(), comp_len);
        add_chunk(png, "IEND", nullptr, 0);
        return png;
    }
};

static std::string format_timestamp(const json& activity, double& out_progress) {
    out_progress = -1.0;
    if (!activity.contains("timestamps") || !activity["timestamps"].is_object()) return "";
    const auto& ts = activity["timestamps"];
    if (!ts.contains("start") || !ts["start"].is_number()) return "";

    double start_sec = ts["start"].get<double>() / 1000.0;
    double now_sec = static_cast<double>(std::time(nullptr));
    double elapsed = std::max(0.0, now_sec - start_sec);

    if (ts.contains("end") && ts["end"].is_number()) {
        double end_sec = ts["end"].get<double>() / 1000.0;
        double dur = std::max(1.0, end_sec - start_sec);
        double pos = std::min(elapsed, dur);
        out_progress = std::clamp(pos / dur, 0.0, 1.0);
        int pm = static_cast<int>(pos) / 60, ps = static_cast<int>(pos) % 60;
        int dm = static_cast<int>(dur) / 60, ds = static_cast<int>(dur) % 60;
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(2) << pm << ":" << std::setw(2) << ps
            << "/" << std::setw(2) << dm << ":" << std::setw(2) << ds;
        return oss.str();
    }

    int total = static_cast<int>(elapsed);
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;
    std::ostringstream oss;
    if (h > 0) {
        oss << h << ":" << std::setfill('0') << std::setw(2) << m << ":" << std::setw(2) << s;
    } else {
        oss << m << ":" << std::setfill('0') << std::setw(2) << s;
    }
    return oss.str();
}

static std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

static const koto_setting_field SETTINGS[] = {
    { "user_id", "Ваш Discord User ID", KOTO_FIELD_STR, "", nullptr },
    { "theme", "Тема баннера (dark / light / custom)", KOTO_FIELD_CHOICE, "dark", "dark,light,custom" },
    { "show_banner", "Отправлять PNG-баннер с активностью", KOTO_FIELD_BOOL, "1", nullptr },
    { "custom_message", "Свой шаблон ({name}, {details}, {state}, {timestamp})", KOTO_FIELD_STR, "", nullptr }
};

static void cmd_dsguide(koto_ctx* ctx, const koto_api* api) {
    std::string pfx = get_prefix(ctx, api);
    api->c_reset(ctx);
    api->c_fmt(ctx, "Как начать пользоваться Discordo?\n\n", KOTO_ENT_BOLD);
    api->c_fmt(ctx, "1. Получение Discord User ID\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "Перейдите в Настройки Discord => Расширенные => Режим разработчика => Включить.\n"
                     "Нажмите на свой профиль и скопируйте ID пользователя.\n\n"
                     "Затем укажите ID в настройках модуля:\n");
    api->c_fmt(ctx, (pfx + "cfg discordo user_id ВАШ_ID").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n\n");
    api->c_fmt(ctx, "2. Вступите на сервер Lanyard\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "Чтобы Lanyard API видел вашу активность, нужно находиться на их Discord-сервере.\n\n"
                     "После этого используйте ");
    api->c_fmt(ctx, (pfx + "dsnow").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " для отображения активности и ");
    api->c_fmt(ctx, (pfx + "ctheme").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " для настройки цветов.");

    if (api->c_button_ex) {
        api->c_button_ex(ctx, "Войти в Lanyard", "https://discord.gg/lanyard", 1, KOTO_BTN_PRIMARY, 0);
    } else if (api->c_button) {
        api->c_button(ctx, "Войти в Lanyard", "https://discord.gg/lanyard", 1);
    } else {
        api->c_text(ctx, "\n\n");
        api->c_url(ctx, "Войти в Lanyard", "https://discord.gg/lanyard");
    }
    reply_or_edit(ctx, api, KOTO_NO_LINK_PREVIEW);
}

static void cmd_ctheme(koto_ctx* ctx, const koto_api* api) {
    const char* raw = api->cmd_args ? api->cmd_args(ctx) : nullptr;
    std::string args = trim(raw ? raw : "");
    std::string json_str;

    size_t brace = args.find('{');
    if (brace != std::string::npos) {
        json_str = args.substr(brace);
    } else if (api->reply_file) {
        std::vector<unsigned char> fbuf(64 * 1024);
        long long fn = api->reply_file(ctx, fbuf.data(), static_cast<int>(fbuf.size()));
        if (fn > 0) {
            json_str.assign(reinterpret_cast<const char*>(fbuf.data()), static_cast<size_t>(fn));
        }
    }
    if (trim(json_str).empty() && api->reply_text) {
        const char* rep = api->reply_text(ctx);
        if (rep && rep[0]) json_str = rep;
    }

    if (trim(json_str).empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "Использование: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".ctheme {\"bg\":\"#1e1f22\",\"card\":\"#2b2d31\",\"text\":\"#ffffff\",\"subtext\":\"#b5bac1\",\"accent\":\"#5865f2\"}", KOTO_ENT_CODE);
        api->c_text(ctx, "\n\nЦвета можно передать текстом, ответом на сообщение или на .json файл.");
        reply_or_edit(ctx, api);
        return;
    }

    json j = json::parse(json_str, nullptr, false);
    if (!j.is_object()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "Нужен формат JSON: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, "{\"bg\":\"#1e1f22\",\"text\":\"#ffffff\",\"accent\":\"#5865f2\"}", KOTO_ENT_CODE);
        reply_or_edit(ctx, api);
        return;
    }

    json valid = json::object();
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!it.value().is_string()) continue;
        RGBA tmp;
        if (parse_hex_color(it.value().get<std::string>(), tmp)) {
            valid[it.key()] = it.value().get<std::string>();
        }
    }

    if (valid.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "Не найдено корректных HEX-цветов (пример: #5865f2).", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }

    api->db_set(ctx, "custom_theme_json", valid.dump().c_str());
    if (api->cfg_set) api->cfg_set(ctx, "theme", "custom");

    api->c_reset(ctx);
    api->c_fmt(ctx, ("\xE2\x9C\x85 Цвета для темы custom сохранены (" + std::to_string(valid.size()) + ").").c_str(), KOTO_ENT_BOLD);
    reply_or_edit(ctx, api);
}

static void cmd_dsnow(koto_ctx* ctx, const koto_api* api) {
    std::string uid = cfg_str(ctx, api, "user_id");
    if (uid.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "Не указан Discord User ID! Используйте команду ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (get_prefix(ctx, api) + "dsguide").c_str(), KOTO_ENT_CODE);
        reply_or_edit(ctx, api);
        return;
    }

    api->c_reset(ctx);
    api->c_fmt(ctx, "\xE2\x8F\xB3 Получаю активность Discord...", KOTO_ENT_ITALIC);
    reply_or_edit(ctx, api);

    std::string url = "https://api.lanyard.rest/v1/users/" + uid;
    std::vector<unsigned char> buf(256 * 1024);
    long long n = api->http_get(ctx, url.c_str(), buf.data(), static_cast<int>(buf.size()));
    if (n <= 0) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "Не удалось получить данные от Lanyard API. Воспользуйтесь командой ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (get_prefix(ctx, api) + "dsguide").c_str(), KOTO_ENT_CODE);
        reply_or_edit(ctx, api);
        return;
    }

    std::string body(reinterpret_cast<const char*>(buf.data()), static_cast<size_t>(n));
    json root = json::parse(body, nullptr, false);
    if (!root.is_object() || !root.value("success", false) || !root.contains("data") || !root["data"].is_object()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "Не удалось получить данные от API или пользователь не на сервере Lanyard. Используйте ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (get_prefix(ctx, api) + "dsguide").c_str(), KOTO_ENT_CODE);
        reply_or_edit(ctx, api);
        return;
    }

    const auto& data = root["data"];
    const auto& activities = data.contains("activities") && data["activities"].is_array()
                                 ? data["activities"]
                                 : json::array();

    // Ищем первую значимую активность (игнорируем Custom Status type==4, если есть игра/музыка)
    json activity;
    bool has_activity = false;
    for (const auto& a : activities) {
        if (!a.is_object()) continue;
        if (a.value("type", 0) != 4) {
            activity = a;
            has_activity = true;
            break;
        }
    }
    if (!has_activity && !activities.empty() && activities[0].is_object()) {
        activity = activities[0];
        has_activity = true;
    }

    if (!has_activity) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "В данный момент активностей не наблюдается...", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }

    std::string act_name = activity.value("name", "");
    std::string act_details = activity.value("details", "");
    std::string act_state = activity.value("state", "");
    double progress = -1.0;
    std::string timestamp = format_timestamp(activity, progress);

    std::string username = "Discord";
    if (data.contains("discord_user") && data["discord_user"].is_object()) {
        const auto& du = data["discord_user"];
        if (du.contains("global_name") && du["global_name"].is_string() && !du["global_name"].get<std::string>().empty())
            username = du["global_name"].get<std::string>();
        else if (du.contains("username") && du["username"].is_string())
            username = du["username"].get<std::string>();
    }
    std::string status = data.value("discord_status", "offline");

    // Сборка текста подписи
    std::string custom_msg = cfg_str(ctx, api, "custom_message", "");
    auto build_caption = [&]() {
        api->c_reset(ctx);
        if (!custom_msg.empty()) {
            std::string t = custom_msg;
            t = replace_all(t, "{name}", act_name);
            t = replace_all(t, "{details}", act_details.empty() ? act_state : act_details);
            t = replace_all(t, "{state}", act_state);
            t = replace_all(t, "{timestamp}", timestamp);
            api->c_text(ctx, t.c_str());
        } else {
            api->c_fmt(ctx, "сейчас в: ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, act_name.c_str(), KOTO_ENT_CODE);
            if (!act_details.empty() || !act_state.empty()) {
                api->c_text(ctx, "\n");
                api->c_fmt(ctx, "доп.инфо: ", KOTO_ENT_BOLD);
                std::string info = act_details;
                if (!act_state.empty()) {
                    if (!info.empty()) info += " — ";
                    info += act_state;
                }
                api->c_fmt(ctx, info.c_str(), KOTO_ENT_CODE);
            }
            if (!timestamp.empty()) {
                api->c_text(ctx, "\n");
                api->c_emoji(ctx, "\xF0\x9F\x92\xA0", 5350737576674243497LL);
                api->c_text(ctx, " ");
                api->c_fmt(ctx, timestamp.c_str(), KOTO_ENT_CODE);
            }
        }
    };

    std::string show_b = to_lower(cfg_str(ctx, api, "show_banner", "1"));
    bool render_banner = (show_b == "1" || show_b == "true" || show_b == "yes");

    if (!render_banner) {
        build_caption();
        reply_or_edit(ctx, api, KOTO_NO_LINK_PREVIEW);
        return;
    }

    // Определение палитры темы
    std::string theme = to_lower(cfg_str(ctx, api, "theme", "dark"));
    RGBA c_bg      = {30, 31, 34, 255};
    RGBA c_card    = {43, 45, 49, 255};
    RGBA c_text    = {255, 255, 255, 255};
    RGBA c_sub     = {181, 186, 193, 255};
    RGBA c_accent  = {88, 101, 242, 255};
    RGBA c_bar_bg  = {62, 65, 72, 255};

    if (theme == "light") {
        c_bg     = {242, 243, 245, 255};
        c_card   = {255, 255, 255, 255};
        c_text   = {30, 31, 34, 255};
        c_sub    = {92, 97, 105, 255};
        c_accent = {88, 101, 242, 255};
        c_bar_bg = {218, 220, 224, 255};
    } else if (theme == "custom") {
        std::string cj = cfg_str(ctx, api, "custom_theme_json", "");
        if (!cj.empty()) {
            json tj = json::parse(cj, nullptr, false);
            if (tj.is_object()) {
                if (tj.contains("bg") && tj["bg"].is_string()) parse_hex_color(tj["bg"], c_bg);
                if (tj.contains("card") && tj["card"].is_string()) parse_hex_color(tj["card"], c_card);
                if (tj.contains("text") && tj["text"].is_string()) parse_hex_color(tj["text"], c_text);
                if (tj.contains("subtext") && tj["subtext"].is_string()) parse_hex_color(tj["subtext"], c_sub);
                if (tj.contains("accent") && tj["accent"].is_string()) parse_hex_color(tj["accent"], c_accent);
            }
        }
    }

    RGBA c_status = {128, 132, 142, 255};
    if (status == "online") c_status = {35, 165, 90, 255};
    else if (status == "idle") c_status = {240, 178, 50, 255};
    else if (status == "dnd") c_status = {242, 63, 67, 255};

    // Рисуем стильную карточку 760x240
    Canvas cv(760, 240);
    cv.fill_rect(0, 0, 760, 240, c_bg);
    cv.fill_round_rect(18, 18, 724, 204, 20, c_card);
    // Левый акцентный квадрат-значок активности
    cv.fill_round_rect(38, 42, 144, 144, 18, c_accent);
    // Иконка внутри квадрата (инициалы или значок)
    std::string badge = act_name.empty() ? "DS" : act_name.substr(0, 2);
    cv.draw_text(62, 92, badge, 6, {255, 255, 255, 255}, 2);
    // Статус-точка в правом нижнем углу иконки
    cv.fill_circle(172, 176, 17, c_card);
    cv.fill_circle(172, 176, 12, c_status);

    // Заголовок пользователя + статус
    cv.draw_text(208, 42, username + " [" + status + "]", 2, c_sub, 32);
    // Название игры / приложения
    cv.draw_text(208, 74, act_name, 3, c_text, 21);
    // Детали
    if (!act_details.empty()) {
        cv.draw_text(208, 114, act_details, 2, c_sub, 32);
    }
    if (!act_state.empty()) {
        cv.draw_text(208, 142, act_state, 2, c_sub, 32);
    }
    // Таймстамп и прогресс-бар
    if (!timestamp.empty()) {
        if (progress >= 0.0) {
            cv.fill_round_rect(208, 178, 360, 10, 5, c_bar_bg);
            int fill_w = std::max(10, static_cast<int>(360.0 * progress));
            cv.fill_round_rect(208, 178, fill_w, 10, 5, c_accent);
            cv.draw_text(580, 174, timestamp, 2, c_sub, 12);
        } else {
            cv.draw_text(208, 174, "Elapsed: " + timestamp, 2, c_accent, 24);
        }
    }

    auto png_bytes = cv.encode_png();
    build_caption();
    api->c_send_bytes(ctx, api->chat_id(ctx), "discordo_banner.png",
                      png_bytes.data(), static_cast<int>(png_bytes.size()), 0);
    api->delete_msg(ctx);
}

static const koto_command COMMANDS[] = {
    {
        "dsnow",
        &cmd_dsnow,
        KOTO_LEVEL_TRUSTED,
        "[ru] Показать текущую активность в Discord с PNG-баннером. [en] Show current Discord activity with PNG banner.",
        nullptr
    },
    {
        "dsguide",
        &cmd_dsguide,
        KOTO_LEVEL_TRUSTED,
        "[ru] Гайд по настройке Discordo (Discord ID + Lanyard). [en] Usage guide for Discordo.",
        nullptr
    },
    {
        "ctheme",
        &cmd_ctheme,
        KOTO_LEVEL_OWNER,
        "[ru] Настроить кастомные цвета баннера JSON-объектом. [en] Set custom banner theme colors via JSON.",
        "{\"bg\":\"#1e1f22\",\"text\":\"#ffffff\",\"accent\":\"#5865f2\"}"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "discordo",
    "[ru] Показывает вашу активность в Discord через Lanyard API с генерацией PNG-баннера (порт discordo.py). C++ ABI 3. [en] Show Discord Rich Presence via Lanyard API. C++ ABI 3.",
    "1.0.0",
    24,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    SETTINGS,
    sizeof(SETTINGS) / sizeof(SETTINGS[0]),
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/discordo.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
