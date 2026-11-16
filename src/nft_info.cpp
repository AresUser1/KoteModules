// nft_info.cpp
// Информация о Telegram NFT подарке (Unique Star Gift) по ссылке или slug.
// Автор: Kote.
//
// Использование:
//   .nft <ссылка или slug>     — найти владельца (ID, юзернейм, профиль, статус)
//   .nft (в ответ на реплай)  — взять ссылку из сообщения
//
// Примеры:
//   .nft t.me/nft/SnakeBox-190727
//   .nft SnakeBox-190727

#include "module_abi.h"
#include "koto_utils.hpp"
#include <string>
#include <vector>
#include <cstring>
#include <sstream>

// ── Извлечение slug из любого формата ссылки или текста ──────────────────────
static std::string extract_slug(const std::string& input) {
    if (input.empty()) return "";

    size_t s_pos = input.find("slug=");
    if (s_pos != std::string::npos) {
        std::string slug = input.substr(s_pos + 5);
        size_t end_p = slug.find_first_of(" &?#\t\r\n'\"");
        if (end_p != std::string::npos) slug = slug.substr(0, end_p);
        return slug;
    }

    size_t nft_pos = input.find("/nft/");
    if (nft_pos != std::string::npos) {
        std::string slug = input.substr(nft_pos + 5);
        size_t end_p = slug.find_first_of(" /?#\t\r\n'\"");
        if (end_p != std::string::npos) slug = slug.substr(0, end_p);
        return slug;
    }

    std::string trimmed = input;
    size_t start = trimmed.find_first_not_of(" \t\r\n");
    if (start != std::string::npos) trimmed = trimmed.substr(start);
    size_t end_p = trimmed.find_first_of(" \t\r\n/?#");
    if (end_p != std::string::npos) trimmed = trimmed.substr(0, end_p);

    return trimmed;
}

// ── Удаление HTML тегов ──────────────────────────────────────────────────────
static std::string strip_tags(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    bool in_tag = false;
    for (char c : html) {
        if (c == '<') in_tag = true;
        else if (c == '>') {
            in_tag = false;
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
        } else if (!in_tag) {
            out.push_back(c);
        }
    }
    return koto_utils::trim(out);
}

// ── Поиск значения строки таблицы <th>Name</th><td>Value</td> ───────────────
static std::string get_table_row(const std::string& html, const std::string& header) {
    std::string pat = "<th>" + header + "</th>";
    size_t p = html.find(pat);
    if (p == std::string::npos) {
        pat = "<th> " + header + " </th>";
        p = html.find(pat);
    }
    if (p == std::string::npos) return "";

    size_t td_start = html.find("<td>", p);
    if (td_start == std::string::npos) return "";
    td_start += 4;
    size_t td_end = html.find("</td>", td_start);
    if (td_end == std::string::npos) return "";

    return strip_tags(html.substr(td_start, td_end - td_start));
}

// ── Извлечение meta-тега ────────────────────────────────────────────────────
static std::string get_meta(const std::string& html, const std::string& prop) {
    std::string pat = "property=\"" + prop + "\" content=\"";
    size_t p = html.find(pat);
    if (p == std::string::npos) {
        pat = "property='" + prop + "' content='";
        p = html.find(pat);
    }
    if (p == std::string::npos) return "";
    p += pat.size();
    size_t end = html.find_first_of("\"'", p);
    if (end == std::string::npos) return "";
    return html.substr(p, end - p);
}

// ── Поиск строкового значения ключа в плоском JSON ──────────────────────────
static std::string json_get_str(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\":\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return "";
    p += pat.size();
    size_t end = json.find('"', p);
    if (end == std::string::npos) return "";
    return json.substr(p, end - p);
}

static long long json_get_int64(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\":";
    size_t p = json.find(pat);
    if (p == std::string::npos) return 0;
    p += pat.size();
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) p++;
    size_t end = json.find_first_of(",}\n\r", p);
    if (end == std::string::npos) end = json.size();
    std::string sub = json.substr(p, end - p);
    try {
        return std::stoll(sub);
    } catch (...) {
        return 0;
    }
}

static bool json_get_bool(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\":true";
    return json.find(pat) != std::string::npos;
}

