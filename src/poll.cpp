// poll.cpp — Мощный модуль для работы с опросами.
// C++ ABI 3 port of poll.py (v3.2.2 / v3.3.0).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <algorithm>
#include <cstdlib>

namespace {
using nlohmann::json;

static constexpr long long ICON_POLL   = 5956561916573782596LL; // 📋
static constexpr long long ICON_VOTE   = 5774022692642492953LL; // ✅
static constexpr long long ICON_NOVOTE = 5431449001532594346LL; // ❌

struct OptionItem {
    std::string text;
    json entities = json::array();
};

struct TemplateItem {
    std::vector<OptionItem> options;
    bool is_public = true;       // Имена участников (public_voters)
    bool multi = false;          // Несколько ответов (multiple_choice)
    bool open_answers = false;   // Добавление вариантов (open_answers)
    bool revote = true;          // Изменение ответа (revoting_disabled = !revote)
    bool shuffle = false;        // Случайный порядок (shuffle_answers)
    bool quiz = false;           // Викторина / Правильный ответ (quiz)
    bool pin = false;            // 📌 Закреп (pin)
};

static std::string to_lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static int utf16_offset(const std::string& full_text, size_t byte_pos) {
    int utf16 = 0;
    size_t i = 0;
    while (i < byte_pos && i < full_text.size()) {
        unsigned char c = (unsigned char)full_text[i];
        if (c < 0x80) { i += 1; utf16 += 1; }
        else if ((c & 0xE0) == 0xC0) { i += 2; utf16 += 1; }
        else if ((c & 0xF0) == 0xE0) { i += 3; utf16 += 1; }
        else if ((c & 0xF8) == 0xF0) { i += 4; utf16 += 2; }
        else { i += 1; }
    }
    return utf16;
}

static std::string format_user_name(const json& u) {
    if (u.contains("first_name")) {
        std::string fn = u.value("first_name", "");
        std::string ln = u.value("last_name", "");
        std::string full = fn;
        if (!ln.empty()) {
            if (!full.empty()) full += " ";
            full += ln;
        }
        if (!full.empty()) return full;
    }
    if (u.contains("username") && !u["username"].get<std::string>().empty()) {
        return "@" + u["username"].get<std::string>();
    }
    return "User " + std::to_string(u.value("id", 0LL));
}

static std::map<std::string, TemplateItem> get_templates(koto_ctx* ctx, const koto_api* api) {
    std::map<std::string, TemplateItem> res;

    // Дефолтные шаблоны
    res["данет"] = {
        { {"Да", json::array()}, {"Нет", json::array()} },
        true, false, false, true, false, false, false
    };
    res["оценка"] = {
        { {"1", json::array()}, {"2", json::array()}, {"3", json::array()}, {"4", json::array()}, {"5", json::array()} },
        true, false, false, true, false, false, false
    };
    res["время"] = {
        { {"Сейчас", json::array()}, {"Через 5 мин", json::array()}, {"Через 15 мин", json::array()}, {"Позже", json::array()} },
        true, false, false, true, false, false, false
    };

    std::vector<char> buf(65536, 0);
    int len = api->db_get(ctx, "templates_v3", buf.data(), (int)buf.size());
    if (len > 0) {
        try {
            auto j = json::parse(std::string(buf.data(), len));
            if (j.is_object()) {
                res.clear();
                for (auto it = j.begin(); it != j.end(); ++it) {
                    TemplateItem item;
                    const auto& val = it.value();
                    if (val.contains("options") && val["options"].is_array()) {
                        for (const auto& opt_j : val["options"]) {
                            if (opt_j.is_object()) {
                                item.options.push_back({
                                    opt_j.value("text", ""),
                                    opt_j.value("entities", json::array())
                                });
                            } else if (opt_j.is_string()) {
                                item.options.push_back({ opt_j.get<std::string>(), json::array() });
                            }
                        }
                    }
                    if (val.contains("flags") && val["flags"].is_object()) {
                        const auto& f = val["flags"];
                        item.is_public = f.value("public", true);
                        item.multi = f.value("multi", false);
                        item.open_answers = f.value("open_answers", false);
                        item.revote = f.value("revote", true);
                        item.shuffle = f.value("shuffle", false);
                        item.quiz = f.value("quiz", false);
                        item.pin = f.value("pin", false);
                    }
                    res[it.key()] = std::move(item);
                }
            }
        } catch (...) {}
    }
    return res;
}

static void save_templates(koto_ctx* ctx, const koto_api* api, const std::map<std::string, TemplateItem>& tpls) {
    json j = json::object();
    for (const auto& [name, td] : tpls) {
        json opt_arr = json::array();
        for (const auto& opt : td.options) {
            opt_arr.push_back({
                {"text", opt.text},
                {"entities", opt.entities}
            });
        }
        j[name] = {
            {"options", opt_arr},
            {"flags", {
                {"public", td.is_public},
                {"multi", td.multi},
                {"open_answers", td.open_answers},
                {"revote", td.revote},
                {"shuffle", td.shuffle},
                {"quiz", td.quiz},
                {"pin", td.pin}
            }}
        };
    }
    std::string s = j.dump();
    api->db_set(ctx, "templates_v3", s.c_str());
}

static void render_poll_menu(koto_ctx* ctx, const koto_api* api, bool is_callback, long long chat_id, long long msg_id) {
    auto templates = get_templates(ctx, api);

    api->c_reset(ctx);
    api->c_emoji(ctx, "📂", ICON_POLL);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, "Шаблоны опросов", KOTO_ENT_BOLD);
    api->c_text(ctx, "\n\nВыберите шаблон для настройки или создания опроса:\n");

