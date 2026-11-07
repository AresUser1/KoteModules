#include "module_abi.h"
#include "nlohmann/json.hpp"

#include <string>
#include <vector>
#include <sstream>
#include <regex>
#include <algorithm>
#include <cmath>

namespace {
using json = nlohmann::json;

constexpr const char* VERSION = "1.0.1";

// Helper URL encoding
std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : value) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::uppercase << ((int)(unsigned char)c) << std::nouppercase;
        }
    }
    return escaped.str();
}

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

// HTML unescape
std::string html_unescape(const std::string& str) {
    std::string res = str;
    static const std::vector<std::pair<std::string, std::string>> reps = {
        {"&quot;", "\""}, {"&amp;", "&"}, {"&apos;", "'"}, {"&#39;", "'"},
        {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "}
    };
    for (const auto& [from, to] : reps) {
        size_t pos = 0;
        while ((pos = res.find(from, pos)) != std::string::npos) {
            res.replace(pos, from.length(), to);
            pos += to.length();
        }
    }
    return res;
}

std::string aki_headers(const std::string& lang) {
    return "Content-Type: application/x-www-form-urlencoded\r\n"
           "x-requested-with: XMLHttpRequest\r\n"
           "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\n"
           "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8\r\n"
           "Accept-Language: ru-RU,ru;q=0.9,en-US;q=0.8,en;q=0.7\r\n"
           "Origin: https://" + lang + ".akinator.com\r\n"
           "Referer: https://" + lang + ".akinator.com/game";
}

std::string http_post_req(koto_ctx* ctx, const koto_api* api, const std::string& url, const std::string& body, const std::string& headers) {
    if (!api->http_post) return "";
    std::vector<unsigned char> buf(256 * 1024, 0);
    long long n = api->http_post(ctx, url.c_str(), body.c_str(), headers.c_str(), buf.data(), (int)buf.size());
    if (n <= 0) return "";
    return std::string(reinterpret_cast<const char*>(buf.data()), (size_t)n);
}

// Game state struct stored in DB
struct AkiState {
    std::string lang = "ru";
    std::string session;
    std::string step = "1";
    std::string prog = "0";
    std::string slp;
    int num = 1;
    bool child_mode = false;
    std::string last_q;
    std::string guess_name;
    std::string guess_desc;
    std::string guess_photo;
};

std::string state_db_key(long long chat_id) {
    return "aki_state:" + std::to_string(chat_id);
}

void save_state(koto_ctx* ctx, const koto_api* api, long long chat_id, const AkiState& st) {
    json j;
    j["lang"] = st.lang;
    j["session"] = st.session;
    j["step"] = st.step;
    j["prog"] = st.prog;
    j["slp"] = st.slp;
    j["num"] = st.num;
    j["child_mode"] = st.child_mode;
    j["last_q"] = st.last_q;
    j["guess_name"] = st.guess_name;
    j["guess_desc"] = st.guess_desc;
    j["guess_photo"] = st.guess_photo;
    std::string s = j.dump();
    api->db_set(ctx, state_db_key(chat_id).c_str(), s.c_str());
}

AkiState load_state(koto_ctx* ctx, const koto_api* api, long long chat_id) {
    AkiState st;
    char buf[16384] = {0};
    int n = api->db_get(ctx, state_db_key(chat_id).c_str(), buf, sizeof(buf));
    if (n <= 0) return st;
    try {
        json j = json::parse(std::string(buf, (size_t)n));
        st.lang = j.value("lang", "ru");
        st.session = j.value("session", "");
        st.step = j.value("step", "1");
        st.prog = j.value("prog", "0");
        st.slp = j.value("slp", "");
        st.num = j.value("num", 1);
        st.child_mode = j.value("child_mode", false);
        st.last_q = j.value("last_q", "");
        st.guess_name = j.value("guess_name", "");
        st.guess_desc = j.value("guess_desc", "");
        st.guess_photo = j.value("guess_photo", "");
    } catch (...) {}
    return st;
}

void clear_state(koto_ctx* ctx, const koto_api* api, long long chat_id) {
    api->db_del(ctx, state_db_key(chat_id).c_str());
}

void add_question_buttons(koto_ctx* ctx, const koto_api* api, const std::string& lang) {
    bool is_ru = (lang == "ru");
    if (api->c_button_ex) {
        api->c_button_ex(ctx, is_ru ? "✅ Да" : "✅ Yes", "aki:ans:0", 1, KOTO_BTN_SUCCESS, 0);
        api->c_button_ex(ctx, is_ru ? "❌ Нет" : "❌ No", "aki:ans:1", 0, KOTO_BTN_DANGER, 0);
        api->c_button_ex(ctx, is_ru ? "🤷 Не знаю" : "🤷 Don't know", "aki:ans:2", 0, KOTO_BTN_DEFAULT, 0);
        
        api->c_button_ex(ctx, is_ru ? "🤔 Возможно" : "🤔 Probably", "aki:ans:3", 1, KOTO_BTN_PRIMARY, 0);
        api->c_button_ex(ctx, is_ru ? "👎 Скорее нет" : "👎 Probably not", "aki:ans:4", 0, KOTO_BTN_DEFAULT, 0);
        api->c_button_ex(ctx, is_ru ? "🛑 Стоп" : "🛑 Stop", "aki:stop", 1, KOTO_BTN_DANGER, 0);
    } else {
        api->c_button(ctx, is_ru ? "✅ Да" : "✅ Yes", "aki:ans:0", 1);
        api->c_button(ctx, is_ru ? "❌ Нет" : "❌ No", "aki:ans:1", 0);
        api->c_button(ctx, is_ru ? "🤷 Не знаю" : "🤷 Don't know", "aki:ans:2", 0);
        
        api->c_button(ctx, is_ru ? "🤔 Возможно" : "🤔 Probably", "aki:ans:3", 1);
        api->c_button(ctx, is_ru ? "👎 Скорее нет" : "👎 Probably not", "aki:ans:4", 0);
        api->c_button(ctx, is_ru ? "🛑 Стоп" : "🛑 Stop", "aki:stop", 1);
    }
}

void add_guess_buttons(koto_ctx* ctx, const koto_api* api, const std::string& lang) {
    bool is_ru = (lang == "ru");
    if (api->c_button_ex) {
        api->c_button_ex(ctx, is_ru ? "✅ Да, это он!" : "✅ Yes, that's him!", "aki:win", 1, KOTO_BTN_SUCCESS, 0);
        api->c_button_ex(ctx, is_ru ? "❌ Это не он" : "❌ Not him", "aki:rej", 0, KOTO_BTN_DANGER, 0);
    } else {
        api->c_button(ctx, is_ru ? "✅ Да, это он!" : "✅ Yes, that's him!", "aki:win", 1);
        api->c_button(ctx, is_ru ? "❌ Это не он" : "❌ Not him", "aki:rej", 0);
    }
}

// ── Command: .aki / .akinator ────────────────────────────────────────────────
void cmd_akinator(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";
    
    char lang_buf[32] = {0};
    std::string lang = "ru";
    if (api->setting_get && api->setting_get(ctx, "lang", lang_buf, sizeof(lang_buf)) > 0 && lang_buf[0]) {
        lang = lang_buf;
    }
    if (args.find("en") != std::string::npos) lang = "en";
    else if (args.find("ru") != std::string::npos) lang = "ru";

    bool child = (args.find("--child") != std::string::npos || args.find("-c") != std::string::npos);

    api->c_reset(ctx);
    api->c_fmt(ctx, lang == "ru" ? "🔮 Акинатор запускается…" : "🔮 Akinator is starting…", KOTO_ENT_BOLD);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);

    // Call Akinator start game
    std::string url = "https://" + lang + ".akinator.com/game";
    std::string body = "sid=1&cm=" + std::string(child ? "true" : "false");
    std::string headers = aki_headers(lang);

    std::string resp = http_post_req(ctx, api, url, body, headers);
    if (resp.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, lang == "ru" ? "❌ Ошибка подключения к серверу Akinator." : "❌ Failed to connect to Akinator server.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    // Extract session and question safely without throwing regex exceptions
    std::string session, step = "1", question;
    try {
        std::smatch m;
        std::regex ses_re1(R"(localStorage\.setItem\(['"]session['"],\s*['"]([^'"]+)['"])");
        std::regex ses_re2(R"(session['"]\)\.val\(['"]([^'"]+)['"])");
        if (std::regex_search(resp, m, ses_re1) || std::regex_search(resp, m, ses_re2)) {
            session = m[1].str();
        }
        std::regex step_re(R"(localStorage\.setItem\(['"]step['"],\s*['"](\d+)['"])");
        if (std::regex_search(resp, m, step_re)) {
            step = m[1].str();
        }
        std::regex q_re(R"(class="question-text"[^>]*>([^<]+)</p>)");
        if (std::regex_search(resp, m, q_re)) {
            question = html_unescape(trim(m[1].str()));
        }
    } catch (...) {}

    if (session.empty() || question.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, lang == "ru" ? "❌ Сервис Akinator временно перегружен или недоступен." : "❌ Akinator service is temporarily busy.", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
        return;
    }

    AkiState st;
    st.lang = lang;
    st.session = session;
    st.step = step;
    st.prog = "0";
    st.num = 1;
    st.child_mode = child;
    st.last_q = question;
    save_state(ctx, api, api->chat_id(ctx), st);

    api->c_reset(ctx);
    std::string heading = (lang == "ru") ? "🔮 Акинатор · Вопрос #1" : "🔮 Akinator · Question #1";
    api->c_fmt(ctx, heading.c_str(), KOTO_ENT_BOLD);
    api->c_text(ctx, "\n\n");
    api->c_fmt(ctx, question.c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n\n");
    std::string hint = (lang == "ru") ? "📊 Прогресс: 0% · by kote" : "📊 Progress: 0% · by kote";
    api->c_fmt(ctx, hint.c_str(), KOTO_ENT_ITALIC);

    add_question_buttons(ctx, api, lang);
    api->c_edit(ctx, 0);
}

