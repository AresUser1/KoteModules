// weather.cpp — Модуль текущей погоды и прогноза наперёд (C++ ABI 3).
// Использует wttr.in (текущая погода + прогноз на 3 дня с разбивкой по времени)
// и Open-Meteo (расширенный прогноз на 7 дней).
// Команды: .weather / .w (текущая + прогноз), .forecast / .fc / .прогноз (подробный прогноз наперёд).
// Поддержка параметров: .w Москва, .w Москва завтра, .w Москва 7, .fc Москва 7.
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <cmath>
#include <sstream>
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

        static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
        int sy = y - (m < 3);
        int dow = (sy + sy/4 - sy/100 + sy/400 + t[m-1] + d) % 7;
        static const char* dows[] = { "Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб" };
        if (dow >= 0 && dow < 7) dow_str = dows[dow];
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
        auto codes = daily.value("weathercode", std::vector<int>{});
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

// ── Подробный прогноз на 3 дня (wttr.in) ──────────────────────────────────────

static void show_detailed_forecast(koto_ctx* ctx, const koto_api* api, const nlohmann::json& j, int single_day = -1) {
    auto nearest = j["nearest_area"][0];
    std::string area_name = nearest["areaName"][0]["value"].get<std::string>();
    std::string country = nearest["country"][0]["value"].get<std::string>();

    api->c_reset(ctx);
    api->c_emoji(ctx, "📍", EMO_LOC);
    if (single_day == 1) {
        api->c_fmt(ctx, " Прогноз на завтра: ", KOTO_ENT_BOLD);
    } else {
        api->c_fmt(ctx, " Прогноз погоды на 3 дня: ", KOTO_ENT_BOLD);
    }
    api->c_fmt(ctx, (area_name + ", " + country + "\n\n").c_str(), KOTO_ENT_CODE);

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
            // Дневной интервал (12:00 / 15:00)
            const auto& noon = (hourly.size() > 4) ? hourly[4] : hourly[0];
            std::string noon_desc = "Ясно";
            if (noon.contains("lang_ru") && !noon["lang_ru"].empty()) {
                noon_desc = noon["lang_ru"][0]["value"].get<std::string>();
            } else if (noon.contains("weatherDesc") && !noon["weatherDesc"].empty()) {
                noon_desc = noon["weatherDesc"][0]["value"].get<std::string>();
            }
            int noon_code = 113;
            if (noon.contains("weatherCode")) noon_code = std::atoi(noon["weatherCode"].get<std::string>().c_str());
            std::string noon_emoji = get_condition_emoji(noon_code);
            std::string noon_temp = format_temp(noon.value("tempC", "0"));
            std::string noon_feels = format_temp(noon.value("FeelsLikeC", "0"));
            std::string noon_rain = noon.value("chanceofrain", "0");

            // Ночной/утренний интервал (00:00 / 06:00)
            const auto& night = hourly[0];
            std::string night_desc = "Ясно";
            if (night.contains("lang_ru") && !night["lang_ru"].empty()) {
                night_desc = night["lang_ru"][0]["value"].get<std::string>();
            } else if (night.contains("weatherDesc") && !night["weatherDesc"].empty()) {
                night_desc = night["weatherDesc"][0]["value"].get<std::string>();
            }
            int night_code = 113;
            if (night.contains("weatherCode")) night_code = std::atoi(night["weatherCode"].get<std::string>().c_str());
            std::string night_emoji = get_condition_emoji(night_code);
            std::string night_temp = format_temp(night.value("tempC", "0"));
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

    if (single_day < 0) {
        api->c_fmt(ctx, "💡 Прогноз на 7 дней: ", KOTO_ENT_ITALIC);
        api->c_fmt(ctx, (".w " + area_name + " 7\n").c_str(), KOTO_ENT_CODE);
    }

    send_result(ctx, api);
}

// ── Основной обработчик погоды ───────────────────────────────────────────────

