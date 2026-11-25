// financemod.cpp — Курсы валют ЦБ РФ и криптовалют (CoinLore) (C++ ABI 3).
// Портирован из Python-модуля FinanceMod (by kote).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using json = nlohmann::json;

constexpr const char* VERSION = "1.0.0";
constexpr int CACHE_TTL_SEC = 300;
constexpr const char* CBR_JSON_URL = "https://www.cbr-xml-daily.ru/daily_json.js";
constexpr const char* CBR_XML_URL  = "https://www.cbr.ru/scripts/XML_daily.asp";
constexpr const char* ER_FALLBACK_URL = "https://open.er-api.com/v6/latest/RUB";
constexpr const char* COINLORE_URL_0 = "https://api.coinlore.net/api/tickers/?start=0&limit=100";
constexpr const char* COINLORE_URL_100 = "https://api.coinlore.net/api/tickers/?start=100&limit=100";

struct CurrencyMeta {
    const char* code;
    const char* flag;
    const char* ru_name;
    const char* en_name;
};

static constexpr CurrencyMeta CURRENCY_TABLE[] = {
    {"AUD", "🇦🇺", "Австралийский доллар", "Australian Dollar"},
    {"AZN", "🇦🇿", "Азербайджанский манат", "Azerbaijani Manat"},
    {"GBP", "🇬🇧", "Фунт стерлингов", "British Pound"},
    {"AMD", "🇦🇲", "Армянский драм", "Armenian Dram"},
    {"BYN", "🇧🇾", "Белорусский рубль", "Belarusian Ruble"},
    {"BGN", "🇧🇬", "Болгарский лев", "Bulgarian Lev"},
    {"BRL", "🇧🇷", "Бразильский реал", "Brazilian Real"},
    {"HUF", "🇭🇺", "Венгерский форинт", "Hungarian Forint"},
    {"VND", "🇻🇳", "Вьетнамский донг", "Vietnamese Dong"},
    {"HKD", "🇭🇰", "Гонконгский доллар", "Hong Kong Dollar"},
    {"GEL", "🇬🇪", "Грузинский лари", "Georgian Lari"},
    {"DKK", "🇩🇰", "Датская крона", "Danish Krone"},
    {"AED", "🇦🇪", "Дирхам ОАЭ", "UAE Dirham"},
    {"USD", "🇺🇸", "Доллар США", "US Dollar"},
    {"EUR", "🇪🇺", "Евро", "Euro"},
    {"EGP", "🇪🇬", "Египетский фунт", "Egyptian Pound"},
    {"INR", "🇮🇳", "Индийская рупия", "Indian Rupee"},
    {"IDR", "🇮🇩", "Индонезийская рупия", "Indonesian Rupiah"},
    {"KZT", "🇰🇿", "Казахстанский тенге", "Kazakhstani Tenge"},
    {"CAD", "🇨🇦", "Канадский доллар", "Canadian Dollar"},
    {"QAR", "🇶🇦", "Катарский риал", "Qatari Riyal"},
    {"KGS", "🇰🇬", "Киргизский сом", "Kyrgyzstani Som"},
    {"CNY", "🇨🇳", "Китайский юань", "Chinese Yuan"},
    {"MDL", "🇲🇩", "Молдавский лей", "Moldovan Leu"},
    {"NZD", "🇳🇿", "Новозеландский доллар", "New Zealand Dollar"},
    {"NOK", "🇳🇴", "Норвежская крона", "Norwegian Krone"},
    {"PLN", "🇵🇱", "Польский злотый", "Polish Zloty"},
    {"RON", "🇷🇴", "Румынский лей", "Romanian Leu"},
    {"SGD", "🇸🇬", "Сингапурский доллар", "Singapore Dollar"},
    {"TJS", "🇹🇯", "Таджикский сомони", "Tajikistani Somoni"},
    {"THB", "🇹🇭", "Таиландский бат", "Thai Baht"},
    {"TRY", "🇹🇷", "Турецкая лира", "Turkish Lira"},
    {"TMT", "🇹🇲", "Новый туркменский манат", "Turkmenistani Manat"},
    {"UZS", "🇺🇿", "Узбекский сум", "Uzbekistani Som"},
    {"UAH", "🇺🇦", "Украинская гривна", "Ukrainian Hryvnia"},
    {"CZK", "🇨🇿", "Чешская крона", "Czech Koruna"},
    {"SEK", "🇸🇪", "Шведская крона", "Swedish Krona"},
    {"CHF", "🇨🇭", "Швейцарский франк", "Swiss Franc"},
    {"RSD", "🇷🇸", "Сербский динар", "Serbian Dinar"},
    {"ZAR", "🇿🇦", "Южноафриканский рэнд", "South African Rand"},
    {"KRW", "🇰🇷", "Вон Республики Корея", "South Korean Won"},
    {"JPY", "🇯🇵", "Японская иена", "Japanese Yen"},
};

