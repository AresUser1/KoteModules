// file_creator.cpp
// Сбор текста/кода из нескольких сообщений и создание файла любого формата.
// Порт модуля File-creator.py (by Killu / @dubai_ip) на Kotogram C++ ABI 3.
//
// Использование:
//   .cfile <формат> [название]  — начать сбор кода в текущем чате
//   .cfile cancel               — отменить текущую сессию
//   .done                       — завершить сбор и отправить готовый файл

#include "module_abi.h"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string to_lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

static std::string get_prefix(koto_ctx* ctx, const koto_api* api) {
    if (api->setting_get) {
        char buf[32] = {0};
        int n = api->setting_get(ctx, "prefix", buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            std::string p = trim(buf);
            if (!p.empty()) return p;
        }
    }
    return ".";
}

static std::string session_key(long long cid) {
    return "sess_" + std::to_string(cid);
}

static std::string db_read_str(koto_ctx* ctx, const koto_api* api, const std::string& key) {
    if (!api->db_get) return "";
    std::vector<char> buf(512 * 1024);
    int n = api->db_get(ctx, key.c_str(), buf.data(), static_cast<int>(buf.size()) - 1);
    if (n <= 0) return "";
    if (n >= static_cast<int>(buf.size())) n = static_cast<int>(buf.size()) - 1;
    buf[n] = '\0';
    return std::string(buf.data(), static_cast<size_t>(n));
}

static void reply_or_edit(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing && api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else if (api->c_reply) api->c_reply(ctx, 0);
    else api->c_edit(ctx, 0);
}

static void cmd_cfile(koto_ctx* ctx, const koto_api* api) {
    const char* raw = api->cmd_args ? api->cmd_args(ctx) : nullptr;
    std::string args = trim(raw ? raw : "");
    long long cid = api->chat_id(ctx);
    std::string key = session_key(cid);

    if (args.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "\xE2\x9D\x8C", 5980869107891838347LL);
        api->c_text(ctx, " Укажи формат и название: ");
        api->c_fmt(ctx, (get_prefix(ctx, api) + "cfile py myscript").c_str(), KOTO_ENT_CODE);
        reply_or_edit(ctx, api);
        return;
    }

    if (to_lower(args) == "cancel" || to_lower(args) == "stop") {
        if (api->db_del) api->db_del(ctx, key.c_str());
        api->c_reset(ctx);
        api->c_fmt(ctx, "\xE2\x9C\x85 Создание файла отменено.", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }

    std::string ext, name = "code";
    size_t sp = args.find_first_of(" \t\r\n");
    if (sp == std::string::npos) {
        ext = to_lower(args);
    } else {
        ext = to_lower(args.substr(0, sp));
        std::string rest = trim(args.substr(sp + 1));
        if (!rest.empty()) name = rest;
    }

    if (!ext.empty() && ext[0] != '.') ext = "." + ext;
    // Защита от слешей в имени файла
    for (char& c : name) {
        if (c == '/' || c == '\\') c = '_';
    }
    std::string filename = name + ext;

    json sess = {
        {"name", filename},
        {"chunks", json::array()}
    };
    api->db_set(ctx, key.c_str(), sess.dump().c_str());

    std::string pfx = get_prefix(ctx, api);
    api->c_reset(ctx);
    api->c_fmt(ctx, "Отправь код", KOTO_ENT_BOLD);
    api->c_text(ctx, " (можно в несколько сообщений).\nФайл: ");
    api->c_fmt(ctx, filename.c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\nКогда закончишь — напиши ");
    api->c_fmt(ctx, (pfx + "done").c_str(), KOTO_ENT_CODE);
    reply_or_edit(ctx, api);
}

static void cmd_done(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);
    std::string key = session_key(cid);
    std::string raw_sess = db_read_str(ctx, api, key);
    if (raw_sess.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "Ты не начинал создание файла!", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }

    if (api->db_del) api->db_del(ctx, key.c_str());

    json sess = json::parse(raw_sess, nullptr, false);
    if (!sess.is_object() || !sess.contains("chunks") || !sess["chunks"].is_array()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "Ошибка чтения сессии!", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }

    std::string full_code;
    const auto& chunks = sess["chunks"];
    for (size_t i = 0; i < chunks.size(); ++i) {
        if (!chunks[i].is_string()) continue;
        if (!full_code.empty()) full_code += "\n";
        full_code += chunks[i].get<std::string>();
    }

    if (trim(full_code).empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "Код пустой!", KOTO_ENT_BOLD);
        reply_or_edit(ctx, api);
        return;
    }

    std::string filename = sess.value("name", "code.txt");

    api->c_reset(ctx);
    api->c_fmt(ctx, "Готово!", KOTO_ENT_BOLD);
    api->c_text(ctx, " Создаю файл...");
    reply_or_edit(ctx, api);

    api->c_reset(ctx);
    api->c_emoji(ctx, "\xF0\x9F\x92\xBB", 5193177581888755275LL);
    api->c_text(ctx, " ");
    api->c_fmt(ctx, filename.c_str(), KOTO_ENT_CODE);

    api->c_send_bytes(
        ctx,
        cid,
        filename.c_str(),
        reinterpret_cast<const unsigned char*>(full_code.data()),
        static_cast<int>(full_code.size()),
        KOTO_SEND_AS_FILE
    );
    api->delete_msg(ctx);
}

static int on_watch(koto_ctx* ctx, const koto_api* api) {
    if (!api->is_outgoing || !api->is_outgoing(ctx)) return 0;

    long long cid = api->chat_id(ctx);
    std::string key = session_key(cid);
    std::string raw_sess = db_read_str(ctx, api, key);
    if (raw_sess.empty()) return 0;

    const char* txt_ptr = api->msg_text ? api->msg_text(ctx) : nullptr;
    std::string text = txt_ptr ? txt_ptr : "";
    if (trim(text).empty()) return 0;

    std::string pfx = get_prefix(ctx, api);
    if (!pfx.empty() && text.rfind(pfx, 0) == 0) {
        return 0; // Команды (.done, .cfile и т.д.) не попадают в файл
    }

    json sess = json::parse(raw_sess, nullptr, false);
    if (!sess.is_object() || !sess.contains("chunks") || !sess["chunks"].is_array()) {
        return 0;
    }

    sess["chunks"].push_back(text);
    api->db_set(ctx, key.c_str(), sess.dump().c_str());

    api->c_reset(ctx);
    api->c_fmt(ctx, "Добавлено", KOTO_ENT_BOLD);
    api->c_reply(ctx, 0);
    return 1;
}

static const koto_command COMMANDS[] = {
    {
        "cfile",
        &cmd_cfile,
        KOTO_LEVEL_OWNER,
        "[ru] Начать создание файла из сообщений: .cfile <формат> [название] | cancel. [en] Start collecting code into a file: .cfile <ext> [name] | cancel.",
        "<формат> [название] | cancel"
    },
    {
        "done",
        &cmd_done,
        KOTO_LEVEL_OWNER,
        "[ru] Завершить сбор кода и отправить готовый файл. [en] Finish collecting code and send the file.",
        nullptr
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "file_creator",
    "[ru] Создаёт файл любого формата из одного или нескольких сообщений (порт File-creator.py). C++ ABI 3. [en] Creates files from messages. C++ ABI 3.",
    "1.0.0",
    24,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    &on_watch,
    KOTO_WATCH_OUTGOING,
    nullptr,
    nullptr, 0,
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/file_creator.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
