// hookusername.cpp — Проверка доступности и захват Telegram-юзернеймов (.uz) (C++ ABI 3).
// Портирован 1:1 из Python-модуля HookUsername (v1.0.1) с проверкой Fragment.com и созданием резервного канала.
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr const char* VERSION = "1.0.0";
constexpr const char* DEFAULT_TITLE  = "Этот юзернейм зарезервирован.";
constexpr const char* DEFAULT_ABOUT  = "";
constexpr const char* DEFAULT_AVATAR = "https://raw.githubusercontent.com/neistv/mods/main/assets/rezerv.png";

constexpr long long EMO_EXCL  = 5220197908342648622LL; // ❗️
constexpr long long EMO_COIN  = 6039802097916974085LL; // 🪙
constexpr long long EMO_BOLT  = 5219943216781995020LL; // ⚡️
constexpr long long EMO_LINK  = 5902449142575141204LL; // 🔗
constexpr long long EMO_DOWN  = 5208663713539704322LL; // 👎
constexpr long long EMO_BOOM  = 5219901967916084166LL; // 💥

static bool contains_cyrillic(const std::string& s) {
    for (size_t i = 0; i + 1 < s.size(); ++i) {
        unsigned char c0 = static_cast<unsigned char>(s[i]);
        unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        if ((c0 == 0xD0 && ((c1 >= 0x90 && c1 <= 0xBF) || c1 == 0x81)) ||
            (c0 == 0xD1 && ((c1 >= 0x80 && c1 <= 0x8F) || c1 == 0x91))) {
            return true;
        }
    }
    return false;
}

static bool valid_username_chars(const std::string& s) {
    if (s.size() < 4 || s.size() > 32) return false;
    for (unsigned char c : s) {
        if (!(std::isalnum(c) || c == '_')) return false;
    }
    return true;
}

static std::string read_cfg(koto_ctx* ctx, const koto_api* api, const char* key, const char* def_val) {
    if (api && api->cfg_get) {
        char buf[1024] = {0};
        int n = api->cfg_get(ctx, key, buf, sizeof(buf) - 1);
        if (n >= 0) return std::string(buf, static_cast<size_t>(std::min<int>(n, (int)sizeof(buf) - 1)));
    }
    return def_val ? def_val : "";
}

static std::string strip_html_tags(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    bool in_tag = false;
    for (char c : raw) {
        if (c == '<') { in_tag = true; continue; }
        if (c == '>') { in_tag = false; continue; }
        if (!in_tag) out.push_back(c);
    }
    out = koto_utils::replace_all(out, "&nbsp;", " ");
    out = koto_utils::replace_all(out, "&amp;", "&");
    out = koto_utils::replace_all(out, "&lt;", "<");
    out = koto_utils::replace_all(out, "&gt;", ">");
    return koto_utils::trim(out);
}

static std::string extract_class_text(const std::string& html, const std::string& cls_needle) {
    size_t pos = html.find(cls_needle);
    if (pos == std::string::npos) return "";
    size_t gt = html.find('>', pos);
    if (gt == std::string::npos) return "";
    size_t close = html.find("</", gt + 1);
    if (close == std::string::npos) return "";
    return strip_html_tags(html.substr(gt + 1, close - (gt + 1)));
}

struct FragmentInfo {
    bool found = false;
    std::string status; // "sold", "available", "none"
    std::string price;
};

static FragmentInfo check_fragment(koto_ctx* ctx, const koto_api* api, const std::string& username) {
    FragmentInfo info;
    std::string url = "https://fragment.com/username/" + koto_utils::url_encode(username);
    std::string html = koto_utils::http_get(
        api, ctx, url,
        {"User-Agent: Mozilla/5.0 (Linux; Android 14) AppleWebKit/537.36 Chrome/124.0 Safari/537.36"},
        10
    );
    if (html.empty()) return info;

    std::string status = extract_class_text(html, "tm-section-header-status");
    std::string lower = status;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower.find("sold") != std::string::npos) {
        info.found = true;
        info.status = "sold";
        info.price = extract_class_text(html, "tm-value");
    } else if (lower.find("available") != std::string::npos || lower.find("auction") != std::string::npos) {
        info.found = true;
        info.status = "available";
        info.price = extract_class_text(html, "tm-value");
    }
    return info;
}

static void send_answer(koto_ctx* ctx, const koto_api* api, int flags = KOTO_NO_LINK_PREVIEW) {
    if (api->is_outgoing(ctx)) api->c_edit(ctx, flags);
    else api->c_reply(ctx, flags);
}

static void add_close_btn(koto_ctx* ctx, const koto_api* api, bool en) {
    if (api->c_button) {
        api->c_button(ctx, en ? "✖ Close" : "✖ закрыть", "hu:close", 1);
    }
}