struct CryptoMeta {
    const char* symbol;
    const char* name;
    long long emoji_id;
};

static constexpr CryptoMeta KNOWN_CRYPTOS[] = {
    {"BTC",   "Bitcoin",          5289519973285257969LL},
    {"ETH",   "Ethereum",         5287735049301550386LL},
    {"XMR",   "Monero",           5375507073977038661LL},
    {"LTC",   "Litecoin",         5373035462032113888LL},
    {"XRP",   "XRP",              5373312921214401986LL},
    {"ADA",   "Cardano",          5373076801092338046LL},
    {"DOGE",  "Dogecoin",         5375192042420842380LL},
    {"SOL",   "Solana",           5251712673258697260LL},
    {"DOT",   "Polkadot",         5375224568208177973LL},
    {"USDT",  "Tether",           5289904548951911168LL},
    {"TON",   "Toncoin",          5289648693455119919LL},
    {"USDC",  "USD Coin",         5372958453268497353LL},
    {"TRX",   "TRON",             5375187081733616165LL},
    {"AVAX",  "Avalanche",        5375311275007947936LL},
    {"BCH",   "Bitcoin Cash",     5375596920397903962LL},
    {"ATOM",  "Cosmos",           5375468745688889977LL},
    {"XLM",   "Stellar",          5372823290647690288LL},
    {"SHIB",  "Shiba Inu",        5375231036428924778LL},
    {"UNI",   "Uniswap",          5372953110329180525LL},
    {"LINK",  "Chainlink",        5375149651093633217LL},
    {"ETC",   "Ethereum Classic", 5375543306321146693LL},
    {"SUI",   "Sui",              5391002164929772708LL},
    {"NEAR",  "NEAR Protocol",    5391181990915487346LL},
    {"VET",   "VeChain",          5391091302681033446LL},
    {"FIL",   "Filecoin",         5373117173784919811LL},
    {"XTZ",   "Tezos",            5390985478981829698LL},
    {"ALGO",  "Algorand",         5391337713544738420LL},
    {"THETA", "Theta Network",    5391256014676833736LL},
    {"FTM",   "Fantom",           5393179395521263785LL},
    {"XDAI",  "xDai",             5391325992578988886LL},
    {"RUNE",  "THORChain",        5391347570494684983LL},
};

struct ValuteRate {
    std::string code;
    std::string name;
    double rub = 0.0;
};

struct CoinRate {
    std::string symbol;
    std::string name;
    double price_usd = 0.0;
};

struct CacheState {
    std::mutex mu;
    bool has_cbr = false;
    std::chrono::steady_clock::time_point cbr_ts;
    std::string cbr_date;
    std::vector<ValuteRate> rates;

    bool has_crypto = false;
    std::chrono::steady_clock::time_point crypto_ts;
    std::vector<CoinRate> coins;
};

CacheState g_state;

static std::string ascii_upper(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

static bool parse_double_locale(const std::string& raw, double& out) {
    std::string s = koto_utils::trim(raw);
    if (s.empty()) return false;
    for (char& c : s) {
        if (c == ',') c = '.';
    }
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0' || !std::isfinite(v) || v <= 0.0) {
        return false;
    }
    out = v;
    return true;
}

static const CurrencyMeta* find_currency_meta(const std::string& code) {
    for (const auto& m : CURRENCY_TABLE) {
        if (code == m.code) return &m;
    }
    return nullptr;
}

static const CryptoMeta* find_crypto_meta(const std::string& sym) {
    for (const auto& m : KNOWN_CRYPTOS) {
        if (sym == m.symbol) return &m;
    }
    return nullptr;
}

