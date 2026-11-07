#include "module_abi.h"
#include "nlohmann/json.hpp"

#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <iomanip>

namespace {
using json = nlohmann::json;

constexpr const char* VERSION = "1.0.0";
constexpr const char* GOOGLE_STT_KEY = "AIzaSyBOti4mM-6x9WDnZIjIeyEU21OpBXqWBgw";

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

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

std::string transcribe_audio(koto_ctx* ctx, const koto_api* api, const unsigned char* data, int size, const std::string& lang) {
    if (!api->http_post || !data || size <= 0) return "";

    std::string target_lang = lang.empty() ? "ru-RU" : lang;
    std::string url = "https://www.google.com/speech-api/v2/recognize?output=json&lang=" + url_encode(target_lang) +
                      "&key=" + GOOGLE_STT_KEY + "&pFilter=0";

    std::string headers = "Content-Type: audio/ogg; codecs=opus\r\nUser-Agent: Mozilla/5.0";

    std::vector<unsigned char> resp(256 * 1024, 0);
    long long n = api->http_post(ctx, url.c_str(), reinterpret_cast<const char*>(data), headers.c_str(), resp.data(), (int)resp.size());
    if (n <= 0) return "";

    std::string body(reinterpret_cast<const char*>(resp.data()), (size_t)n);
    std::istringstream stream(body);
    std::string line;
    std::string best_transcript;

    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty()) continue;
        try {
            json j = json::parse(line);
            if (j.contains("result") && j["result"].is_array() && !j["result"].empty()) {
                for (const auto& res : j["result"]) {
                    if (res.contains("alternative") && res["alternative"].is_array() && !res["alternative"].empty()) {
                        std::string trans = res["alternative"][0].value("transcript", "");
                        if (!trans.empty()) {
                            if (!best_transcript.empty()) best_transcript += " ";
                            best_transcript += trans;
                        }
                    }
                }
            }
        } catch (...) {}
    }

    return best_transcript;
}

// ── Command: .v2t ────────────────────────────────────────────────────────────
void cmd_v2t(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    if (api->is_outgoing(ctx)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "🎙️ Распознаю речь…", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
    }

    std::vector<unsigned char> data;
    long long fsz = api->reply_file ? api->reply_file(ctx, nullptr, 0) : -1;
    if (fsz > 0 && fsz <= 25 * 1024 * 1024) {
        data.resize((size_t)fsz);
        long long got = api->reply_file(ctx, data.data(), (int)data.size());
        if (got != fsz) data.clear();
    }

    if (data.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Ответьте на голосовое сообщение или видео-кружок: .v2t", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    char lang_buf[32] = {0};
    std::string lang = "ru-RU";
    if (api->setting_get && api->setting_get(ctx, "lang", lang_buf, sizeof(lang_buf)) > 0 && lang_buf[0]) {
        lang = lang_buf;
    }

    const char* raw_args = api->cmd_args(ctx);
    if (raw_args) {
        std::string a = trim(raw_args);
        if (a == "en") lang = "en-US";
        else if (a == "ru") lang = "ru-RU";
        else if (!a.empty()) lang = a;
    }

    std::string result = transcribe_audio(ctx, api, data.data(), (int)data.size(), lang);

    api->c_reset(ctx);
    if (result.empty()) {
        api->c_fmt(ctx, "🫥 Не удалось распознать речь в сообщении.", KOTO_ENT_ITALIC);
    } else {
        api->c_fmt(ctx, "🎙️ Распознанный текст (by kote):\n\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, result.c_str(), KOTO_ENT_BLOCKQUOTE);
    }

    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Command: .v2tauto ────────────────────────────────────────────────────────
void cmd_v2tauto(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    std::string key = "v2t_auto:" + std::to_string(cid);

    char buf[16] = {0};
    int n = api->db_get(ctx, key.c_str(), buf, sizeof(buf));
    bool is_on = (n > 0 && std::string(buf) == "1");

    const char* raw = api->cmd_args(ctx);
    std::string arg = raw ? trim(raw) : "";

    if (arg == "on" || arg == "1") is_on = false;
    else if (arg == "off" || arg == "0") is_on = true;

    if (is_on) {
        api->db_del(ctx, key.c_str());
        api->c_reset(ctx);
        api->c_fmt(ctx, "🔕 Авто-транскрибация голосовых в этом чате выключена.", KOTO_ENT_BOLD);
    } else {
        api->db_set(ctx, key.c_str(), "1");
        api->c_reset(ctx);
        api->c_fmt(ctx, "🔔 Авто-транскрибация голосовых в этом чате включена! (by kote)", KOTO_ENT_BOLD);
    }

    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Watcher: auto-transcribe on incoming voice ───────────────────────────────
int on_watch(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);
    std::string key = "v2t_auto:" + std::to_string(cid);
    char buf[16] = {0};
    if (api->db_get(ctx, key.c_str(), buf, sizeof(buf)) <= 0 || std::string(buf) != "1") {
        return 0;
    }

    long long fsz = api->reply_file ? api->reply_file(ctx, nullptr, 0) : -1;
    if (fsz <= 0 || fsz > 15 * 1024 * 1024) return 0;

    std::vector<unsigned char> data(static_cast<size_t>(fsz));
    long long got = api->reply_file(ctx, data.data(), (int)data.size());
    if (got != fsz) return 0;

    char lang_buf[32] = {0};
    std::string lang = "ru-RU";
    if (api->setting_get && api->setting_get(ctx, "lang", lang_buf, sizeof(lang_buf)) > 0 && lang_buf[0]) {
        lang = lang_buf;
    }

    std::string result = transcribe_audio(ctx, api, data.data(), (int)data.size(), lang);
    if (!result.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "🎙️ ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, result.c_str(), KOTO_ENT_BLOCKQUOTE);
        api->c_reply(ctx, 0);
    }

    return 0; // don't swallow original message
}

const koto_setting_field SETTINGS[] = {
    {"lang", "Язык распознавания (ru-RU / en-US)", KOTO_FIELD_CHOICE, "ru-RU", "ru-RU,en-US"}
};

const koto_command COMMANDS[] = {
    {"v2t", cmd_v2t, KOTO_LEVEL_TRUSTED, "[ru] Перевести голосовое или видео-кружок в текст (by kote) [en] Transcribe voice/video to text (by kote).", "reply на голосовое [ru|en]"},
    {"v2tauto", cmd_v2tauto, KOTO_LEVEL_TRUSTED, "[ru] Включить/выключить авто-перевод голосовых в чате [en] Toggle auto-transcription in chat.", "[on|off]"}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "VoiceToText",
    "[ru] Преобразование голосовых сообщений и кружков в текст (by kote). [en] Voice and video note speech-to-text transcriber (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    on_watch,
    KOTO_WATCH_INCOMING,
    nullptr, // on_callback
    SETTINGS,
    static_cast<int>(sizeof(SETTINGS) / sizeof(SETTINGS[0])),
    "https://github.com/AresUser1/KoteModules/blob/main/src/voicetotext.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