void cmd_uz(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) return;
    bool en = koto_utils::is_en(ctx, api);

    std::string args = api->cmd_args ? koto_utils::trim(api->cmd_args(ctx) ? api->cmd_args(ctx) : "") : "";
    while (!args.empty() && args.front() == '@') args.erase(args.begin());
    size_t sp = args.find_first_of(" \t\r\n");
    if (sp != std::string::npos) args = args.substr(0, sp);

    if (args.empty()) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❗️", EMO_EXCL);
        api->c_fmt(ctx, en ? " Specify a username!!" : " укажи юзак!!", KOTO_ENT_BOLD);
        send_answer(ctx, api);
        return;
    }

    if (args.size() < 4) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❗️", EMO_EXCL);
        api->c_fmt(ctx, en ? " Minimum 4 characters!!" : " минимум 4 символа!!", KOTO_ENT_BOLD);
        send_answer(ctx, api);
        return;
    }

    if (contains_cyrillic(args)) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❗️", EMO_EXCL);
        api->c_fmt(ctx, en ? " Username cannot contain Cyrillic letters!!"
                           : " в юзернейме не может быть русских букв!!", KOTO_ENT_BOLD);
        send_answer(ctx, api);
        return;
    }

    if (!api->host_version || api->host_version(ctx) < 118 || !api->check_username) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❗️", EMO_EXCL);
        api->c_fmt(ctx, en ? " KoteLoader v0.4.21+ (code 118) is required."
                           : " Требуется KoteLoader v0.4.21+ (код 118).", KOTO_ENT_BOLD);
        send_answer(ctx, api);
        return;
    }

    int avail = valid_username_chars(args) ? api->check_username(ctx, args.c_str()) : -1;

    if (avail == 1) {
        api->c_reset(ctx);
        api->c_text(ctx, en ? "username " : "юзак ");
        api->c_fmt(ctx, ("@" + args).c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, en ? " — is free!!!\n\nDo you want to claim this username?"
                            : " — свободен!!!\n\nхочешь занять этот юзернейм?");
        if (api->c_button_ex) {
            api->c_button_ex(ctx, en ? "✔ Claim" : "✔ занять", ("hu:grab:" + args).c_str(), 1, KOTO_BTN_SUCCESS, 0);
            api->c_button_ex(ctx, "✖", "hu:close", 0, KOTO_BTN_DANGER, 0);
        } else if (api->c_button) {
            api->c_button(ctx, en ? "✔ Claim" : "✔ занять", ("hu:grab:" + args).c_str(), 1);
            api->c_button(ctx, "✖", "hu:close", 0);
        }
        send_answer(ctx, api);
        return;
    }

    // Показываем статус проверки (живой progress edit)
    api->c_reset(ctx);
    api->c_fmt(ctx, (std::string(en ? "checking.. @" : "проверяю.. @") + args + "...").c_str(), KOTO_ENT_BOLD);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, KOTO_NO_LINK_PREVIEW);

    FragmentInfo frag = check_fragment(ctx, api, args);
    if (frag.found) {
        std::string frag_url = "https://fragment.com/username/" + args;
        api->c_reset(ctx);
        api->c_emoji(ctx, "❗️", EMO_EXCL);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, ("@" + args).c_str(), KOTO_ENT_BOLD);
        if (frag.status == "sold") {
            api->c_text(ctx, en ? " — sold on Fragment.\n\n" : " — продан на Fragment.\n\n");
        } else {
            api->c_text(ctx, en ? " — occupied.\n\n" : " — занят.\n\n");
        }
        api->c_emoji(ctx, "⚡️", EMO_BOLT);
        api->c_fmt(ctx, en ? " Found on Fragment:\n" : " найден на Fragment:\n", KOTO_ENT_BOLD);
        if (!frag.price.empty()) {
            api->c_emoji(ctx, "🪙", EMO_COIN);
            api->c_fmt(ctx, en ? " price: " : " цена: ", KOTO_ENT_BOLD);
            api->c_fmt(ctx, frag.price.c_str(), KOTO_ENT_CODE);
            api->c_text(ctx, " TON\n");
        }
        api->c_emoji(ctx, "🔗", EMO_LINK);
        api->c_fmt(ctx, en ? " link: " : " ссылка: ", KOTO_ENT_BOLD);
        api->c_url(ctx, frag_url.c_str(), frag_url.c_str());
        add_close_btn(ctx, api, en);
        send_answer(ctx, api);
        return;
    }

    // Не найден на Fragment — проверяем через resolve: если не резолвится, значит в бане или недопустим
    long long resolved = (avail == -1) ? 0 : (api->resolve ? api->resolve(ctx, args.c_str()) : 0);
    api->c_reset(ctx);
    if (resolved == 0) {
        api->c_emoji(ctx, "👎", EMO_DOWN);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, ("@" + args).c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, en ? " — banned or invalid" : " — в бане или недопустим");
    } else {
        api->c_emoji(ctx, "❗️", EMO_EXCL);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, ("@" + args).c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, en ? " — taken." : " — занят.");
    }
    add_close_btn(ctx, api, en);
    send_answer(ctx, api);
}

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    const char* raw = api->cb_data ? api->cb_data(ctx) : nullptr;
    if (!raw || std::strncmp(raw, "hu:", 3) != 0) return;

    bool en = koto_utils::is_en(ctx, api);
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) {
        if (api->cb_answer) api->cb_answer(ctx, en ? "❌ Access denied" : "❌ Нет доступа", 1);
        return;
    }

    std::string data(raw);
    if (data == "hu:close") {
        if (api->cb_answer) api->cb_answer(ctx, en ? "Closed" : "Закрыто", 0);
        if (api->delete_msg) api->delete_msg(ctx);
        return;
    }

    if (data.rfind("hu:grab:", 0) == 0) {
        std::string username = data.substr(8);
        if (username.empty()) return;

        if (!api->create_channel || !api->update_channel_username) {
            if (api->cb_answer) api->cb_answer(ctx, en ? "Requires KoteLoader 0.4.21+" : "Нужен KoteLoader 0.4.21+", 1);
            return;
        }

        if (api->cb_answer) api->cb_answer(ctx, en ? "Claiming..." : "захватываю...", 0);

        std::string title  = read_cfg(ctx, api, "channel_title", DEFAULT_TITLE);
        if (title.empty()) title = DEFAULT_TITLE;
        std::string about  = read_cfg(ctx, api, "channel_about", DEFAULT_ABOUT);
        std::string avatar = koto_utils::trim(read_cfg(ctx, api, "channel_avatar_url", DEFAULT_AVATAR));

        char err[256] = {0};
        long long cid = api->create_channel(ctx, title.c_str(), about.c_str(), 0, err, sizeof(err) - 1);
        if (cid == 0) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❗️", EMO_EXCL);
            api->c_fmt(ctx, en ? " Error:\n" : " Ошибка:\n", KOTO_ENT_BOLD);
            api->c_fmt(ctx, err[0] ? err : "create_channel failed", KOTO_ENT_CODE);
            add_close_btn(ctx, api, en);
            api->c_edit(ctx, KOTO_NO_LINK_PREVIEW);
            return;
        }

        err[0] = '\0';
        int ok_un = api->update_channel_username(ctx, cid, username.c_str(), err, sizeof(err) - 1);
        if (!ok_un) {
            api->c_reset(ctx);
            api->c_emoji(ctx, "❗️", EMO_EXCL);
            api->c_fmt(ctx, en ? " Error:\n" : " Ошибка:\n", KOTO_ENT_BOLD);
            api->c_fmt(ctx, err[0] ? err : "update_channel_username failed", KOTO_ENT_CODE);
            add_close_btn(ctx, api, en);
            api->c_edit(ctx, KOTO_NO_LINK_PREVIEW);
            return;
        }

        if (!avatar.empty() && api->edit_channel_photo) {
            char perr[256] = {0};
            api->edit_channel_photo(ctx, cid, avatar.c_str(), perr, sizeof(perr) - 1);
        }

        api->c_reset(ctx);
        api->c_emoji(ctx, "💥", EMO_BOOM);
        api->c_text(ctx, " ");
        api->c_fmt(ctx, ("@" + username).c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, en ? " claimed successfully!\n\nChannel: t.me/" : " успешно занят!\n\nКанал: t.me/");
        api->c_text(ctx, username.c_str());
        add_close_btn(ctx, api, en);
        api->c_edit(ctx, KOTO_NO_LINK_PREVIEW);
    }
}