static bool is_clean_utf8(const std::string& s) {
    if (s.empty()) return false;
    // Проверка на U+FFFD (EF BF BD) после неверного перекодирования CP1251 -> UTF-8.
    return s.find("\xEF\xBF\xBD") == std::string::npos;
}

static std::string format_number(double value, int decimals) {
    if (!std::isfinite(value)) return "0";
    if (decimals == 0) {
        long long iv = static_cast<long long>(std::llround(value));
        std::string digits = std::to_string(iv < 0 ? -iv : iv);
        std::string grouped;
        for (size_t i = 0; i < digits.size(); ++i) {
            if (i > 0 && (digits.size() - i) % 3 == 0) grouped.push_back(' ');
            grouped.push_back(digits[i]);
        }
        return (iv < 0 ? "-" : "") + grouped;
    }

    int eff_dec = decimals;
    if (value > 0.0 && value < 0.01) eff_dec = 6;

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(eff_dec) << value;
    std::string raw = ss.str();

    size_t dot = raw.find('.');
    std::string int_part = (dot == std::string::npos) ? raw : raw.substr(0, dot);
    std::string dec_part = (dot == std::string::npos) ? "" : raw.substr(dot + 1);

    while (!dec_part.empty() && dec_part.back() == '0') dec_part.pop_back();

    bool neg = (!int_part.empty() && int_part[0] == '-');
    if (neg) int_part = int_part.substr(1);

    std::string grouped;
    for (size_t i = 0; i < int_part.size(); ++i) {
        if (i > 0 && (int_part.size() - i) % 3 == 0) grouped.push_back(' ');
        grouped.push_back(int_part[i]);
    }
    if (neg) grouped = "-" + grouped;
    return dec_part.empty() ? grouped : (grouped + "," + dec_part);
}

static std::string extract_xml_tag(const std::string& block, const char* tag) {
    std::string open = std::string("<") + tag + ">";
    std::string close = std::string("</") + tag + ">";
    size_t p1 = block.find(open);
    if (p1 == std::string::npos) return "";
    p1 += open.size();
    size_t p2 = block.find(close, p1);
    if (p2 == std::string::npos) return "";
    return koto_utils::trim(block.substr(p1, p2 - p1));
}

static std::string format_iso_date(const std::string& iso) {
    if (iso.size() >= 10 && iso[4] == '-' && iso[7] == '-') {
        return iso.substr(8, 2) + "." + iso.substr(5, 2) + "." + iso.substr(0, 4);
    }
    return iso;
}

static bool parse_cbr_daily_json(const std::string& body, bool en,
                                 std::string& out_date, std::vector<ValuteRate>& out_rates) {
    try {
        json j = json::parse(body);
        if (!j.is_object() || !j.contains("Valute") || !j["Valute"].is_object()) return false;

        out_date = format_iso_date(j.value("Date", ""));
        out_rates.clear();

        // Сохраняем порядок как на ЦБ РФ, добавляя известные или из ответа.
        std::map<std::string, ValuteRate> by_code;
        for (auto it = j["Valute"].begin(); it != j["Valute"].end(); ++it) {
            if (!it.value().is_object()) continue;
            const auto& v = it.value();
            std::string code = ascii_upper(v.value("CharCode", it.key()));
            if (code.empty() || code == "XDR") continue;

            double nom = v.value("Nominal", 1.0);
            double val = v.value("Value", 0.0);
            if (nom <= 0.0 || val <= 0.0) continue;

            const auto* meta = find_currency_meta(code);
            std::string name;
            if (en && meta) {
                name = meta->en_name;
            } else {
                name = v.value("Name", "");
                if (!is_clean_utf8(name)) {
                    name = meta ? meta->ru_name : code;
                }
            }
            by_code[code] = {code, name, val / nom};
        }

        for (const auto& m : CURRENCY_TABLE) {
            auto f = by_code.find(m.code);
            if (f != by_code.end()) {
                out_rates.push_back(f->second);
                by_code.erase(f);
            }
        }
        for (const auto& kv : by_code) {
            out_rates.push_back(kv.second);
        }
        return !out_rates.empty();
    } catch (...) {
        return false;
    }
}

