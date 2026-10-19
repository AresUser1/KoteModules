// weather.cpp — Модуль погоды через wttr.in (C++ ABI 3).
// Портирован из Python-модуля weather.py (v1.0.2).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>

namespace {

static constexpr long long EMO_MAIN     = 5283097055852503586LL; // 🌦️
static constexpr long long EMO_LOC      = 5778661935927004845LL; // 📍
static constexpr long long EMO_TEMP     = 5470049770997292425LL; // 🌡️
static constexpr long long EMO_FEELS    = 5373067498193166118LL; // 🥵
static constexpr long long EMO_WIND     = 5458875859525970449LL; // 💨
static constexpr long long EMO_HUMIDITY = 6050944866580435869LL; // 💧
static constexpr long long EMO_SUN      = 5469947168523558652LL; // ☀️
static constexpr long long EMO_MOON     = 5465643984955120548LL; // 🌙

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
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

static void cmd_weather(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string location = raw ? koto_utils::trim(raw) : "";

    if (location.empty()) {
        char pbuf[32] = ".";
        api->setting_get(ctx, "prefix", pbuf, sizeof(pbuf));
        std::string p = pbuf;

        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, "Укажите город.", KOTO_ENT_BOLD);
        api->c_text(ctx, "\nПример: ");
        api->c_fmt(ctx, (p + "weather Москва").c_str(), KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

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

        auto current = j["current_condition"][0];
        auto today = j["weather"][0];
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
        std::string temp_c = current.value("temp_C", "0");
        std::string feels_c = current.value("FeelsLikeC", "0");
        std::string wind_kmph = current.value("windspeedKmph", "0");
        std::string wind_dir = current.value("winddir16Point", "");
        std::string humidity = current.value("humidity", "0");
        std::string max_c = today.value("maxtempC", "0");
        std::string min_c = today.value("mintempC", "0");

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
        api->c_fmt(ctx, (temp_c + "°C\n").c_str(), KOTO_ENT_CODE);

        // Ощущается как
        api->c_emoji(ctx, "🥵", EMO_FEELS);
        api->c_fmt(ctx, " Ощущается как: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (feels_c + "°C\n").c_str(), KOTO_ENT_CODE);

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

        // Прогноз на сегодня
        api->c_fmt(ctx, "--- Прогноз на сегодня ---\n", KOTO_ENT_ITALIC);
        api->c_emoji(ctx, "☀️", EMO_SUN);
        api->c_fmt(ctx, " Макс: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (max_c + "°C").c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "  /  ");
        api->c_emoji(ctx, "🌙", EMO_MOON);
        api->c_fmt(ctx, " Мин: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (min_c + "°C\n").c_str(), KOTO_ENT_CODE);

        send_result(ctx, api);
    } catch (const std::exception& e) {
        api->c_reset(ctx);
        api->c_text(ctx, "❌ ");
        api->c_fmt(ctx, "Ошибка парсинга ответа погоды: ", KOTO_ENT_BOLD);
        api->c_text(ctx, e.what());
        send_result(ctx, api);
    }
}

const koto_command COMMANDS[] = {
    {
        "weather",
        &cmd_weather,
        KOTO_LEVEL_TRUSTED,
        "Показывает текущую погоду и прогноз в указанном городе.",
        "<город>"
    },
    {
        "w",
        &cmd_weather,
        KOTO_LEVEL_TRUSTED,
        "Погода (псевдоним weather).",
        "<город>"
    }
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "weather",
    "Текущая погода и прогноз через wttr.in (C++ ABI 3).",
    "1.0.2",
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
