#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <cctype>

#include "module_abi.h"
#include "nlohmann/json.hpp"

// ── Генератор случайного ID секретки ──────────────────────────────────────────
static std::string gen_wisp_id() {
    static const char hex_chars[] = "0123456789abcdef";
    std::string id;
    id.reserve(8);
    std::random_device rd;
    std::mt19937 gen(rd() ^ (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<int> dis(0, 15);
    for (int i = 0; i < 8; ++i) {
        id.push_back(hex_chars[dis(gen)]);
    }
    return id;
}

// ── Рендер уведомления о прочтении по шаблону ──────────────────────────────────
static void render_read_notice(koto_ctx* ctx, const koto_api* api, const std::string& tmpl,
                               const std::string& reader_name, long long reader_id) {
    std::string t = tmpl.empty() ? KOTO_TR(ctx, api, "{name} прочитал(а) секретку.", "{name} read the secret message.") : tmpl;
    std::string id_str = std::to_string(reader_id);
    std::string user_url = "tg://user?id=" + id_str;

    // Подставляем {id}
    size_t pos = 0;
    while ((pos = t.find("{id}", pos)) != std::string::npos) {
        t.replace(pos, 4, id_str);
        pos += id_str.size();
    }

    // Подставляем {name} с помощью c_url
    pos = 0;
    size_t name_pos = t.find("{name}");
    if (name_pos == std::string::npos) {
        api->c_text(ctx, t.c_str());
    } else {
        while (name_pos != std::string::npos) {
            std::string before = t.substr(pos, name_pos - pos);
            if (!before.empty()) api->c_text(ctx, before.c_str());
            api->c_url(ctx, reader_name.c_str(), user_url.c_str());
            pos = name_pos + 6; // len("{name}") == 6
            name_pos = t.find("{name}", pos);
        }
        std::string after = t.substr(pos);
        if (!after.empty()) api->c_text(ctx, after.c_str());
    }
}

// ── Команда: .wisp <id/username> <текст> ──────────────────────────────────────
static void cmd_wisp(koto_ctx* ctx, const koto_api* api) {
    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? raw_args : "";
    while (!args.empty() && (args.front() == ' ' || args.front() == '\t')) args.erase(args.begin());
    while (!args.empty() && (args.back() == ' ' || args.back() == '\t')) args.pop_back();

    api->c_reset(ctx);

    if (args.empty()) {
        api->c_fmt(ctx, KOTO_TR(ctx, api, "❌ Использование: ", "❌ Usage: "), KOTO_ENT_NONE);
        api->c_fmt(ctx, ".wisp <id/username> <текст>", KOTO_ENT_CODE);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0); else api->c_reply(ctx, 0);
        return;
    }

    std::string target, message_text;
    bool matched = false;

    size_t sp = args.find_first_of(" \t\r\n");
    if (sp != std::string::npos) {
        std::string first_token = args.substr(0, sp);
        std::string rest = args.substr(sp + 1);
        while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t' || rest.front() == '\r' || rest.front() == '\n')) {
            rest.erase(rest.begin());
        }

        std::string clean = first_token;
        if (!clean.empty() && clean.front() == '-') clean = clean.substr(1);
        bool is_num = !clean.empty() && std::all_of(clean.begin(), clean.end(), ::isdigit);
        bool is_user = (first_token.size() > 1 && first_token.front() == '@');

        if ((is_num || is_user) && !rest.empty()) {
            target = first_token;
            message_text = rest;
            matched = true;
        }
    }

    // Поддержка ответа на сообщение: .wisp <текст>
    if (!matched) {
        long long reply_id = (api->reply_sender_id) ? api->reply_sender_id(ctx) : 0;
        if (reply_id != 0) {
            target = std::to_string(reply_id);
            message_text = args;
            matched = true;
        }
    }

    if (!matched || message_text.empty()) {
        api->c_fmt(ctx, KOTO_TR(ctx, api,
            "❌ Неверный формат.\nПример: ",
            "❌ Invalid format.\nExample: "), KOTO_ENT_NONE);
        api->c_fmt(ctx, ".wisp @user привет", KOTO_ENT_CODE);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0); else api->c_reply(ctx, 0);
        return;
    }

    // Лимит answerCallbackQuery — 200 байт UTF-8
    size_t msg_bytes = message_text.size();
    if (msg_bytes > 200) {
        size_t over = msg_bytes - 200;
        char msg_buf[512];
        snprintf(msg_buf, sizeof(msg_buf),
            KOTO_TR(ctx, api,
                "❌ Текст слишком длинный!\nЛимит: 200 байт, у вас: %zu (+%zu лишних).\nСовет: ~100 кириллических или ~200 латинских символов.",
                "❌ Text too long!\nLimit: 200 bytes, yours: %zu (+%zu extra).\nTip: ~100 Cyrillic or ~200 Latin chars."),
            msg_bytes, over);
        api->c_text(ctx, msg_buf);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0); else api->c_reply(ctx, 0);
        return;
    }

    long long recipient_id = 0;
    std::string recipient_name = target;
    std::string recipient_username = "";

    std::string clean = target;
    if (!clean.empty() && clean.front() == '-') clean = clean.substr(1);
    if (!clean.empty() && std::all_of(clean.begin(), clean.end(), ::isdigit)) {
        recipient_id = std::stoll(target);
        char name_buf[256] = {0};
        if (api->user_first_name && api->user_first_name(ctx, recipient_id, name_buf, sizeof(name_buf)) > 0 && name_buf[0]) {
            recipient_name = name_buf;
        } else if (api->user_name && api->user_name(ctx, recipient_id, name_buf, sizeof(name_buf)) > 0 && name_buf[0]) {
            recipient_name = name_buf;
        } else {
            recipient_name = "ID: " + std::to_string(recipient_id);
        }
    } else {
        std::string uname = target;
        if (!uname.empty() && uname.front() == '@') uname = uname.substr(1);
        recipient_username = uname;
        for (char& c : recipient_username) c = (char)::tolower((unsigned char)c);

        if (api->resolve) {
            recipient_id = api->resolve(ctx, target.c_str());
            if (recipient_id == 0 && target.front() != '@') {
                recipient_id = api->resolve(ctx, ("@" + target).c_str());
            }
        }
        if (recipient_id != 0) {
            char name_buf[256] = {0};
            if (api->user_first_name && api->user_first_name(ctx, recipient_id, name_buf, sizeof(name_buf)) > 0 && name_buf[0]) {
                recipient_name = name_buf;
            } else if (api->user_name && api->user_name(ctx, recipient_id, name_buf, sizeof(name_buf)) > 0 && name_buf[0]) {
                recipient_name = name_buf;
            }
        }
    }

    long long sender_id = (api->get_me) ? api->get_me(ctx) : 0;
    if (sender_id == 0 && api->sender_id) sender_id = api->sender_id(ctx);

    std::string wisp_id = gen_wisp_id();

    nlohmann::json j;
    j["text"] = message_text;
    j["recipient_id"] = recipient_id;
    j["recipient_username"] = recipient_username;
    j["sender_id"] = sender_id;
    j["recipient_name"] = recipient_name;
    j["read"] = false;
    j["read_by"] = 0;
    j["reader_name"] = "";

    api->db_set(ctx, ("msg_" + wisp_id).c_str(), j.dump().c_str());

    // Формируем карточку секретного сообщения
    api->c_reset(ctx);
    api->c_text(ctx, "🔐 ");
    api->c_fmt(ctx, (KOTO_TR(ctx, api, "Секретное сообщение для ", "Secret message for ") + recipient_name).c_str(), KOTO_ENT_BOLD);
    api->c_text(ctx, "\n\n");
    api->c_fmt(ctx, KOTO_TR(ctx, api,
        "Прочитать его может только получатель и отправитель.",
        "Only the recipient and sender can read this."), KOTO_ENT_ITALIC);

    std::string btn_label = KOTO_TR(ctx, api, "📥 Прочитать сообщение", "📥 Read message");
    std::string cb_data = "wisp:read:" + wisp_id;
    api->c_button(ctx, btn_label.c_str(), cb_data.c_str(), 1);

    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