static bool parse_cbr_xml_fallback(const std::string& body, bool en,
                                   std::string& out_date, std::vector<ValuteRate>& out_rates) {
    if (body.find("<ValCurs") == std::string::npos) return false;
    out_date.clear();
    size_t dpos = body.find("Date=\"");
    if (dpos != std::string::npos) {
        dpos += 6;
        size_t dend = body.find('"', dpos);
        if (dend != std::string::npos) out_date = body.substr(dpos, dend - dpos);
    }

    out_rates.clear();
    size_t pos = 0;
    while ((pos = body.find("<Valute", pos)) != std::string::npos) {
        size_t end = body.find("</Valute>", pos);
        if (end == std::string::npos) break;
        std::string block = body.substr(pos, end - pos);
        pos = end + 9;

        std::string code = ascii_upper(extract_xml_tag(block, "CharCode"));
        if (code.empty() || code == "XDR") continue;

        double nom = 1.0;
        double val = 0.0;
        if (!parse_double_locale(extract_xml_tag(block, "Nominal"), nom)) continue;
        if (!parse_double_locale(extract_xml_tag(block, "Value"), val)) continue;

        const auto* meta = find_currency_meta(code);
        std::string name;
        if (en && meta) {
            name = meta->en_name;
        } else if (meta) {
            name = meta->ru_name;
        } else {
            std::string raw_name = extract_xml_tag(block, "Name");
            name = is_clean_utf8(raw_name) ? raw_name : code;
        }
        out_rates.push_back({code, name, val / nom});
    }
    return !out_rates.empty();
}

static bool parse_open_er_fallback(const std::string& body, bool en,
                                   std::string& out_date, std::vector<ValuteRate>& out_rates) {
    try {
        json j = json::parse(body);
        if (!j.is_object() || j.value("result", "") != "success" || !j.contains("rates")) return false;
        const auto& r = j["rates"];
        if (!r.is_object()) return false;

        out_date = j.value("time_last_update_utc", "");
        if (out_date.size() > 16) out_date = out_date.substr(0, 16);
        out_rates.clear();

        for (const auto& m : CURRENCY_TABLE) {
            if (!r.contains(m.code) || !r[m.code].is_number()) continue;
            double per_rub = r[m.code].get<double>();
            if (per_rub <= 0.0) continue;
            out_rates.push_back({m.code, en ? m.en_name : m.ru_name, 1.0 / per_rub});
        }
        return !out_rates.empty();
    } catch (...) {
        return false;
    }
}

static bool get_cbr_data(koto_ctx* ctx, const koto_api* api, bool en,
                         std::string& out_date, std::vector<ValuteRate>& out_rates) {
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lk(g_state.mu);
        if (g_state.has_cbr &&
            std::chrono::duration_cast<std::chrono::seconds>(now - g_state.cbr_ts).count() < CACHE_TTL_SEC) {
            out_date = g_state.cbr_date;
            out_rates = g_state.rates;
            return true;
        }
    }

    std::string date;
    std::vector<ValuteRate> rates;
    bool ok = false;

    std::string body = koto_utils::http_get(api, ctx, CBR_JSON_URL, {}, 10);
    if (!body.empty()) ok = parse_cbr_daily_json(body, en, date, rates);

    if (!ok) {
        body = koto_utils::http_get(api, ctx, CBR_XML_URL, {}, 10);
        if (!body.empty()) ok = parse_cbr_xml_fallback(body, en, date, rates);
    }

    if (!ok) {
        body = koto_utils::http_get(api, ctx, ER_FALLBACK_URL, {}, 10);
        if (!body.empty()) ok = parse_open_er_fallback(body, en, date, rates);
    }

    std::lock_guard<std::mutex> lk(g_state.mu);
    if (ok) {
        g_state.has_cbr = true;
        g_state.cbr_ts = now;
        g_state.cbr_date = date;
        g_state.rates = rates;
        out_date = date;
        out_rates = rates;
        return true;
    }
    if (g_state.has_cbr) {
        out_date = g_state.cbr_date;
        out_rates = g_state.rates;
        return true;
    }
    return false;
}