// ── Command: .akistop ────────────────────────────────────────────────────────
void cmd_akistop(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    long long cid = api->chat_id(ctx);
    clear_state(ctx, api, cid);
    api->c_reset(ctx);
    api->c_fmt(ctx, "🛑 Игра в Акинатора завершена.", KOTO_ENT_BOLD);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Callback Handler ─────────────────────────────────────────────────────────
void on_callback(koto_ctx* ctx, const koto_api* api) {
    const char* raw_data = api->cb_data(ctx);
    if (!raw_data || std::strncmp(raw_data, "aki:", 4) != 0) return;

    std::string data = raw_data;
    long long cid = api->chat_id(ctx);
    long long mid = api->cb_message_id(ctx);
    AkiState st = load_state(ctx, api, cid);

    if (data == "aki:stop") {
        clear_state(ctx, api, cid);
        api->cb_answer(ctx, "Игра завершена", 0);
        api->c_reset(ctx);
        api->c_fmt(ctx, st.lang == "ru" ? "🛑 Игра в Акинатора завершена." : "🛑 Akinator game ended.", KOTO_ENT_BOLD);
        api->c_edit_message(ctx, cid, mid, 0);
        return;
    }

    if (data == "aki:win") {
        clear_state(ctx, api, cid);
        api->cb_answer(ctx, st.lang == "ru" ? "Победа Акинатора!" : "Akinator won!", 0);
        api->c_reset(ctx);
        std::string win_text = (st.lang == "ru") ? "🎉 Ура! Я снова угадал персонажа!\n\n" : "🎉 Hurray! I guessed your character again!\n\n";
        api->c_fmt(ctx, win_text.c_str(), KOTO_ENT_BOLD);
        api->c_fmt(ctx, st.guess_name.c_str(), KOTO_ENT_CODE);
        if (!st.guess_desc.empty()) {
            api->c_text(ctx, "\n");
            api->c_fmt(ctx, st.guess_desc.c_str(), KOTO_ENT_ITALIC);
        }
        api->c_text(ctx, "\n\n<i>by kote</i>");
        api->c_edit_message(ctx, cid, mid, 0);
        return;
    }

    if (data == "aki:rej") {
        api->cb_answer(ctx, st.lang == "ru" ? "Продолжаем поиск…" : "Continuing search…", 0);
        // Exclude current proposition
        std::string url = "https://" + st.lang + ".akinator.com/exclude";
        std::string body = "step=" + url_encode(st.step) +
                           "&progression=" + url_encode(st.prog) +
                           "&sid=1&cm=" + std::string(st.child_mode ? "true" : "false") +
                           "&session=" + url_encode(st.session) +
                           "&forward_answer=1";
        std::string headers = aki_headers(st.lang);
        std::string resp = http_post_req(ctx, api, url, body, headers);

        try {
            json j = json::parse(resp);
            if (j.contains("id_proposition") && !j["id_proposition"].is_null()) {
                st.guess_name = html_unescape(j.value("name_proposition", ""));
                st.guess_desc = html_unescape(j.value("description_proposition", ""));
                st.guess_photo = j.value("photo", "");
                save_state(ctx, api, cid, st);

                api->c_reset(ctx);
                std::string header = (st.lang == "ru") ? "🔮 Акинатор предполагает:\n\n" : "🔮 Akinator thinks of:\n\n";
                api->c_fmt(ctx, header.c_str(), KOTO_ENT_BOLD);
                api->c_fmt(ctx, st.guess_name.c_str(), KOTO_ENT_CODE);
                if (!st.guess_desc.empty()) {
                    api->c_text(ctx, "\n");
                    api->c_fmt(ctx, st.guess_desc.c_str(), KOTO_ENT_ITALIC);
                }
                api->c_text(ctx, st.lang == "ru" ? "\n\nЯ угадал?" : "\n\nDid I guess right?");
                add_guess_buttons(ctx, api, st.lang);
                api->c_edit_message(ctx, cid, mid, 0);
                return;
            } else if (j.contains("question") && !j["question"].is_null()) {
                st.last_q = html_unescape(j.value("question", ""));
                if (j.contains("progression")) {
                    double p = j["progression"].is_number() ? j["progression"].get<double>() : std::stod(j.value("progression", "0"));
                    st.prog = std::to_string((int)std::round(p));
                }
                if (j.contains("step")) {
                    st.step = j["step"].is_string() ? j["step"].get<std::string>() : std::to_string(j["step"].get<int>());
                }
                st.num++;
                save_state(ctx, api, cid, st);

                api->c_reset(ctx);
                std::string heading = (st.lang == "ru" ? "🔮 Акинатор · Вопрос #" : "🔮 Akinator · Question #") + std::to_string(st.num);
                api->c_fmt(ctx, heading.c_str(), KOTO_ENT_BOLD);
                api->c_text(ctx, "\n\n");
                api->c_fmt(ctx, st.last_q.c_str(), KOTO_ENT_CODE);
                api->c_text(ctx, "\n\n");
                std::string hint = (st.lang == "ru" ? "📊 Прогресс: " : "📊 Progress: ") + st.prog + "% · by kote";
                api->c_fmt(ctx, hint.c_str(), KOTO_ENT_ITALIC);
                add_question_buttons(ctx, api, st.lang);
                api->c_edit_message(ctx, cid, mid, 0);
                return;
            }
        } catch (...) {}

        clear_state(ctx, api, cid);
        api->c_reset(ctx);
        api->c_fmt(ctx, st.lang == "ru" ? "😔 Не удалось угадать персонажа. Вы победили Акинатора!\n\n<i>by kote</i>" : "😔 Couldn't guess the character. You won!\n\n<i>by kote</i>", KOTO_ENT_BOLD);
        api->c_edit_message(ctx, cid, mid, 0);
        return;
    }

    if (data.rfind("aki:ans:", 0) == 0) {
        std::string ans_id = data.substr(8);
        api->cb_answer(ctx, st.lang == "ru" ? "Ответ принят" : "Answer received", 0);

        std::string url = "https://" + st.lang + ".akinator.com/answer";
        std::string body = "step=" + url_encode(st.step) +
                           "&progression=" + url_encode(st.prog) +
                           "&sid=1&cm=" + std::string(st.child_mode ? "true" : "false") +
                           "&answer=" + url_encode(ans_id) +
                           "&step_last_proposition=" + url_encode(st.slp) +
                           "&num_question=" + std::to_string(st.num) +
                           "&session=" + url_encode(st.session);
        std::string headers = aki_headers(st.lang);
        std::string resp = http_post_req(ctx, api, url, body, headers);

        try {
            json j = json::parse(resp);
            if (j.value("completion", "") == "KO") {
                clear_state(ctx, api, cid);
                api->c_reset(ctx);
                api->c_fmt(ctx, st.lang == "ru" ? "😔 Не удалось угадать персонажа. Вы победили Акинатора!\n\n<i>by kote</i>" : "😔 Couldn't guess the character. You beat Akinator!\n\n<i>by kote</i>", KOTO_ENT_BOLD);
                api->c_edit_message(ctx, cid, mid, 0);
                return;
            }

            if (j.contains("id_proposition") && !j["id_proposition"].is_null()) {
                st.guess_name = html_unescape(j.value("name_proposition", ""));
                st.guess_desc = html_unescape(j.value("description_proposition", ""));
                st.guess_photo = j.value("photo", "");
                st.slp = std::to_string(st.num);
                if (j.contains("step")) {
                    st.step = j["step"].is_string() ? j["step"].get<std::string>() : std::to_string(j["step"].get<int>());
                }
                save_state(ctx, api, cid, st);

                api->c_reset(ctx);
                std::string header = (st.lang == "ru") ? "🔮 Акинатор предполагает:\n\n" : "🔮 Akinator thinks of:\n\n";
                api->c_fmt(ctx, header.c_str(), KOTO_ENT_BOLD);
                api->c_fmt(ctx, st.guess_name.c_str(), KOTO_ENT_CODE);
                if (!st.guess_desc.empty()) {
                    api->c_text(ctx, "\n");
                    api->c_fmt(ctx, st.guess_desc.c_str(), KOTO_ENT_ITALIC);
                }
                api->c_text(ctx, st.lang == "ru" ? "\n\nЯ угадал?" : "\n\nDid I guess right?");
                add_guess_buttons(ctx, api, st.lang);
                api->c_edit_message(ctx, cid, mid, 0);
                return;
            }

            if (j.contains("question") && !j["question"].is_null()) {
                st.last_q = html_unescape(j.value("question", ""));
                if (j.contains("progression")) {
                    double p = j["progression"].is_number() ? j["progression"].get<double>() : std::stod(j.value("progression", "0"));
                    st.prog = std::to_string((int)std::round(p));
                }
                if (j.contains("step")) {
                    st.step = j["step"].is_string() ? j["step"].get<std::string>() : std::to_string(j["step"].get<int>());
                }
                st.num++;
                save_state(ctx, api, cid, st);

                api->c_reset(ctx);
                std::string heading = (st.lang == "ru" ? "🔮 Акинатор · Вопрос #" : "🔮 Akinator · Question #") + std::to_string(st.num);
                api->c_fmt(ctx, heading.c_str(), KOTO_ENT_BOLD);
                api->c_text(ctx, "\n\n");
                api->c_fmt(ctx, st.last_q.c_str(), KOTO_ENT_CODE);
                api->c_text(ctx, "\n\n");
                std::string hint = (st.lang == "ru" ? "📊 Прогресс: " : "📊 Progress: ") + st.prog + "% · by kote";
                api->c_fmt(ctx, hint.c_str(), KOTO_ENT_ITALIC);
                add_question_buttons(ctx, api, st.lang);
                api->c_edit_message(ctx, cid, mid, 0);
                return;
            }
        } catch (...) {}

        clear_state(ctx, api, cid);
        api->c_reset(ctx);
        api->c_fmt(ctx, st.lang == "ru" ? "😔 Не удалось угадать персонажа. Вы победили Акинатора!\n\n<i>by kote</i>" : "😔 Couldn't guess the character. You beat Akinator!\n\n<i>by kote</i>", KOTO_ENT_BOLD);
        api->c_edit_message(ctx, cid, mid, 0);
    }
}

// ── Settings & Commands ──────────────────────────────────────────────────────
const koto_setting_field SETTINGS[] = {
    {"lang", "Язык игры (ru / en)", KOTO_FIELD_CHOICE, "ru", "ru,en"}
};

const koto_command COMMANDS[] = {
    {"aki", cmd_akinator, KOTO_LEVEL_TRUSTED, "[ru] Запустить игру Акинатор [en] Start Akinator guessing game.", "[ru|en|--child]"},
    {"akinator", cmd_akinator, KOTO_LEVEL_TRUSTED, "[ru] Запустить игру Акинатор [en] Start Akinator guessing game.", "[ru|en|--child]"},
    {"akistop", cmd_akistop, KOTO_LEVEL_TRUSTED, "[ru] Остановить текущую игру Акинатор [en] Stop current Akinator game.", nullptr}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "akinator",
    "[ru] Акинатор угадает любого задуманного персонажа по вопросам (by kote). [en] Akinator character guessing game (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    nullptr, // on_watch
    0,       // watch_flags
    on_callback,
    SETTINGS,
    static_cast<int>(sizeof(SETTINGS) / sizeof(SETTINGS[0])),
    "https://github.com/AresUser1/KoteModules/blob/main/src/akinator.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
