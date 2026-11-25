// mellstroyvoice.cpp — Звуковые эффекты Мелстроя (.банан, .быстрее и др.) (C++ ABI 3).
// Портирован из Python-модуля MellstroyVoice (by kote).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"

#include <cctype>
#include <filesystem>
#include <string>

namespace {

namespace fs = std::filesystem;

constexpr const char* VERSION = "1.1.0";
constexpr const char* BASE_URL = "https://raw.githubusercontent.com/neistv/mods/main/assets/other/mellstroy/";

struct VoiceEntry {
    const char* cmd;
    const char* filename;
};

static constexpr VoiceEntry VOICES[] = {
    {"банан", "1банан.ogg"},
    {"быстрее", "BISTREE.mp3"},
    {"ааа", "aaa.ogg"},
    {"амамам", "amamam.mp3"},
    {"бембембем", "bembembem.mp3"},
    {"блять", "bluat.mp3"},
    {"браво", "bravoo.mp3"},
    {"щавель", "chavel.mp3"},
    {"чтотыдумал", "chotidumal.mp3"},
    {"эхты", "extisuka.mp3"},
    {"идинахуй", "idinaxyu.mp3"},
    {"посидим", "posidim.mp3"},
    {"раздва", "razdva.mp3"},
    {"скольконахуй", "skolkanaxuy.mp3"},
    {"стопстопстоп", "stopstopstop.mp3"},
    {"свист", "svist.mp3"},
    {"виноград", "vinpgrad.mp3"},
    {"хамам", "xamamm.ogg"},
    {"хахаха", "xaxaxaxa.mp3"},
    {"уху", "yxyyy.mp3"},
    {"аоао", "аоао.ogg"},
    {"бабка", "бабка.ogg"},
    {"брбрбр", "брбрбр.ogg"},
    {"бурмофиксики", "бурмофиксики.ogg"},
    {"буяя", "буяя.ogg"},
    {"возняафриканская", "возняафриканская.ogg"},
    {"закинь5миллионов", "закинь5милионов.ogg"},
    {"зевсятина", "зевсятина.ogg"},
    {"кислый", "кислый.ogg"},
    {"милиция", "милиция.ogg"},
    {"нормалды", "нормалды.ogg"},
    {"омайгад", "омайгад.ogg"},
    {"отказано", "отказано.ogg"},
    {"подразбитьбы", "подразбитьбы.ogg"},
    {"реально", "реально.ogg"},
    {"тише", "тише.ogg"},
    {"учись", "учись.ogg"},
    {"че", "че.ogg"},
    {"элджиевка", "элджеевка.ogg"},
    {"этокто", "этокто.ogg"},
    {"яблоко", "яблоко.ogg"},
    {"яужекрасный", "яужекрасный.ogg"},
    {"бурмалда", "бурмалда.ogg"},
    {"салатик", "салатик.ogg"},
    {"стоит", "стоит.ogg"},
};

// Простой UTF-8 lowercase (ASCII + русская кириллица).
static std::string utf8_lower_ru(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c0 = static_cast<unsigned char>(s[i]);
        if (c0 < 0x80) {
            out.push_back(static_cast<char>(std::tolower(c0)));
            i += 1;
        } else if (i + 1 < s.size() && (c0 == 0xD0 || c0 == 0xD1)) {
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            if (c0 == 0xD0 && c1 >= 0x90 && c1 <= 0x9F) {
                // А..П -> а..п (D0 B0 .. D0 BF)
                out.push_back(static_cast<char>(0xD0));
                out.push_back(static_cast<char>(c1 + 0x20));
            } else if (c0 == 0xD0 && c1 >= 0xA0 && c1 <= 0xAF) {
                // Р..Я -> р..я (D1 80 .. D1 8F)
                out.push_back(static_cast<char>(0xD1));
                out.push_back(static_cast<char>(c1 - 0x20));
            } else if (c0 == 0xD0 && c1 == 0x81) {
                // Ё -> ё (D1 91)
                out.push_back(static_cast<char>(0xD1));
                out.push_back(static_cast<char>(0x91));
            } else {
                out.push_back(static_cast<char>(c0));
                out.push_back(static_cast<char>(c1));
            }
            i += 2;
        } else {
            out.push_back(static_cast<char>(c0));
            i += 1;
        }
    }
    return out;
}

static const VoiceEntry* find_voice(const std::string& query) {
    std::string q = utf8_lower_ru(koto_utils::trim(query));
    if (q.empty()) return nullptr;
    for (const auto& v : VOICES) {
        if (q == v.cmd) return &v;
    }
    return nullptr;
}

static std::string get_cache_root(koto_ctx* ctx, const koto_api* api) {
    if (api && api->cache_dir) {
        char buf[512] = {0};
        int n = api->cache_dir(ctx, buf, sizeof(buf) - 1);
        if (n > 0) return std::string(buf, static_cast<size_t>(n)) + "/mellstroy";
    }
    return "/data/local/tmp/mellstroy_cache";
}