static void parse_coinlore_page(const std::string& body, std::vector<CoinRate>& out_coins) {
    try {
        json j = json::parse(body);
        if (!j.is_object() || !j.contains("data") || !j["data"].is_array()) return;
        for (const auto& item : j["data"]) {
            if (!item.is_object()) continue;
            std::string sym = ascii_upper(item.value("symbol", ""));
            if (sym.empty()) continue;

            double p = 0.0;
            if (item.contains("price_usd")) {
                if (item["price_usd"].is_number()) {
                    p = item["price_usd"].get<double>();
                } else if (item["price_usd"].is_string()) {
                    if (!parse_double_locale(item["price_usd"].get<std::string>(), p)) continue;
                }
            }
            if (p <= 0.0) continue;
            out_coins.push_back({sym, item.value("name", sym), p});
        }
    } catch (...) {}
}

static bool get_crypto_data(koto_ctx* ctx, const koto_api* api,
                            const std::string& wanted_sym, std::vector<CoinRate>& out_coins) {
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lk(g_state.mu);
        if (g_state.has_crypto &&
            std::chrono::duration_cast<std::chrono::seconds>(now - g_state.crypto_ts).count() < CACHE_TTL_SEC) {
            bool need_more = !wanted_sym.empty();
            if (need_more) {
                for (const auto& c : g_state.coins) {
                    if (c.symbol == wanted_sym) { need_more = false; break; }
                }
            }
            if (!need_more || g_state.coins.size() > 110) {
                out_coins = g_state.coins;
                return true;
            }
        }
    }

    std::vector<CoinRate> fresh;
    std::string b0 = koto_utils::http_get(api, ctx, COINLORE_URL_0, {}, 10);
    if (!b0.empty()) parse_coinlore_page(b0, fresh);

    if (!fresh.empty() && !wanted_sym.empty()) {
        bool found = false;
        for (const auto& c : fresh) {
            if (c.symbol == wanted_sym) { found = true; break; }
        }
        if (!found) {
            std::string b1 = koto_utils::http_get(api, ctx, COINLORE_URL_100, {}, 10);
            if (!b1.empty()) parse_coinlore_page(b1, fresh);
        }
    }

    std::lock_guard<std::mutex> lk(g_state.mu);
    if (!fresh.empty()) {
        g_state.has_crypto = true;
        g_state.crypto_ts = now;
        g_state.coins = fresh;
        out_coins = fresh;
        return true;
    }
    if (g_state.has_crypto) {
        out_coins = g_state.coins;
        return true;
    }
    return false;
}

static double find_fiat_rate(const std::vector<ValuteRate>& rates, const std::string& code) {
    for (const auto& r : rates) {
        if (r.code == code) return r.rub;
    }
    return 0.0;
}

static std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> res;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) res.push_back(tok);
    return res;
}

static int amount_decimals(double a) {
    return (std::abs(a - std::llround(a)) < 1e-6) ? 0 : 4;
}

static void emit_valute_line(koto_ctx* ctx, const koto_api* api, const ValuteRate& v, double amount) {
    const auto* meta = find_currency_meta(v.code);
    std::string flag = meta ? meta->flag : "🏳";
    double total = v.rub * amount;
    std::string line = flag + " [" + format_number(amount, amount_decimals(amount)) + "] " + v.name
                     + " (" + v.code + ") — " + format_number(total, 3) + " ₽";
    api->c_text(ctx, line.c_str());
}

static void emit_crypto_line(koto_ctx* ctx, const koto_api* api, const CoinRate& c,
                             const std::string& target_cur, double usd_rub, double eur_rub, double amount) {
    double price = c.price_usd;
    std::string sign = "$";
    if (target_cur == "RUB") {
        if (usd_rub <= 0.0) return;
        price = c.price_usd * usd_rub;
        sign = "₽";
    } else if (target_cur == "EUR") {
        if (usd_rub <= 0.0 || eur_rub <= 0.0) return;
        price = c.price_usd * (usd_rub / eur_rub);
        sign = "€";
    }

    const auto* meta = find_crypto_meta(c.symbol);
    std::string cname = meta ? meta->name : c.name;
    if (meta && meta->emoji_id != 0 && api->c_emoji) {
        api->c_emoji(ctx, "💠", meta->emoji_id);
    } else {
        api->c_text(ctx, "💠");
    }

    double total = price * amount;
    std::string line = " [" + format_number(amount, amount_decimals(amount)) + "] " + cname
                     + " (" + c.symbol + ") — " + format_number(total, 3) + sign;
    api->c_text(ctx, line.c_str());
}

