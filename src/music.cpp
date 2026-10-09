// music.cpp
// Поиск и скачивание музыки с YouTube напрямую на C++ (Kotogram ABI 3).
// Команды:
// .mus <запрос или ссылка> - Поиск на YouTube, скачивание лучшего аудиопотока (M4A/AAC) и отправка в чат
// .musconfig              - Статус модуля, список инстансов и справка
// .musclean               - Очистка кэша скачанных аудиотреков

#include "module_abi.h"
#include <string>
#include <vector>
#include <sstream>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace {

// Доступные зеркала Invidious API для поиска и стриминга без ограничений
const char* const INSTANCES[] = {
    "https://invidious.f5.si",
    "https://inv.tux.pizza",
    "https://yewtu.be",
    "https://invidious.nerdvpn.de"
};
constexpr size_t NUM_INSTANCES = sizeof(INSTANCES) / sizeof(INSTANCES[0]);

// URL-encoding для поискового запроса
std::string url_encode(const std::string& val) {
    std::string out;
    for (unsigned char c : val) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else if (c == ' ') {
            out += '+';
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

// Декодирование JSON-эскейпов (включая \uXXXX и обычные \")
std::string json_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char next = s[i + 1];
            if (next == '"') { out += '"'; i++; }
            else if (next == '\\') { out += '\\'; i++; }
            else if (next == '/') { out += '/'; i++; }
            else if (next == 'n') { out += '\n'; i++; }
            else if (next == 'r') { i++; }
            else if (next == 't') { out += '\t'; i++; }
            else if (next == 'u' && i + 5 < s.size()) {
                unsigned int code = 0;
                if (sscanf(s.substr(i + 2, 4).c_str(), "%x", &code) == 1) {
                    if (code < 0x80) {
                        out += (char)code;
                    } else if (code < 0x800) {
                        out += (char)(0xC0 | (code >> 6));
                        out += (char)(0x80 | (code & 0x3F));
                    } else {
                        out += (char)(0xE0 | (code >> 12));
                        out += (char)(0x80 | ((code >> 6) & 0x3F));
                        out += (char)(0x80 | (code & 0x3F));
                    }
                    i += 5;
                } else {
                    out += s[i];
                }
            } else {
                out += s[i];
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

// Поиск строки по ключу в JSON
std::string extract_json_str(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    size_t end = pos;
    while (end < json.size()) {
        if (json[end] == '"' && json[end - 1] != '\\') break;
        end++;
    }
    if (end > json.size()) return "";
    return json_unescape(json.substr(pos, end - pos));
}

// Поиск числа по ключу в JSON
int extract_json_int(const std::string& json, const std::string& key, int def = 0) {
    std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return def;
    pos += needle.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    int val = def;
    if (sscanf(json.c_str() + pos, "%d", &val) == 1) return val;
    return def;
}

// Извлечение чистого ID видео, если передана ссылка на YouTube
std::string extract_video_id_from_url(const std::string& arg) {
    size_t pos = arg.find("youtu.be/");
    if (pos != std::string::npos) {
        std::string sub = arg.substr(pos + 9);
        size_t q = sub.find_first_of("?&/ ");
        return (q != std::string::npos) ? sub.substr(0, q) : sub;
    }
    pos = arg.find("watch?v=");
    if (pos != std::string::npos) {
        std::string sub = arg.substr(pos + 8);
        size_t q = sub.find_first_of("&/ ");
        return (q != std::string::npos) ? sub.substr(0, q) : sub;
    }
    return "";
}

// Очистка имени файла от запрещенных символов файловой системы
std::string sanitize_filename(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || (unsigned char)c < 32) {
            out += '_';
        } else {
            out += c;
        }
    }
    if (out.size() > 80) out = out.substr(0, 80);
    return out.empty() ? "audio" : out;
}

// Выполнение HTTP GET с буфером 256 КБ
std::string fetch_http(koto_ctx* ctx, const koto_api* api, const std::string& url) {
    std::vector<unsigned char> buf(262144);
    long long n = api->http_get(ctx, url.c_str(), buf.data(), (int)buf.size() - 1);
    if (n <= 0) return "";
    buf[n] = '\0';
    return std::string((char*)buf.data(), (size_t)n);
}

// Форматирование длительности (секунды -> ММ:СС)
std::string format_duration(int seconds) {
    if (seconds <= 0) return "00:00";
    int m = seconds / 60;
    int s = seconds % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return buf;
}

// ── 1. Команда .mus <запрос или ссылка> ──────────────────────────────────────
void cmd_mus(koto_ctx* ctx, const koto_api* api) {
    const char* raw_args = api->cmd_args(ctx);
    while (raw_args && (*raw_args == ' ' || *raw_args == '\t')) raw_args++;
    if (!raw_args || !*raw_args) {
        api->c_reset(ctx);
        api->c_markdown(ctx,
            "⚠️ **Использование:** `.mus <название песни / артист / ссылка YouTube>`\n"
            "> Например: `.mus Linkin Park Numb` или `.mus Queen Bohemian Rhapsody`");
        api->c_edit(ctx, 0);
        return;
    }

    std::string query = raw_args;
    long long cid = api->chat_id(ctx);

    // 1. Уведомляем пользователя о начале поиска
    api->c_reset(ctx);
    api->c_markdown(ctx, "🔍 **Ищу музыку на YouTube:** `");
    api->c_text(ctx, query.c_str());
    api->c_markdown(ctx, "`...");
    api->c_edit(ctx, 0);

    std::string video_id = extract_video_id_from_url(query);
    std::string title;
    std::string author;

    // 2. Если это не прямая ссылка — ищем через Invidious Search API
    if (video_id.empty()) {
        std::string encoded_q = url_encode(query);
        for (size_t i = 0; i < NUM_INSTANCES; ++i) {
            std::string s_url = std::string(INSTANCES[i]) + "/api/v1/search?q=" + encoded_q +
                                "&type=video&fields=title,videoId,author";
            std::string resp = fetch_http(ctx, api, s_url);
            if (resp.empty() || resp.find("\"videoId\":") == std::string::npos) continue;

            video_id = extract_json_str(resp, "videoId");
            title    = extract_json_str(resp, "title");
            author   = extract_json_str(resp, "author");
            if (!video_id.empty()) break;
        }
    }

    if (video_id.empty()) {
        api->c_reset(ctx);
        api->c_markdown(ctx, "❌ **Трек не найден на YouTube!** Попробуйте уточнить название или указать исполнителя.");
        api->c_edit(ctx, 0);
        return;
    }

    // 3. Получаем аудиопотоки видео
    std::string audio_stream_url;
    int duration_sec = 0;

    for (size_t i = 0; i < NUM_INSTANCES; ++i) {
        std::string v_url = std::string(INSTANCES[i]) + "/api/v1/videos/" + video_id +
                            "?fields=title,author,lengthSeconds,adaptiveFormats";
        std::string v_json = fetch_http(ctx, api, v_url);
        if (v_json.empty()) continue;

        if (title.empty())  title  = extract_json_str(v_json, "title");
        if (author.empty()) author = extract_json_str(v_json, "author");
        duration_sec = extract_json_int(v_json, "lengthSeconds", 0);

        // Ищем наилучший аудиопоток M4A/AAC
        size_t m4a_pos = v_json.find("\"audio/mp4");
        if (m4a_pos == std::string::npos) {
            m4a_pos = v_json.find("\"audio/");
        }

        if (m4a_pos != std::string::npos) {
            size_t u_pos = v_json.rfind("\"url\":\"", m4a_pos);
            if (u_pos != std::string::npos) {
                u_pos += 7;
                size_t u_end = v_json.find("\"", u_pos);
                if (u_end != std::string::npos) {
                    audio_stream_url = json_unescape(v_json.substr(u_pos, u_end - u_pos));
                    break;
                }
            }
        }
    }

    if (audio_stream_url.empty()) {
        api->c_reset(ctx);
        api->c_markdown(ctx, "❌ **Не удалось получить аудиопоток с YouTube.** Попробуйте другой запрос.");
        api->c_edit(ctx, 0);
        return;
    }

    if (title.empty()) title = "YouTube Audio";
    if (author.empty()) author = "YouTube";

    // 4. Оповещаем о начале загрузки
    api->c_reset(ctx);
    api->c_markdown(ctx, "⬇️ **Скачиваю аудио с YouTube...**\n🎵 **Трек:** ");
    api->c_text(ctx, title.c_str());
    api->c_markdown(ctx, "\n👤 **Артист:** ");
    api->c_text(ctx, author.c_str());
    if (duration_sec > 0) {
        api->c_markdown(ctx, "\n⏱ **Длительность:** `");
        api->c_text(ctx, format_duration(duration_sec).c_str());
        api->c_markdown(ctx, "`");
    }
    api->c_edit(ctx, 0);

    // 5. Скачиваем аудиофайл в кэш приложения
    char cache_buf[512] = {0};
    api->cache_dir(ctx, cache_buf, sizeof(cache_buf));
    std::string cache_dir = (*cache_buf) ? cache_buf : "/data/local/tmp";

    std::string file_base = sanitize_filename(author + " - " + title);
    std::string local_path = cache_dir + "/yt_" + file_base + ".m4a";

    bool downloaded = api->download_file(ctx, audio_stream_url.c_str(), local_path.c_str()) != 0;
    std::error_code ec;
    if (!downloaded || !std::filesystem::exists(local_path, ec) || std::filesystem::file_size(local_path, ec) == 0) {
        api->c_reset(ctx);
        api->c_markdown(ctx, "❌ **Ошибка скачивания файла с YouTube!** Возможно сервер перегружен.");
        api->c_edit(ctx, 0);
        return;
    }

    // 6. Формируем подпись к медиа и отправляем файл
    api->c_reset(ctx);
    api->c_markdown(ctx, "🎵 **");
    api->c_text(ctx, title.c_str());
    api->c_markdown(ctx, "**\n👤 **Исполнитель:** ");
    api->c_text(ctx, author.c_str());
    if (duration_sec > 0) {
        api->c_markdown(ctx, "\n⏱ **Длительность:** `");
        api->c_text(ctx, format_duration(duration_sec).c_str());
        api->c_markdown(ctx, "`");
    }
    api->c_markdown(ctx, "\n> 🎧 Скачано через Kotogram YouTube Music");

    api->c_send_file(ctx, cid, local_path.c_str(), 0);

    // 7. Обновляем статусное сообщение
    api->c_reset(ctx);
    api->c_markdown(ctx, "✅ **Трек успешно отправлен!**\n> 🎵 ");
    api->c_text(ctx, title.c_str());
    api->c_edit(ctx, 0);
}

// ── 2. Команда .musconfig ───────────────────────────────────────────────────
void cmd_mus_config(koto_ctx* ctx, const koto_api* api) {
    api->c_reset(ctx);
    api->c_markdown(ctx,
        "🎵 **Kotogram YouTube Music Module (C++ ABI 3)**\n\n"
        "✨ **Возможности:**\n"
        "• Мгновенный поиск любого трека на YouTube без сторонних ботов\n"
        "• Прямое скачивание оригинального потока M4A/AAC\n"
        "• Поддержка отправки в плеер Telegram с артистом и названием\n"
        "• Поддержка как текста, так и ссылок `youtu.be` / `youtube.com`\n\n"
        "🌐 **Зеркала Invidious API:**\n");
    for (size_t i = 0; i < NUM_INSTANCES; ++i) {
        api->c_markdown(ctx, "• `");
        api->c_text(ctx, INSTANCES[i]);
        api->c_markdown(ctx, "`\n");
    }
    api->c_markdown(ctx, "\n🧹 **Очистка кэша:** `.musclean`");
    api->c_edit(ctx, 0);
}

// ── 3. Команда .musclean ────────────────────────────────────────────────────
void cmd_mus_clean(koto_ctx* ctx, const koto_api* api) {
    char cache_buf[512] = {0};
    api->cache_dir(ctx, cache_buf, sizeof(cache_buf));
    std::string cache_dir = (*cache_buf) ? cache_buf : "";

    int removed = 0;
    if (!cache_dir.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(cache_dir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(cache_dir, ec)) {
                if (entry.is_regular_file()) {
                    std::string fname = entry.path().filename().string();
                    if (fname.rfind("yt_", 0) == 0 && (fname.ends_with(".m4a") || fname.ends_with(".mp3") || fname.ends_with(".webm"))) {
                        std::filesystem::remove(entry.path(), ec);
                        removed++;
                    }
                }
            }
        }
    }

    api->c_reset(ctx);
    api->c_markdown(ctx, "🧹 **Очистка кэша музыки:** Удалено файлов: `");
    api->c_text(ctx, std::to_string(removed).c_str());
    api->c_markdown(ctx, "`");
    api->c_edit(ctx, 0);
}

// Декларация команд модуля
const koto_command COMMANDS[] = {
    {
        "mus",
        &cmd_mus,
        KOTO_LEVEL_ALL,
        "Поиск и отправка музыки с YouTube.",
        "<запрос или ссылка>"
    },
    {
        "musconfig",
        &cmd_mus_config,
        KOTO_LEVEL_ALL,
        "Информация и конфигурация музыкального модуля.",
        ""
    },
    {
        "musclean",
        &cmd_mus_clean,
        KOTO_LEVEL_ALL,
        "Очистить кэш скачанных треков на устройстве.",
        ""
    }
};

// Дескриптор модуля
const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "music",
    "Поиск и скачивание музыки с YouTube (C++ ABI 3)",
    "2.2.0",
    12, // KoteLoader v0.2.1+
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    nullptr,
    nullptr, 0,
    nullptr
};

} // namespace

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
