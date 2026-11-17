// gift_stats.cpp
// Статистика подарков Telegram (обычные, лимитированные, NFT, общая стоимость, транзакции Stars).
// Порт модуля GiftStats.py (by Killu / @dubai_ip) на Kotogram C++ ABI 3 (Host 112+).
//
// Использование:
//   .giftstat                  — статистика своих подарков + баланс и транзакции Stars
//   .giftstat @username / ID   — статистика подарков указанного пользователя
//   .giftstat (в реплай)       — статистика подарков автора сообщения

#include "module_abi.h"
#include "koto_services.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cctype>

using json = nlohmann::json;

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static void reply_or_edit(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing && api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else if (api->c_reply) api->c_reply(ctx, 0);
    else api->c_edit(ctx, 0);
}

static std::string format_stars_num(double val) {
    double r = std::round(val * 100.0) / 100.0;
    if (std::fabs(r - std::round(r)) < 1e-9) {
        return std::to_string(static_cast<long long>(std::llround(r)));
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << r;
    std::string s = oss.str();
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

static std::string resolve_display_name(koto_ctx* ctx, const koto_api* api, long long uid, const std::string& fallback) {
    if (uid == 0) return fallback.empty() ? "пользователь" : fallback;
    char buf[256] = {0};
    if (api->user_first_name) {
        int n = api->user_first_name(ctx, uid, buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = '\0'; std::string s = trim(buf); if (!s.empty()) return s; }
    }
    if (api->user_username) {
        int n = api->user_username(ctx, uid, buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = '\0'; std::string s = trim(buf); if (!s.empty()) return s; }
    }
    if (api->user_name) {
        int n = api->user_name(ctx, uid, buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = '\0'; std::string s = trim(buf); if (!s.empty()) return s; }
    }
    return fallback.empty() ? std::to_string(uid) : fallback;
}

static void cmd_giftstat(koto_ctx* ctx, const koto_api* api) {
    if (!koto::services::available(ctx, api, 112)) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "\xE2\x9D\x8C", 5980869107891838347LL);
        api->c_text(ctx, " Требуется обновление приложения Kotogram (Host >= 112) для работы GiftStats.");
        reply_or_edit(ctx, api);
        return;
    }

    // Моментальный статус
    api->c_reset(ctx);
    api->c_emoji(ctx, "\xF0\x9F\x94\xB4", 5328311576736833844LL);
    api->c_text(ctx, " Получаю статистику подарков...");
    reply_or_edit(ctx, api);

    const char* raw_args = api->cmd_args ? api->cmd_args(ctx) : nullptr;
    std::string args = trim(raw_args ? raw_args : "");
    long long me_id = api->get_me ? api->get_me(ctx) : 0;

    bool is_self = false;
    long long target_uid = 0;
    std::string target_uname;
    std::string display_name;

    if (!args.empty()) {
        // Берем первый токен
        size_t sp = args.find_first_of(" \t\r\n");
        std::string tok = (sp == std::string::npos) ? args : args.substr(0, sp);
        bool is_num = !tok.empty();
        size_t start = (tok[0] == '-') ? 1 : 0;
        if (start == tok.size()) is_num = false;
        for (size_t i = start; i < tok.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(tok[i]))) { is_num = false; break; }
        }
        if (is_num) {
            try { target_uid = std::stoll(tok); } catch (...) { target_uid = 0; }
            is_self = (target_uid != 0 && target_uid == me_id);
            display_name = resolve_display_name(ctx, api, target_uid, tok);
        } else {
            target_uname = tok;
            if (!target_uname.empty() && target_uname[0] == '@') target_uname = target_uname.substr(1);
            if (api->resolve) target_uid = api->resolve(ctx, target_uname.c_str());
            is_self = (target_uid != 0 && target_uid == me_id);
            display_name = resolve_display_name(ctx, api, target_uid, "@" + target_uname);
        }
    } else if (api->reply_sender_id && api->reply_sender_id(ctx) != 0) {
        target_uid = api->reply_sender_id(ctx);
        is_self = (target_uid == me_id);
        display_name = resolve_display_name(ctx, api, target_uid, std::to_string(target_uid));
    } else {
        is_self = true;
        target_uid = me_id;
        char mbuf[256] = {0};
        if (api->get_me_name) {
            int n = api->get_me_name(ctx, mbuf, sizeof(mbuf) - 1);
            if (n > 0) { mbuf[n] = '\0'; display_name = trim(mbuf); }
        }
        if (display_name.empty()) display_name = resolve_display_name(ctx, api, me_id, "вы");
    }

    // Собираем все сохранённые подарки постранично
    std::vector<json> all_gifts;
    std::string offset;
    bool api_ok = true;
    std::string err_msg;

    for (int page = 0; page < 50; ++page) {
        json req = {{"offset", offset}, {"limit", 100}};
        if (is_self) req["self"] = true;
        else if (!target_uname.empty()) req["username"] = target_uname;
        else if (target_uid != 0) req["user_id"] = target_uid;
        else req["self"] = true;

        std::string res_str = koto::services::stargifts_saved(ctx, api, req.dump());
        json res = json::parse(res_str, nullptr, false);
        if (!res.is_object() || !res.value("ok", false)) {
            if (page == 0) {
                api_ok = false;
                err_msg = res.is_object() ? res.value("error", "не удалось получить подарки") : "некорректный ответ";
            }
            break;
        }
        if (res.value("is_self", false)) is_self = true;
        if (!res.contains("gifts") || !res["gifts"].is_array()) break;

        const auto& batch = res["gifts"];
        for (const auto& g : batch) all_gifts.push_back(g);

        offset = res.value("next_offset", "");
        if (offset.empty() || batch.size() < 100) break;
    }

    if (!api_ok) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "\xE2\x9D\x8C", 5980869107891838347LL);
        api->c_text(ctx, (" Ошибка при получении данных: " + err_msg).c_str());
        reply_or_edit(ctx, api);
        return;
    }

    if (all_gifts.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "\xE2\x9D\x8C", 5980869107891838347LL);
        api->c_text(ctx, (" У пользователя " + display_name + " нет подарков.").c_str());
        reply_or_edit(ctx, api);
        return;
    }

    int regular = 0, limited = 0, nft = 0, hidden = 0, visible = 0;
    long long max_regular = 0, max_limited = 0, total_stars = 0;
    std::vector<std::string> nft_slugs;
    std::map<std::string, long long> nft_totals;

    for (const auto& g : all_gifts) {
        bool is_nft = g.value("is_nft", false);
        bool is_lim = g.value("limited", false);
        long long stars = g.value("stars", 0LL);
        bool unsaved = g.value("unsaved", false);

        total_stars += stars;
        if (is_self) {
            if (unsaved) hidden++;
            else visible++;
        }

        if (is_nft) {
            nft++;
            long long val_amt = g.value("value_amount", 0LL);
            std::string val_cur = g.value("value_currency", "");
            if (val_amt > 0 && !val_cur.empty()) {
                nft_totals[val_cur] += val_amt;
            } else {
                std::string slug = g.value("slug", "");
                if (!slug.empty()) nft_slugs.push_back(slug);
            }
        }

        if (is_lim) {
            limited++;
            if (stars > max_limited) max_limited = stars;
        } else if (!is_nft) {
            regular++;
            if (stars > max_regular) max_regular = stars;
        }
    }

    // Дозапрашиваем оценку для NFT, у которых value_amount не пришёл сразу (лимит 40 запросов для защиты от FloodWait)
    size_t query_lim = std::min<size_t>(40, nft_slugs.size());
    for (size_t i = 0; i < query_lim; ++i) {
        std::string vres = koto::services::stargift_value(ctx, api, nft_slugs[i]);
        json vj = json::parse(vres, nullptr, false);
        if (vj.is_object() && vj.value("ok", false)) {
            std::string cur = vj.value("currency", "");
            long long val = vj.value("value", 0LL);
            if (!cur.empty() && val > 0) nft_totals[cur] += val;
        }
    }

    std::string nft_value_str;
    if (nft == 0) {
        nft_value_str = "нету NFT";
    } else if (nft_totals.empty()) {
        nft_value_str = "нету данных о ценах";
    } else {
        for (const auto& kv : nft_totals) {
            if (!nft_value_str.empty()) nft_value_str += ", ";
            nft_value_str += std::to_string(kv.second / 100) + " " + kv.first;
        }
    }

    int total = regular + limited + nft;

    // Сборка финального сообщения в стиле оригинала
    api->c_reset(ctx);
    api->c_emoji(ctx, "\xF0\x9F\x8E\x81", 6037175527846975726LL);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, ("Статистика подарков " + display_name).c_str(), KOTO_ENT_BOLD);
    api->c_text(ctx, ":\n");

    api->c_text(ctx, "\xE2\x80\xA2 ");
    api->c_emoji(ctx, "\xE2\x84\xB9\xEF\xB8\x8F", 6028435952299413210LL);
    api->c_text(ctx, (" Всего подарков: " + std::to_string(total) + "\n").c_str());

    api->c_text(ctx, "\xE2\x80\xA2 ");
    api->c_emoji(ctx, "\xF0\x9F\x96\x8C", 6050847684355428245LL);
    api->c_text(ctx, (" Обычных подарков: " + std::to_string(regular) + "\n").c_str());

    api->c_text(ctx, "\xE2\x80\xA2 ");
    api->c_emoji(ctx, "\xF0\x9F\x96\x8C", 5811925731785052842LL);
    api->c_text(ctx, (" Лимитированных: " + std::to_string(limited) + "\n").c_str());

    api->c_text(ctx, "\xE2\x80\xA2 ");
    api->c_emoji(ctx, "\xF0\x9F\x8F\xB7", 5890727932011223292LL);
    api->c_text(ctx, (" Самый дорогой обычный: " + (max_regular > 0 ? std::to_string(max_regular) : std::string("нету")) + " ").c_str());
    api->c_emoji(ctx, "\xE2\xAD\x90\xEF\xB8\x8F", 5924870095925942277LL);
    api->c_text(ctx, "\n");

    api->c_text(ctx, "\xE2\x80\xA2 ");
    api->c_emoji(ctx, "\xF0\x9F\x8F\xB7", 5890883384057533697LL);
    api->c_text(ctx, (" Самый дорогой\n   лимитированный: " + (max_limited > 0 ? std::to_string(max_limited) : std::string("нету")) + " ").c_str());
    api->c_emoji(ctx, "\xE2\xAD\x90\xEF\xB8\x8F", 5924870095925942277LL);
    api->c_text(ctx, "\n");

    api->c_text(ctx, "\xE2\x80\xA2 ");
    api->c_emoji(ctx, "\xE2\x9C\xA8", 5890925363067886150LL);
    api->c_text(ctx, (" NFT подарков: " + std::to_string(nft) + "\n").c_str());

    if (is_self) {
        api->c_text(ctx, "\xE2\x80\xA2 ");
        api->c_emoji(ctx, "\xF0\x9F\x91\x81", 6037243349675544634LL);
        api->c_text(ctx, (" Скрытых: " + std::to_string(hidden) + "\n").c_str());

        api->c_text(ctx, "\xE2\x80\xA2 ");
        api->c_emoji(ctx, "\xF0\x9F\x91\x81", 6037397706505195857LL);
        api->c_text(ctx, (" Открытых: " + std::to_string(visible) + "\n").c_str());
    }

    api->c_text(ctx, "\xE2\x80\xA2 ");
    api->c_emoji(ctx, "\xF0\x9F\x91\xA5", 5879905000972358125LL);
    api->c_text(ctx, (" Общая стоимость подарков\n   за звезды: " + std::to_string(total_stars) + " ").c_str());
    api->c_emoji(ctx, "\xE2\xAD\x90\xEF\xB8\x8F", 5924870095925942277LL);
    api->c_text(ctx, "\n");

    api->c_text(ctx, "\xE2\x80\xA2 ");
    api->c_emoji(ctx, "\xF0\x9F\x92\x8E", 5891105528356018797LL);
    api->c_text(ctx, (" Общая стоимость NFT: " + nft_value_str).c_str());

    if (is_self) {
        std::string sres = koto::services::stars_summary(ctx, api, 30);
        json sj = json::parse(sres, nullptr, false);
        if (sj.is_object() && sj.value("ok", false)) {
            int total_tx = sj.value("total_tx", 0);
            double sent = sj.value("sent", 0.0);
            double received = sj.value("received", 0.0);
            bool has_bal = sj.value("has_balance", false);
            std::string bal_str = has_bal ? format_stars_num(sj.value("balance", 0.0)) : "н/д";

            api->c_text(ctx, "\n\n");
            api->c_emoji(ctx, "\xF0\x9F\x92\xB0", 5444860552310457690LL);
            api->c_fmt(ctx, " Количество транзакций:", KOTO_ENT_BOLD);
            api->c_text(ctx, (" " + std::to_string(total_tx) + "\n").c_str());
            api->c_text(ctx, ("\xE2\x94\x9C Звезд отправлено: " + format_stars_num(sent) + "\n").c_str());
            api->c_text(ctx, ("\xE2\x94\x94 Звезд получено: " + format_stars_num(received) + "\n").c_str());
            api->c_fmt(ctx, "Баланс сейчас:", KOTO_ENT_BOLD);
            api->c_text(ctx, (" " + bal_str).c_str());
        }
    }

    reply_or_edit(ctx, api);
}

static const koto_command COMMANDS[] = {
    {
        "giftstat",
        &cmd_giftstat,
        KOTO_LEVEL_TRUSTED,
        "[ru] Статистика подарков пользователя и их стоимость в звёздах/NFT. [en] User gift statistics and value.",
        "[@username | ID | реплай]"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "gift_stats",
    "[ru] Анализирует подарки в профиле, считает стоимость NFT и баланс Stars (порт GiftStats.py). Host 112+, C++ ABI 3. [en] Telegram Gifts & Stars analyzer. C++ ABI 3.",
    "1.0.0",
    112,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    nullptr, 0,
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/gift_stats.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