static void respond(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

void cmd_valute(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    bool en = koto_utils::is_en(ctx, api);

    std::string raw_args = api->cmd_args ? (api->cmd_args(ctx) ? api->cmd_args(ctx) : "") : "";
    std::vector<std::string> args = split_ws(raw_args);

    double amount = 1.0;
    std::string code;

    if (args.size() >= 2) {
        if (!parse_double_locale(args[0], amount)) {
            api->c_reset(ctx);
            api->c_fmt(ctx, en ? "🚫 Invalid number" : "🚫 Некорректное число", KOTO_ENT_BOLD);
            respond(ctx, api);
            return;
        }
        code = ascii_upper(args[1]);
    } else if (args.size() == 1) {
        double tmp = 1.0;
        if (parse_double_locale(args[0], tmp)) {
            amount = tmp;
        } else {
            code = ascii_upper(args[0]);
        }
    }

    if (api->is_outgoing(ctx)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, en ? "💵 Fetching CBR exchange rates..." : "💵 Загружаю курсы ЦБ РФ...", KOTO_ENT_ITALIC);
        api->c_edit(ctx, 0);
    }

    std::string date;
    std::vector<ValuteRate> rates;
    if (!get_cbr_data(ctx, api, en, date, rates)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, en ? "🚫 Failed to fetch CBR rates" : "🚫 Не удалось получить данные ЦБ РФ", KOTO_ENT_BOLD);
        respond(ctx, api);
        return;
    }

    api->c_reset(ctx);
    api->c_text(ctx, "💵 ");
    api->c_fmt(ctx, en ? "Exchange rate" : "Курс валюты", KOTO_ENT_BOLD);
    api->c_text(ctx, " · ");
    api->c_url(ctx, en ? "CBR" : "ЦБ РФ", "https://www.cbr.ru/");
    if (!date.empty()) {
        api->c_text(ctx, "\n");
        api->c_fmt(ctx, en ? "Updated on " : "Актуально на ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, date.c_str(), KOTO_ENT_ITALIC);
    }
    api->c_text(ctx, "\n\n");

    if (!code.empty()) {
        const ValuteRate* found = nullptr;
        for (const auto& r : rates) {
            if (r.code == code) { found = &r; break; }
        }
        if (!found) {
            api->c_reset(ctx);
            api->c_text(ctx, en ? "🚫 Currency " : "🚫 Валюта ");
            api->c_fmt(ctx, code.c_str(), KOTO_ENT_BOLD);
            api->c_text(ctx, en ? " not found" : " не найдена");
            respond(ctx, api);
            return;
        }
        emit_valute_line(ctx, api, *found, amount);
        respond(ctx, api);
        return;
    }

    int mark = api->c_mark ? api->c_mark(ctx) : -1;
    for (size_t i = 0; i < rates.size(); ++i) {
        if (i > 0) api->c_text(ctx, "\n");
        emit_valute_line(ctx, api, rates[i], amount);
    }
    if (mark >= 0 && api->c_wrap) {
        api->c_wrap(ctx, mark, KOTO_ENT_BLOCKQUOTE, 1);
    }
    respond(ctx, api);
}