    for (const auto& [name, td] : templates) {
        std::string label = "📝 " + name;
        std::string cb = "poll:tpl:edit:" + name;
        if (api->c_button_ex) api->c_button_ex(ctx, label.c_str(), cb.c_str(), 1, KOTO_BTN_DEFAULT, 0);
        else api->c_button(ctx, label.c_str(), cb.c_str(), 1);
    }

    if (api->c_button_ex) {
        api->c_button_ex(ctx, "➕ Создать новый", "poll:tpl:create", 1, KOTO_BTN_PRIMARY, 0);
        api->c_button_ex(ctx, "❌ Закрыть", "poll:close", 1, KOTO_BTN_DANGER, 0);
    } else {
        api->c_button(ctx, "➕ Создать новый", "poll:tpl:create", 1);
        api->c_button(ctx, "❌ Закрыть", "poll:close", 1);
    }

    if (is_callback) {
        api->c_edit_message(ctx, chat_id, msg_id, 0);
    } else {
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    }
}

static void render_template_edit(koto_ctx* ctx, const koto_api* api, const std::string& tpl_name, long long chat_id, long long msg_id) {
    auto templates = get_templates(ctx, api);
    if (templates.find(tpl_name) == templates.end()) {
        if (api->cb_answer) api->cb_answer(ctx, "❌ Шаблон не найден", 1);
        render_poll_menu(ctx, api, true, chat_id, msg_id);
        return;
    }

    const auto& td = templates[tpl_name];

    api->c_reset(ctx);
    api->c_fmt(ctx, "Шаблон: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, tpl_name.c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n\n⚙️ ");
    api->c_fmt(ctx, "Настройки:", KOTO_ENT_BOLD);
    api->c_text(ctx, "\n");
    api->c_text(ctx, (std::string("• Имена участников: ") + (td.is_public ? "✅" : "❌") + "\n").c_str());
    api->c_text(ctx, (std::string("• Несколько ответов: ") + (td.multi ? "✅" : "❌") + "\n").c_str());
    api->c_text(ctx, (std::string("• Добавление вариантов: ") + (td.open_answers ? "✅" : "❌") + "\n").c_str());
    api->c_text(ctx, (std::string("• Изменение ответа: ") + (td.revote ? "✅" : "❌") + "\n").c_str());
    api->c_text(ctx, (std::string("• Случайный порядок: ") + (td.shuffle ? "✅" : "❌") + "\n").c_str());
    api->c_text(ctx, (std::string("• Викторина: ") + (td.quiz ? "✅" : "❌") + "\n").c_str());
    api->c_text(ctx, (std::string("• 📌 Закреп: ") + (td.pin ? "✅" : "❌") + "\n\n").c_str());

    api->c_fmt(ctx, "Варианты ответов:", KOTO_ENT_BOLD);
    api->c_text(ctx, "\n");
    if (td.options.empty()) {
        api->c_fmt(ctx, "(Нет вариантов ответов)", KOTO_ENT_ITALIC);
        api->c_text(ctx, "\n");
    } else {
        for (size_t i = 0; i < td.options.size(); ++i) {
            api->c_text(ctx, (std::to_string(i + 1) + ". " + td.options[i].text + "\n").c_str());
        }
    }

    std::string btn_pub = (td.is_public ? "✅" : "❌") + std::string(" Имена");
    std::string btn_multi = (td.multi ? "✅" : "❌") + std::string(" Несколько");
    std::string btn_open = (td.open_answers ? "✅" : "❌") + std::string(" Доб. варик");
    std::string btn_revote = (td.revote ? "✅" : "❌") + std::string(" Изм. ответ");
    std::string btn_shuf = (td.shuffle ? "✅" : "❌") + std::string(" Рандом");
    std::string btn_quiz = (td.quiz ? "✅" : "❌") + std::string(" Викторина");
    std::string btn_pin = (td.pin ? "✅" : "❌") + std::string(" 📌 Закреп");

    if (api->c_button_ex) {
        api->c_button_ex(ctx, btn_pub.c_str(), ("poll:set:" + tpl_name + ":public").c_str(), 1, td.is_public ? KOTO_BTN_SUCCESS : KOTO_BTN_DEFAULT, 0);
        api->c_button_ex(ctx, btn_multi.c_str(), ("poll:set:" + tpl_name + ":multi").c_str(), 0, td.multi ? KOTO_BTN_SUCCESS : KOTO_BTN_DEFAULT, 0);

        api->c_button_ex(ctx, btn_open.c_str(), ("poll:set:" + tpl_name + ":open_answers").c_str(), 1, td.open_answers ? KOTO_BTN_SUCCESS : KOTO_BTN_DEFAULT, 0);
        api->c_button_ex(ctx, btn_revote.c_str(), ("poll:set:" + tpl_name + ":revote").c_str(), 0, td.revote ? KOTO_BTN_SUCCESS : KOTO_BTN_DEFAULT, 0);

        api->c_button_ex(ctx, btn_shuf.c_str(), ("poll:set:" + tpl_name + ":shuffle").c_str(), 1, td.shuffle ? KOTO_BTN_SUCCESS : KOTO_BTN_DEFAULT, 0);
        api->c_button_ex(ctx, btn_quiz.c_str(), ("poll:set:" + tpl_name + ":quiz").c_str(), 0, td.quiz ? KOTO_BTN_SUCCESS : KOTO_BTN_DEFAULT, 0);

        api->c_button_ex(ctx, btn_pin.c_str(), ("poll:set:" + tpl_name + ":pin").c_str(), 1, td.pin ? KOTO_BTN_SUCCESS : KOTO_BTN_DEFAULT, 0);

        api->c_button_ex(ctx, "➕ Вариант", ("poll:opt:add:" + tpl_name).c_str(), 1, KOTO_BTN_PRIMARY, 0);
        api->c_button_ex(ctx, "➖ Вариант", ("poll:opt:del_menu:" + tpl_name).c_str(), 0, KOTO_BTN_DANGER, 0);

        api->c_button_ex(ctx, "🗑️ Удалить шаблон", ("poll:tpl:del:" + tpl_name).c_str(), 1, KOTO_BTN_DANGER, 0);
        api->c_button_ex(ctx, "🔙 Назад", "poll:menu", 1, KOTO_BTN_DEFAULT, 0);
    } else {
        api->c_button(ctx, btn_pub.c_str(), ("poll:set:" + tpl_name + ":public").c_str(), 1);
        api->c_button(ctx, btn_multi.c_str(), ("poll:set:" + tpl_name + ":multi").c_str(), 0);

        api->c_button(ctx, btn_open.c_str(), ("poll:set:" + tpl_name + ":open_answers").c_str(), 1);
        api->c_button(ctx, btn_revote.c_str(), ("poll:set:" + tpl_name + ":revote").c_str(), 0);

        api->c_button(ctx, btn_shuf.c_str(), ("poll:set:" + tpl_name + ":shuffle").c_str(), 1);
        api->c_button(ctx, btn_quiz.c_str(), ("poll:set:" + tpl_name + ":quiz").c_str(), 0);

        api->c_button(ctx, btn_pin.c_str(), ("poll:set:" + tpl_name + ":pin").c_str(), 1);

        api->c_button(ctx, "➕ Вариант", ("poll:opt:add:" + tpl_name).c_str(), 1);
        api->c_button(ctx, "➖ Вариант", ("poll:opt:del_menu:" + tpl_name).c_str(), 0);

        api->c_button(ctx, "🗑️ Удалить шаблон", ("poll:tpl:del:" + tpl_name).c_str(), 1);
        api->c_button(ctx, "🔙 Назад", "poll:menu", 1);
    }

    if (msg_id > 0) {
        api->c_edit_message(ctx, chat_id, msg_id, 0);
    } else {
        if (api->c_send) api->c_send(ctx, chat_id, 0);
        else if (api->c_reply) api->c_reply(ctx, 0);
        else if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    }
}

