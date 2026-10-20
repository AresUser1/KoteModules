#ifndef KOTO_UTILS_HPP
#define KOTO_UTILS_HPP

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <array>
#include <cstdarg>
#include <ctime>
#include <mutex>
#include <unistd.h>
#include "module_abi.h"

namespace koto_utils {

struct LogEntry {
    std::string time_str;
    std::string level;
    std::string tag;
    std::string msg;
};

class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    void log(const char* level, const char* tag, const char* fmt, ...) {
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        time_t now = time(nullptr);
        struct tm tm_buf;
        localtime_r(&now, &tm_buf);
        char t_str[32];
        snprintf(t_str, sizeof(t_str), "%02d:%02d:%02d", tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

        fprintf(stderr, "[%s][%s][%s] %s\n", t_str, level, tag, buf);

        std::lock_guard<std::mutex> lock(mtx_);
        entries_.push_back({t_str, level, tag, buf});
        if (entries_.size() > 50) {
            entries_.erase(entries_.begin());
        }
    }

    std::vector<LogEntry> get_entries() {
        std::lock_guard<std::mutex> lock(mtx_);
        return entries_;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mtx_);
        entries_.clear();
    }

private:
    std::mutex mtx_;
    std::vector<LogEntry> entries_;
};

} // namespace koto_utils

#ifndef KOTO_LOGI
#define KOTO_LOGI(tag, fmt, ...) ::koto_utils::Logger::instance().log("INFO", tag, fmt, ##__VA_ARGS__)
#endif
#ifndef KOTO_LOGW
#define KOTO_LOGW(tag, fmt, ...) ::koto_utils::Logger::instance().log("WARN", tag, fmt, ##__VA_ARGS__)
#endif
#ifndef KOTO_LOGE
#define KOTO_LOGE(tag, fmt, ...) ::koto_utils::Logger::instance().log("ERR", tag, fmt, ##__VA_ARGS__)
#endif
#ifndef KOTO_LOGD
#define KOTO_LOGD(tag, fmt, ...) ::koto_utils::Logger::instance().log("DEBUG", tag, fmt, ##__VA_ARGS__)
#endif

namespace koto_utils {

// Обрезка пробельных символов
inline std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Замена всех вхождений подстроки
inline std::string replace_all(std::string str, const std::string& from, const std::string& to) {
    if (from.empty()) return str;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
    return str;
}

// URL-кодирование для поисковых запросов и параметров
inline std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << std::uppercase << int(c);
        }
    }
    return escaped.str();
}