static void cmd_weather(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? koto_utils::trim(raw) : "";

    if (args.empty()) {
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
        send_result(ctx, api);
        return;
    }

    // Разбираем параметры (город и опции прогноза: "7", "завтра", "прогноз", "fc", "+1")
    std::string location = args;
    bool want_forecast_only = false;
    bool want_tomorrow_only = false;
    int want_days = 3;

    std::string args_lower = args;
    for (char& c : args_lower) c = (char)tolower((unsigned char)c);

    if (args_lower.find(" 7") != std::string::npos || args_lower.find("7 дней") != std::string::npos ||
        args_lower.find("неделя") != std::string::npos || args_lower.find("week") != std::string::npos) {
        want_days = 7;
        want_forecast_only = true;
        // Убираем маркеры из города
        size_t p = args.rfind(" 7");
        if (p != std::string::npos) location = koto_utils::trim(args.substr(0, p));
    } else if (args_lower.find("завтра") != std::string::npos || args_lower.find("tomorrow") != std::string::npos ||
               args_lower.find("+1") != std::string::npos) {
        want_tomorrow_only = true;
        size_t p = args_lower.rfind("завтра");
        if (p == std::string::npos) p = args_lower.rfind("tomorrow");
        if (p == std::string::npos) p = args_lower.rfind("+1");
        if (p != std::string::npos && p > 0) location = koto_utils::trim(args.substr(0, p));
    } else if (args_lower.find("прогноз") != std::string::npos || args_lower.find("forecast") != std::string::npos ||
               args_lower.find(" 3") != std::string::npos) {
        want_forecast_only = true;
        size_t p = args_lower.rfind("прогноз");
        if (p == std::string::npos) p = args_lower.rfind("forecast");
        if (p == std::string::npos) p = args_lower.rfind(" 3");
        if (p != std::string::npos && p > 0) location = koto_utils::trim(args.substr(0, p));
    }

    // Если запрошен прогноз на 7 дней — пробуем Open-Meteo
    if (want_days == 7) {
        api->c_reset(ctx);
        api->c_text(ctx, "⏳ ");
        api->c_fmt(ctx, "Запрашиваю прогноз на 7 дней для ", KOTO_ENT_ITALIC);
        api->c_fmt(ctx, location.c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, "...", KOTO_ENT_ITALIC);
        send_result(ctx, api);

        if (fetch_open_meteo_7days(ctx, api, location)) {
            return;
        }
        // Если Open-Meteo не ответил, откатываемся к wttr.in
    }

    // Запрос через wttr.in
    api->c_reset(ctx);
    api->c_text(ctx, "⏳ ");
    api->c_fmt(ctx, "Запрашиваю погоду для ", KOTO_ENT_ITALIC);
    api->c_fmt(ctx, location.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, "...", KOTO_ENT_ITALIC);
    send_result(ctx, api);

    std::string encoded_loc = koto_utils::url_encode(location);
    std::string url = "https://wttr.in/" + encoded_loc + "?format=j1&lang=ru";

    std::string json_str = koto_utils::http_get(api, ctx, url);
    if (json_str.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, "Ошибка получения данных wttr.in. Проверьте город или сеть.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    try {
        auto j = nlohmann::json::parse(json_str);

        // Если запрошен только подробный прогноз
        if (want_forecast_only) {
            show_detailed_forecast(ctx, api, j);
            return;
        }
        if (want_tomorrow_only) {
            show_detailed_forecast(ctx, api, j, 1);
            return;
        }

        auto current = j["current_condition"][0];
        auto nearest = j["nearest_area"][0];

        std::string area_name = nearest["areaName"][0]["value"].get<std::string>();
        std::string country = nearest["country"][0]["value"].get<std::string>();

        std::string desc_text = "Ясно";
        if (current.contains("lang_ru") && !current["lang_ru"].empty()) {
            desc_text = current["lang_ru"][0]["value"].get<std::string>();
        } else if (current.contains("weatherDesc") && !current["weatherDesc"].empty()) {
            desc_text = current["weatherDesc"][0]["value"].get<std::string>();
        }

        int weather_code = 113;
        if (current.contains("weatherCode")) {
            std::string code_s = current["weatherCode"].get<std::string>();
            weather_code = std::atoi(code_s.c_str());
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
        api->c_fmt(ctx, (area_name + ", " + country + "\n\n").c_str(), KOTO_ENT_CODE);

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
                        d_desc = h_mid["lang_ru"][0]["value"].get<std::string>();
                    } else if (h_mid.contains("weatherDesc") && !h_mid["weatherDesc"].empty()) {
                        d_desc = h_mid["weatherDesc"][0]["value"].get<std::string>();
                    }
                    if (h_mid.contains("weatherCode")) {
                        d_code = std::atoi(h_mid["weatherCode"].get<std::string>().c_str());
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
        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, "Ошибка парсинга ответа погоды: ", KOTO_ENT_BOLD);
        api->c_text(ctx, e.what());
        send_result(ctx, api);
    }
}

static void cmd_forecast(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? koto_utils::trim(raw) : "";

    if (args.empty()) {
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

    // Если запрошен прогноз на 7 дней
    if (args.find(" 7") != std::string::npos || args.find("недел") != std::string::npos) {
        std::string loc = args;
        size_t p = loc.rfind(" 7");
        if (p != std::string::npos) loc = koto_utils::trim(loc.substr(0, p));

        api->c_reset(ctx);
        api->c_text(ctx, "⏳ ");
        api->c_fmt(ctx, "Запрашиваю прогноз на 7 дней для ", KOTO_ENT_ITALIC);
        api->c_fmt(ctx, loc.c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, "...", KOTO_ENT_ITALIC);
        send_result(ctx, api);

        if (fetch_open_meteo_7days(ctx, api, loc)) return;
    }

    // Подробный 3-дневный прогноз через wttr.in
    api->c_reset(ctx);
    api->c_text(ctx, "⏳ ");
    api->c_fmt(ctx, "Запрашиваю подробный прогноз для ", KOTO_ENT_ITALIC);
    api->c_fmt(ctx, args.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, "...", KOTO_ENT_ITALIC);
    send_result(ctx, api);

    std::string encoded_loc = koto_utils::url_encode(args);
    std::string url = "https://wttr.in/" + encoded_loc + "?format=j1&lang=ru";
    std::string json_str = koto_utils::http_get(api, ctx, url);

    if (json_str.empty()) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, "Ошибка получения данных wttr.in. Проверьте город или сеть.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    try {
        auto j = nlohmann::json::parse(json_str);
        show_detailed_forecast(ctx, api, j);
    } catch (const std::exception& e) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, "Ошибка парсинга прогноза: ", KOTO_ENT_BOLD);
        api->c_text(ctx, e.what());
        send_result(ctx, api);
    }
}

const koto_command COMMANDS[] = {
    {
        "weather",
        &cmd_weather,
        KOTO_LEVEL_TRUSTED,
        "Текущая погода и прогноз наперёд (поддерживает: <город>, <город> 7, <город> завтра).",
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
    "1.1.0",
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