// ── Команда: .wispcfg [close on|off] | [text <шаблон>] ────────────────────────
static void cmd_wispcfg(koto_ctx* ctx, const koto_api* api) {
    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? raw_args : "";
    while (!args.empty() && (args.front() == ' ' || args.front() == '\t')) args.erase(args.begin());
    while (!args.empty() && (args.back() == ' ' || args.back() == '\t')) args.pop_back();

    api->c_reset(ctx);

    if (args.empty()) {
        long long close_val = api->db_get_int(ctx, "close_on_read", 1);
        char tmpl_buf[512] = {0};
        int len = api->db_get(ctx, "close_text_template", tmpl_buf, sizeof(tmpl_buf));
        std::string tmpl = (len > 0) ? tmpl_buf : KOTO_TR(ctx, api, "{name} прочитал(а) секретку.", "{name} read the secret message.");

        api->c_text(ctx, "🔐 ");
        api->c_fmt(ctx, KOTO_TR(ctx, api, "Настройки секреток (wisp)\n\n", "Secret message settings (wisp)\n\n"), KOTO_ENT_BOLD);

        api->c_text(ctx, KOTO_TR(ctx, api, "Замена карточки после прочтения: ", "Replace card after read: "));
        api->c_fmt(ctx, close_val ? KOTO_TR(ctx, api, "включено ✅\n", "enabled ✅\n") : KOTO_TR(ctx, api, "выключено ⛔️\n", "disabled ⛔️\n"), KOTO_ENT_BOLD);

        api->c_text(ctx, KOTO_TR(ctx, api, "Текст после прочтения: ", "Text after read: "));
        api->c_fmt(ctx, tmpl.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n\n");

        api->c_fmt(ctx, KOTO_TR(ctx, api,
            ".wispcfg close on/off — включить/выключить\n"
            ".wispcfg text <шаблон> — свой текст ({name} — имя со ссылкой, {id} — ID)\n"
            ".wispcfg text reset — вернуть текст по умолчанию",
            ".wispcfg close on/off — enable/disable\n"
            ".wispcfg text <template> — custom text ({name} — name with link, {id} — ID)\n"
            ".wispcfg text reset — reset to default text"
        ), KOTO_ENT_ITALIC);

        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0); else api->c_reply(ctx, 0);
        return;
    }

    std::string sub, val;
    size_t sp = args.find_first_of(" \t");
    if (sp != std::string::npos) {
        sub = args.substr(0, sp);
        val = args.substr(sp + 1);
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());
    } else {
        sub = args;
    }
    for (char& c : sub) c = (char)::tolower((unsigned char)c);

    if (sub == "close") {
        std::string low_val = val;
        for (char& c : low_val) c = (char)::tolower((unsigned char)c);
        if (low_val != "on" && low_val != "off") {
            api->c_text(ctx, KOTO_TR(ctx, api, "❌ Используйте: ", "❌ Use: "));
            api->c_fmt(ctx, ".wispcfg close on", KOTO_ENT_CODE);
            api->c_text(ctx, KOTO_TR(ctx, api, " или ", " or "));
            api->c_fmt(ctx, ".wispcfg close off", KOTO_ENT_CODE);
            if (api->is_outgoing(ctx)) api->c_edit(ctx, 0); else api->c_reply(ctx, 0);
            return;
        }
        bool on = (low_val == "on");
        api->db_set_int(ctx, "close_on_read", on ? 1 : 0);
        api->c_text(ctx, KOTO_TR(ctx, api, "✅ Замена карточки после прочтения ", "✅ Card replacement after read "));
        api->c_fmt(ctx, on ? KOTO_TR(ctx, api, "включена", "enabled") : KOTO_TR(ctx, api, "выключена", "disabled"), KOTO_ENT_BOLD);
        api->c_text(ctx, ".");
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0); else api->c_reply(ctx, 0);
        return;
    }

    if (sub == "text") {
        if (val.empty()) {
            api->c_text(ctx, KOTO_TR(ctx, api, "❌ Укажите текст шаблона или ", "❌ Specify template text or "));
            api->c_fmt(ctx, "reset", KOTO_ENT_CODE);
            api->c_text(ctx, ".");
            if (api->is_outgoing(ctx)) api->c_edit(ctx, 0); else api->c_reply(ctx, 0);
            return;
        }
        std::string low_val = val;
        for (char& c : low_val) c = (char)::tolower((unsigned char)c);
        if (low_val == "reset") {
            api->db_set(ctx, "close_text_template", "{name} прочитал(а) секретку.");
            api->c_text(ctx, KOTO_TR(ctx, api, "✅ Текст после прочтения сброшен на стандартный.", "✅ Read text reset to default."));
            if (api->is_outgoing(ctx)) api->c_edit(ctx, 0); else api->c_reply(ctx, 0);
            return;
        }
        api->db_set(ctx, "close_text_template", val.c_str());
        api->c_text(ctx, KOTO_TR(ctx, api, "✅ Текст после прочтения обновлён.\n\n", "✅ Read text updated.\n\n"));
        api->c_fmt(ctx, KOTO_TR(ctx, api, "Превью: ", "Preview: "), KOTO_ENT_BOLD);
        render_read_notice(ctx, api, val, "Вася", 123456789);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0); else api->c_reply(ctx, 0);
        return;
    }

    api->c_text(ctx, KOTO_TR(ctx, api,
        "❌ Неизвестная подкоманда. Используйте close или text.",
        "❌ Unknown subcommand. Use close or text."));
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0); else api->c_reply(ctx, 0);
}

