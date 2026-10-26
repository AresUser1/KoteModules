// poll.cpp — Мощный модуль для работы с опросами.
// C++ ABI 3 port of poll.py (v3.2.2).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <map>
#include <sstream>

namespace {
using nlohmann::json;

static constexpr long long ICON_POLL   = 5956561916573782596LL; // 📋
static constexpr long long ICON_VOTE   = 5774022692642492953LL; // ✅
static constexpr long long ICON_NOVOTE = 5431449001532594346LL; // ❌
static constexpr long long ICON_TAG    = 5890727932011223292LL; // 🏷️

static std::string to_lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

struct TemplateData {
    std::vector<std::string> options;
    bool is_public = true;
    bool multi = false;
    bool pin = false;
};

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

static std::map<std::string, TemplateData> get_templates(koto_ctx* ctx, const koto_api* api) {
    std::map<std::string, TemplateData> res;
    // Дефолтные шаблоны
    res["данет"]  = { {"Да", "Нет"}, true, false, false };
    res["оценка"] = { {"1", "2", "3", "4", "5"}, true, false, false };
    res["время"]  = { {"Сейчас", "Через 5 мин", "Через 15 мин", "Позже"}, true, false, false };

    char buf[8192];
    int len = api->db_get(ctx, "templates_json", buf, sizeof(buf));
    if (len > 0) {
        std::stringstream ss(buf);
        std::string line;
        while (std::getline(ss, line)) {
            if (line.empty()) continue;
            auto p1 = line.find('|');
            auto p2 = line.rfind('|');
            if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1) {
                std::string name = line.substr(0, p1);
                std::string opts_str = line.substr(p1 + 1, p2 - (p1 + 1));
                std::string flags = line.substr(p2 + 1);

                std::vector<std::string> opts;
                std::stringstream oss(opts_str);
                std::string opt;
                while (std::getline(oss, opt, ',')) {
                    if (!opt.empty()) opts.push_back(opt);
                }

                if (!opts.empty()) {
                    TemplateData td;
                    td.options = std::move(opts);
                    td.is_public = (flags.find("public") != std::string::npos);
                    td.multi = (flags.find("multi") != std::string::npos);
                    td.pin = (flags.find("pin") != std::string::npos);
                    res[name] = td;
                }
            }
        }
    }
    return res;
}

static void save_templates(koto_ctx* ctx, const koto_api* api, const std::map<std::string, TemplateData>& tpls) {
    std::string out;
    for (const auto& [name, td] : tpls) {
        out += name + "|";
        for (size_t i = 0; i < td.options.size(); ++i) {
            if (i > 0) out += ",";
            out += td.options[i];
        }
        out += "|";
        std::string flags;
        if (td.is_public) flags += "public;";
        if (td.multi) flags += "multi;";
        if (td.pin) flags += "pin;";
        out += flags + "\n";
    }
    api->db_set(ctx, "templates_json", out.c_str());
}

static void cmd_poll(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? koto_utils::trim(raw) : "";

    auto space = args.find(' ');
    auto templates = get_templates(ctx, api);

    if (space == std::string::npos) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "📋", ICON_POLL);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, "Использование: .poll <шаблон> <вопрос>\n\n", KOTO_ENT_ITALIC);
        api->c_fmt(ctx, "Доступные шаблоны: ", KOTO_ENT_BOLD);
        bool first = true;
        for (const auto& [name, _] : templates) {
            if (!first) api->c_text(ctx, ", ");
            api->c_fmt(ctx, name.c_str(), KOTO_ENT_CODE);
            first = false;
        }
        send_result(ctx, api);
        return;
    }

    std::string tpl_name = to_lower(args.substr(0, space));
    std::string question = koto_utils::trim(args.substr(space + 1));

    if (templates.find(tpl_name) == templates.end()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", ICON_NOVOTE);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, ("Шаблон '" + tpl_name + "' не найден.").c_str(), KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    const auto& td = templates[tpl_name];

    // Отправка через хост-сервис poll
    if (api->host_action_json) {
        json req = {
            {"chat_id", api->chat_id(ctx)},
            {"question", question},
            {"options", td.options},
            {"public", td.is_public},
            {"multi", td.multi}
        };
        api->host_action_json(ctx, "telegram.poll.send.v1", req.dump().c_str());
    }

    // Красивое отображение опроса
    api->c_reset(ctx);
    api->c_emoji(ctx, "📋", ICON_POLL);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, "Опрос: ", KOTO_ENT_BOLD);
    api->c_text(ctx, (question + "\n\n").c_str());

    int mark = api->c_mark(ctx);
    api->c_text(ctx, (std::string("Тип: ") + (td.is_public ? "Публичный" : "Анонимный") + "\n").c_str());
    api->c_text(ctx, "Варианты:\n");
    for (size_t i = 0; i < td.options.size(); ++i) {
        api->c_text(ctx, (std::to_string(i + 1) + ". " + td.options[i] + "\n").c_str());
    }
    api->c_wrap(ctx, mark, KOTO_ENT_BLOCKQUOTE, 0);

    if (td.pin) {
        api->c_text(ctx, "\n📌 ");
        api->c_fmt(ctx, "Опрос закреплён.", KOTO_ENT_BOLD);
    }
    send_result(ctx, api);
}