// ── Кандидаты поиска по имени ───────────────────────────────────────────────
struct NameCandidate {
    long long id = 0;
    std::string first_name;
    std::string username;
    bool is_premium = false;
};

static std::vector<NameCandidate> parse_candidates(const std::string& json) {
    std::vector<NameCandidate> out;
    size_t arr_pos = json.find("\"users\":[");
    if (arr_pos == std::string::npos) arr_pos = json.find("\"candidates\":[");
    if (arr_pos == std::string::npos) return out;
    arr_pos = json.find('[', arr_pos);
    if (arr_pos == std::string::npos) return out;
    arr_pos += 1;
    size_t end_arr = json.find(']', arr_pos);
    if (end_arr == std::string::npos) end_arr = json.size();

    size_t cur = arr_pos;
    while (cur < end_arr) {
        size_t obj_start = json.find('{', cur);
        if (obj_start == std::string::npos || obj_start >= end_arr) break;
        size_t obj_end = json.find('}', obj_start);
        if (obj_end == std::string::npos || obj_end > end_arr) break;

        std::string obj_str = json.substr(obj_start, obj_end - obj_start + 1);
        NameCandidate c;
        c.id = json_get_int64(obj_str, "id");
        c.first_name = json_get_str(obj_str, "first_name");
        c.username = json_get_str(obj_str, "username");
        c.is_premium = json_get_bool(obj_str, "is_premium");
        if (c.id != 0) out.push_back(c);

        cur = obj_end + 1;
    }
    return out;
}