// ── Обработчик нажатия на инлайн-кнопку ───────────────────────────────────────
static void on_callback(koto_ctx* ctx, const koto_api* api) {
    const char* raw_data = (api->cb_data) ? api->cb_data(ctx) : nullptr;
    if (!raw_data) return;
    std::string data = raw_data;

    std::string wisp_id;
    if (data.rfind("wisp:read:", 0) == 0) {
        wisp_id = data.substr(10);
    } else if (data.rfind("wisp_read:", 0) == 0) {
        wisp_id = data.substr(10);
    } else if (data.rfind("wisp:", 0) == 0) {
        wisp_id = data.substr(5);
    } else {
        return;
    }

    char buf[2048] = {0};
    int len = api->db_get(ctx, ("msg_" + wisp_id).c_str(), buf, sizeof(buf));
    if (len <= 0) {
        if (api->cb_answer) {
            api->cb_answer(ctx, KOTO_TR(ctx, api, "❌ Сообщение больше не доступно.", "❌ Message is no longer available."), 1);
        }
        return;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(buf);
    } catch (...) {
        if (api->cb_answer) api->cb_answer(ctx, "❌ Ошибка данных сообщения.", 1);
        return;
    }

    long long recipient_id = j.value("recipient_id", 0LL);
    std::string recipient_username = j.value("recipient_username", "");
    long long sender_id = j.value("sender_id", 0LL);
    std::string secret_text = j.value("text", "");
    bool is_read = j.value("read", false);

    long long clicker_id = (api->sender_id) ? api->sender_id(ctx) : 0;

    bool is_recipient = (clicker_id != 0 && clicker_id == recipient_id);
    if (!is_recipient && !recipient_username.empty() && api->user_username) {
        char un_buf[128] = {0};
        if (api->user_username(ctx, clicker_id, un_buf, sizeof(un_buf)) > 0) {
            std::string clicker_un = un_buf;
            for (char& c : clicker_un) c = (char)::tolower((unsigned char)c);
            if (clicker_un == recipient_username) {
                is_recipient = true;
                j["recipient_id"] = clicker_id;
                recipient_id = clicker_id;
            }
        }
    }

    bool is_sender = (clicker_id != 0 && clicker_id == sender_id);

    if (!is_recipient && !is_sender) {
        char denied_buf[256];
        snprintf(denied_buf, sizeof(denied_buf),
            KOTO_TR(ctx, api,
                "🔒 Это сообщение не для вас!\n(Ваш ID: %lld, ожидался: %lld)",
                "🔒 This message is not for you!\n(Your ID: %lld, expected: %lld)"),
            clicker_id, recipient_id);
        if (api->cb_answer) api->cb_answer(ctx, denied_buf, 1);
        return;
    }

    // Если секретка уже прочитана — показываем кто прочитал, секрет не раскрываем повторно
    if (is_read) {
        std::string reader = j.value("reader_name", "");
        if (reader.empty()) reader = "ID " + std::to_string(j.value("read_by", 0LL));
        std::string ans = KOTO_TR(ctx, api, "✅ Секретку уже прочитал(а) ", "✅ Secret message was already read by ") + reader;
        if (api->cb_answer) api->cb_answer(ctx, ans.c_str(), 0);
        return;
    }

    // Показываем секрет во всплывающем окне
    if (api->cb_answer) {
        api->cb_answer(ctx, secret_text.c_str(), 1);
    }

    // Карточку меняем ТОЛЬКО когда читает получатель, не отправитель
    if (!is_recipient) {
        return;
    }

    long long close_on_read = api->db_get_int(ctx, "close_on_read", 1);
    if (!close_on_read) {
        return;
    }

    char reader_buf[256] = {0};
    std::string reader_name;
    if (api->user_first_name && api->user_first_name(ctx, clicker_id, reader_buf, sizeof(reader_buf)) > 0 && reader_buf[0]) {
        reader_name = reader_buf;
    } else if (api->user_name && api->user_name(ctx, clicker_id, reader_buf, sizeof(reader_buf)) > 0 && reader_buf[0]) {
        reader_name = reader_buf;
    } else {
        reader_name = "ID " + std::to_string(clicker_id);
    }

    j["read"] = true;
    j["read_by"] = clicker_id;
    j["reader_name"] = reader_name;
    api->db_set(ctx, ("msg_" + wisp_id).c_str(), j.dump().c_str());

    char tmpl_buf[512] = {0};
    int tlen = api->db_get(ctx, "close_text_template", tmpl_buf, sizeof(tmpl_buf));
    std::string tmpl = (tlen > 0) ? tmpl_buf : KOTO_TR(ctx, api, "{name} прочитал(а) секретку.", "{name} read the secret message.");

    // Обновляем текст сообщения и удаляем инлайн-кнопку
    api->c_reset(ctx);
    render_read_notice(ctx, api, tmpl, reader_name, clicker_id);
    api->c_edit(ctx, 0);
}

// ── Декларация команд и настроек модуля ───────────────────────────────────────
static const koto_command COMMANDS[] = {
    {"wisp", cmd_wisp, KOTO_LEVEL_OWNER, "Отправить секретное сообщение для указанного пользователя.", "<id/username> <текст>"},
    {"wispcfg", cmd_wispcfg, KOTO_LEVEL_OWNER, "Настройки секретных сообщений (замена карточки, шаблон текста).", "[close on|off] | [text <шаблон>]"}
};

static const koto_setting_field SETTINGS[] = {
    {"close_on_read", "Заменять карточку после прочтения", KOTO_FIELD_BOOL, "1", nullptr},
    {"close_text_template", "Шаблон после прочтения ({name}, {id})", KOTO_FIELD_STR, "{name} прочитал(а) секретку.", nullptr}
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "wisp",
    "Секретные сообщения с одноразовым прочтением и защитой для получателя.",
    "1.0.4",
    24, // min_host_version
    0,  // flags
    COMMANDS,
    2,  // command_count
    nullptr, // on_watch
    0,       // watch_flags
    on_callback,
    SETTINGS,
    2,  // settings_count
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/dist/android-arm64-v8a/wisp.so"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