// Экранирование для shell аргументов (защита от инъекций при вызове curl)
inline std::string shell_escape(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

// HTTP GET запрос через хост-API или fallback через curl
inline std::string http_get(const koto_api* api, koto_ctx* ctx, const std::string& url, const std::vector<std::string>& headers = {}, int timeout_sec = 10) {
    if (api && api->http_get) {
        std::vector<unsigned char> buf(256 * 1024);
        long long n = api->http_get(ctx, url.c_str(), buf.data(), static_cast<int>(buf.size()));
        if (n > 0) {
            return std::string(reinterpret_cast<char*>(buf.data()), static_cast<size_t>(n));
        }
    }
    std::string cmd = "curl -sL --max-time " + std::to_string(timeout_sec);
    for (const auto& h : headers) {
        cmd += " -H " + shell_escape(h);
    }
    cmd += " " + shell_escape(url);

    struct PipeCloser {
        void operator()(FILE* fp) const { if (fp) pclose(fp); }
    };
    std::array<char, 4096> buffer;
    std::string result;
    std::unique_ptr<FILE, PipeCloser> pipe(popen(cmd.c_str(), "r"));
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

inline std::string http_get(const std::string& url, const std::vector<std::string>& headers = {}, int timeout_sec = 10) {
    return http_get(nullptr, nullptr, url, headers, timeout_sec);
}

// Скачивание файла по URL через хост-API (NativeBridge.downloadFile) или fallback через curl
inline bool download_file(const koto_api* api, koto_ctx* ctx, const std::string& url, const std::string& out_path, const std::vector<std::string>& headers = {}, int timeout_sec = 60) {
    if (api && api->download_file) {
        if (api->download_file(ctx, url.c_str(), out_path.c_str()) != 0) {
            FILE* f = fopen(out_path.c_str(), "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                long sz = ftell(f);
                fclose(f);
                if (sz > 0) return true;
            }
        }
    }
    std::string cmd = "curl -sL --max-time " + std::to_string(timeout_sec);
    for (const auto& h : headers) {
        cmd += " -H " + shell_escape(h);
    }
    cmd += " -o " + shell_escape(out_path) + " " + shell_escape(url);

    int ret = std::system(cmd.c_str());
    if (ret != 0) return false;

    FILE* f = fopen(out_path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    return size > 0;
}

inline bool download_file(const std::string& url, const std::string& out_path, const std::vector<std::string>& headers = {}, int timeout_sec = 60) {
    return download_file(nullptr, nullptr, url, out_path, headers, timeout_sec);
}

// Форматирование секунд в формат MM:SS или HH:MM:SS
inline std::string format_duration(long long total_seconds) {
    if (total_seconds < 0) total_seconds = 0;
    long long hours = total_seconds / 3600;
    long long minutes = (total_seconds % 3600) / 60;
    long long seconds = total_seconds % 60;

    char buf[32];
    if (hours > 0) {
        snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld", hours, minutes, seconds);
    } else {
        snprintf(buf, sizeof(buf), "%02lld:%02lld", minutes, seconds);
    }
    return std::string(buf);
}

// Генерация визуального прогресс-бара для плеера
inline std::string progress_bar(long long current, long long total, int bar_width = 10) {
    if (total <= 0) total = 1;
    if (current < 0) current = 0;
    if (current > total) current = total;

    double progress = static_cast<double>(current) / static_cast<double>(total);
    int pos = static_cast<int>(progress * bar_width);
    if (pos >= bar_width) pos = bar_width - 1;

    std::string bar = "";
    for (int i = 0; i < bar_width; ++i) {
        if (i == pos) {
            bar += "🔘";
        } else {
            bar += "─";
        }
    }
    return bar;
}

// Разделение строки по разделителю
inline std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> elems;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

// Хелперы для работы с локальной базой данных модуля
inline std::string db_get_str(const koto_api* api, koto_ctx* ctx, const std::string& key, const std::string& def = "") {
    if (!api || !api->db_get) return def;
    char buf[2048] = {0};
    int len = api->db_get(ctx, key.c_str(), buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return std::string(buf);
    }
    return def;
}

inline void db_set_str(const koto_api* api, koto_ctx* ctx, const std::string& key, const std::string& val) {
    if (api && api->db_set) {
        api->db_set(ctx, key.c_str(), val.c_str());
    }
}

inline long long db_get_int64(const koto_api* api, koto_ctx* ctx, const std::string& key, long long def = 0) {
    if (!api || !api->db_get_int) return def;
    return api->db_get_int(ctx, key.c_str(), def);
}

inline void db_set_int64(const koto_api* api, koto_ctx* ctx, const std::string& key, long long val) {
    if (api && api->db_set_int) {
        api->db_set_int(ctx, key.c_str(), val);
    }
}

// Хелперы для работы с настройками (.cfg)
inline std::string cfg_get_str(const koto_api* api, koto_ctx* ctx, const std::string& key, const std::string& def = "") {
    if (!api || !api->cfg_get) return def;
    char buf[512] = {0};
    int len = api->cfg_get(ctx, key.c_str(), buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return std::string(buf);
    }
    return def;
}

inline std::string mod_get_setting(const koto_api* api, koto_ctx* ctx, const std::string& key, const std::string& def = "") {
    if (api && api->cfg_get) {
        char buf[512] = {0};
        int len = api->cfg_get(ctx, key.c_str(), buf, sizeof(buf) - 1);
        if (len > 0) return std::string(buf, len);
    }
    return def;
}

inline long long mod_get_setting_int(const koto_api* api, koto_ctx* ctx, const std::string& key, long long def = 0) {
    std::string s = mod_get_setting(api, ctx, key, "");
    if (s.empty()) return def;
    try {
        return std::stoll(s);
    } catch (...) {
        return def;
    }
}

// ── Хелперы для кнопок и баннеров (Kotogram 0.2.8+ / 0.3.6+) ──────────────────

// Добавление инлайн-кнопки (поддерживает обычные кнопки, нативный покрас style и кастом-эмодзи)
inline void add_button(const koto_api* api, koto_ctx* ctx, const std::string& text, const std::string& data, bool new_row = false, int color = KOTO_BTN_DEFAULT, long long icon = 0) {
    if (!api) return;
    if (api->c_button_ex && (color != KOTO_BTN_DEFAULT || icon != 0)) {
        api->c_button_ex(ctx, text.c_str(), data.c_str(), new_row ? 1 : 0, color, icon);
    } else if (api->c_button) {
        api->c_button(ctx, text.c_str(), data.c_str(), new_row ? 1 : 0);
    }
}

// Установка баннера (link-preview) для via-bot сообщения
inline void set_banner(const koto_api* api, koto_ctx* ctx, const std::string& url, int flags = KOTO_BANNER_DEFAULT) {
    if (api && api->c_banner && !url.empty()) {
        api->c_banner(ctx, url.c_str(), flags);
    }
}

// ── Хелперы управления профилем Telegram (Section 26 ABI / Kotogram 0.3.6+) ───

inline void set_profile_name(const koto_api* api, koto_ctx* ctx, const std::string& first, const std::string& last = "") {
    if (api && api->set_name) api->set_name(ctx, first.c_str(), last.c_str());
}

inline void set_profile_bio(const koto_api* api, koto_ctx* ctx, const std::string& bio) {
    if (api && api->set_bio) api->set_bio(ctx, bio.c_str());
}

inline void set_profile_username(const koto_api* api, koto_ctx* ctx, const std::string& username) {
    if (api && api->set_username) api->set_username(ctx, username.c_str());
}

inline void set_profile_emoji_status(const koto_api* api, koto_ctx* ctx, long long doc_id, int until_sec = 0) {
    if (api && api->set_emoji_status) api->set_emoji_status(ctx, doc_id, until_sec);
}

inline void set_profile_avatar(const koto_api* api, koto_ctx* ctx, const std::string& path_or_url) {
    if (api && api->set_profile_photo) api->set_profile_photo(ctx, path_or_url.c_str());
}

inline void delete_profile_avatar(const koto_api* api, koto_ctx* ctx) {
    if (api && api->delete_profile_photo) api->delete_profile_photo(ctx);
}

inline void set_profile_online(const koto_api* api, koto_ctx* ctx, bool online) {
    if (api && api->set_online) api->set_online(ctx, online ? 1 : 0);
}

// ── Хелперы данных пользователей и чатов (Section 27 ABI / Kotogram 0.3.6 Hotfix 4) ───

inline std::string get_user_name(const koto_api* api, koto_ctx* ctx, long long uid = 0) {
    if (!api || !api->user_name) return "";
    char buf[256] = {0};
    int len = api->user_name(ctx, uid, buf, sizeof(buf));
    return (len > 0) ? std::string(buf, len) : "";
}

inline std::string get_user_first_name(const koto_api* api, koto_ctx* ctx, long long uid = 0) {
    if (!api || !api->user_first_name) return "";
    char buf[256] = {0};
    int len = api->user_first_name(ctx, uid, buf, sizeof(buf));
    return (len > 0) ? std::string(buf, len) : "";
}

inline std::string get_user_last_name(const koto_api* api, koto_ctx* ctx, long long uid = 0) {
    if (!api || !api->user_last_name) return "";
    char buf[256] = {0};
    int len = api->user_last_name(ctx, uid, buf, sizeof(buf));
    return (len > 0) ? std::string(buf, len) : "";
}

inline std::string get_user_username(const koto_api* api, koto_ctx* ctx, long long uid = 0) {
    if (!api || !api->user_username) return "";
    char buf[128] = {0};
    int len = api->user_username(ctx, uid, buf, sizeof(buf));
    return (len > 0) ? std::string(buf, len) : "";
}

inline std::string get_user_full_json(const koto_api* api, koto_ctx* ctx, long long uid = 0) {
    if (!api || !api->user_full_json) return "{}";
    std::vector<char> buf(4096, 0);
    int len = api->user_full_json(ctx, uid, buf.data(), (int)buf.size());
    if (len >= (int)buf.size()) {
        buf.resize(len + 16, 0);
        len = api->user_full_json(ctx, uid, buf.data(), (int)buf.size());
    }
    return (len > 0) ? std::string(buf.data(), len) : "{}";
}

inline std::string get_chat_full_json(const koto_api* api, koto_ctx* ctx, long long cid = 0) {
    if (!api || !api->chat_full_json) return "{}";
    std::vector<char> buf(4096, 0);
    int len = api->chat_full_json(ctx, cid, buf.data(), (int)buf.size());
    if (len >= (int)buf.size()) {
        buf.resize(len + 16, 0);
        len = api->chat_full_json(ctx, cid, buf.data(), (int)buf.size());
    }
    return (len > 0) ? std::string(buf.data(), len) : "{}";
}

// Возвращает список ID участников чата из кэшированного chat_full_json (v0.3.13+)
inline std::vector<long long> get_chat_participants(const koto_api* api, koto_ctx* ctx, long long cid = 0) {
    std::vector<long long> uids;
    std::string j = get_chat_full_json(api, ctx, cid);
    size_t ppos = j.find("\"participant_ids\"");
    if (ppos != std::string::npos) {
        size_t b = j.find('[', ppos);
        size_t e = (b != std::string::npos) ? j.find(']', b) : std::string::npos;
        if (b != std::string::npos && e != std::string::npos) {
            std::string sub = j.substr(b + 1, e - b - 1);
            std::stringstream ss(sub);
            std::string item;
            while (std::getline(ss, item, ',')) {
                char* end = nullptr;
                long long id = strtoll(item.c_str(), &end, 10);
                if (id > 0) uids.push_back(id);
            }
        }
    }
    return uids;
}

// ── Хелперы модерации и администрирования чатов (Section 28 ABI / Kotogram 0.3.7) ───

inline void ban_user(const koto_api* api, koto_ctx* ctx, long long chat_id, long long user_id, int until_sec = 0) {
    if (api && api->ban_user) api->ban_user(ctx, chat_id, user_id, until_sec);
}

inline void unban_user(const koto_api* api, koto_ctx* ctx, long long chat_id, long long user_id) {
    if (api && api->unban_user) api->unban_user(ctx, chat_id, user_id);
}

inline void kick_user(const koto_api* api, koto_ctx* ctx, long long chat_id, long long user_id) {
    if (api && api->kick_user) api->kick_user(ctx, chat_id, user_id);
}

inline void mute_user(const koto_api* api, koto_ctx* ctx, long long chat_id, long long user_id, int until_sec = 0) {
    if (api && api->mute_user) api->mute_user(ctx, chat_id, user_id, until_sec);
}

inline void unmute_user(const koto_api* api, koto_ctx* ctx, long long chat_id, long long user_id) {
    if (api && api->unmute_user) api->unmute_user(ctx, chat_id, user_id);
}

inline std::string get_history_json(const koto_api* api, koto_ctx* ctx, long long chat_id, int offset_id = 0, int limit = 20, int add_offset = 0) {
    if (!api || !api->get_history_json) return "[]";
    std::vector<char> buf(65536, 0);
    int len = api->get_history_json(ctx, chat_id, offset_id, limit, add_offset, buf.data(), (int)buf.size());
    if (len >= (int)buf.size()) {
        buf.resize(len + 16, 0);
        len = api->get_history_json(ctx, chat_id, offset_id, limit, add_offset, buf.data(), (int)buf.size());
    }
    return (len > 0) ? std::string(buf.data(), len) : "[]";
}

inline long long reply_msg_id(const koto_api* api, koto_ctx* ctx) {
    if (!api || !api->reply_msg_id) return 0;
    return api->reply_msg_id(ctx);
}

} // namespace koto_utils

#endif // KOTO_UTILS_HPP
