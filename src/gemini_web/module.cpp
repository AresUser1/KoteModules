#include "module_abi.h"
#include "client.hpp"
#include "rich_text.hpp"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>

namespace {
using namespace gweb;
constexpr const char* version = "0.1.2";
std::mutex command_mutex;
#ifdef GWEB_TESTING
Transport test_transport;
#endif

std::string args(koto_ctx* ctx, const koto_api* api) {
    const char* text = api->cmd_args(ctx); return text ? trim(text) : "";
}
std::string prefix(koto_ctx* ctx, const koto_api* api) {
    char buf[128] = {};
    int n = api->setting_get(ctx, "prefix", buf, sizeof(buf));
    return n > 0 && n < static_cast<int>(sizeof(buf)) ? std::string(buf, static_cast<size_t>(n)) : ".";
}
std::string session_file_path(koto_ctx* ctx, const koto_api* api) {
    if (api && api->cache_dir) {
        char buf[4096] = {};
        int n = api->cache_dir(ctx, buf, sizeof(buf));
        if (n > 0 && n < static_cast<int>(sizeof(buf))) {
            return std::string(buf) + "/gemini_web_session.json";
        }
    }
    return "";
}
void write_file_safe(const std::string& path, const std::string& content) {
    if (path.empty()) return;
    std::string tmp = path + ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.flush();
    out.close();
    if (out) {
        std::rename(tmp.c_str(), path.c_str());
    }
}
std::string read_file_safe(const std::string& path) {
    if (path.empty()) return {};
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string read_db(koto_ctx* ctx, const koto_api* api, const std::string& key) {
    std::vector<char> buf(256 * 1024, 0);
    int n = api->db_get(ctx, key.c_str(), buf.data(), static_cast<int>(buf.size()));
    if (n < 0) return {};
    if (n >= static_cast<int>(buf.size())) throw Error("state", "Сохранённые данные слишком велики. Используй .gweblogout и повторный вход.");
    return std::string(buf.data(), static_cast<size_t>(n));
}
Json read_json(koto_ctx* ctx, const koto_api* api, const std::string& key) {
    auto text = read_db(ctx, api, key);
    if (text.empty()) return nullptr;
    Json value = Json::parse(text, nullptr, false);
    if (value.is_discarded()) throw Error("state", "Повреждены сохранённые данные Gemini Web. Выполни .gweblogout и повторный вход.");
    return value;
}
void save(koto_ctx* ctx, const koto_api* api, const Client& client, bool is_logout = false) {
    auto path = session_file_path(ctx, api);
    if (is_logout) {
        api->db_del(ctx, "state");
        if (!path.empty()) std::remove(path.c_str());
        return;
    }
    // Safety guard: if cookies are empty, never create or overwrite session!
    if (client.cookies.get("__Secure-1PSID").empty()) {
        return;
    }
    auto text = client.dump().dump();
    api->db_set(ctx, "state", text.c_str());
    write_file_safe(path, text);
}
Json load_state_json(koto_ctx* ctx, const koto_api* api) {
    Json db_val = nullptr;
    try {
        db_val = read_json(ctx, api, "state");
    } catch (...) {}

    bool db_has_cookies = false;
    if (db_val.is_object() && db_val.contains("cookies") && db_val["cookies"].is_array() && !db_val["cookies"].empty()) {
        db_has_cookies = true;
    }

    auto path = session_file_path(ctx, api);
    auto file_text = read_file_safe(path);
    Json file_val = nullptr;
    if (!file_text.empty()) {
        file_val = Json::parse(file_text, nullptr, false);
    }
    bool file_has_cookies = false;
    if (file_val.is_object() && file_val.contains("cookies") && file_val["cookies"].is_array() && !file_val["cookies"].empty()) {
        file_has_cookies = true;
    }

    if (db_has_cookies) {
        if (!file_has_cookies && !path.empty()) {
            write_file_safe(path, db_val.dump());
        }
        return db_val;
    }

    if (file_has_cookies) {
        api->db_set(ctx, "state", file_val.dump().c_str());
        return file_val;
    }

    return db_val.is_object() ? db_val : (file_val.is_object() ? file_val : nullptr);
}
void send(koto_ctx* ctx, const koto_api* api, const std::string& text) {
    api->c_reset(ctx); ui::compose(ctx,api,ui::plain(text));
    if (api->is_outgoing(ctx)) api->c_edit(ctx, KOTO_NO_LINK_PREVIEW);
    else api->c_reply(ctx, KOTO_NO_LINK_PREVIEW);
}
// c_edit already has a live progress path in the shipped APK. An incoming
// command cannot edit another user's message; don't queue a stale placeholder.
void progress(koto_ctx* ctx,const koto_api* api,const std::string& text) {
    if (!api->is_outgoing(ctx)) return;
    api->c_reset(ctx);api->c_fmt(ctx,text.c_str(),KOTO_ENT_BOLD);
    api->c_edit(ctx,KOTO_NO_LINK_PREVIEW);
}
std::string chat_prefix(koto_ctx* ctx, const koto_api* api, const Client& client) {
    return "chat:" + client.identity + ":" + std::to_string(api->chat_id(ctx)) + ":";
}
std::string chat_key(koto_ctx* ctx, const koto_api* api, const Client& client) {
    return chat_prefix(ctx, api, client) + client.selected;
}
void delete_chats(koto_ctx* ctx, const koto_api* api, const std::string& starts_with) {
    std::vector<char> buf(256 * 1024, 0);
    int n = api->db_keys(ctx, buf.data(), static_cast<int>(buf.size()));
    if (n < 0) return;
    if (n >= static_cast<int>(buf.size())) throw Error("state", "Слишком много записей диалогов для очистки за один вызов.");
    std::istringstream keys(std::string(buf.data(), static_cast<size_t>(n)));
    std::string key;
    while (std::getline(keys, key)) if (key.rfind(starts_with, 0) == 0) api->db_del(ctx, key.c_str());
}

void run(koto_ctx* ctx, const koto_api* api, int level,
         const std::function<void(Client&)>& handler) noexcept {
    try {
        if (!api->check_permission(ctx, level)) return;
        std::unique_lock<std::mutex> lock(command_mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            send(ctx, api, "Gemini Web уже выполняет запрос. Дождись ответа и повтори команду."); return;
        }
#ifdef GWEB_TESTING
        Client client(test_transport ? test_transport : Transport(HttpsTransport{}));
#else
        Client client;
#endif
        client.load(load_state_json(ctx, api));
        // Persist rotated cookies even if a later HTTP/RPC operation fails.
        try { handler(client); }
        catch (...) { save(ctx, api, client); throw; }
        save(ctx, api, client);
    } catch (const Error& error) {
        send(ctx, api, "❌ Gemini Web [" + error.stage + "]\n" + error.what());
    } catch (...) {
        // Never print raw parser exceptions: they can embed cookie/token values.
        send(ctx, api, "❌ Внутренняя ошибка Gemini Web. Секреты в сообщение не выводились. Проверь .gwebstatus и .gwebtest.");
    }
}

void login(koto_ctx* ctx, const koto_api* api) {
    run(ctx, api, KOTO_LEVEL_OWNER, [&](Client& client) {
        // This APK logs command arguments before calling modules. Read secrets
        // from a replied-to message instead; the command itself has no secrets.
        std::string inline_values = args(ctx, api);
        if (!inline_values.empty() && (!api->host_version || api->host_version(ctx) < 74))
            throw Error("cookies", "Вводи cookies отдельным сообщением в Избранном, затем ответь на него командой " + prefix(ctx, api) + "gweblogin без аргументов. Аргументы команд этот APK записывает в журнал.");
        const char* raw = api->reply_text ? api->reply_text(ctx) : nullptr;
        std::istringstream input(inline_values.empty() ? (raw ? raw : "") : inline_values);
        std::string psid, psidts, extra; input >> psid >> psidts >> extra;
        if (psid.empty() || !extra.empty()) throw Error("cookies", "В Избранном отправь отдельным сообщением <PSID> [PSIDTS], затем ответь на него командой " + prefix(ctx, api) + "gweblogin без аргументов.");
        Client fresh; fresh.cookies.login(psid, psidts); fresh.identity = uuid();
        // Different credentials never inherit conversations from a previous login.
        delete_chats(ctx, api, "chat:"); client = std::move(fresh);
        send(ctx, api, "✅ Cookies сохранены локально. Сообщение со значениями теперь можно удалить.\nПроверить подключение: " + prefix(ctx, api) + "gwebtest\nЗадать вопрос: " + prefix(ctx, api) + "gweb привет");
    });
}
void logout(koto_ctx* ctx, const koto_api* api) {
    run(ctx, api, KOTO_LEVEL_OWNER, [&](Client& client) {
        delete_chats(ctx, api, "chat:"); client = Client{};
        save(ctx, api, client, /*is_logout=*/true);
        send(ctx, api, "✅ Cookies и локальная память Gemini Web удалены. Активных фоновых сессий нет. Беседы на сайте Google не удалялись.");
    });
}
std::string models_text(const Client& client, const std::string& p) {
    std::string text = "Выбрано: " + client.selected + "\n\nauto — выбор Google\n";
    for (const auto& m : client.models) text += m.key + " — " + m.label + "\n";
    text += "\nСмена: " + p + "gwebmodel auto или ключ из списка.\nУ каждой модели своя память диалога.";
    return text;
}
void model(koto_ctx* ctx, const koto_api* api) {
    run(ctx, api, KOTO_LEVEL_OWNER, [&](Client& client) {
        std::string name = args(ctx, api);
        if (name.empty()) {
            progress(ctx,api,"✦ Gemini · Получаю список моделей…");
            client.init(); send(ctx, api, models_text(client, prefix(ctx, api))); return;
        }
        if (lower(name) != "auto") {progress(ctx,api,"✦ Gemini · Проверяю модель…");client.init();}
        client.selected = client.model_key(name);
        const Model* current = client.model(client.selected);
        send(ctx, api, "✅ Выбрана модель: " + (current ? current->label : "auto") +
             ".\nДиалог этой модели сохраняется отдельно. Очистить: " + prefix(ctx, api) + "gwebclear");
    });
}
void clear(koto_ctx* ctx, const koto_api* api) {
    run(ctx, api, KOTO_LEVEL_TRUSTED, [&](Client& client) {
        delete_chats(ctx, api, chat_prefix(ctx, api, client));
        send(ctx, api, "🧹 Локальная память всех моделей в этом чате очищена. Следующий запрос начнёт новую беседу. Беседы на сайте Google не удалялись.");
    });
}
void status(koto_ctx* ctx, const koto_api* api) {
    run(ctx, api, KOTO_LEVEL_TRUSTED, [&](Client& client) {
        Chat chat = Chat::load(read_json(ctx, api, chat_key(ctx, api, client)));
        const Model* current = client.model(client.selected);
        std::string text = std::string("Gemini Web ") + version + " · Android ARM64\n";
        text += "Cookies: " + std::string(client.cookies.get("__Secure-1PSID").empty() ? "не сохранены" : "сохранены") + "\n";
        text += "Модель: " + (current ? current->label : client.selected) + "\n";
        text += "Ответов в этом диалоге: " + std::to_string(chat.turns) + "\n";
        text += "Последняя проверка сессии: " + std::string(client.initialized_at ? "была успешна" : "не выполнена") + "\n";
        text += "Таймаут генерации: " + std::to_string(client.timeout) + " с\n";
        text += "Транспорт: собственный HTTPS / OpenSSL, системные CA\nСервер-посредник не используется.\n";
        text += "Cookies обновляются при активном использовании.\nЖивая диагностика: " + prefix(ctx, api) + "gwebtest";
        send(ctx, api, text);
    });
}
void test(koto_ctx* ctx, const koto_api* api) {
    run(ctx, api, KOTO_LEVEL_OWNER, [&](Client& client) {
        progress(ctx,api,"✦ Gemini · Проверяю подключение…");
        if (client.cookies.get("__Secure-1PSID").empty()) {
            Response result = client.probe();
            std::string text = "HTTPS из модуля работает; сертификат Google проверен.\nHTTP: " + std::to_string(result.status);
            text += "\nCookies пока не сохранены. Инструкция: " + prefix(ctx, api) + "gwebcookies.";
            send(ctx, api, text); return;
        }
        client.init(true);
        send(ctx, api, "✅ Прямой HTTPS и инициализация сессии Gemini работают.\nДоступных моделей: " + std::to_string(client.models.size()) +
             "\nСписок: " + prefix(ctx, api) + "gwebmodel\nПроверить генерацию: " + prefix(ctx, api) + "gweb Ответь одним словом: работает");
    });
}
void refresh(koto_ctx* ctx, const koto_api* api) {
    run(ctx, api, KOTO_LEVEL_OWNER, [&](Client& client) {
        progress(ctx,api,"✦ Gemini · Обновляю сессию…");
        client.rotate(); client.init(true);
        send(ctx, api, "✅ Cookies, токен сессии и список моделей обновлены. Память диалогов сохранена.");
    });
}
void cookies_help(koto_ctx* ctx, const koto_api* api) {
    run(ctx, api, KOTO_LEVEL_OWNER, [&](Client&) {
        std::string p = prefix(ctx, api);
        send(ctx, api, "В браузере войди в https://gemini.google.com и проверь, что чат отвечает.\n"
             "В DevTools или редакторе cookies скопируй значения __Secure-1PSID и, если есть, __Secure-1PSIDTS.\n\n"
             "В Избранном отправь отдельным сообщением значения PSID и PSIDTS через пробел или с новой строки.\n"
             "Ответь на сообщение командой " + p + "gweblogin без аргументов. Затем " + p + "gwebtest.\n\n"
             "Вводи только значения, без имён cookies и кавычек. После сохранения сообщение со значениями можно удалить. Cookies хранятся в БД модуля.");
    });
}
void timeout(koto_ctx* ctx, const koto_api* api) {
    run(ctx, api, KOTO_LEVEL_OWNER, [&](Client& client) {
        std::string value = args(ctx, api);
        if (!value.empty()) {
            if (value.find_first_not_of("0123456789") != std::string::npos || value.size() > 3)
                throw Error("timeout", "Укажи таймаут от 30 до 300 секунд.");
            int seconds = std::stoi(value);
            if (seconds < 30 || seconds > 300) throw Error("timeout", "Укажи таймаут от 30 до 300 секунд.");
            client.timeout = seconds;
        }
        send(ctx, api, "Таймаут генерации: " + std::to_string(client.timeout) + " с.");
    });
}
void ask(koto_ctx* ctx, const koto_api* api) {
    run(ctx, api, KOTO_LEVEL_TRUSTED, [&](Client& client) {
        std::string prompt = args(ctx, api);
        const char* reply_raw = api->reply_text ? api->reply_text(ctx) : nullptr;
        std::string reply = reply_raw ? reply_raw : "";
        if (!reply.empty()) {
            if (prompt.empty()) prompt = reply;
            else prompt = "Сообщение, на которое я отвечаю:\n" + reply + "\n\nМой запрос:\n" + prompt;
        }
        if (prompt.empty()) throw Error("prompt", "Используй " + prefix(ctx, api) + "gweb <вопрос> или ответь командой на текст. Эта первая сборка поддерживает текст; вложения пока не отправляются.");
        progress(ctx,api,"✦ Gemini · Думаю…");
        Chat before = Chat::load(read_json(ctx, api, chat_key(ctx, api, client)));
        Answer answer = client.ask(prompt, before);
        std::string state = answer.chat.dump().dump();
        api->db_set(ctx, chat_key(ctx, api, client).c_str(), state.c_str());
        // A very long reply is delivered intact as a text attachment instead of
        // filling the Telegram chat with dozens of messages.
        if (answer.text.size() > 48000 && api->cache_dir && api->c_send_file) {
            char path[4096] = {};
            int n = api->cache_dir(ctx, path, sizeof(path));
            if (n > 0 && n < static_cast<int>(sizeof(path))) {
                std::string file = std::string(path) + "/gemini-web-" + uuid() + ".txt";
                std::ofstream out(file, std::ios::binary);
                out << answer.text; out.close();
                if (out) {
                    send(ctx, api, "✅ Gemini ответил. Полный ответ — в текстовом файле ниже.");
                    api->c_reset(ctx); api->c_text(ctx, "Gemini Web — полный ответ");
                    api->c_send_file(ctx, api->chat_id(ctx), file.c_str(), 0); return;
                }
            }
        }
        auto formatted=ui::markdown(answer.text);
        if (formatted.text.empty()) formatted=ui::plain(answer.text);
        auto chunks = ui::split(formatted);
        if (chunks.size() > 32) throw Error("text", "Ответ получен и диалог сохранён, но он слишком велик для 32 сообщений. Попроси Gemini дать краткую версию.");
        const Model* current = client.model(client.selected);
        auto labels=split_text(current?current->label:"auto",120);
        std::string label=labels.empty()?"auto":labels.front()+(labels.size()>1?"…":"");
        std::string heading="✦ Gemini · "+label+" · "+std::to_string(answer.chat.turns);
        std::string title=heading+"\n\n";
        for (size_t i = 0; i < chunks.size(); ++i) {
            api->c_reset(ctx);
            if (i == 0) {api->c_fmt(ctx,heading.c_str(),KOTO_ENT_BOLD);api->c_text(ctx,"\n\n");}
            ui::compose(ctx,api,chunks[i],i==0?ui::units(title):0);
            if (i == 0 && api->is_outgoing(ctx)) api->c_edit(ctx, KOTO_NO_LINK_PREVIEW);
            else api->c_reply(ctx, KOTO_NO_LINK_PREVIEW);
        }
    });
}

const koto_command commands[] = {
    {"gweblogin", login, KOTO_LEVEL_OWNER, "Сохранить cookies из reply на сообщение с PSID [PSIDTS] в Избранном.", "reply на cookies; без аргументов"},
    {"gweblogout", logout, KOTO_LEVEL_OWNER, "Удалить cookies и локальную память Gemini Web.", nullptr},
    {"gwebcookies", cookies_help, KOTO_LEVEL_OWNER, "Инструкция по получению cookies Google.", nullptr},
    {"gweb", ask, KOTO_LEVEL_TRUSTED, "Спросить Gemini Web; текст и reply на текст, память диалога.", "[вопрос]"},
    {"gwebclear", clear, KOTO_LEVEL_TRUSTED, "Сбросить локальную память всех моделей в этом чате.", nullptr},
    {"gwebmodel", model, KOTO_LEVEL_OWNER, "Список моделей аккаунта или выбор модели; auto — выбор Google.", "[auto|ключ|id]"},
    {"gwebstatus", status, KOTO_LEVEL_TRUSTED, "Локальный статус Gemini Web без показа секретов.", nullptr},
    {"gwebtest", test, KOTO_LEVEL_OWNER, "Проверить прямой HTTPS, затем вход и модели (без генерации).", nullptr},
    {"gwebrefresh", refresh, KOTO_LEVEL_OWNER, "Обновить cookies, токен и модели без сброса диалогов.", nullptr},
    {"gwebtimeout", timeout, KOTO_LEVEL_OWNER, "Таймаут генерации в секундах (30–300).", "[секунды]"},
};
const koto_module module = {
    KOTO_MODULE_ABI, "gemini_web", "Gemini Web напрямую с Android: свой HTTPS, cookies и память диалогов.",
    version, 26, KOTO_MOD_NO_TWIN, commands, static_cast<int>(sizeof(commands) / sizeof(commands[0])),
    nullptr, 0, nullptr, nullptr, 0, nullptr
};
} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &module;
}
#ifdef GWEB_TESTING
namespace gweb {
void module_test_transport(Transport transport) { test_transport = std::move(transport); }
}
#endif