static void render_opt_del_menu(koto_ctx* ctx, const koto_api* api, const std::string& tpl_name, long long chat_id, long long msg_id) {
    auto templates = get_templates(ctx, api);
    if (templates.find(tpl_name) == templates.end()) {
        if (api->cb_answer) api->cb_answer(ctx, "❌ Шаблон не найден", 1);
        render_poll_menu(ctx, api, true, chat_id, msg_id);
        return;
    }

    const auto& td = templates[tpl_name];
    api->c_reset(ctx);
    api->c_fmt(ctx, "Удаление варианта из '", KOTO_ENT_BOLD);
    api->c_fmt(ctx, tpl_name.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, "':\n\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "Нажмите на вариант, чтобы удалить его:\n");

    for (size_t i = 0; i < td.options.size(); ++i) {
        std::string label = "❌ " + td.options[i].text;
        std::string cb = "poll:opt:del:" + tpl_name + ":" + std::to_string(i);
        if (api->c_button_ex) api->c_button_ex(ctx, label.c_str(), cb.c_str(), 1, KOTO_BTN_DANGER, 0);
        else api->c_button(ctx, label.c_str(), cb.c_str(), 1);
    }

    std::string back_cb = "poll:tpl:edit:" + tpl_name;
    if (api->c_button_ex) api->c_button_ex(ctx, "🔙 Назад", back_cb.c_str(), 1, KOTO_BTN_DEFAULT, 0);
    else api->c_button(ctx, "🔙 Назад", back_cb.c_str(), 1);

    api->c_edit_message(ctx, chat_id, msg_id, 0);
}

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    if (!api->cb_data) return;
    const char* raw_cb = api->cb_data(ctx);
    if (!raw_cb) return;
    std::string data = raw_cb;

    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) {
        if (api->cb_answer) api->cb_answer(ctx, "⛔ Доступ запрещён", 1);
        return;
    }

    long long chat_id = api->chat_id(ctx);
    long long msg_id = api->cb_message_id ? api->cb_message_id(ctx) : 0;

    if (data == "poll:close") {
        api->db_del(ctx, "wait_type");
        api->db_del(ctx, "wait_tpl");
        api->db_del(ctx, "wait_msg");
        api->c_reset(ctx);
        api->c_fmt(ctx, "Панель опросов закрыта.", KOTO_ENT_ITALIC);
        api->c_edit_message(ctx, chat_id, msg_id, 0);
        if (api->cb_answer) api->cb_answer(ctx, "Панель закрыта", 0);
        return;
    }

    if (data == "poll:menu") {
        api->db_del(ctx, "wait_type");
        api->db_del(ctx, "wait_tpl");
        api->db_del(ctx, "wait_msg");
        render_poll_menu(ctx, api, true, chat_id, msg_id);
        if (api->cb_answer) api->cb_answer(ctx, "", 0);
        return;
    }

    if (data == "poll:tpl:create") {
        api->db_set(ctx, "wait_type", "new_template");
        api->db_set(ctx, "wait_msg", std::to_string(msg_id).c_str());

        api->c_reset(ctx);
        api->c_text(ctx, "⌨️ ");
        api->c_fmt(ctx, "Введите название для нового шаблона.", KOTO_ENT_BOLD);
        api->c_text(ctx, "\n\nОтправьте сообщение с названием в этот чат.");
        if (api->c_button_ex) api->c_button_ex(ctx, "🔙 Назад", "poll:menu", 1, KOTO_BTN_DEFAULT, 0);
        else api->c_button(ctx, "🔙 Назад", "poll:menu", 1);

        api->c_edit_message(ctx, chat_id, msg_id, 0);
        if (api->cb_answer) api->cb_answer(ctx, "Ожидаю название шаблона...", 0);
        return;
    }

    if (data.rfind("poll:tpl:edit:", 0) == 0) {
        std::string tpl_name = data.substr(14);
        api->db_del(ctx, "wait_type");
        api->db_del(ctx, "wait_tpl");
        api->db_del(ctx, "wait_msg");
        render_template_edit(ctx, api, tpl_name, chat_id, msg_id);
        if (api->cb_answer) api->cb_answer(ctx, "", 0);
        return;
    }

    if (data.rfind("poll:set:", 0) == 0) {
        auto p1 = data.find(':', 9);
        if (p1 != std::string::npos) {
            std::string tpl_name = data.substr(9, p1 - 9);
            std::string flag = data.substr(p1 + 1);

            auto templates = get_templates(ctx, api);
            if (templates.find(tpl_name) != templates.end()) {
                auto& td = templates[tpl_name];
                if (flag == "public") td.is_public = !td.is_public;
                else if (flag == "multi") td.multi = !td.multi;
                else if (flag == "open_answers") td.open_answers = !td.open_answers;
                else if (flag == "revote") td.revote = !td.revote;
                else if (flag == "shuffle") td.shuffle = !td.shuffle;
                else if (flag == "quiz") td.quiz = !td.quiz;
                else if (flag == "pin") td.pin = !td.pin;

                save_templates(ctx, api, templates);
                render_template_edit(ctx, api, tpl_name, chat_id, msg_id);
                if (api->cb_answer) api->cb_answer(ctx, "Настройка сохранена", 0);
                return;
            }
        }
    }

    if (data.rfind("poll:opt:add:", 0) == 0) {
        std::string tpl_name = data.substr(13);
        api->db_set(ctx, "wait_type", "add_option");
        api->db_set(ctx, "wait_tpl", tpl_name.c_str());
        api->db_set(ctx, "wait_msg", std::to_string(msg_id).c_str());

        api->c_reset(ctx);
        api->c_text(ctx, "⌨️ ");
        api->c_fmt(ctx, ("Введите вариант ответа для '" + tpl_name + "'.").c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, "\n\n");
        api->c_fmt(ctx, "Поддерживаются любые символы и Telegram Premium кастом-эмодзи!", KOTO_ENT_ITALIC);
        api->c_text(ctx, "\nОтправьте текст варианта в этот чат.");

        std::string back_cb = "poll:tpl:edit:" + tpl_name;
        if (api->c_button_ex) api->c_button_ex(ctx, "🔙 Назад", back_cb.c_str(), 1, KOTO_BTN_DEFAULT, 0);
        else api->c_button(ctx, "🔙 Назад", back_cb.c_str(), 1);

        api->c_edit_message(ctx, chat_id, msg_id, 0);
        if (api->cb_answer) api->cb_answer(ctx, "Ожидаю вариант ответа...", 0);
        return;
    }

    if (data.rfind("poll:opt:del_menu:", 0) == 0) {
        std::string tpl_name = data.substr(18);
        render_opt_del_menu(ctx, api, tpl_name, chat_id, msg_id);
        if (api->cb_answer) api->cb_answer(ctx, "", 0);
        return;
    }

    if (data.rfind("poll:opt:del:", 0) == 0) {
        auto p1 = data.find(':', 13);
        if (p1 != std::string::npos) {
            std::string tpl_name = data.substr(13, p1 - 13);
            int idx = std::atoi(data.substr(p1 + 1).c_str());

            auto templates = get_templates(ctx, api);
            if (templates.find(tpl_name) != templates.end()) {
                auto& td = templates[tpl_name];
                if (idx >= 0 && idx < (int)td.options.size()) {
                    td.options.erase(td.options.begin() + idx);
                    save_templates(ctx, api, templates);
                    render_opt_del_menu(ctx, api, tpl_name, chat_id, msg_id);
                    if (api->cb_answer) api->cb_answer(ctx, "Вариант удалён", 0);
                    return;
                }
            }
        }
    }

    if (data.rfind("poll:tpl:del:", 0) == 0) {
        std::string tpl_name = data.substr(13);
        auto templates = get_templates(ctx, api);
        if (templates.erase(tpl_name)) {
            save_templates(ctx, api, templates);
            if (api->cb_answer) api->cb_answer(ctx, "Шаблон удалён", 1);
        }
        render_poll_menu(ctx, api, true, chat_id, msg_id);
        return;
    }
}