static void send_voice_entry(koto_ctx* ctx, const koto_api* api, bool en, const VoiceEntry& entry) {
    std::error_code ec;
    std::string dir = get_cache_root(ctx, api);
    fs::create_directories(dir, ec);

    std::string path = dir + "/" + entry.filename;
    bool cached = fs::is_regular_file(path, ec) && fs::file_size(path, ec) > 256;

    if (!cached) {
        std::string url = std::string(BASE_URL) + koto_utils::url_encode(entry.filename);
        if (!koto_utils::download_file(api, ctx, url, path, {"User-Agent: MellstroyVoice/1.0"}, 20)) {
            fs::remove(path, ec);
            api->c_reset(ctx);
            api->c_fmt(ctx, en ? "❌ Failed to download audio clip." : "❌ Не удалось скачать звук.", KOTO_ENT_BOLD);
            if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
            else api->c_reply(ctx, 0);
            return;
        }
    }

    if (api->c_send_file && api->chat_id) {
        api->c_reset(ctx);
        int flags = 0;
        if (api->host_version && api->host_version(ctx) >= 118) {
            flags = KOTO_SEND_AS_VOICE | KOTO_REPLY_TO_TARGET;
        }
        api->c_send_file(ctx, api->chat_id(ctx), path.c_str(), flags);
        if (api->is_outgoing(ctx) && api->delete_msg) api->delete_msg(ctx);
    }
}

void cmd_voice(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    bool en = koto_utils::is_en(ctx, api);

    const char* cname = api->cmd_name ? api->cmd_name(ctx) : nullptr;
    if (!cname) return;

    const VoiceEntry* entry = find_voice(cname);
    if (!entry) return;
    send_voice_entry(ctx, api, en, *entry);
}

void cmd_mellstroy_help(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    bool en = koto_utils::is_en(ctx, api);

    std::string arg = api->cmd_args ? koto_utils::trim(api->cmd_args(ctx) ? api->cmd_args(ctx) : "") : "";
    if (!arg.empty()) {
        if (const VoiceEntry* e = find_voice(arg)) {
            send_voice_entry(ctx, api, en, *e);
            return;
        }
        api->c_reset(ctx);
        api->c_fmt(ctx, en ? "❌ Sound not found: " : "❌ Звук не найден: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, arg.c_str(), KOTO_ENT_CODE);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    api->c_reset(ctx);
    api->c_fmt(ctx, en ? "🎙 MellstroyVoice — 45 clips (by kote)\n\n" : "🎙 MellstroyVoice — 45 звуков (by kote)\n\n", KOTO_ENT_BOLD);
    int mark = api->c_mark ? api->c_mark(ctx) : -1;
    constexpr size_t count = sizeof(VOICES) / sizeof(VOICES[0]);
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) api->c_text(ctx, ", ");
        api->c_fmt(ctx, ("." + std::string(VOICES[i].cmd)).c_str(), KOTO_ENT_CODE);
    }
    if (mark >= 0 && api->c_wrap) {
        api->c_wrap(ctx, mark, KOTO_ENT_BLOCKQUOTE, 1);
    }
    api->c_text(ctx, en ? "\n\nAlso: .mellstroy <name>" : "\n\nТакже: .mellstroy <название>");
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    (void)ctx; (void)api;
}

static const koto_command COMMANDS[] = {
    {"mellstroy", cmd_mellstroy_help, KOTO_LEVEL_TRUSTED, "[ru] Список всех 45 звуков Мелстроя или запуск по имени (by kote). [en] List all 45 Mellstroy clips or play by name (by kote).", "[название]"},
    {"банан", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"быстрее", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"ааа", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"амамам", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"бембембем", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"блять", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"браво", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"щавель", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"чтотыдумал", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"эхты", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"идинахуй", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"посидим", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"раздва", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"скольконахуй", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"стопстопстоп", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"свист", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"виноград", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"хамам", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"хахаха", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"уху", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"аоао", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"бабка", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"брбрбр", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"бурмофиксики", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"буяя", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"возняафриканская", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"закинь5миллионов", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"зевсятина", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"кислый", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"милиция", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"нормалды", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"омайгад", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"отказано", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"подразбитьбы", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"реально", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"тише", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"учись", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"че", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"элджиевка", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"этокто", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"яблоко", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"яужекрасный", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"бурмалда", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"салатик", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""},
    {"стоит", cmd_voice, KOTO_LEVEL_TRUSTED, "​", ""}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "mellstroyvoice",
    "[ru] 45 аудиофраз и звуковых эффектов Мелстроя (by kote). [en] 45 Mellstroy audio clips and voice memes (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    nullptr, 0,
    on_callback,
    nullptr, 0,
    "https://github.com/AresUser1/KoteModules/blob/main/src/mellstroyvoice.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