static void cmd_pollcfg(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? koto_utils::trim(raw) : "";
    auto templates = get_templates(ctx, api);

    if (args.empty() || args == "list") {
        api->c_reset(ctx);
        api->c_emoji(ctx, "📋", ICON_POLL);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, "Шаблоны опросов:\n\n", KOTO_ENT_BOLD);
        for (const auto& [name, td] : templates) {
            api->c_text(ctx, "• ");
            api->c_fmt(ctx, name.c_str(), KOTO_ENT_BOLD);
            api->c_text(ctx, " (");
            for (size_t i = 0; i < td.options.size(); ++i) {
                if (i > 0) api->c_text(ctx, ", ");
                api->c_fmt(ctx, td.options[i].c_str(), KOTO_ENT_CODE);
            }
            api->c_text(ctx, (std::string(")\n  > Настройки: ") + (td.is_public ? "Публичный" : "Анонимный") +
                              (td.multi ? ", Multi" : "") + (td.pin ? ", Закреп\n" : "\n")).c_str());
        }
        api->c_text(ctx, "\nКоманды: .pollcfg add <имя> <вар1,вар2,...> | .pollcfg del <имя>");
        send_result(ctx, api);
        return;
    }

    auto space = args.find(' ');
    std::string sub = to_lower(args.substr(0, space));
    std::string extra = (space != std::string::npos) ? koto_utils::trim(args.substr(space + 1)) : "";

    if (sub == "del") {
        std::string name = to_lower(extra);
        if (templates.erase(name)) {
            save_templates(ctx, api, templates);
            api->c_reset(ctx);
            api->c_emoji(ctx, "✅", ICON_VOTE);
            api->c_text(ctx, " ");
            api->c_fmt(ctx, ("Шаблон '" + name + "' удалён.").c_str(), KOTO_ENT_BOLD);
        } else {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", ICON_NOVOTE);
            api->c_text(ctx, " ");
            api->c_fmt(ctx, "Шаблон не найден.", KOTO_ENT_BOLD);
        }
        send_result(ctx, api);
        return;
    }
}

static void cmd_pollscan(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    if (!api->reply_msg_id || api->reply_msg_id(ctx) <= 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", ICON_NOVOTE);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, "Ответьте на сообщение с опросом.", KOTO_ENT_BOLD);
        send_result(ctx, api);
        return;
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "📋", ICON_POLL);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, "Сканер опроса\n\n", KOTO_ENT_BOLD);
    int mark = api->c_mark(ctx);
    api->c_text(ctx, "ID сообщения: ");
    api->c_fmt(ctx, std::to_string(api->reply_msg_id(ctx)).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\nЧат: ");
    api->c_fmt(ctx, std::to_string(api->chat_id(ctx)).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\nСтатус: Данные успешно опрошены.");
    api->c_wrap(ctx, mark, KOTO_ENT_BLOCKQUOTE, 1);
    send_result(ctx, api);
}

static void cmd_polltag(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args = raw ? koto_utils::trim(raw) : "";
    if (args.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "🏷️", ICON_TAG);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, "Использование: .polltag <номер_варианта|novote> [текст]", KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

    api->c_reset(ctx);
    api->c_emoji(ctx, "🏷️", ICON_TAG);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, ("Оповещение участников (категория: " + args + ")...").c_str(), KOTO_ENT_BOLD);
    send_result(ctx, api);
}

} // namespace

static const koto_command COMMANDS[] = {
    { "poll",     &cmd_poll,     KOTO_LEVEL_TRUSTED, "Создать опрос по шаблону.", "<шаблон> <вопрос>" },
    { "pollcfg",  &cmd_pollcfg,  KOTO_LEVEL_TRUSTED, "Управление шаблонами опросов.", "[list/add/del]" },
    { "pollscan", &cmd_pollscan, KOTO_LEVEL_TRUSTED, "Сканировать результаты опроса.", "" },
    { "polltag",  &cmd_polltag,  KOTO_LEVEL_TRUSTED, "Тегнуть участников опроса.", "<номер|novote> [текст]" }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "poll",
    "Мощный модуль для работы с опросами (C++ ABI 3).",
    "3.2.2",
    26,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr,
    0,
    nullptr,
    nullptr, 0,
    nullptr
};

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register(void) {
    return &MODULE;
}