static int on_watch(koto_ctx* ctx, const koto_api* api) {
    char wtype[64] = {0};
    int wlen = api->db_get(ctx, "wait_type", wtype, sizeof(wtype));
    if (wlen <= 0 || !wtype[0]) return 0;

    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER) && !api->is_outgoing(ctx)) return 0;

    const char* raw_text = api->msg_text(ctx);
    std::string text_val = raw_text ? koto_utils::trim(raw_text) : "";
    if (text_val.empty()) return 0;

    char msg_buf[32] = {0};
    long long panel_msg_id = 0;
    if (api->db_get(ctx, "wait_msg", msg_buf, sizeof(msg_buf)) > 0) {
        panel_msg_id = std::atoll(msg_buf);
    }
    long long chat_id = api->chat_id(ctx);

    if (api->is_outgoing(ctx) && api->delete_msg) {
        api->delete_msg(ctx);
    }

    if (strcmp(wtype, "new_template") == 0) {
        std::string name = to_lower(text_val);
        auto templates = get_templates(ctx, api);
        if (templates.find(name) != templates.end()) {
            if (panel_msg_id > 0) {
                api->c_reset(ctx);
                api->c_fmt(ctx, ("⚠️ Шаблон '" + name + "' уже существует!").c_str(), KOTO_ENT_BOLD);
                api->c_text(ctx, "\n\nВведите другое имя для нового шаблона:");
                if (api->c_button_ex) api->c_button_ex(ctx, "🔙 Назад", "poll:menu", 1, KOTO_BTN_DEFAULT, 0);
                else api->c_button(ctx, "🔙 Назад", "poll:menu", 1);
                api->c_edit_message(ctx, chat_id, panel_msg_id, 0);
            }
            return 1;
        }

        TemplateItem item;
        item.is_public = true;
        item.multi = false;
        item.open_answers = false;
        item.revote = true;
        item.shuffle = false;
        item.quiz = false;
        item.pin = false;
        templates[name] = std::move(item);
        save_templates(ctx, api, templates);

        api->db_del(ctx, "wait_type");
        api->db_del(ctx, "wait_msg");

        render_template_edit(ctx, api, name, chat_id, panel_msg_id);
        return 1;
    }

    if (strcmp(wtype, "add_option") == 0) {
        char tpl_buf[128] = {0};
        api->db_get(ctx, "wait_tpl", tpl_buf, sizeof(tpl_buf));
        std::string tpl_name = tpl_buf;

        auto templates = get_templates(ctx, api);
        if (templates.find(tpl_name) == templates.end()) {
            api->db_del(ctx, "wait_type");
            api->db_del(ctx, "wait_tpl");
            api->db_del(ctx, "wait_msg");
            return 1;
        }

        auto& td = templates[tpl_name];
        if (td.options.size() >= 12) {
            render_template_edit(ctx, api, tpl_name, chat_id, panel_msg_id);
            api->db_del(ctx, "wait_type");
            api->db_del(ctx, "wait_tpl");
            api->db_del(ctx, "wait_msg");
            return 1;
        }

        json ents_arr = json::array();
        int ent_cnt = api->msg_entity_count ? api->msg_entity_count(ctx) : 0;
        for (int i = 0; i < ent_cnt; ++i) {
            int type = 0, off = 0, len = 0;
            long long doc_id = 0;
            char url[256] = {0};
            api->msg_entity(ctx, i, &type, &off, &len, &doc_id, url, sizeof(url));
            json ej = {{"type_id", type}, {"offset", off}, {"length", len}};
            if (doc_id != 0) ej["document_id"] = doc_id;
            if (url[0]) ej["url"] = url;
            ents_arr.push_back(std::move(ej));
        }

        td.options.push_back({ text_val, std::move(ents_arr) });
        save_templates(ctx, api, templates);

        api->db_del(ctx, "wait_type");
        api->db_del(ctx, "wait_tpl");
        api->db_del(ctx, "wait_msg");

        render_template_edit(ctx, api, tpl_name, chat_id, panel_msg_id);
        return 1;
    }

    return 0;
}

