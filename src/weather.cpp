// weather.cpp — Модуль текущей погоды и прогноза наперёд (C++ ABI 3).
// Использует wttr.in (текущая погода + прогноз на 3 дня с разбивкой по времени)
// и Open-Meteo (расширенный прогноз на 7 дней и отказоустойчивый fallback).
// Команды: .weather / .w (текущая + прогноз), .forecast / .fc (подробный прогноз наперёд).
// Поддержка параметров: .w Москва, .w Москва завтра, .w погода на завтра, .w Москва 7, .w set <город>.
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <cmath>
#include <sstream>
#include <unordered_set>
#include <algorithm>
#include <cctype>

namespace {

static constexpr long long EMO_MAIN     = 5283097055852503586LL; // 🌦️
static constexpr long long EMO_LOC      = 5778661935927004845LL; // 📍
static constexpr long long EMO_TEMP     = 5470049770997292425LL; // 🌡️
static constexpr long long EMO_FEELS    = 5373067498193166118LL; // 🥵
static constexpr long long EMO_WIND     = 5458875859525970449LL; // 💨
static constexpr long long EMO_HUMIDITY = 6050944866580435869LL; // 💧
static constexpr long long EMO_SUN      = 5469947168523558652LL; // ☀️
static constexpr long long EMO_MOON     = 5465643984955120548LL; // 🌙
static constexpr long long EMO_CALENDAR = 5467657976867195821LL; // 📅

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

static std::string utf8_tolower(const std::string& str) {
    std::string out;
    out.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        unsigned char c = str[i];
        if (c < 128) {
            out.push_back((char)std::tolower(c));
        } else if (c == 0xD0 && i + 1 < str.size()) {
            unsigned char c2 = str[i + 1];
            if (c2 >= 0x90 && c2 <= 0x9F) { // А..П -> а..п
                out.push_back((char)0xD0);
                out.push_back((char)(c2 + 0x20));
                i++;
            } else if (c2 >= 0xA0 && c2 <= 0xAF) { // Р..Я -> р..я
                out.push_back((char)0xD1);
                out.push_back((char)(c2 - 0x20));
                i++;
            } else if (c2 == 0x81) { // Ё -> ё
                out.push_back((char)0xD1);
                out.push_back((char)0x91);
                i++;
            } else {
                out.push_back(c);
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

static std::string format_temp(const std::string& t_str) {
    if (t_str.empty()) return "0°C";
    try {
        double val = std::stod(t_str);
        int ival = static_cast<int>(std::round(val));
        if (ival > 0) return "+" + std::to_string(ival) + "°C";
        return std::to_string(ival) + "°C";
    } catch (...) {
        if (!t_str.empty() && isdigit(static_cast<unsigned char>(t_str[0]))) return "+" + t_str + "°C";
        return t_str + "°C";
    }
}

static std::string format_temp_double(double val) {
    int ival = static_cast<int>(std::round(val));
    if (ival > 0) return "+" + std::to_string(ival) + "°C";
    return std::to_string(ival) + "°C";
}

static std::string get_condition_emoji(int code) {
    if (code == 113) return "☀️";
    if (code == 116 || code == 119 || code == 122) return "☁️";
    if (code == 143 || code == 248 || code == 260) return "🌫️";
    if ((code >= 293 && code <= 314) || code == 176 || code == 263 || code == 266 ||
        code == 281 || code == 284 || code == 353 || code == 356 || code == 359) {
        return "🌧️";
    }
    if ((code >= 323 && code <= 338) || code == 179 || code == 182 || code == 185 ||
        code == 227 || code == 230 || code == 350 || code == 368 || code == 371) {
        return "❄️";
    }
    if (code == 317 || code == 320 || code == 362 || code == 365 || code == 374 || code == 377) {
        return "🌨️";
    }
    if (code == 200 || code == 386 || code == 389 || code == 392 || code == 395) {
        return "🌩️";
    }
    return "🌦️";
}

static std::pair<std::string, std::string> wmo_code_info(int code) {
    switch (code) {
        case 0: return {"☀️", "Ясно"};
        case 1: return {"🌤️", "Преимущественно ясно"};
        case 2: return {"⛅", "Переменная облачность"};
        case 3: return {"☁️", "Пасмурно"};
        case 45: case 48: return {"🌫️", "Туман"};
        case 51: case 53: case 55: return {"🌧️", "Морось"};
        case 56: case 57: return {"🌨️", "Ледяная морось"};
        case 61: return {"🌧️", "Небольшой дождь"};
        case 63: return {"🌧️", "Умеренный дождь"};
        case 65: return {"🌧️", "Сильный дождь"};
        case 66: case 67: return {"🌨️", "Ледяной дождь"};
        case 71: return {"❄️", "Небольшой снег"};
        case 73: return {"❄️", "Умеренный снег"};
        case 75: return {"❄️", "Сильный снегопад"};
        case 77: return {"🌨️", "Снежные зерна"};
        case 80: case 81: case 82: return {"🌧️", "Ливень"};
        case 85: case 86: return {"🌨️", "Снегопад"};
        case 95: return {"🌩️", "Гроза"};
        case 96: case 99: return {"⛈️", "Гроза с градом"};
        default: return {"🌦️", "Переменная облачность"};
    }
}

static std::string format_date_label(int day_idx, const std::string& date_iso) {
    std::string day_str = date_iso;
    std::string dow_str = "";
    if (date_iso.size() >= 10 && date_iso[4] == '-' && date_iso[7] == '-') {
        int y = std::atoi(date_iso.substr(0, 4).c_str());
        int m = std::atoi(date_iso.substr(5, 2).c_str());
        int d = std::atoi(date_iso.substr(8, 2).c_str());

        char buf[16];
        snprintf(buf, sizeof(buf), "%02d.%02d", d, m);
        day_str = buf;

        if (m >= 1 && m <= 12) {
            static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
            int sy = y - (m < 3);
            int dow = (sy + sy/4 - sy/100 + sy/400 + t[m-1] + d) % 7;
            if (dow < 0) dow += 7;
            static const char* dows[] = { "Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб" };
            if (dow >= 0 && dow < 7) dow_str = dows[dow];
        }
    }

    if (day_idx == 0) {
        return "Сегодня (" + day_str + (!dow_str.empty() ? ", " + dow_str : "") + ")";
    } else if (day_idx == 1) {
        return "Завтра (" + day_str + (!dow_str.empty() ? ", " + dow_str : "") + ")";
    } else if (day_idx == 2) {
        return "Послезавтра (" + day_str + (!dow_str.empty() ? ", " + dow_str : "") + ")";
    }
    return day_str + (!dow_str.empty() ? " (" + dow_str + ")" : "");
}

// ── Прогноз на 7 дней через Open-Meteo ─────────────────────────────────────────

static bool fetch_open_meteo_7days(koto_ctx* ctx, const koto_api* api, const std::string& location) {
    std::string geo_url = "https://geocoding-api.open-meteo.com/v1/search?name=" +
                          koto_utils::url_encode(location) + "&count=1&language=ru";
    std::string geo_json = koto_utils::http_get(api, ctx, geo_url);
    if (geo_json.empty()) return false;

    try {
        auto gj = nlohmann::json::parse(geo_json);
        if (!gj.contains("results") || gj["results"].empty()) return false;

        const auto& res = gj["results"][0];
        double lat = res.value("latitude", 0.0);
        double lon = res.value("longitude", 0.0);
        std::string city_name = res.value("name", location);
        std::string country = res.value("country", "");

        char coord_buf[128];
        snprintf(coord_buf, sizeof(coord_buf), "%.4f&longitude=%.4f", lat, lon);

        std::string fc_url = "https://api.open-meteo.com/v1/forecast?latitude=" + std::string(coord_buf) +
                             "&daily=weathercode,temperature_2m_max,temperature_2m_min,precipitation_probability_max,windspeed_10m_max&timezone=auto";
        std::string fc_json = koto_utils::http_get(api, ctx, fc_url);
        if (fc_json.empty()) return false;

        auto fj = nlohmann::json::parse(fc_json);
        if (!fj.contains("daily")) return false;

        const auto& daily = fj["daily"];
        auto times = daily.value("time", std::vector<std::string>{});
        auto mins = daily.value("temperature_2m_min", std::vector<double>{});
        auto maxs = daily.value("temperature_2m_max", std::vector<double>{});
        std::vector<int> codes;
        if (daily.contains("weathercode")) codes = daily.value("weathercode", std::vector<int>{});
        else if (daily.contains("weather_code")) codes = daily.value("weather_code", std::vector<int>{});
        auto rains = daily.value("precipitation_probability_max", std::vector<int>{});

        size_t n = times.size();
        if (n == 0) return false;

        api->c_reset(ctx);
        api->c_emoji(ctx, "📍", EMO_LOC);
        api->c_fmt(ctx, " Прогноз на 7 дней: ", KOTO_ENT_BOLD);
        std::string loc_header = city_name + (!country.empty() ? ", " + country : "") + "\n\n";
        api->c_fmt(ctx, loc_header.c_str(), KOTO_ENT_CODE);

        for (size_t i = 0; i < n && i < 7; ++i) {
            std::string date_label = format_date_label(static_cast<int>(i), times[i]);
            auto [emoji, desc] = wmo_code_info(i < codes.size() ? codes[i] : 0);
            std::string min_t = format_temp_double(i < mins.size() ? mins[i] : 0.0);
            std::string max_t = format_temp_double(i < maxs.size() ? maxs[i] : 0.0);

            api->c_text(ctx, "• ");
            api->c_fmt(ctx, (date_label + ": ").c_str(), KOTO_ENT_BOLD);
            api->c_text(ctx, (emoji + " ").c_str());
            api->c_fmt(ctx, (min_t + " ... " + max_t).c_str(), KOTO_ENT_CODE);
            api->c_text(ctx, (" · " + desc).c_str());

            if (i < rains.size() && rains[i] > 15) {
                api->c_text(ctx, (" (осадки " + std::to_string(rains[i]) + "%)").c_str());
            }
            api->c_text(ctx, "\n");
        }

        send_result(ctx, api);
        return true;
    } catch (...) {
        return false;
    }
}

// ── Прогноз на завтра через Open-Meteo (отказоустойчивый fallback) ─────────────

static bool fetch_open_meteo_tomorrow(koto_ctx* ctx, const koto_api* api, const std::string& location) {
    std::string geo_url = "https://geocoding-api.open-meteo.com/v1/search?name=" +
                          koto_utils::url_encode(location) + "&count=1&language=ru";
    std::string geo_json = koto_utils::http_get(api, ctx, geo_url);
    if (geo_json.empty()) return false;

    try {
        auto gj = nlohmann::json::parse(geo_json);
        if (!gj.contains("results") || gj["results"].empty()) return false;

        const auto& res = gj["results"][0];
        double lat = res.value("latitude", 0.0);
        double lon = res.value("longitude", 0.0);
        std::string city_name = res.value("name", location);
        std::string country = res.value("country", "");

        char coord_buf[128];
        snprintf(coord_buf, sizeof(coord_buf), "%.4f&longitude=%.4f", lat, lon);

        std::string fc_url = "https://api.open-meteo.com/v1/forecast?latitude=" + std::string(coord_buf) +
                             "&daily=weathercode,temperature_2m_max,temperature_2m_min,precipitation_probability_max,windspeed_10m_max&timezone=auto";
        std::string fc_json = koto_utils::http_get(api, ctx, fc_url);
        if (fc_json.empty()) return false;

        auto fj = nlohmann::json::parse(fc_json);
        if (!fj.contains("daily")) return false;

        const auto& daily = fj["daily"];
        auto times = daily.value("time", std::vector<std::string>{});
        auto mins = daily.value("temperature_2m_min", std::vector<double>{});
        auto maxs = daily.value("temperature_2m_max", std::vector<double>{});
        std::vector<int> codes;
        if (daily.contains("weathercode")) codes = daily.value("weathercode", std::vector<int>{});
        else if (daily.contains("weather_code")) codes = daily.value("weather_code", std::vector<int>{});
        auto rains = daily.value("precipitation_probability_max", std::vector<int>{});
        auto winds = daily.value("windspeed_10m_max", std::vector<double>{});

        if (times.size() < 2) return false;

        std::string date_label = format_date_label(1, times[1]);
        int code = codes.size() > 1 ? codes[1] : 0;
        auto [emoji, desc] = wmo_code_info(code);
        std::string min_t = format_temp_double(mins.size() > 1 ? mins[1] : 0.0);
        std::string max_t = format_temp_double(maxs.size() > 1 ? maxs[1] : 0.0);
        int rain = rains.size() > 1 ? rains[1] : 0;
        int wind = winds.size() > 1 ? static_cast<int>(std::round(winds[1])) : 0;

        api->c_reset(ctx);
        api->c_emoji(ctx, "📍", EMO_LOC);
        api->c_fmt(ctx, " Прогноз на завтра: ", KOTO_ENT_BOLD);
        std::string loc_header = city_name + (!country.empty() ? ", " + country : "") + "\n\n";
        api->c_fmt(ctx, loc_header.c_str(), KOTO_ENT_CODE);

        api->c_text(ctx, (emoji + " ").c_str());
        api->c_fmt(ctx, (desc + "\n\n").c_str(), KOTO_ENT_BOLD);

        api->c_emoji(ctx, "📅", EMO_CALENDAR);
        api->c_fmt(ctx, (" " + date_label + ":\n").c_str(), KOTO_ENT_BOLD);

        api->c_text(ctx, "  ");
        api->c_emoji(ctx, "☀️", EMO_SUN);
        api->c_fmt(ctx, " Днём: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, max_t.c_str(), KOTO_ENT_CODE);
        if (rain > 15) {
            api->c_text(ctx, (" · 🌧️ " + std::to_string(rain) + "%").c_str());
        }
        api->c_text(ctx, "\n");

        api->c_text(ctx, "  ");
        api->c_emoji(ctx, "🌙", EMO_MOON);
        api->c_fmt(ctx, " Ночью: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, min_t.c_str(), KOTO_ENT_CODE);
        if (wind > 0) {
            api->c_text(ctx, (" · 💨 до " + std::to_string(wind) + " км/ч\n\n").c_str());
        } else {
            api->c_text(ctx, "\n\n");
        }

        api->c_fmt(ctx, "💡 Прогноз на 7 дней: ", KOTO_ENT_ITALIC);
        api->c_fmt(ctx, (".w " + city_name + " 7\n").c_str(), KOTO_ENT_CODE);

        send_result(ctx, api);
        return true;
    } catch (...) {
        return false;
    }
}

// ── Текущая погода и прогноз на 3 дня через Open-Meteo (отказоустойчивый fallback) ──

static bool fetch_open_meteo_current(koto_ctx* ctx, const koto_api* api, const std::string& location) {
    std::string geo_url = "https://geocoding-api.open-meteo.com/v1/search?name=" +
                          koto_utils::url_encode(location) + "&count=1&language=ru";
    std::string geo_json = koto_utils::http_get(api, ctx, geo_url);
    if (geo_json.empty()) return false;

    try {
        auto gj = nlohmann::json::parse(geo_json);
        if (!gj.contains("results") || gj["results"].empty()) return false;

        const auto& res = gj["results"][0];
        double lat = res.value("latitude", 0.0);
        double lon = res.value("longitude", 0.0);
        std::string city_name = res.value("name", location);
        std::string country = res.value("country", "");

        char coord_buf[128];
        snprintf(coord_buf, sizeof(coord_buf), "%.4f&longitude=%.4f", lat, lon);

        std::string cur_url = "https://api.open-meteo.com/v1/forecast?latitude=" + std::string(coord_buf) +
                              "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m" +
                              "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max&timezone=auto";
        std::string cur_json = koto_utils::http_get(api, ctx, cur_url);
        if (cur_json.empty()) return false;

        auto cj = nlohmann::json::parse(cur_json);
        if (!cj.contains("current")) return false;

        const auto& cur = cj["current"];
        double temp = cur.value("temperature_2m", 0.0);
        double feels = cur.value("apparent_temperature", temp);
        double humidity = cur.value("relative_humidity_2m", 0.0);
        double wind = cur.value("wind_speed_10m", 0.0);
        int cur_code = 0;
        if (cur.contains("weather_code")) cur_code = cur.value("weather_code", 0);
        else if (cur.contains("weathercode")) cur_code = cur.value("weathercode", 0);
        auto [cond_emoji, cond_desc] = wmo_code_info(cur_code);

        api->c_reset(ctx);
        api->c_emoji(ctx, "📍", EMO_LOC);
        api->c_fmt(ctx, " Погода в: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (city_name + (!country.empty() ? ", " + country : "") + "\n\n").c_str(), KOTO_ENT_CODE);

        api->c_text(ctx, (cond_emoji + " ").c_str());
        api->c_fmt(ctx, (cond_desc + "\n\n").c_str(), KOTO_ENT_BOLD);

        api->c_emoji(ctx, "🌡️", EMO_TEMP);
        api->c_fmt(ctx, " Температура: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (format_temp_double(temp) + "\n").c_str(), KOTO_ENT_CODE);

        api->c_emoji(ctx, "🥵", EMO_FEELS);
        api->c_fmt(ctx, " Ощущается как: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (format_temp_double(feels) + "\n").c_str(), KOTO_ENT_CODE);

        api->c_emoji(ctx, "💨", EMO_WIND);
        api->c_fmt(ctx, " Ветер: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (std::to_string(static_cast<int>(std::round(wind))) + " км/ч\n").c_str(), KOTO_ENT_CODE);

        api->c_emoji(ctx, "💧", EMO_HUMIDITY);
        api->c_fmt(ctx, " Влажность: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (std::to_string(static_cast<int>(std::round(humidity))) + "%\n\n").c_str(), KOTO_ENT_CODE);

        if (cj.contains("daily")) {
            const auto& daily = cj["daily"];
            auto times = daily.value("time", std::vector<std::string>{});
            auto mins = daily.value("temperature_2m_min", std::vector<double>{});
            auto maxs = daily.value("temperature_2m_max", std::vector<double>{});
            std::vector<int> codes;
            if (daily.contains("weather_code")) codes = daily.value("weather_code", std::vector<int>{});
            else if (daily.contains("weathercode")) codes = daily.value("weathercode", std::vector<int>{});
            auto rains = daily.value("precipitation_probability_max", std::vector<int>{});

            size_t max_d = std::min(size_t(3), times.size());
            if (max_d > 0) {
                api->c_emoji(ctx, "📅", EMO_CALENDAR);
                api->c_fmt(ctx, " Прогноз на 3 дня:\n", KOTO_ENT_BOLD);
                for (size_t d = 0; d < max_d; ++d) {
                    std::string d_label = format_date_label(static_cast<int>(d), times[d]);
                    auto [d_emoji, d_desc] = wmo_code_info(d < codes.size() ? codes[d] : 0);
                    std::string d_min = format_temp_double(d < mins.size() ? mins[d] : 0.0);
                    std::string d_max = format_temp_double(d < maxs.size() ? maxs[d] : 0.0);

                    api->c_text(ctx, "• ");
                    api->c_fmt(ctx, (d_label + ": ").c_str(), KOTO_ENT_BOLD);
                    api->c_text(ctx, (d_emoji + " ").c_str());
                    api->c_fmt(ctx, (d_min + " ... " + d_max).c_str(), KOTO_ENT_CODE);
                    api->c_text(ctx, (" · " + d_desc).c_str());
                    if (d < rains.size() && rains[d] > 0) {
                        api->c_text(ctx, (" (" + std::to_string(rains[d]) + "% осадки)").c_str());
                    }
                    api->c_text(ctx, "\n");
                }
            }
        }

        send_result(ctx, api);
        return true;
    } catch (...) {
        return false;
    }
}

// ── Подробный прогноз на 3 дня (wttr.in) ──────────────────────────────────────

static void show_detailed_forecast(koto_ctx* ctx, const koto_api* api, const nlohmann::json& j, int single_day = -1) {
    std::string area_name = "Локация";
    std::string country = "";
    if (j.contains("nearest_area") && !j["nearest_area"].empty()) {
        auto nearest = j["nearest_area"][0];
        if (nearest.contains("areaName") && !nearest["areaName"].empty()) {
            area_name = nearest["areaName"][0].value("value", "Локация");
        }
        if (nearest.contains("country") && !nearest["country"].empty()) {
            country = nearest["country"][0].value("value", "");
        }
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "📍", EMO_LOC);
    if (single_day == 1) {
        api->c_fmt(ctx, " Прогноз на завтра: ", KOTO_ENT_BOLD);
    } else {
        api->c_fmt(ctx, " Прогноз погоды на 3 дня: ", KOTO_ENT_BOLD);
    }
    api->c_fmt(ctx, (area_name + (!country.empty() ? ", " + country : "") + "\n\n").c_str(), KOTO_ENT_CODE);

    if (j.contains("weather") && !j["weather"].empty()) {
        const auto& weather_days = j["weather"];
        size_t start_day = (single_day >= 0 && single_day < (int)weather_days.size()) ? single_day : 0;
        size_t end_day = (single_day >= 0 && single_day < (int)weather_days.size()) ? (single_day + 1) : std::min(weather_days.size(), size_t(3));

        for (size_t d = start_day; d < end_day; ++d) {
            const auto& day = weather_days[d];
            std::string date_str = day.value("date", "");
            std::string date_label = format_date_label(static_cast<int>(d), date_str);

            std::string min_c = format_temp(day.value("mintempC", "0"));
            std::string max_c = format_temp(day.value("maxtempC", "0"));

            api->c_emoji(ctx, "📅", EMO_CALENDAR);
            api->c_fmt(ctx, (" " + date_label + ":\n").c_str(), KOTO_ENT_BOLD);

            if (day.contains("hourly") && !day["hourly"].empty()) {
                const auto& hourly = day["hourly"];
                const auto& noon = (hourly.size() > 4) ? hourly[4] : hourly[0];
                std::string noon_desc = "Ясно";
                if (noon.contains("lang_ru") && !noon["lang_ru"].empty()) {
                    noon_desc = noon["lang_ru"][0].value("value", "Ясно");
                } else if (noon.contains("weatherDesc") && !noon["weatherDesc"].empty()) {
                    noon_desc = noon["weatherDesc"][0].value("value", "Ясно");
                }
                int noon_code = 113;
                if (noon.contains("weatherCode")) {
                    if (noon["weatherCode"].is_number()) noon_code = noon["weatherCode"].get<int>();
                    else if (noon["weatherCode"].is_string()) noon_code = std::atoi(noon["weatherCode"].get<std::string>().c_str());
                }
                std::string noon_emoji = get_condition_emoji(noon_code);
                std::string noon_rain = noon.value("chanceofrain", "0");

                const auto& night = hourly[0];
                std::string night_desc = "Ясно";
                if (night.contains("lang_ru") && !night["lang_ru"].empty()) {
                    night_desc = night["lang_ru"][0].value("value", "Ясно");
                } else if (night.contains("weatherDesc") && !night["weatherDesc"].empty()) {
                    night_desc = night["weatherDesc"][0].value("value", "Ясно");
                }
                int night_code = 113;
                if (night.contains("weatherCode")) {
                    if (night["weatherCode"].is_number()) night_code = night["weatherCode"].get<int>();
                    else if (night["weatherCode"].is_string()) night_code = std::atoi(night["weatherCode"].get<std::string>().c_str());
                }
                std::string night_emoji = get_condition_emoji(night_code);
                std::string wind_speed = noon.value("windspeedKmph", "0");
                std::string wind_dir = noon.value("winddir16Point", "");
                std::string humidity = noon.value("humidity", "0");

                api->c_text(ctx, "  ");
                api->c_emoji(ctx, "☀️", EMO_SUN);
                api->c_fmt(ctx, " Днём: ", KOTO_ENT_BOLD);
                api->c_fmt(ctx, (max_c + " (" + noon_desc + ")").c_str(), KOTO_ENT_CODE);
                if (std::atoi(noon_rain.c_str()) > 20) {
                    api->c_text(ctx, (" · 🌧️ " + noon_rain + "%").c_str());
                }
                api->c_text(ctx, "\n");

                api->c_text(ctx, "  ");
                api->c_emoji(ctx, "🌙", EMO_MOON);
                api->c_fmt(ctx, " Ночью: ", KOTO_ENT_BOLD);
                api->c_fmt(ctx, (min_c + " (" + night_desc + ")").c_str(), KOTO_ENT_CODE);
                api->c_text(ctx, (" · 💨 " + wind_speed + " км/ч (" + wind_dir + ") · 💧 " + humidity + "%\n\n").c_str());
            } else {
                api->c_text(ctx, "  ");
                api->c_fmt(ctx, (min_c + " ... " + max_c + "\n\n").c_str(), KOTO_ENT_CODE);
            }
        }
    }

    api->c_fmt(ctx, "💡 Прогноз на 7 дней: ", KOTO_ENT_ITALIC);
    api->c_fmt(ctx, (".w " + area_name + " 7\n").c_str(), KOTO_ENT_CODE);

    send_result(ctx, api);
}

// ── Разбор аргументов с поддержкой флагов завтра/7 дней/прогноз и очисткой города ──

struct WeatherQuery {
    std::string location;
    bool is_set_command = false;
    std::string set_location;
    bool want_tomorrow = false;
    bool want_forecast = false;
    int want_days = 3;
};

static WeatherQuery parse_weather_query(koto_ctx* ctx, const koto_api* api, const std::string& raw_args) {
    WeatherQuery q;
    std::string trimmed = koto_utils::trim(raw_args);

    if (trimmed.empty()) {
        char def_city[256] = {0};
        if (api->db_get && api->db_get(ctx, "default_city", def_city, sizeof(def_city)) > 0 && def_city[0] != '\0') {
            q.location = def_city;
        }
        return q;
    }

    std::string low_full = utf8_tolower(trimmed);

    // Проверка команды установки города: .w set <город> или .w город <город>
    if (low_full.rfind("set ", 0) == 0 || low_full.rfind("город ", 0) == 0) {
        size_t sp = trimmed.find(' ');
        q.is_set_command = true;
        q.set_location = koto_utils::trim(trimmed.substr(sp + 1));
        return q;
    }

    // Распознаём намерения пользователя
    if (low_full.find(" 7") != std::string::npos || low_full.find("7 дней") != std::string::npos ||
        low_full.find("7дней") != std::string::npos || low_full.find("недел") != std::string::npos ||
        low_full.find("week") != std::string::npos) {
        q.want_days = 7;
        q.want_forecast = true;
    } else if (low_full.find("завтра") != std::string::npos || low_full.find("tomorrow") != std::string::npos ||
               low_full.find("+1") != std::string::npos) {
        q.want_tomorrow = true;
    } else if (low_full.find("прогноз") != std::string::npos || low_full.find("forecast") != std::string::npos ||
               low_full.find(" 3") != std::string::npos || low_full.find("3 дня") != std::string::npos) {
        q.want_forecast = true;
    }

    // Очищаем токенами от служебных слов и предлогов
    std::istringstream iss(trimmed);
    std::string token;
    std::vector<std::string> clean_tokens;

    static const std::unordered_set<std::string> stop_words = {
        "погода", "погоду", "погоде", "прогноз", "прогноза", "weather", "forecast",
        "завтра", "tomorrow", "+1",
        "7", "7дней", "неделя", "недели", "неделю", "week",
        "3", "3дня",
        "на", "в", "во", "для", "по"
    };

    while (iss >> token) {
        std::string low_token = utf8_tolower(token);
        while (!low_token.empty() && (low_token.back() == ',' || low_token.back() == '.' || low_token.back() == ':')) {
            low_token.pop_back();
        }
        if (stop_words.count(low_token) == 0) {
            clean_tokens.push_back(token);
        }
    }

    std::string loc;
    for (size_t i = 0; i < clean_tokens.size(); ++i) {
        if (i > 0) loc += " ";
        loc += clean_tokens[i];
    }
    loc = koto_utils::trim(loc);

    if (loc.empty()) {
        char def_city[256] = {0};
        if (api->db_get && api->db_get(ctx, "default_city", def_city, sizeof(def_city)) > 0 && def_city[0] != '\0') {
            loc = def_city;
        } else if (q.want_tomorrow || q.want_forecast) {
            loc = "Москва";
        }
    }

    q.location = loc;
    return q;
}

// ── Основной обработчик погоды ───────────────────────────────────────────────

static void cmd_weather(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? raw : "";

    WeatherQuery q = parse_weather_query(ctx, api, args);

    if (q.is_set_command) {
        if (q.set_location.empty()) {
            api->c_reset(ctx);
            api->c_text(ctx, "❌ ");
            api->c_fmt(ctx, "Укажите город для сохранения: .w set Москва", KOTO_ENT_BOLD);
        } else {
            api->db_set(ctx, "default_city", q.set_location.c_str());
            api->c_reset(ctx);
            api->c_emoji(ctx, "✅", EMO_LOC);
            api->c_fmt(ctx, " Город по умолчанию сохранён: ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, q.set_location.c_str(), KOTO_ENT_CODE);
        }
        send_result(ctx, api);
        return;
    }

    if (q.location.empty()) {
        char pbuf[32] = ".";
        api->setting_get(ctx, "prefix", pbuf, sizeof(pbuf));
        std::string p = pbuf;

        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, "Укажите город.", KOTO_ENT_BOLD);
        api->c_text(ctx, "\nПримеры:");
        api->c_text(ctx, "\n• "); api->c_fmt(ctx, (p + "weather Москва").c_str(), KOTO_ENT_CODE); api->c_text(ctx, " — текущая погода и прогноз");
        api->c_text(ctx, "\n• "); api->c_fmt(ctx, (p + "w Москва завтра").c_str(), KOTO_ENT_CODE); api->c_text(ctx, " — прогноз на завтра");
        api->c_text(ctx, "\n• "); api->c_fmt(ctx, (p + "fc Москва").c_str(), KOTO_ENT_CODE); api->c_text(ctx, " — подробный прогноз на 3 дня");
        api->c_text(ctx, "\n• "); api->c_fmt(ctx, (p + "w Москва 7").c_str(), KOTO_ENT_CODE); api->c_text(ctx, " — прогноз на неделю");
        api->c_text(ctx, "\n• "); api->c_fmt(ctx, (p + "w set Москва").c_str(), KOTO_ENT_CODE); api->c_text(ctx, " — сохранить город по умолчанию");
        send_result(ctx, api);
        return;
    }

    // Прогноз на 7 дней
    if (q.want_days == 7) {
        if (fetch_open_meteo_7days(ctx, api, q.location)) {
            return;
        }
    }

    // Запрос через wttr.in
    std::string encoded_loc = koto_utils::url_encode(q.location);
    std::string url = "https://wttr.in/" + encoded_loc + "?format=j1&lang=ru";
    std::string json_str = koto_utils::http_get(api, ctx, url);

    if (json_str.empty()) {
        if (q.want_tomorrow) {
            if (fetch_open_meteo_tomorrow(ctx, api, q.location)) return;
        }
        if (q.want_forecast) {
            if (fetch_open_meteo_7days(ctx, api, q.location)) return;
        }
        if (fetch_open_meteo_current(ctx, api, q.location)) {
            return;
        }
        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, "Ошибка получения данных о погоде. Проверьте город или сеть.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    try {
        auto j = nlohmann::json::parse(json_str);

        if (q.want_forecast) {
            show_detailed_forecast(ctx, api, j);
            return;
        }
        if (q.want_tomorrow) {
            show_detailed_forecast(ctx, api, j, 1);
            return;
        }

        if (!j.contains("current_condition") || j["current_condition"].empty() ||
            !j.contains("nearest_area") || j["nearest_area"].empty()) {
            if (fetch_open_meteo_current(ctx, api, q.location)) return;
        }

        auto current = j["current_condition"][0];
        auto nearest = j["nearest_area"][0];

        std::string area_name = nearest["areaName"][0].value("value", q.location);
        std::string country = nearest["country"][0].value("value", "");

        std::string desc_text = "Ясно";
        if (current.contains("lang_ru") && !current["lang_ru"].empty()) {
            desc_text = current["lang_ru"][0].value("value", "Ясно");
        } else if (current.contains("weatherDesc") && !current["weatherDesc"].empty()) {
            desc_text = current["weatherDesc"][0].value("value", "Ясно");
        }

        int weather_code = 113;
        if (current.contains("weatherCode")) {
            if (current["weatherCode"].is_number()) weather_code = current["weatherCode"].get<int>();
            else if (current["weatherCode"].is_string()) weather_code = std::atoi(current["weatherCode"].get<std::string>().c_str());
        }

        std::string cond_emoji = get_condition_emoji(weather_code);
        std::string temp_c = format_temp(current.value("temp_C", "0"));
        std::string feels_c = format_temp(current.value("FeelsLikeC", "0"));
        std::string wind_kmph = current.value("windspeedKmph", "0");
        std::string wind_dir = current.value("winddir16Point", "");
        std::string humidity = current.value("humidity", "0");

        api->c_reset(ctx);

        // Город и локация
        api->c_emoji(ctx, "📍", EMO_LOC);
        api->c_fmt(ctx, " Погода в: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (area_name + (!country.empty() ? ", " + country : "") + "\n\n").c_str(), KOTO_ENT_CODE);

        // Состояние
        api->c_text(ctx, (cond_emoji + " ").c_str());
        api->c_fmt(ctx, (desc_text + "\n\n").c_str(), KOTO_ENT_BOLD);

        // Температура
        api->c_emoji(ctx, "🌡️", EMO_TEMP);
        api->c_fmt(ctx, " Температура: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (temp_c + "\n").c_str(), KOTO_ENT_CODE);

        // Ощущается как
        api->c_emoji(ctx, "🥵", EMO_FEELS);
        api->c_fmt(ctx, " Ощущается как: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (feels_c + "\n").c_str(), KOTO_ENT_CODE);

        // Ветер
        api->c_emoji(ctx, "💨", EMO_WIND);
        api->c_fmt(ctx, " Ветер: ", KOTO_ENT_BOLD);
        std::string wind_str = wind_kmph + " км/ч";
        if (!wind_dir.empty()) wind_str += " (" + wind_dir + ")";
        api->c_fmt(ctx, (wind_str + "\n").c_str(), KOTO_ENT_CODE);

        // Влажность
        api->c_emoji(ctx, "💧", EMO_HUMIDITY);
        api->c_fmt(ctx, " Влажность: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (humidity + "%\n\n").c_str(), KOTO_ENT_CODE);

        // Блок прогноза на 3 дня наперёд
        if (j.contains("weather") && !j["weather"].empty()) {
            api->c_emoji(ctx, "📅", EMO_CALENDAR);
            api->c_fmt(ctx, " Прогноз на 3 дня:\n", KOTO_ENT_BOLD);

            const auto& wdays = j["weather"];
            for (size_t i = 0; i < wdays.size() && i < 3; ++i) {
                const auto& wd = wdays[i];
                std::string d_label = format_date_label(static_cast<int>(i), wd.value("date", ""));
                std::string d_min = format_temp(wd.value("mintempC", "0"));
                std::string d_max = format_temp(wd.value("maxtempC", "0"));

                std::string d_desc = "Ясно";
                int d_code = 113;
                std::string d_rain = "0";

                if (wd.contains("hourly") && !wd["hourly"].empty()) {
                    const auto& h_mid = (wd["hourly"].size() > 4) ? wd["hourly"][4] : wd["hourly"][0];
                    if (h_mid.contains("lang_ru") && !h_mid["lang_ru"].empty()) {
                        d_desc = h_mid["lang_ru"][0].value("value", "Ясно");
                    } else if (h_mid.contains("weatherDesc") && !h_mid["weatherDesc"].empty()) {
                        d_desc = h_mid["weatherDesc"][0].value("value", "Ясно");
                    }
                    if (h_mid.contains("weatherCode")) {
                        if (h_mid["weatherCode"].is_number()) d_code = h_mid["weatherCode"].get<int>();
                        else if (h_mid["weatherCode"].is_string()) d_code = std::atoi(h_mid["weatherCode"].get<std::string>().c_str());
                    }
                    d_rain = h_mid.value("chanceofrain", "0");
                }

                std::string d_emoji = get_condition_emoji(d_code);

                api->c_text(ctx, "• ");
                api->c_fmt(ctx, (d_label + ": ").c_str(), KOTO_ENT_BOLD);
                api->c_text(ctx, (d_emoji + " ").c_str());
                api->c_fmt(ctx, (d_min + " ... " + d_max).c_str(), KOTO_ENT_CODE);
                api->c_text(ctx, (" · " + d_desc).c_str());
                if (std::atoi(d_rain.c_str()) > 20) {
                    api->c_text(ctx, (" (" + d_rain + "% осадки)").c_str());
                }
                api->c_text(ctx, "\n");
            }
        }

        send_result(ctx, api);
    } catch (const std::exception& e) {
        if (q.want_tomorrow) {
            if (fetch_open_meteo_tomorrow(ctx, api, q.location)) return;
        }
        if (q.want_forecast) {
            if (fetch_open_meteo_7days(ctx, api, q.location)) return;
        }
        if (fetch_open_meteo_current(ctx, api, q.location)) {
            return;
        }
        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, "Ошибка получения данных о погоде: ", KOTO_ENT_BOLD);
        api->c_text(ctx, e.what());
        send_result(ctx, api);
    }
}

static void cmd_forecast(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? raw : "";

    WeatherQuery q = parse_weather_query(ctx, api, args);

    if (q.location.empty()) {
        char pbuf[32] = ".";
        api->setting_get(ctx, "prefix", pbuf, sizeof(pbuf));
        std::string p = pbuf;

        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, "Укажите город для прогноза.", KOTO_ENT_BOLD);
        api->c_text(ctx, "\nПример: ");
        api->c_fmt(ctx, (p + "forecast Москва").c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, " или ");
        api->c_fmt(ctx, (p + "fc Москва 7").c_str(), KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

    if (q.want_days == 7) {
        if (fetch_open_meteo_7days(ctx, api, q.location)) return;
    }

    std::string encoded_loc = koto_utils::url_encode(q.location);
    std::string url = "https://wttr.in/" + encoded_loc + "?format=j1&lang=ru";
    std::string json_str = koto_utils::http_get(api, ctx, url);

    if (json_str.empty()) {
        if (fetch_open_meteo_7days(ctx, api, q.location)) return;
        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, "Ошибка получения данных о прогнозе погоды. Проверьте город или сеть.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    try {
        auto j = nlohmann::json::parse(json_str);
        show_detailed_forecast(ctx, api, j);
    } catch (const std::exception& e) {
        if (fetch_open_meteo_7days(ctx, api, q.location)) return;
        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, "Ошибка получения прогноза: ", KOTO_ENT_BOLD);
        api->c_text(ctx, e.what());
        send_result(ctx, api);
    }
}

const koto_command COMMANDS[] = {
    {
        "weather",
        &cmd_weather,
        KOTO_LEVEL_TRUSTED,
        "Текущая погода и прогноз наперёд (поддерживает: <город>, <город> 7, <город> завтра, set <город>).",
        "<город>"
    },
    {
        "w",
        &cmd_weather,
        KOTO_LEVEL_TRUSTED,
        "Погода (псевдоним weather).",
        "<город>"
    },
    {
        "forecast",
        &cmd_forecast,
        KOTO_LEVEL_TRUSTED,
        "Подробный прогноз погоды на 3 или 7 дней наперёд.",
        "<город> [7]"
    },
    {
        "fc",
        &cmd_forecast,
        KOTO_LEVEL_TRUSTED,
        "Прогноз погоды (псевдоним forecast).",
        "<город> [7]"
    }
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "weather",
    "Текущая погода и прогноз наперёд через wttr.in и Open-Meteo (C++ ABI 3).",
    "1.1.3",
    26,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    nullptr, 0,
    nullptr
};

} // namespace

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