// ── Основная команда .nft ───────────────────────────────────────────────────
static void cmd_nft(koto_ctx* ctx, const koto_api* api) {
    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? raw_args : "";
    std::string slug;

    if (args.empty()) {
        if (api->reply_text) {
            const char* rep = api->reply_text(ctx);
            if (rep && rep[0] != '\0') {
                slug = extract_slug(rep);
            }
        }
    } else {
        slug = extract_slug(args);
    }

    if (slug.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Укажите ссылку на NFT подарок или ответьте на сообщение.", KOTO_ENT_BOLD);
        api->c_text(ctx, "\n\nПример: ");
        api->c_fmt(ctx, ".nft t.me/nft/SnakeBox-190727", KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    // 1. Статус поиска
    api->c_reset(ctx);
    api->c_fmt(ctx, "🔍 Поиск данных о подарке...", KOTO_ENT_ITALIC);
    api->c_edit(ctx, 0);

    // Данные подарка
    std::string title;
    std::string owner_name;
    std::string owner_username;
    long long owner_user_id = 0;
    long long owner_channel_id = 0;
    std::string owner_address;
    std::string gift_address;
    std::string model;
    std::string backdrop;
    std::string symbol;
    std::string quantity;
    bool is_premium = false;
    std::vector<NameCandidate> candidates;

    // 2. Стандартный запрос через Telegram MTProto (payments.getUniqueStarGift)
    if (api->host_call_json) {
        std::string req_json = "{\"slug\":\"" + slug + "\"}";
        std::vector<char> buf(64 * 1024);
        int len = api->host_call_json(ctx, "telegram.stargift.info.v1", req_json.c_str(), buf.data(), (int)buf.size());
        if (len > 0) {
            std::string resp(buf.data(), len);
            if (resp.find("\"ok\":true") != std::string::npos) {
                title = json_get_str(resp, "title");
                long long num = json_get_int64(resp, "num");
                if (!title.empty() && num > 0) {
                    title += " #" + std::to_string(num);
                }
                owner_user_id = json_get_int64(resp, "owner_user_id");
                owner_channel_id = json_get_int64(resp, "owner_channel_id");
                owner_name = json_get_str(resp, "owner_name");
                owner_address = json_get_str(resp, "owner_address");
                gift_address = json_get_str(resp, "gift_address");

                size_t u_pos = resp.find("\"user\":{");
                if (u_pos != std::string::npos) {
                    size_t u_end = resp.find('}', u_pos);
                    std::string u_block = resp.substr(u_pos, u_end - u_pos + 1);
                    owner_username = json_get_str(u_block, "username");
                    if (owner_name.empty()) {
                        std::string fn = json_get_str(u_block, "first_name");
                        std::string ln = json_get_str(u_block, "last_name");
                        owner_name = fn + (ln.empty() ? "" : (" " + ln));
                    }
                    if (u_block.find("\"is_premium\":true") != std::string::npos) {
                        is_premium = true;
                    }
                }
            }
        }
    }

    // 3. Запрос веб-страницы t.me/nft/<slug> (для атрибутов Model/Backdrop/Symbol)
    std::string web_url = "https://t.me/nft/" + slug;
    std::vector<unsigned char> http_buf(128 * 1024);
    long long n = api->http_get ? api->http_get(ctx, web_url.c_str(), http_buf.data(), (int)http_buf.size()) : -1;
    if (n > 0) {
        std::string html((const char*)http_buf.data(), (size_t)n);

        if (title.empty()) {
            title = get_meta(html, "og:title");
        }
        if (owner_name.empty()) {
            owner_name = get_table_row(html, "Owner");
        }
        model = get_table_row(html, "Model");
        backdrop = get_table_row(html, "Backdrop");
        symbol = get_table_row(html, "Symbol");
        quantity = get_table_row(html, "Quantity");
    }

    if (title.empty() && owner_name.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ NFT не найден или страница недоступна.", KOTO_ENT_BOLD);
        api->c_text(ctx, "\nПроверьте правильность ссылки: ");
        api->c_fmt(ctx, slug.c_str(), KOTO_ENT_CODE);
        api->c_edit(ctx, 0);
        return;
    }

    if (title.empty()) title = slug;

    // 4. Поиск в модуле: если ID скрыт в настройках, модуль пытается найти аккаунты по имени
    if (owner_user_id == 0 && !owner_name.empty()) {
        // 4.1 Проверяем резолв имени как юзернейма
        std::string probe_user = owner_name;
        if (!probe_user.empty() && probe_user[0] == '@') probe_user = probe_user.substr(1);
        if (probe_user.find(' ') == std::string::npos) {
            long long resolved = api->resolve(ctx, probe_user.c_str());
            if (resolved > 0) {
                owner_user_id = resolved;
                owner_username = probe_user;
            }
        }

        // 4.2 Поиск кандидатов через contacts.search (если доступен в хосте)
        if (owner_user_id == 0 && api->host_call_json) {
            std::string s_req = "{\"q\":\"" + owner_name + "\",\"limit\":10}";
            std::vector<char> s_buf(32 * 1024);
            int s_len = api->host_call_json(ctx, "telegram.contacts.search.v1", s_req.c_str(), s_buf.data(), (int)s_buf.size());
            if (s_len > 0) {
                std::string s_resp(s_buf.data(), s_len);
                candidates = parse_candidates(s_resp);
            }
        }
    }

    // 5. Формирование структурированного ответа
    api->c_reset(ctx);

    api->c_text(ctx, "🎁 ");
    api->c_fmt(ctx, "NFT Подарок: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, title.c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n\n");

    api->c_fmt(ctx, "👤 Информация о владельце:", KOTO_ENT_BOLD);
    api->c_text(ctx, "\n");

    if (!owner_name.empty()) {
        api->c_text(ctx, "• Имя: ");
        api->c_fmt(ctx, owner_name.c_str(), KOTO_ENT_BOLD);
        if (is_premium) api->c_text(ctx, " ⭐");
        api->c_text(ctx, "\n");
    }

    if (owner_user_id != 0) {
        api->c_text(ctx, "• ID: ");
        api->c_fmt(ctx, std::to_string(owner_user_id).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n");

        if (!owner_username.empty()) {
            api->c_text(ctx, "• Юзернейм: @");
            api->c_fmt(ctx, owner_username.c_str(), KOTO_ENT_CODE);
            api->c_text(ctx, "\n");
        }

        api->c_text(ctx, "• Профиль: ");
        std::string tg_link = "tg://user?id=" + std::to_string(owner_user_id);
        api->c_url(ctx, "Открыть профиль в Telegram", tg_link.c_str());
        api->c_text(ctx, "\n");
    } else if (owner_channel_id != 0) {
        api->c_text(ctx, "• Канал/Чат ID: ");
        api->c_fmt(ctx, std::to_string(owner_channel_id).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n");
    } else {
        api->c_text(ctx, "• ID / Юзернейм: ");
        api->c_fmt(ctx, "Скрыт владельцем в настройках приватности подарка", KOTO_ENT_ITALIC);
        api->c_text(ctx, "\n");

        if (!candidates.empty()) {
            api->c_text(ctx, "\n");
            std::string cand_hdr = "🔍 Пользователи с именем «" + owner_name + "» (поиск TG):";
            api->c_fmt(ctx, cand_hdr.c_str(), KOTO_ENT_BOLD);
            api->c_text(ctx, "\n");
            int c_idx = 1;
            for (const auto& c : candidates) {
                if (c_idx > 5) break;
                api->c_text(ctx, (std::to_string(c_idx++) + ". ").c_str());
                api->c_fmt(ctx, c.first_name.c_str(), KOTO_ENT_BOLD);
                if (!c.username.empty()) {
                    api->c_text(ctx, " (@");
                    api->c_fmt(ctx, c.username.c_str(), KOTO_ENT_CODE);
                    api->c_text(ctx, ")");
                }
                api->c_text(ctx, " — ID: ");
                api->c_fmt(ctx, std::to_string(c.id).c_str(), KOTO_ENT_CODE);
                api->c_text(ctx, " [");
                std::string c_link = "tg://user?id=" + std::to_string(c.id);
                api->c_url(ctx, "Профиль", c_link.c_str());
                api->c_text(ctx, "]\n");
            }
        }
    }

    // Блокчейн статус
    api->c_text(ctx, "\n");
    api->c_fmt(ctx, "💎 Блокчейн TON:", KOTO_ENT_BOLD);
    api->c_text(ctx, "\n");
    if (!owner_address.empty()) {
        api->c_text(ctx, "• TON кошелёк владельца: ");
        api->c_fmt(ctx, owner_address.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n");
        std::string tv_url = "https://tonviewer.com/" + owner_address;
        api->c_text(ctx, "• Эксплорер: ");
        api->c_url(ctx, "Открыть в Tonviewer", tv_url.c_str());
        api->c_text(ctx, "\n");
    } else {
        api->c_text(ctx, "• Статус: Не выведен в блокчейн (хранится внутри Telegram off-chain)\n");
    }

    // Характеристики
    if (!model.empty() || !backdrop.empty() || !symbol.empty()) {
        api->c_text(ctx, "\n");
        api->c_fmt(ctx, "💎 Характеристики:", KOTO_ENT_BOLD);
        api->c_text(ctx, "\n");
        if (!model.empty()) {
            api->c_text(ctx, "• Модель: ");
            api->c_text(ctx, model.c_str());
            api->c_text(ctx, "\n");
        }
        if (!backdrop.empty()) {
            api->c_text(ctx, "• Фон: ");
            api->c_text(ctx, backdrop.c_str());
            api->c_text(ctx, "\n");
        }
        if (!symbol.empty()) {
            api->c_text(ctx, "• Символ: ");
            api->c_text(ctx, symbol.c_str());
            api->c_text(ctx, "\n");
        }
        if (!quantity.empty()) {
            api->c_text(ctx, "• Тираж: ");
            api->c_text(ctx, quantity.c_str());
            api->c_text(ctx, "\n");
        }
    }

    api->c_text(ctx, "\n🔗 ");
    std::string full_link = "https://t.me/nft/" + slug;
    api->c_url(ctx, "t.me/nft/", full_link.c_str());
    api->c_url(ctx, slug.c_str(), full_link.c_str());

    api->c_edit(ctx, 0);
}

// ── Регистрация команд модуля ────────────────────────────────────────────────
static const koto_command COMMANDS[] = {
    {
        "nft",
        &cmd_nft,
        KOTO_LEVEL_TRUSTED,
        "[ru] Информация об NFT подарке по ссылке. [en] Info about NFT gift by link.",
        "<ссылка|slug>"
    },
    {
        "stargift",
        &cmd_nft,
        KOTO_LEVEL_TRUSTED,
        "[ru] Информация о Telegram Unique Star Gift. [en] Info about Telegram Unique Star Gift.",
        "<ссылка|slug>"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "nft_info",
    "[ru] Поиск владельца и атрибутов Telegram NFT подарка по ссылке (t.me/nft/...). [en] Find Telegram NFT gift owner and attributes by link.",
    "1.2.0",
    24,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    nullptr, 0,
    nullptr
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
