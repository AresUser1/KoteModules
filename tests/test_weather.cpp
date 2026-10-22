#include "module_abi.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <map>
#include <string>
#include <vector>

struct koto_ctx {
    std::string args;
    std::string composed;
    std::string mock_http_url;
    std::string mock_http_resp;
    std::map<std::string, std::string> db;
    int edited = 0;
    int replied = 0;
};

static int checks = 0;
#define CHECK(x) do { \
    ++checks; \
    if (!(x)) { \
        std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); \
        std::exit(1); \
    } \
} while(0)

static int write_str(const std::string& value, char* buf, int cap) {
    if (cap > 0) {
        size_t n = std::min(value.size(), size_t(cap - 1));
        std::memcpy(buf, value.data(), n);
        buf[n] = '\0';
    }
    return (int)value.size();
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    CHECK(lib != nullptr);

    auto load = reinterpret_cast<const koto_module*(*)()>(dlsym(lib, "koto_module_register"));
    CHECK(load != nullptr);
    const koto_module* mod = load();
    CHECK(mod != nullptr);

    CHECK(std::string(mod->name) == "weather");
    CHECK(std::string(mod->version) == "1.1.3");
    CHECK(mod->command_count == 4);
    CHECK(std::string(mod->commands[0].name) == "weather");
    CHECK(std::string(mod->commands[1].name) == "w");
    CHECK(std::string(mod->commands[2].name) == "forecast");
    CHECK(std::string(mod->commands[3].name) == "fc");

    koto_api api{};
    api.cmd_args = [](koto_ctx* c) -> const char* { return c->args.c_str(); };
    api.is_outgoing = [](koto_ctx*) -> int { return 1; };
    api.check_permission = [](koto_ctx*, int) -> int { return 1; };
    api.setting_get = [](koto_ctx*, const char*, char* b, int n) -> int { return write_str(".", b, n); };

    api.db_get = [](koto_ctx* c, const char* k, char* buf, int buflen) -> int {
        auto it = c->db.find(k ? k : "");
        if (it == c->db.end()) return 0;
        int to_copy = std::min((int)it->second.size(), buflen - 1);
        std::memcpy(buf, it->second.data(), to_copy);
        buf[to_copy] = '\0';
        return to_copy;
    };
    api.db_set = [](koto_ctx* c, const char* k, const char* val) {
        c->db[k ? k : ""] = val ? val : "";
    };

    api.c_reset = [](koto_ctx* c) { c->composed.clear(); };
    api.c_text = [](koto_ctx* c, const char* s) { if (s) c->composed += s; };
    api.c_fmt = [](koto_ctx* c, const char* s, int) { if (s) c->composed += s; };
    api.c_emoji = [](koto_ctx* c, const char* s, long long) { if (s) c->composed += s; };
    api.c_edit = [](koto_ctx* c, int) { c->edited++; };
    api.c_reply = [](koto_ctx* c, int) { c->replied++; };

    api.http_get = [](koto_ctx* c, const char* url, unsigned char* buf, int buflen) -> long long {
        c->mock_http_url = url ? url : "";
        if (c->mock_http_resp.empty()) return -1;
        int n = std::min((int)c->mock_http_resp.size(), buflen);
        std::memcpy(buf, c->mock_http_resp.data(), n);
        return n;
    };

    koto_ctx ctx;

    // 1. Пустые аргументы без default_city
    ctx.args = "";
    mod->commands[0].handler(&ctx, &api);
    CHECK(ctx.composed.find("Укажите город") != std::string::npos);
    CHECK(ctx.composed.find(".w Москва 7") != std::string::npos);

    // 2. Тестовый ответ wttr.in на 3 дня
    std::string wttr_json = R"({
      "current_condition": [{
        "temp_C": "12",
        "FeelsLikeC": "10",
        "weatherCode": "113",
        "weatherDesc": [{"value": "Sunny"}],
        "lang_ru": [{"value": "Солнечно"}],
        "windspeedKmph": "15",
        "winddir16Point": "W",
        "humidity": "55"
      }],
      "nearest_area": [{
        "areaName": [{"value": "Москва"}],
        "country": [{"value": "Россия"}]
      }],
      "weather": [
        {
          "date": "2026-10-21",
          "maxtempC": "14",
          "mintempC": "5",
          "hourly": [
            {"tempC": "5", "FeelsLikeC": "3", "weatherCode": "113", "lang_ru": [{"value": "Ясно"}], "windspeedKmph": "8", "winddir16Point": "W", "humidity": "70", "chanceofrain": "0"},
            {}, {}, {},
            {"tempC": "14", "FeelsLikeC": "12", "weatherCode": "113", "lang_ru": [{"value": "Солнечно"}], "windspeedKmph": "15", "winddir16Point": "W", "humidity": "50", "chanceofrain": "5"}
          ]
        },
        {
          "date": "2026-10-22",
          "maxtempC": "11",
          "mintempC": "6",
          "hourly": [
            {"tempC": "6", "FeelsLikeC": "4", "weatherCode": "119", "lang_ru": [{"value": "Пасмурно"}], "windspeedKmph": "10", "winddir16Point": "NW", "humidity": "75", "chanceofrain": "10"},
            {}, {}, {},
            {"tempC": "11", "FeelsLikeC": "9", "weatherCode": "116", "lang_ru": [{"value": "Облачно"}], "windspeedKmph": "12", "winddir16Point": "NW", "humidity": "60", "chanceofrain": "15"}
          ]
        },
        {
          "date": "2026-10-23",
          "maxtempC": "9",
          "mintempC": "4",
          "hourly": [
            {"tempC": "4", "FeelsLikeC": "1", "weatherCode": "296", "lang_ru": [{"value": "Слабый дождь"}], "windspeedKmph": "16", "winddir16Point": "N", "humidity": "85", "chanceofrain": "70"},
            {}, {}, {},
            {"tempC": "9", "FeelsLikeC": "6", "weatherCode": "296", "lang_ru": [{"value": "Небольшой дождь"}], "windspeedKmph": "18", "winddir16Point": "N", "humidity": "80", "chanceofrain": "65"}
          ]
        }
      ]
    })";

    ctx.mock_http_resp = wttr_json;
    ctx.args = "Москва";
    mod->commands[0].handler(&ctx, &api);

    // Проверяем наличие текущей погоды и 3-дневного прогноза наперёд
    CHECK(ctx.composed.find("Погода в: Москва, Россия") != std::string::npos);
    CHECK(ctx.composed.find("+12°C") != std::string::npos);
    CHECK(ctx.composed.find("Прогноз на 3 дня:") != std::string::npos);
    CHECK(ctx.composed.find("Сегодня (21.10, Ср):") != std::string::npos);
    CHECK(ctx.composed.find("+5°C ... +14°C") != std::string::npos);
    CHECK(ctx.composed.find("Завтра (22.10, Чт):") != std::string::npos);
    CHECK(ctx.composed.find("+6°C ... +11°C") != std::string::npos);
    CHECK(ctx.composed.find("Послезавтра (23.10, Пт):") != std::string::npos);
    CHECK(ctx.composed.find("65% осадки") != std::string::npos);

    // 3. Проверка команды .fc (подробный прогноз на 3 дня)
    ctx.args = "Москва";
    mod->commands[3].handler(&ctx, &api); // fc
    CHECK(ctx.composed.find("Прогноз погоды на 3 дня: Москва, Россия") != std::string::npos);
    CHECK(ctx.composed.find("Днём:") != std::string::npos);
    CHECK(ctx.composed.find("Ночью:") != std::string::npos);
    CHECK(ctx.composed.find("Завтра") != std::string::npos);
    CHECK(ctx.composed.find("Послезавтра") != std::string::npos);

    // 4. Проверка прогноза на завтра (.w Москва завтра)
    ctx.args = "Москва завтра";
    mod->commands[1].handler(&ctx, &api); // w
    CHECK(ctx.composed.find("Прогноз на завтра: Москва, Россия") != std::string::npos);
    CHECK(ctx.composed.find("Завтра (22.10, Чт):") != std::string::npos);

    // 4b. Проверка в нижнем регистре кириллицы (москва завтра)
    ctx.args = "москва завтра";
    ctx.composed.clear();
    mod->commands[1].handler(&ctx, &api);
    CHECK(ctx.composed.find("Прогноз на завтра: Москва, Россия") != std::string::npos);
    CHECK(ctx.composed.find("Завтра (22.10, Чт):") != std::string::npos);

    // 4c. Проверка фразы "Москва на завтра"
    ctx.args = "Москва на завтра";
    ctx.composed.clear();
    mod->commands[1].handler(&ctx, &api);
    CHECK(ctx.composed.find("Прогноз на завтра: Москва, Россия") != std::string::npos);
    CHECK(ctx.composed.find("Завтра (22.10, Чт):") != std::string::npos);

    // 4d. Проверка фразы "погода на завтра" (без указания города — автоподстановка Москвы)
    ctx.args = "погода на завтра";
    ctx.composed.clear();
    mod->commands[1].handler(&ctx, &api);
    CHECK(ctx.composed.find("Прогноз на завтра: Москва, Россия") != std::string::npos);
    CHECK(ctx.composed.find("Завтра (22.10, Чт):") != std::string::npos);

    // 4e. Проверка запроса на неделю (.w Москва 7)
    ctx.args = "Москва 7";
    ctx.composed.clear();
    mod->commands[1].handler(&ctx, &api);
    CHECK(!ctx.composed.empty());

    // 4f. Проверка сохранения города по умолчанию (.w set Воронеж)
    ctx.args = "set Воронеж";
    ctx.composed.clear();
    mod->commands[1].handler(&ctx, &api);
    CHECK(ctx.composed.find("Город по умолчанию сохранён: Воронеж") != std::string::npos);
    CHECK(ctx.db["default_city"] == "Воронеж");

    // 5. Проверка автоматического fallback на Open-Meteo при сбое wttr.in
    api.http_get = [](koto_ctx*, const char* url, unsigned char* buf, int buflen) -> long long {
        std::string s_url = url ? url : "";
        if (s_url.find("wttr.in") != std::string::npos) {
            std::string err = "<html>503 Service Unavailable</html>";
            if (buf && buflen > 0) {
                int to_copy = std::min((int)err.size(), buflen);
                std::memcpy(buf, err.data(), to_copy);
                return to_copy;
            }
            return (long long)err.size();
        }
        if (s_url.find("geocoding-api.open-meteo.com") != std::string::npos) {
            std::string res = "{\"results\":[{\"name\":\"Москва\",\"country\":\"Россия\",\"latitude\":55.75,\"longitude\":37.61}]}";
            if (buf && buflen > 0) {
                int to_copy = std::min((int)res.size(), buflen);
                std::memcpy(buf, res.data(), to_copy);
                return to_copy;
            }
        }
        if (s_url.find("api.open-meteo.com") != std::string::npos) {
            std::string res = "{\"current\":{\"temperature_2m\":10.5,\"apparent_temperature\":9.2,\"relative_humidity_2m\":45.0,\"wind_speed_10m\":12.0,\"weather_code\":1},"
                              "\"daily\":{\"time\":[\"2026-10-21\",\"2026-10-22\"],\"temperature_2m_max\":[12.0,11.0],\"temperature_2m_min\":[6.0,5.0],\"weather_code\":[1,2],\"precipitation_probability_max\":[10,20]}}";
            if (buf && buflen > 0) {
                int to_copy = std::min((int)res.size(), buflen);
                std::memcpy(buf, res.data(), to_copy);
                return to_copy;
            }
        }
        return 0;
    };
    ctx.args = "Москва";
    ctx.composed.clear();
    mod->commands[1].handler(&ctx, &api); // w
    CHECK(ctx.composed.find("Погода в: Москва, Россия") != std::string::npos);
    CHECK(ctx.composed.find("Температура:") != std::string::npos);
    CHECK(ctx.composed.find("+11°C") != std::string::npos);
    CHECK(ctx.composed.find("Прогноз на 3 дня:") != std::string::npos);

    // 5b. Fallback на Open-Meteo для прогноза на завтра
    ctx.args = "Москва завтра";
    ctx.composed.clear();
    mod->commands[1].handler(&ctx, &api);
    CHECK(ctx.composed.find("Прогноз на завтра: Москва, Россия") != std::string::npos);
    CHECK(ctx.composed.find("Завтра (22.10, Чт):") != std::string::npos);

    dlclose(lib);
    std::printf("Weather tests: %d checks passed successfully!\n", checks);
    return 0;
}