void cmd_crypto(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    bool en = koto_utils::is_en(ctx, api);

    std::string raw_args = api->cmd_args ? (api->cmd_args(ctx) ? api->cmd_args(ctx) : "") : "";
    std::vector<std::string> args = split_ws(raw_args);

    double amount = 1.0;
    std::string symbol;

    if (args.size() >= 2) {
        if (!parse_double_locale(args[0], amount)) {
            api->c_reset(ctx);
            api->c_fmt(ctx, en ? "🚫 Invalid number" : "🚫 Некорректное число", KOTO_ENT_BOLD);
            respond(ctx, api);
            return;
        }
        symbol = ascii_upper(args[1]);
    } else if (args.size() == 1) {
        double tmp = 1.0;
        if (parse_double_locale(args[0], tmp)) {
            amount = tmp;
        } else {
            symbol = ascii_upper(args[0]);
        }
    }

    std::string target_cur = ascii_upper(koto_utils::cfg_get_str(api, ctx, "crypto_currency", "USD"));
    if (target_cur != "USD" && target_cur != "RUB" && target_cur != "EUR") target_cur = "USD";

    if (api->is_outgoing(ctx)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, en ? "💎 Fetching crypto rates..." : "💎 Загружаю курсы криптовалют...", KOTO_ENT_ITALIC);
        api->c_edit(ctx, 0);
    }

    std::vector<CoinRate> coins;
    if (!get_crypto_data(ctx, api, symbol, coins)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, en ? "🚫 Failed to fetch crypto rates" : "🚫 Не удалось получить данные крипты", KOTO_ENT_BOLD);
        respond(ctx, api);
        return;
    }

    double usd_rub = 0.0;
    double eur_rub = 0.0;
    if (target_cur != "USD") {
        std::string d;
        std::vector<ValuteRate> vr;
        if (get_cbr_data(ctx, api, en, d, vr)) {
            usd_rub = find_fiat_rate(vr, "USD");
            eur_rub = find_fiat_rate(vr, "EUR");
        }
        if (usd_rub <= 0.0 || (target_cur == "EUR" && eur_rub <= 0.0)) {
            target_cur = "USD";
        }
    }

    api->c_reset(ctx);
    api->c_text(ctx, "💎 ");
    api->c_fmt(ctx, en ? "Crypto rates" : "Курсы криптовалют", KOTO_ENT_BOLD);
    api->c_text(ctx, " · ");
    api->c_fmt(ctx, target_cur.c_str(), KOTO_ENT_ITALIC);
    api->c_text(ctx, "\n\n");

    if (!symbol.empty()) {
        const CoinRate* found = nullptr;
        for (const auto& c : coins) {
            if (c.symbol == symbol) { found = &c; break; }
        }
        if (!found) {
            api->c_reset(ctx);
            api->c_text(ctx, en ? "🚫 Crypto " : "🚫 Крипта ");
            api->c_fmt(ctx, symbol.c_str(), KOTO_ENT_BOLD);
            api->c_text(ctx, en ? " not found" : " не найдена");
            respond(ctx, api);
            return;
        }
        emit_crypto_line(ctx, api, *found, target_cur, usd_rub, eur_rub, amount);
        respond(ctx, api);
        return;
    }

    std::vector<const CoinRate*> ordered;
    for (const auto& km : KNOWN_CRYPTOS) {
        for (const auto& c : coins) {
            if (c.symbol == km.symbol) {
                ordered.push_back(&c);
                break;
            }
        }
    }

    int mark = api->c_mark ? api->c_mark(ctx) : -1;
    for (size_t i = 0; i < ordered.size(); ++i) {
        if (i > 0) api->c_text(ctx, "\n");
        emit_crypto_line(ctx, api, *ordered[i], target_cur, usd_rub, eur_rub, amount);
    }
    if (mark >= 0 && api->c_wrap) {
        api->c_wrap(ctx, mark, KOTO_ENT_BLOCKQUOTE, 1);
    }
    respond(ctx, api);
}

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    (void)ctx; (void)api;
}

static const koto_command COMMANDS[] = {
    {"valute", cmd_valute, KOTO_LEVEL_TRUSTED, "[ru] Курс валюты по ЦБ РФ (.valute [кол-во] [код]) (by kote). [en] CBR currency exchange rates (.valute [amount] [code]) (by kote).", "[кол-во] [код]"},
    {"crypto", cmd_crypto, KOTO_LEVEL_TRUSTED, "[ru] Курс криптовалют CoinLore (.crypto [кол-во] [символ]) (by kote). [en] Crypto exchange rates via CoinLore (.crypto [amount] [symbol]) (by kote).", "[кол-во] [код]"}
};

static const koto_setting_field SETTINGS[] = {
    {"crypto_currency", "[ru] Валюта для отображения крипты [en] Currency for crypto display", KOTO_FIELD_CHOICE, "USD", "USD,RUB,EUR"}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "financemod",
    "[ru] Курсы валют ЦБ РФ и криптовалют CoinLore (by kote). [en] CBR fiat rates and CoinLore crypto rates (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    nullptr, 0,
    on_callback,
    SETTINGS,
    static_cast<int>(sizeof(SETTINGS) / sizeof(SETTINGS[0])),
    "https://github.com/AresUser1/KoteModules/blob/main/src/financemod.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