static const koto_setting_field SETTINGS[] = {
    {"channel_title", "Название канала при захвате", KOTO_FIELD_STR, DEFAULT_TITLE, nullptr},
    {"channel_about", "Описание канала при захвате", KOTO_FIELD_STR, DEFAULT_ABOUT, nullptr},
    {"channel_avatar_url", "Ссылка на аватарку канала", KOTO_FIELD_URL, DEFAULT_AVATAR, nullptr},
};

static const koto_command COMMANDS[] = {
    {
        "uz",
        cmd_uz,
        KOTO_LEVEL_OWNER,
        "[ru] Проверить доступность юзернейма (Telegram + Fragment) с возможностью сразу занять его на канал (by Kote). "
        "[en] Check username availability (Telegram + Fragment) and claim it onto a channel (by Kote).",
        "<username>"
    }
};

static const koto_module MODULE = {
    .abi_version      = KOTO_MODULE_ABI,
    .name             = "hookusername",
    .description      = "[ru] Проверка и мгновенный захват Telegram-юзернеймов с проверкой Fragment.com (by Kote). "
                        "[en] Check and instantly claim Telegram usernames with Fragment.com lookup (by Kote).",
    .version          = VERSION,
    .min_host_version = 118,
    .flags            = KOTO_MOD_NO_TWIN,
    .commands         = COMMANDS,
    .command_count    = static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    .on_watch         = nullptr,
    .watch_flags      = 0,
    .on_callback      = on_callback,
    .settings         = SETTINGS,
    .settings_count   = static_cast<int>(sizeof(SETTINGS) / sizeof(SETTINGS[0])),
    .update_url       = "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/hookusername.so",
};

} // namespace

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