static void cmd_poll(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? koto_utils::trim(raw_args) : "";
    auto templates = get_templates(ctx, api);

    auto space = args.find(' ');
    if (args.empty() || space == std::string::npos) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "📋", ICON_POLL);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, "Используйте: .poll <шаблон> <вопрос>\n\n", KOTO_ENT_ITALIC);
        api->c_fmt(ctx, "Доступные шаблоны: ", KOTO_ENT_BOLD);
        bool first = true;
        for (const auto& [name, _] : templates) {
            if (!first) api->c_text(ctx, ", ");
            api->c_fmt(ctx, name.c_str(), KOTO_ENT_CODE);
            first = false;
        }
        if (templates.empty()) api->c_text(ctx, "(нет шаблонов)");
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    std::string tpl_name = to_lower(args.substr(0, space));
    std::string question = koto_utils::trim(args.substr(space + 1));

    if (templates.find(tpl_name) == templates.end()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", ICON_NOVOTE);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, ("Шаблон '" + tpl_name + "' не найден.").c_str(), KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    const auto& td = templates[tpl_name];
    if (td.options.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", ICON_NOVOTE);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, ("Шаблон '" + tpl_name + "' пуст.").c_str(), KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    const char* full_msg = api->msg_text(ctx);
    std::string full_text = full_msg ? full_msg : "";
    size_t q_pos = full_text.find(question);
    int q_u16 = (q_pos != std::string::npos) ? utf16_offset(full_text, q_pos) : 0;

    json q_entities = json::array();
    int ent_cnt = api->msg_entity_count ? api->msg_entity_count(ctx) : 0;
    for (int i = 0; i < ent_cnt; ++i) {
        int type = 0, off = 0, len = 0;
        long long doc_id = 0;
        char url[256] = {0};
        api->msg_entity(ctx, i, &type, &off, &len, &doc_id, url, sizeof(url));
        if (off >= q_u16) {
            json ej = {{"type_id", type}, {"offset", off - q_u16}, {"length", len}};
            if (doc_id != 0) ej["document_id"] = doc_id;
            if (url[0]) ej["url"] = url;
            q_entities.push_back(std::move(ej));
        }
    }

    json answers_arr = json::array();
    for (const auto& opt : td.options) {
        answers_arr.push_back({
            {"text", opt.text},
            {"entities", opt.entities}
        });
    }

    long long cid = api->chat_id(ctx);
    json req = {
        {"peer", {
            {"type", (cid < 0 ? "channel" : "user")},
            {"id", std::abs(cid)}
        }},
        {"question", {
            {"text", question},
            {"entities", q_entities}
        }},
        {"answers", answers_arr},
        {"public_voters", td.is_public},
        {"multiple_choice", td.multi},
        {"open_answers", td.open_answers},
        {"revoting_disabled", !td.revote},
        {"shuffle_answers", td.shuffle},
        {"quiz", td.quiz},
        {"pin", td.pin}
    };

    if (api->host_action_json) {
        api->host_action_json(ctx, "telegram.poll.send.v1", req.dump().c_str());
    }

    if (api->is_outgoing(ctx) && api->delete_msg) {
        api->delete_msg(ctx);
    }
}

static void cmd_pollcfg(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    render_poll_menu(ctx, api, false, api->chat_id(ctx), api->msg_id(ctx));
}

static json make_poll_scan_req(koto_ctx* ctx, const koto_api* api, long long reply_id) {
    long long cid = api->chat_id(ctx);
    long long bare_id = std::abs(cid);
    if (bare_id > 1000000000000LL) {
        bare_id -= 1000000000000LL;
    }
    int ct = api->chat_type(ctx);
    std::string ptype = "user";
    if (ct == KOTO_CHAT_CHANNEL || cid <= -1000000000000LL) {
        ptype = "channel";
    } else if (ct == KOTO_CHAT_GROUP || cid < 0) {
        ptype = "chat";
    }
    return {
        {"peer", {
            {"type", ptype},
            {"id", bare_id}
        }},
        {"id", (int)reply_id}
    };
}

static void cmd_pollscan(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long reply_id = api->reply_msg_id ? api->reply_msg_id(ctx) : 0;
    if (reply_id <= 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", ICON_NOVOTE);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, "Ответьте на сообщение с опросом.", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    json q_req = make_poll_scan_req(ctx, api, reply_id);

    std::vector<char> res_buf(256 * 1024, 0);
    int res_len = api->host_call_json ? api->host_call_json(ctx, "telegram.poll.scan.v1", q_req.dump().c_str(), res_buf.data(), (int)res_buf.size()) : -1;

    if (res_len <= 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", ICON_NOVOTE);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, "Не удалось получить данные опроса (возможно, опрос не найден или хост не подтвердил запрос).", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    try {
        auto resp = json::parse(std::string(res_buf.data(), res_len));
        if (!resp.value("ok", false)) {
            std::string err = resp.value("error", "unknown_error");
            if (err == "poll_is_anonymous") {
                api->c_reset(ctx);
                api->c_emoji(ctx, "ℹ️", ICON_POLL);
                api->c_text(ctx, " ");
                api->c_fmt(ctx, "Опрос анонимный. Данные недоступны.", KOTO_ENT_ITALIC);
            } else {
                api->c_reset(ctx);
                api->c_emoji(ctx, "❌", ICON_NOVOTE);
                api->c_text(ctx, " ");
                api->c_fmt(ctx, ("Ошибка: " + err).c_str(), KOTO_ENT_BOLD);
            }
            if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
            else api->c_reply(ctx, 0);
            return;
        }

        const auto& poll = resp["poll"];
        std::string q_text = poll["question"].value("text", "");

        api->c_reset(ctx);
        int mark = api->c_mark(ctx);

        api->c_emoji(ctx, "📋", ICON_POLL);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, "Сканер опроса", KOTO_ENT_BOLD);
        api->c_text(ctx, "\n");
        api->c_fmt(ctx, q_text.c_str(), KOTO_ENT_ITALIC);
        api->c_text(ctx, "\n\n");

        int total_chars = (int)q_text.size() + 50;

        if (poll.contains("options") && poll["options"].is_array()) {
            int opt_num = 1;
            for (const auto& opt : poll["options"]) {
                std::string opt_text = opt.value("text", "");
                int count = opt.value("voter_count", 0);

                api->c_emoji(ctx, "✅", ICON_VOTE);
                api->c_text(ctx, " ");
                std::string opt_title = std::to_string(opt_num++) + ". " + opt_text + " (" + std::to_string(count) + ")";
                api->c_fmt(ctx, opt_title.c_str(), KOTO_ENT_BOLD);
                api->c_text(ctx, "\n");
                total_chars += (int)opt_text.size() + 20;

                if (count == 0) {
                    api->c_fmt(ctx, "    (Нет голосов)", KOTO_ENT_ITALIC);
                    api->c_text(ctx, "\n\n");
                    total_chars += 20;
                } else if (opt.contains("voters") && opt["voters"].is_array()) {
                    const auto& vlist = opt["voters"];
                    int v_idx = 0;
                    for (const auto& v : vlist) {
                        if (total_chars > 3300) {
                            int rem = (int)vlist.size() - v_idx;
                            if (rem > 0) {
                                api->c_fmt(ctx, ("    ... и ещё " + std::to_string(rem) + " чел.").c_str(), KOTO_ENT_ITALIC);
                                api->c_text(ctx, "\n");
                                total_chars += 25;
                            }
                            break;
                        }
                        long long uid = v.value("id", 0LL);
                        std::string uname = format_user_name(v);
                        api->c_text(ctx, "    • ");
                        api->c_url(ctx, uname.c_str(), ("tg://user?id=" + std::to_string(uid)).c_str());
                        api->c_text(ctx, "\n");
                        total_chars += (int)uname.size() + 10;
                        v_idx++;
                    }
                    api->c_text(ctx, "\n");
                    total_chars += 1;
                }
            }
        }

        // Не голосовавшие
        if (resp.contains("non_voters") && resp["non_voters"].is_array()) {
            const auto& nv = resp["non_voters"];
            api->c_emoji(ctx, "❌", ICON_NOVOTE);
            api->c_text(ctx, " ");
            std::string nv_title = "Не голосовали (" + std::to_string(nv.size()) + ")";
            api->c_fmt(ctx, nv_title.c_str(), KOTO_ENT_BOLD);
            api->c_text(ctx, "\n");
            total_chars += 30;

            if (nv.empty()) {
                api->c_fmt(ctx, "    (Все проголосовали или нет данных)", KOTO_ENT_ITALIC);
                api->c_text(ctx, "\n\n");
                total_chars += 40;
            } else {
                int nv_idx = 0;
                for (const auto& v : nv) {
                    if (total_chars > 3500) {
                        int rem = (int)nv.size() - nv_idx;
                        if (rem > 0) {
                            api->c_fmt(ctx, ("    ... и ещё " + std::to_string(rem) + " чел.").c_str(), KOTO_ENT_ITALIC);
                            api->c_text(ctx, "\n");
                            total_chars += 25;
                        }
                        break;
                    }
                    long long uid = v.value("id", 0LL);
                    std::string uname = format_user_name(v);
                    api->c_text(ctx, "    • ");
                    api->c_url(ctx, uname.c_str(), ("tg://user?id=" + std::to_string(uid)).c_str());
                    api->c_text(ctx, "\n");
                    total_chars += (int)uname.size() + 10;
                    nv_idx++;
                }
                api->c_text(ctx, "\n");
                total_chars += 1;
            }
        }

        // Не состоят в группе
        if (resp.contains("not_in_chat") && resp["not_in_chat"].is_array()) {
            const auto& nic = resp["not_in_chat"];
            if (!nic.empty()) {
                api->c_emoji(ctx, "🚪", ICON_NOVOTE);
                api->c_text(ctx, " ");
                std::string nic_title = "Не состоят в группе (" + std::to_string(nic.size()) + ")";
                api->c_fmt(ctx, nic_title.c_str(), KOTO_ENT_BOLD);
                api->c_text(ctx, "\n");
                total_chars += 35;
                int nic_idx = 0;
                for (const auto& v : nic) {
                    if (total_chars > 3700) {
                        int rem = (int)nic.size() - nic_idx;
                        if (rem > 0) {
                            api->c_fmt(ctx, ("    ... и ещё " + std::to_string(rem) + " чел.").c_str(), KOTO_ENT_ITALIC);
                            api->c_text(ctx, "\n");
                            total_chars += 25;
                        }
                        break;
                    }
                    long long uid = v.value("id", 0LL);
                    std::string uname = format_user_name(v);
                    api->c_text(ctx, "    • ");
                    api->c_url(ctx, uname.c_str(), ("tg://user?id=" + std::to_string(uid)).c_str());
                    api->c_text(ctx, "\n");
                    total_chars += (int)uname.size() + 10;
                    nic_idx++;
                }
                api->c_text(ctx, "\n");
            }
        }

        api->c_wrap(ctx, mark, KOTO_ENT_BLOCKQUOTE, 1);

        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    } catch (const std::exception& e) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", ICON_NOVOTE);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, ("Ошибка обработки данных: " + std::string(e.what())).c_str(), KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    }
}

static void cmd_polltag(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? koto_utils::trim(raw_args) : "";
    if (args.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", ICON_NOVOTE);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, "Использование: .polltag <номер|novote> [текст]", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    long long reply_id = api->reply_msg_id ? api->reply_msg_id(ctx) : 0;
    if (reply_id <= 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", ICON_NOVOTE);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, "Ответьте на сообщение с опросом.", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    auto space = args.find(' ');
    std::string cat_arg = to_lower(space == std::string::npos ? args : args.substr(0, space));
    std::string custom_text = (space != std::string::npos) ? koto_utils::trim(args.substr(space + 1)) : "";

    long long cid = api->chat_id(ctx);
    json q_req = make_poll_scan_req(ctx, api, reply_id);

    std::vector<char> res_buf(256 * 1024, 0);
    int res_len = api->host_call_json ? api->host_call_json(ctx, "telegram.poll.scan.v1", q_req.dump().c_str(), res_buf.data(), (int)res_buf.size()) : -1;

    if (res_len <= 0) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", ICON_NOVOTE);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, "Не удалось получить данные опроса.", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    try {
        auto resp = json::parse(std::string(res_buf.data(), res_len));
        if (!resp.value("ok", false)) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❌", ICON_NOVOTE);
            api->c_text(ctx, " ");
            api->c_fmt(ctx, "Опрос анонимный или недоступен.", KOTO_ENT_BOLD);
            if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
            else api->c_reply(ctx, 0);
            return;
        }

        std::vector<json> target_users;
        std::string target_label;

        if (cat_arg == "novote") {
            target_label = "Не проголосовавшие";
            if (resp.contains("non_voters") && resp["non_voters"].is_array()) {
                for (const auto& u : resp["non_voters"]) target_users.push_back(u);
            }
        } else {
            int opt_idx = std::atoi(cat_arg.c_str()) - 1;
            const auto& opts = resp["poll"]["options"];
            if (opt_idx >= 0 && opt_idx < (int)opts.size()) {
                const auto& opt = opts[opt_idx];
                target_label = "Выбравшие '" + opt.value("text", "") + "'";
                if (opt.contains("voters") && opt["voters"].is_array()) {
                    for (const auto& u : opt["voters"]) target_users.push_back(u);
                }
            } else {
                api->c_reset(ctx);
                api->c_emoji(ctx, "❌", ICON_NOVOTE);
                api->c_text(ctx, " ");
                api->c_fmt(ctx, ("Нет варианта №" + cat_arg + ".").c_str(), KOTO_ENT_BOLD);
                if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
                else api->c_reply(ctx, 0);
                return;
            }
        }

        if (target_users.empty()) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "ℹ️", ICON_POLL);
            api->c_text(ctx, " ");
            api->c_fmt(ctx, ("В категории '" + target_label + "' никого нет.").c_str(), KOTO_ENT_ITALIC);
            if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
            else api->c_reply(ctx, 0);
            return;
        }

        if (api->is_outgoing(ctx) && api->delete_msg) {
            api->delete_msg(ctx);
        }

        size_t chunk_size = 5;
        for (size_t i = 0; i < target_users.size(); i += chunk_size) {
            api->c_reset(ctx);
            if (!custom_text.empty()) {
                api->c_text(ctx, (custom_text + "\n").c_str());
            }

            for (size_t j = i; j < i + chunk_size && j < target_users.size(); ++j) {
                if (j > i) api->c_text(ctx, " ");
                const auto& u = target_users[j];
                std::string un = u.value("username", "");
                long long uid = u.value("id", 0LL);
                if (!un.empty()) {
                    api->c_text(ctx, ("@" + un).c_str());
                } else {
                    std::string name = format_user_name(u);
                    api->c_url(ctx, name.c_str(), ("tg://user?id=" + std::to_string(uid)).c_str());
                }
            }
            api->c_send(ctx, cid, 0);
        }
    } catch (const std::exception& e) {
        api->c_reset(ctx);
        api->c_fmt(ctx, ("❌ Ошибка: " + std::string(e.what())).c_str(), KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
    }
}

static const koto_command COMMANDS[] = {
    {
        "poll",
        &cmd_poll,
        KOTO_LEVEL_TRUSTED,
        "Создать нативный опрос по шаблону с поддержкой кастом-эмодзи и закрепа.",
        "<шаблон> <вопрос>"
    },
    {
        "pollcfg",
        &cmd_pollcfg,
        KOTO_LEVEL_TRUSTED,
        "Открыть интерактивные настройки шаблонов опросов с инлайн-кнопками.",
        nullptr
    },
    {
        "pollscan",
        &cmd_pollscan,
        KOTO_LEVEL_TRUSTED,
        "Сканировать результаты опроса (в ответ на опрос).",
        nullptr
    },
    {
        "polltag",
        &cmd_polltag,
        KOTO_LEVEL_TRUSTED,
        "Тегать пользователей из категории опроса.",
        "<номер|novote> [текст]"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "poll",
    "Мощный модуль для работы с опросами (C++ ABI 3).",
    "3.4.0",
    86,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    &on_watch,
    KOTO_WATCH_ALL,
    &on_callback,
    nullptr, 0,
    "https://gitflic.ru/project/koteyka/kotemodules/blob/raw?file=modules/poll.so"
};

} // namespace

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
