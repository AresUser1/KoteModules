// music_dl.cpp
// Полнофункциональный модуль поиска и загрузки музыки для Kotogram (C-ABI 3).
// Перенесено из music_dl.plugin.
// Автор: @Diezdd

#include <string>
#include <vector>
#include <sstream>
#include <regex>
#include <unordered_map>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdint>
#include "module_abi.h"
#include <dirent.h>
#include "koto_utils.hpp"
#include "koto_file.hpp"
#include "nlohmann/json.hpp"

using json = nlohmann::json;
using namespace koto_utils;

struct TrackInfo {
    std::string id;
    std::string title;
    std::string artist;
    long long duration_sec = 0;
    std::string stream_url;
    std::string transcode_url;
    std::string artwork_url;
    long long likes = 0;
    std::string service = "sc";
    bool has_drm = false;
};

struct SearchSession {
    long long chat_id = 0;
    std::string query;
    int page = 1;
    int per_page = 5;
    std::vector<TrackInfo> tracks;
};

// Хранилище сессий в оперативной памяти (быстро, надежно, не зависит от размера буфера)
static std::unordered_map<long long, SearchSession> g_sessions;
static SearchSession g_last_session;

// ─── SoundCloud Client ID Management ─────────────────────────────────────────
static std::string sc_parse_client_id(const koto_api* api, koto_ctx* ctx) {
    std::vector<std::string> headers = {
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        "Referer: https://soundcloud.com/"
    };
    std::string html = http_get(api, ctx, "https://soundcloud.com", headers, 8);
    if (html.empty()) return "";

    std::regex js_regex(R"(https://a-v2\.sndcdn\.com/assets/[^"'\s]+\.js)");
    auto words_begin = std::sregex_iterator(html.begin(), html.end(), js_regex);
    auto words_end = std::sregex_iterator();

    std::vector<std::string> js_urls;
    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        js_urls.push_back((*i).str());
    }

    std::regex cid_regex(R"raw(client_id\s*:\s*"([a-zA-Z0-9]{32})")raw");
    for (auto it = js_urls.rbegin(); it != js_urls.rend(); ++it) {
        std::string js = http_get(api, ctx, *it, headers, 6);
        std::smatch match;
        if (std::regex_search(js, match, cid_regex) && match.size() > 1) {
            return match[1].str();
        }
    }
    return "";
}

static std::string sc_get_cid(const koto_api* api, koto_ctx* ctx) {
    std::string custom = mod_get_setting(api, ctx, "sc_cid", "");
    if (!custom.empty()) return custom;

    std::string cached = db_get_str(api, ctx, "sc_cached_cid", "");
    if (!cached.empty()) return cached;

    std::string parsed = sc_parse_client_id(api, ctx);
    if (!parsed.empty()) {
        db_set_str(api, ctx, "sc_cached_cid", parsed);
        return parsed;
    }

    // Запасной ключ
    return "T574VRKI5KSLMzLdUreWOpXss1R3Fzfx";
}

// ─── SoundCloud Search ───────────────────────────────────────────────────────
static std::vector<TrackInfo> soundcloud_search(const koto_api* api, koto_ctx* ctx, const std::string& query, int limit = 15) {
    std::vector<TrackInfo> results;
    std::string cid = sc_get_cid(api, ctx);

    std::vector<std::string> headers = {
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        "Referer: https://soundcloud.com/"
    };

    std::string url = "https://api-v2.soundcloud.com/search/tracks?q=" + url_encode(query) +
                      "&client_id=" + cid + "&limit=" + std::to_string(limit) + "&offset=0";

    std::string body = http_get(api, ctx, url, headers, 10);
    if (body.empty() || body.find("401") != std::string::npos) {
        // Пробуем обновить client_id если старый протух
        std::string fresh = sc_parse_client_id(api, ctx);
        if (!fresh.empty() && fresh != cid) {
            db_set_str(api, ctx, "sc_cached_cid", fresh);
            cid = fresh;
            url = "https://api-v2.soundcloud.com/search/tracks?q=" + url_encode(query) +
                  "&client_id=" + cid + "&limit=" + std::to_string(limit) + "&offset=0";
            body = http_get(api, ctx, url, headers, 10);
        }
    }

    try {
        json j = json::parse(body);
        if (j.contains("collection") && j["collection"].is_array()) {
            for (const auto& item : j["collection"]) {
                TrackInfo t;
                t.id = std::to_string(item.value("id", 0LL));
                t.title = item.value("title", "Unknown Title");

                if (item.contains("user") && item["user"].is_object()) {
                    t.artist = item["user"].value("username", "Unknown Artist");
                } else {
                    t.artist = "SoundCloud";
                }

                long long dur_ms = item.value("duration", 0LL);
                t.duration_sec = dur_ms / 1000;
                t.likes = item.value("likes_count", 0LL);
                t.artwork_url = item.value("artwork_url", "");
                t.service = "sc";

                // Сохраняем URL потока для последующего разрешения при скачивании
                if (item.contains("media") && item["media"].contains("transcodings") && item["media"]["transcodings"].is_array()) {
                    std::string prog_url;
                    std::string hls_url;
                    bool has_encrypted = false;
                    for (const auto& tr : item["media"]["transcodings"]) {
                        std::string format = tr.value("format", json::object()).value("protocol", "");
                        std::string tr_url = tr.value("url", "");
                        if (format.find("encrypted") != std::string::npos) {
                            has_encrypted = true;
                        }
                        if (format == "progressive" && prog_url.empty()) {
                            prog_url = tr_url;
                        } else if (format == "hls" && hls_url.empty()) {
                            hls_url = tr_url;
                        }
                    }
                    t.has_drm = has_encrypted;
                    // Приоритет progressive (прямой mp3), если нет - hls
                    t.transcode_url = !prog_url.empty() ? prog_url : hls_url;
                }

                results.push_back(t);
            }
        }
    } catch (...) {}

    return results;
}

// ─── Разрешение прямой ссылки на аудиопоток (с повторной попыткой при 401) ────
static std::string resolve_stream_url(const koto_api* api, koto_ctx* ctx, const TrackInfo& track, std::string* err_out = nullptr) {
    if (!track.stream_url.empty()) {
        KOTO_LOGI("music_dl", "Используем готовый stream_url для трека %s", track.id.c_str());
        return track.stream_url;
    }
    if (track.transcode_url.empty()) {
        std::string err = "У трека " + track.id + " ('" + track.title + "') отсутствует transcode_url";
        KOTO_LOGE("music_dl", "%s", err.c_str());
        if (err_out) *err_out = err;
        return "";
    }

    std::vector<std::string> headers = {
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        "Referer: https://soundcloud.com/"
    };

    std::string cid = sc_get_cid(api, ctx);
    std::string req = track.transcode_url + "?client_id=" + cid;
    KOTO_LOGI("music_dl", "Разрешение потока: %s", req.c_str());
    std::string res = http_get(api, ctx, req, headers, 10);
    KOTO_LOGI("music_dl", "Ответ транскодинга: %s", res.substr(0, 150).c_str());

    try {
        json sj = json::parse(res);
        std::string u = sj.value("url", "");
        if (!u.empty()) {
            KOTO_LOGI("music_dl", "Поток успешно разрешен: %s", u.substr(0, 80).c_str());
            return u;
        }
    } catch (...) {}

    // Если трек защищен DRM (или вернул пустой JSON {}), это ограничение правообладателя
    if (track.has_drm || res == "{}") {
        std::string err = "Трек защищён DRM-шифрованием автором/лейблом и недоступен для скачивания (SoundCloud Go+ DRM)";
        KOTO_LOGE("music_dl", "%s", err.c_str());
        if (err_out) *err_out = err;
        return "";
    }

    // Если 401 или ссылка пустая — сбрасываем кеш и парсим свежий CID
    KOTO_LOGW("music_dl", "Поток не разрешен с текущим CID, обновляем SoundCloud Client ID...");
    db_set_str(api, ctx, "sc_cached_cid", "");
    cid = sc_parse_client_id(api, ctx);
    if (!cid.empty()) {
        db_set_str(api, ctx, "sc_cached_cid", cid);
        req = track.transcode_url + "?client_id=" + cid;
        KOTO_LOGI("music_dl", "Повторная попытка с новым CID: %s", cid.c_str());
        res = http_get(api, ctx, req, headers, 10);
        KOTO_LOGI("music_dl", "Ответ повторного транскодинга: %s", res.substr(0, 150).c_str());
        try {
            json sj = json::parse(res);
            std::string u = sj.value("url", "");
            if (!u.empty()) {
                KOTO_LOGI("music_dl", "Поток успешно разрешен со 2-й попытки: %s", u.substr(0, 80).c_str());
                return u;
            }
        } catch (...) {}
    }

    std::string err = (res == "{}" || track.has_drm)
        ? "Трек защищён DRM-шифрованием автором/лейблом и недоступен для скачивания (SoundCloud Go+ DRM)"
        : "SoundCloud API не вернул URL аудиопотока. Ответ: " + (res.empty() ? "(пусто)" : res.substr(0, 100));
    KOTO_LOGE("music_dl", "%s", err.c_str());
    if (err_out) *err_out = err;
    return "";
}

// ─── Сериализация и хранение сессии поиска ──────────────────────────────────
static void save_search_session(const koto_api* api, koto_ctx* ctx, long long chat_id, const std::string& query,
                                int page, int per_page, const std::vector<TrackInfo>& tracks) {
    SearchSession s;
    s.chat_id = chat_id;
    s.query = query;
    s.page = page;
    s.per_page = per_page;
    s.tracks = tracks;

    // 1. Сохранение в память
    if (chat_id != 0) {
        g_sessions[chat_id] = s;
        db_set_int64(api, ctx, "last_search_chat_id", chat_id);
    }
    g_last_session = s;

    // 2. Компактное сохранение в SQLite
    json j;
    j["cid"] = chat_id;
    j["q"] = query;
    j["p"] = page;
    j["pp"] = per_page;
    j["t"] = json::array();

    for (const auto& t : tracks) {
        json tj;
        tj["i"] = t.id;
        tj["t"] = t.title;
        tj["a"] = t.artist;
        tj["d"] = t.duration_sec;
        tj["u"] = t.transcode_url;
        tj["s"] = t.stream_url;
        tj["drm"] = t.has_drm;
        j["t"].push_back(tj);
    }

    std::string dumped = j.dump();
    if (chat_id != 0) {
        db_set_str(api, ctx, "s_sess_" + std::to_string(chat_id), dumped);
    }
    db_set_str(api, ctx, "s_sess_last", dumped);
}

static bool load_search_session(const koto_api* api, koto_ctx* ctx, long long& chat_id, std::string& query,
                                int& page, int& per_page, std::vector<TrackInfo>& tracks) {
    // 1. Проверка в памяти по chat_id
    if (chat_id != 0) {
        auto it = g_sessions.find(chat_id);
        if (it != g_sessions.end() && !it->second.tracks.empty()) {
            query = it->second.query;
            page = it->second.page;
            per_page = it->second.per_page;
            tracks = it->second.tracks;
            return true;
        }
    }

    // 2. Фолбэк на последнюю сессию в памяти
    if (!g_last_session.tracks.empty()) {
        if (chat_id == 0 && g_last_session.chat_id != 0) {
            chat_id = g_last_session.chat_id;
        }
        query = g_last_session.query;
        page = g_last_session.page;
        per_page = g_last_session.per_page;
        tracks = g_last_session.tracks;
        return true;
    }

    // 3. Загрузка из базы данных SQLite
    std::string data;
    if (chat_id != 0) {
        data = db_get_str(api, ctx, "s_sess_" + std::to_string(chat_id), "");
    }
    if (data.empty()) {
        data = db_get_str(api, ctx, "s_sess_last", "");
    }
    if (data.empty()) return false;

    try {
        json j = json::parse(data);
        long long saved_cid = j.value("cid", 0LL);
        if (chat_id == 0 && saved_cid != 0) {
            chat_id = saved_cid;
        }
        query = j.value("q", j.value("query", ""));
        page = j.value("p", j.value("page", 1));
        per_page = j.value("pp", j.value("per_page", 5));
        tracks.clear();

        auto arr_key = j.contains("t") ? "t" : "tracks";
        if (j.contains(arr_key) && j[arr_key].is_array()) {
            for (const auto& tj : j[arr_key]) {
                TrackInfo t;
                t.id = tj.value("i", tj.value("id", ""));
                t.title = tj.value("t", tj.value("title", ""));
                t.artist = tj.value("a", tj.value("artist", ""));
                t.duration_sec = tj.value("d", tj.value("duration_sec", 0LL));
                t.transcode_url = tj.value("u", tj.value("transcode_url", ""));
                t.stream_url = tj.value("s", tj.value("stream_url", ""));
                t.has_drm = tj.value("drm", false);
                t.service = "sc";
                tracks.push_back(t);
            }
        }
        if (!tracks.empty()) {
            SearchSession s;
            s.chat_id = chat_id;
            s.query = query; s.page = page; s.per_page = per_page; s.tracks = tracks;
            if (chat_id != 0) g_sessions[chat_id] = s;
            g_last_session = s;
            return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}

// ─── Отрисовка страницы результатов ─────────────────────────────────────────
static void render_page(koto_ctx* ctx, const koto_api* api, const std::string& query,
                        int page, int per_page, const std::vector<TrackInfo>& tracks,
                        long long chat_id = 0) {
    int total_tracks = static_cast<int>(tracks.size());
    int total_pages = (total_tracks + per_page - 1) / per_page;
    if (total_pages < 1) total_pages = 1;
    if (page < 1) page = 1;
    if (page > total_pages) page = total_pages;

    int start_idx = (page - 1) * per_page;
    int end_idx = std::min(start_idx + per_page, total_tracks);

    api->c_reset(ctx);
    api->c_emoji(ctx, "🔍", 5188621441926438751LL);
    api->c_fmt(ctx, " Результаты поиска: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, query.c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n\n");

    for (int i = start_idx; i < end_idx; ++i) {
        const auto& t = tracks[i];
        std::string num = std::to_string(i + 1) + ". ";
        api->c_fmt(ctx, num.c_str(), KOTO_ENT_BOLD);

        if (t.service == "sc") {
            api->c_text(ctx, "☁️ ");
        } else if (t.service == "sp") {
            api->c_text(ctx, "🟢 ");
        } else {
            api->c_text(ctx, "🎵 ");
        }

        api->c_fmt(ctx, t.title.c_str(), KOTO_ENT_BOLD);
        api->c_text(ctx, " — ");
        api->c_fmt(ctx, t.artist.c_str(), KOTO_ENT_ITALIC);

        std::string dur = " [" + format_duration(t.duration_sec) + "]";
        api->c_fmt(ctx, dur.c_str(), KOTO_ENT_CODE);

        if (t.likes > 0) {
            std::string likes_str = " (" + std::to_string(t.likes) + " ❤️)";
            api->c_text(ctx, likes_str.c_str());
        }
        if (t.has_drm) {
            api->c_text(ctx, " 🔒");
        }
        api->c_text(ctx, "\n");
    }

    std::string footer = "\n📄 Страница " + std::to_string(page) + "/" + std::to_string(total_pages) +
                         " • Всего треков: " + std::to_string(total_tracks);
    api->c_fmt(ctx, footer.c_str(), KOTO_ENT_ITALIC);

    // Подсказка для мгновенного скачивания
    api->c_text(ctx, "\n\n💡 Чтобы скачать трек, отправьте: ");
    api->c_fmt(ctx, ("dlf " + std::to_string(start_idx + 1)).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " (🔒 = DRM защита)");

    // ── Кнопки скачивания и навигации ──
    long long btn_cid = chat_id;
    if (btn_cid == 0 && api && api->get_me) btn_cid = api->get_me(ctx);
    std::string cid_s = ":" + std::to_string(btn_cid);

    // Ряд кнопок скачивания треков (по одной или парами в ряду)
    int row_counter = 1;
    for (int i = start_idx; i < end_idx; ++i) {
        int track_num = i + 1;
        std::string btn_title = "🎧 " + std::to_string(track_num);
        if (tracks[i].has_drm) btn_title += " 🔒";
        std::string cb_dl = "mdl:dl:" + std::to_string(track_num) + cid_s;
        
        // 0 = в тот же ряд, 1 = новый ряд. Делаем по 5 кнопок в ряд (1, 2, 3, 4, 5)
        int new_row = (i == start_idx) ? 1 : 0;
        if (api->c_button_ex) {
            api->c_button_ex(ctx, btn_title.c_str(), cb_dl.c_str(), new_row, KOTO_BTN_PRIMARY, 0);
        } else {
            api->c_button(ctx, btn_title.c_str(), cb_dl.c_str(), new_row);
        }
    }

    // Навигация: [◀️ Назад] [❌ Закрыть] [Вперёд ▶️]
    bool has_prev = (page > 1);
    bool has_next = (page < total_pages);

    if (has_prev) {
        std::string cb_prev = "mdl:pg:prev" + cid_s;
        if (api->c_button_ex) {
            api->c_button_ex(ctx, "◀️ Назад", cb_prev.c_str(), 1, KOTO_BTN_DEFAULT, 0);
        } else {
            api->c_button(ctx, "◀️ Назад", cb_prev.c_str(), 1);
        }
    }

    std::string cb_close = "mdl:close" + cid_s;
    int close_row = has_prev ? 0 : 1;
    if (api->c_button_ex) {
        api->c_button_ex(ctx, "❌ Закрыть", cb_close.c_str(), close_row, KOTO_BTN_DANGER, 0);
    } else {
        api->c_button(ctx, "❌ Закрыть", cb_close.c_str(), close_row);
    }

    if (has_next) {
        std::string cb_next = "mdl:pg:next" + cid_s;
        if (api->c_button_ex) {
            api->c_button_ex(ctx, "Вперёд ▶️", cb_next.c_str(), 0, KOTO_BTN_DEFAULT, 0);
        } else {
            api->c_button(ctx, "Вперёд ▶️", cb_next.c_str(), 0);
        }
    }
}

// ─── Команда .find ──────────────────────────────────────────────────────────
static void cmd_find(koto_ctx* ctx, const koto_api* api) {
    const char* args = api->cmd_args(ctx);
    if (!args || !*args) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "⚠️ Использование: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".find <название трека или исполнитель>", KOTO_ENT_CODE);
        api->c_edit(ctx, KOTO_FMT_PLAIN);
        return;
    }

    std::string query = trim(args);
    api->c_reset(ctx);
    api->c_fmt(ctx, "🔎 Ищу в SoundCloud: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, query.c_str(), KOTO_ENT_CODE);
    api->c_fmt(ctx, "...", KOTO_ENT_ITALIC);
    api->c_edit(ctx, KOTO_FMT_PLAIN);

    auto tracks = soundcloud_search(api, ctx, query, 15);
    if (tracks.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ По запросу ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, query.c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, " ничего не найдено.", KOTO_ENT_BOLD);
        api->c_edit(ctx, KOTO_FMT_PLAIN);
        return;
    }

    long long chat_id = api->chat_id(ctx);
    if (chat_id == 0 && api->get_me) chat_id = api->get_me(ctx);
    int per_page = static_cast<int>(mod_get_setting_int(api, ctx, "per_page", 5));
    if (per_page < 1 || per_page > 10) per_page = 5;

    save_search_session(api, ctx, chat_id, query, 1, per_page, tracks);
    render_page(ctx, api, query, 1, per_page, tracks, chat_id);
    api->c_edit(ctx, KOTO_FMT_PLAIN);
}

// ─── Команда .fc (пагинация) ────────────────────────────────────────────────
static void cmd_fc(koto_ctx* ctx, const koto_api* api) {
    const char* args = api->cmd_args(ctx);
    std::string action = args ? trim(args) : "next";

    long long chat_id = api->chat_id(ctx);
    if (chat_id == 0 && api->get_me) chat_id = api->get_me(ctx);
    std::string query;
    int page = 1;
    int per_page = 5;
    std::vector<TrackInfo> tracks;

    if (!load_search_session(api, ctx, chat_id, query, page, per_page, tracks)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "⚠️ Нет активного сеанса поиска. Сначала выполните: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".find <запрос>", KOTO_ENT_CODE);
        api->c_edit(ctx, KOTO_FMT_PLAIN);
        return;
    }

    int total_pages = (static_cast<int>(tracks.size()) + per_page - 1) / per_page;

    if (action == "next") {
        if (page < total_pages) page++;
        else {
            api->c_reset(ctx);
            api->c_fmt(ctx, "ℹ️ Вы уже на последней странице.", KOTO_ENT_ITALIC);
            api->c_edit(ctx, KOTO_FMT_PLAIN);
            return;
        }
    } else if (action == "prev" || action == "previous") {
        if (page > 1) page--;
        else {
            api->c_reset(ctx);
            api->c_fmt(ctx, "ℹ️ Вы уже на первой странице.", KOTO_ENT_ITALIC);
            api->c_edit(ctx, KOTO_FMT_PLAIN);
            return;
        }
    }

    save_search_session(api, ctx, chat_id, query, page, per_page, tracks);
    render_page(ctx, api, query, page, per_page, tracks, chat_id);
    api->c_edit(ctx, KOTO_FMT_PLAIN);
}

static long get_file_size(const std::string& path) {
    if (path.empty()) return -1;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    return sz;
}

// ─── Очистка имени файла от запрещенных символов ─────────────────────────────
static std::string sanitize_filename(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || (unsigned char)c < 32) {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    std::string collapsed;
    collapsed.reserve(out.size());
    bool in_space = false;
    for (char c : out) {
        if (c == ' ') {
            if (!in_space && !collapsed.empty()) {
                collapsed.push_back(' ');
            }
            in_space = true;
        } else {
            collapsed.push_back(c);
            in_space = false;
        }
    }
    while (!collapsed.empty() && collapsed.back() == ' ') {
        collapsed.pop_back();
    }
    if (collapsed.size() > 150) {
        size_t len = 150;
        while (len > 0 && ((unsigned char)collapsed[len] & 0xC0) == 0x80) {
            len--;
        }
        collapsed = collapsed.substr(0, len);
    }
    if (collapsed.empty()) {
        collapsed = "track";
    }
    return collapsed;
}

// ─── Получение юзернейма владельца для метаданных ────────────────────────────
static std::string get_owner_tag(const koto_api* api, koto_ctx* ctx) {
    std::string custom = mod_get_setting(api, ctx, "owner_username", "");
    if (custom.empty()) {
        custom = mod_get_setting(api, ctx, "owner_tag", "");
    }
    if (custom.empty()) {
        custom = db_get_str(api, ctx, "owner_tag", "");
    }
    if (!custom.empty()) {
        if (custom[0] != '@') custom = "@" + custom;
        return custom;
    }

    return "@Diezdd";
}

// ─── Команда .musictag: просмотр или смена юзернейма владельца ───────────────
static void cmd_musictag(koto_ctx* ctx, const koto_api* api) {
    const char* args = api->cmd_args(ctx);
    if (!args || !*args) {
        std::string current = get_owner_tag(api, ctx);
        api->c_reset(ctx);
        api->c_fmt(ctx, "🏷 Текущий тег исполнителя для аудио: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, current.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n💡 Чтобы изменить, укажите: ");
        api->c_fmt(ctx, ".musictag @username", KOTO_ENT_CODE);
        api->c_edit(ctx, KOTO_FMT_PLAIN);
        return;
    }

    std::string new_tag = trim(args);
    if (new_tag[0] != '@') new_tag = "@" + new_tag;
    db_set_str(api, ctx, "owner_tag", new_tag);

    api->c_reset(ctx);
    api->c_fmt(ctx, "✅ Тег исполнителя успешно обновлен на: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, new_tag.c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\nТеперь все скачиваемые треки будут подписываться этим юзернеймом в плеере.");
    api->c_edit(ctx, KOTO_FMT_PLAIN);
}

// ─── Внедрение ID3v2.3 метаданных (Title, Artist, TLEN = длительность) ─────────────
static bool inject_id3_tags(const std::string& path, const std::string& title, const std::string& artist, long long duration_sec = 0) {
    FILE* in = fopen(path.c_str(), "rb");
    if (!in) return false;

    unsigned char hdr[10] = {0};
    size_t rd = fread(hdr, 1, 10, in);
    size_t audio_offset = 0;

    if (rd == 10 && hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3') {
        uint32_t tag_sz = ((hdr[6] & 0x7F) << 21) |
                          ((hdr[7] & 0x7F) << 14) |
                          ((hdr[8] & 0x7F) << 7)  |
                          (hdr[9] & 0x7F);
        audio_offset = 10 + tag_sz;
        if (hdr[5] & 0x10) audio_offset += 10; // ID3v2 footer
    }

    std::vector<uint8_t> frames;
    // В ID3v2.3 размер фрейма — стандартный 4-байтный big-endian целое число
    auto append_text_frame_v23 = [&](const char id[4], const std::string& text) {
        if (text.empty()) return;
        for (int i = 0; i < 4; ++i) frames.push_back((uint8_t)id[i]);
        uint32_t fsize = (uint32_t)text.size() + 1; // 1 байт маркера кодировки (0x03 = UTF-8)
        frames.push_back((fsize >> 24) & 0xFF);
        frames.push_back((fsize >> 16) & 0xFF);
        frames.push_back((fsize >> 8) & 0xFF);
        frames.push_back(fsize & 0xFF);
        frames.push_back(0); // flags
        frames.push_back(0);
        frames.push_back(3); // 3 = UTF-8 кодировка
        frames.insert(frames.end(), text.begin(), text.end());
    };

    append_text_frame_v23("TIT2", title);
    append_text_frame_v23("TPE1", artist);
    if (duration_sec > 0) {
        // TLEN: Длительность трека в миллисекундах (критично для Telegram плеера)
        std::string tlen_str = std::to_string(duration_sec * 1000);
        append_text_frame_v23("TLEN", tlen_str);
    }

    std::string tmp_path = path + ".tmp";
    FILE* out = fopen(tmp_path.c_str(), "wb");
    if (!out) {
        fclose(in);
        return false;
    }

    // Заголовок ID3v2.3 (ID3 0x03 0x00) с синхобезопасным размером общего тега
    unsigned char id3_hdr[10] = {'I', 'D', '3', 3, 0, 0, 0, 0, 0, 0};
    uint32_t tot_sz = (uint32_t)frames.size();
    id3_hdr[6] = (tot_sz >> 21) & 0x7F;
    id3_hdr[7] = (tot_sz >> 14) & 0x7F;
    id3_hdr[8] = (tot_sz >> 7) & 0x7F;
    id3_hdr[9] = tot_sz & 0x7F;

    fwrite(id3_hdr, 1, 10, out);
    if (!frames.empty()) {
        fwrite(frames.data(), 1, frames.size(), out);
    }

    fseek(in, (long)audio_offset, SEEK_SET);
    unsigned char chunk[65536];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), in)) > 0) {
        fwrite(chunk, 1, n, out);
    }

    fclose(in);
    fclose(out);

    if (rename(tmp_path.c_str(), path.c_str()) != 0) {
        unlink(tmp_path.c_str());
        return false;
    }
    chmod(path.c_str(), 0666);
    return true;
}

// Общая надежная функция скачивания и отправки аудиофайла (и для .dlf, и для инлайн-кнопок)
static bool download_and_send_track(koto_ctx* ctx, const koto_api* api, const TrackInfo& track, long long chat_id, int track_num = 0, std::string* err_out = nullptr) {
    if (chat_id == 0 && g_last_session.chat_id != 0) {
        chat_id = g_last_session.chat_id;
    }
    if (chat_id == 0) {
        chat_id = db_get_int64(api, ctx, "last_search_chat_id", 0);
    }
    if (chat_id == 0 && api && api->get_me) {
        chat_id = api->get_me(ctx);
    }

    std::string resolve_err;
    std::string stream_url = resolve_stream_url(api, ctx, track, &resolve_err);
    if (stream_url.empty()) {
        KOTO_LOGE("music_dl", "download_and_send: не удалось получить аудиопоток: %s", resolve_err.c_str());
        if (err_out) *err_out = resolve_err;
        return false;
    }

    char cache[512] = {0};
    api->cache_dir(ctx, cache, sizeof(cache));

    // Название файла: <название песни> — <исполнитель>.mp3
    std::string safe_name = sanitize_filename(track.title + " — " + track.artist);
    char track_path[768] = {0};
    snprintf(track_path, sizeof(track_path), "%s/%s.mp3", cache, safe_name.c_str());

    std::string owner_tag = get_owner_tag(api, ctx);

    long sz = get_file_size(track_path);
    bool downloaded = false;
    if (sz > 50000) {
        downloaded = true;
        KOTO_LOGI("music_dl", "Трек уже в кэше (%ld байт): %s", sz, track_path);
    } else {
        std::vector<std::string> dl_headers = {
            "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
            "Referer: https://soundcloud.com/"
        };
        downloaded = download_file(api, ctx, stream_url, track_path, dl_headers, 60);
    }

    if (get_file_size(track_path) > 50000) {
        // Прописываем или обновляем ID3v2.3 теги с явной длительностью TLEN при каждой отправке
        std::string custom_tag = db_get_str(api, ctx, "owner_tag", "");
        std::string tag_artist = !custom_tag.empty() ? custom_tag : (!track.artist.empty() ? track.artist : owner_tag);
        if (inject_id3_tags(track_path, track.title, tag_artist, track.duration_sec)) {
            KOTO_LOGI("music_dl", "Прописаны ID3-теги: Title='%s', Artist='%s', Duration=%llds", track.title.c_str(), tag_artist.c_str(), track.duration_sec);
        }
        downloaded = true;
    }

    const char* send_target = (downloaded && get_file_size(track_path) > 0) ? track_path : stream_url.c_str();
    KOTO_LOGI("music_dl", "Отправка трека '%s' (цель: %s) в чат %lld", track.title.c_str(), send_target, chat_id);

    // Составляем подпись к файлу
    api->c_reset(ctx);
    api->c_text(ctx, "🎵 ");
    api->c_fmt(ctx, track.title.c_str(), KOTO_ENT_BOLD);
    api->c_text(ctx, " — ");
    api->c_fmt(ctx, track.artist.c_str(), KOTO_ENT_ITALIC);
    api->c_text(ctx, "\n⏱ Длительность: ");
    api->c_fmt(ctx, format_duration(track.duration_sec).c_str(), KOTO_ENT_CODE);
    std::string source_tag = "\n🎧 Источник: SoundCloud | " + owner_tag;
    api->c_text(ctx, source_tag.c_str());

    // Отправляем файл
    api->c_send_file(ctx, chat_id, send_target, 0);
    return true;
}

// ─── Логика скачивания трека (do_dlf) ───────────────────────────────────────
static void do_dlf(koto_ctx* ctx, const koto_api* api, const char* args_raw) {
    if (!args_raw || !*args_raw) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "⚠️ Использование: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".dlf <номер>", KOTO_ENT_CODE);
        api->c_edit(ctx, KOTO_FMT_PLAIN);
        return;
    }

    int idx = 0;
    try {
        idx = std::stoi(trim(args_raw)) - 1;
    } catch (...) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "⚠️ Укажите корректный номер трека из списка.", KOTO_ENT_BOLD);
        api->c_edit(ctx, KOTO_FMT_PLAIN);
        return;
    }

    long long chat_id = api->chat_id(ctx);
    if (chat_id == 0 && g_last_session.chat_id != 0) {
        chat_id = g_last_session.chat_id;
    }
    if (chat_id == 0) {
        chat_id = db_get_int64(api, ctx, "last_search_chat_id", 0);
    }
    if (chat_id == 0 && api && api->get_me) {
        chat_id = api->get_me(ctx);
    }

    std::string query;
    int page = 1;
    int per_page = 5;
    std::vector<TrackInfo> tracks;

    if (!load_search_session(api, ctx, chat_id, query, page, per_page, tracks)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "⚠️ Нет активного сеанса поиска. Сделайте: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".find <запрос>", KOTO_ENT_CODE);
        api->c_edit(ctx, KOTO_FMT_PLAIN);
        return;
    }

    if (idx < 0 || idx >= static_cast<int>(tracks.size())) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "⚠️ Номер трека вне диапазона (1..", KOTO_ENT_BOLD);
        api->c_text(ctx, std::to_string(tracks.size()).c_str());
        api->c_fmt(ctx, ").", KOTO_ENT_BOLD);
        api->c_edit(ctx, KOTO_FMT_PLAIN);
        return;
    }

    const auto& track = tracks[idx];
    KOTO_LOGI("music_dl", "Запрос на скачивание трека #%d: '%s' (ID: %s, chat_id: %lld)", idx + 1, track.title.c_str(), track.id.c_str(), chat_id);

    // Показываем статус отправки
    api->c_reset(ctx);
    api->c_markdown(ctx, "⬇️ **Скачивание и отправка аудиофайла...**\n> ");
    api->c_fmt(ctx, track.title.c_str(), KOTO_ENT_BOLD);
    api->c_text(ctx, " — ");
    api->c_fmt(ctx, track.artist.c_str(), KOTO_ENT_ITALIC);
    api->c_edit(ctx, 0);

    std::string dl_err;
    bool ok = download_and_send_track(ctx, api, track, chat_id, idx + 1, &dl_err);
    if (!ok) {
        api->c_reset(ctx);
        if (track.has_drm || dl_err.find("DRM") != std::string::npos) {
            api->c_fmt(ctx, "🔒 Этот трек защищён DRM-шифрованием правообладателя.\n\n", KOTO_ENT_BOLD);
            api->c_text(ctx, "Прямой поток SoundCloud недоступен без платной подписки Go+.\n💡 Выберите альтернативную версию через ");
            api->c_fmt(ctx, ".fc", KOTO_ENT_CODE);
            api->c_text(ctx, " или скачайте другой номер (например: ");
            std::string alt_hint = "dlf " + std::to_string((idx == 0 && tracks.size() > 1) ? 2 : 1);
            api->c_fmt(ctx, alt_hint.c_str(), KOTO_ENT_CODE);
            api->c_text(ctx, ").");
        } else {
            api->c_fmt(ctx, "❌ Ошибка получения аудиопотока.", KOTO_ENT_BOLD);
            api->c_fmt(ctx, "\n\n💡 Введите .musiclog для просмотра логов разработки.", KOTO_ENT_ITALIC);
        }
        api->c_edit(ctx, 0);
    }
}

static void cmd_dlf(koto_ctx* ctx, const koto_api* api) {
    const char* args = api->cmd_args(ctx);
    do_dlf(ctx, api, args ? args : "");
}

// ─── Watcher: перехват "dlf 1", "dlf 2" без точки ───────────────────────────
static int music_dl_watch(koto_ctx* ctx, const koto_api* api) {
    const char* text = api->msg_text(ctx);
    if (!text || !*text) return 0;
    while (*text == ' ' || *text == '\t') text++;

    if (strncmp(text, "dlf", 3) == 0 && (text[3] == ' ' || text[3] == '\0' || isdigit(text[3]))) {
        const char* args = text + 3;
        while (*args == ' ' || *args == '\t') args++;
        do_dlf(ctx, api, args);
        return 1;
    }
    return 0;
}

// ─── Команда .download (быстрый поиск + скачивание) ─────────────────────────
static void cmd_download(koto_ctx* ctx, const koto_api* api) {
    const char* args = api->cmd_args(ctx);
    if (!args || !*args) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "⚠️ Использование: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".download <название трека>", KOTO_ENT_CODE);
        api->c_edit(ctx, KOTO_FMT_PLAIN);
        return;
    }

    std::string query = trim(args);
    api->c_reset(ctx);
    api->c_fmt(ctx, "⚡️ Быстрый поиск и скачивание: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, query.c_str(), KOTO_ENT_CODE);
    api->c_edit(ctx, KOTO_FMT_PLAIN);

    auto tracks = soundcloud_search(api, ctx, query, 5);
    if (tracks.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Трек не найден в сети.", KOTO_ENT_BOLD);
        api->c_edit(ctx, KOTO_FMT_PLAIN);
        return;
    }

    // Ищем первый доступный трек без DRM
    size_t chosen_idx = 0;
    bool found_valid = false;
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (!tracks[i].has_drm) {
            chosen_idx = i;
            found_valid = true;
            break;
        }
    }

    if (!found_valid && tracks[0].has_drm) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "🔒 Этот трек защищён DRM-шифрованием правообладателя.\n\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "Прямой поток SoundCloud заблокирован без платной подписки Go+.\n💡 Воспользуйтесь поиском ");
        api->c_fmt(ctx, (".find " + query).c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, ", чтобы найти кавер или ремикс.");
        api->c_edit(ctx, KOTO_FMT_PLAIN);
        return;
    }

    const auto& track = tracks[chosen_idx];
    KOTO_LOGI("music_dl", "Быстрое скачивание трека #%zu: '%s' (ID: %s)", chosen_idx + 1, track.title.c_str(), track.id.c_str());

    // Показываем статус отправки
    api->c_reset(ctx);
    api->c_markdown(ctx, "⬇️ **Скачивание и отправка аудиофайла...**\n> ");
    api->c_fmt(ctx, track.title.c_str(), KOTO_ENT_BOLD);
    api->c_text(ctx, " — ");
    api->c_fmt(ctx, track.artist.c_str(), KOTO_ENT_ITALIC);
    api->c_edit(ctx, 0);

    long long chat_id = api->chat_id(ctx);
    std::string dl_err;
    bool ok = download_and_send_track(ctx, api, track, chat_id, chosen_idx + 1, &dl_err);
    if (!ok) {
        api->c_reset(ctx);
        if (track.has_drm || dl_err.find("DRM") != std::string::npos) {
            api->c_fmt(ctx, "🔒 Этот трек защищён DRM-шифрованием правообладателя.\n\n", KOTO_ENT_BOLD);
            api->c_text(ctx, "Прямой поток SoundCloud заблокирован без платной подписки Go+.\n💡 Воспользуйтесь поиском ");
            api->c_fmt(ctx, (".find " + query).c_str(), KOTO_ENT_CODE);
            api->c_text(ctx, ", чтобы найти кавер или ремикс.");
        } else {
            api->c_fmt(ctx, "❌ Не удалось скачать или отправить трек.", KOTO_ENT_BOLD);
            api->c_fmt(ctx, "\n\n💡 Введите .musiclog для просмотра логов разработки.", KOTO_ENT_ITALIC);
        }
        api->c_edit(ctx, 0);
    }
}

// ─── Команда .mfile (тест отправки локальных файлов из кэша приложения) ──────
static void cmd_mfile(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);
    const char* args = api->cmd_args(ctx);
    while (args && (*args == ' ' || *args == '\t')) args++;

    char cache[512] = {0};
    api->cache_dir(ctx, cache, sizeof(cache));

    char path_buf[600] = {0};
    const char* target = path_buf;
    const char* label = "🎵 Аудиофайл";

    if (args && (strstr(args, "jpg") || strstr(args, "photo") || strstr(args, "pic") || strstr(args, "image"))) {
        snprintf(path_buf, sizeof(path_buf), "%s/media_15051_19_jpg.jpg", cache);
        label = "🖼 Фотография (media_15051_19_jpg.jpg)";
    } else if (args && *args && strchr(args, '/')) {
        target = args;
        label = "📦 Кастомный файл";
    } else {
        snprintf(path_buf, sizeof(path_buf), "%s/media_15051_3368_mp3.mp3", cache);
        label = "🎵 Аудиофайл (media_15051_3368_mp3.mp3)";
    }

    FILE* test_f = fopen(target, "rb");
    if (!test_f) {
        // Создаём реальный тестовый файл, чтобы c_send_file не падал на несуществующем файле
        test_f = fopen(target, "wb");
        if (test_f) {
            if (strstr(target, ".jpg")) {
                static const unsigned char dummy_jpg[] = {
                    0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00, 0x01, 0x01, 0x01, 0x00, 0x48,
                    0x00, 0x48, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x01,
                    0x00, 0x01, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xC4, 0x00, 0x1F, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01,
                    0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04,
                    0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3F,
                    0x00, 0xBF, 0x00, 0xFF, 0xD9
                };
                fwrite(dummy_jpg, 1, sizeof(dummy_jpg), test_f);
            } else {
                fputs("Kotogram Audio Test Track Data\n", test_f);
            }
            fclose(test_f);
            chmod(target, 0666);
            test_f = fopen(target, "rb");
        }
    }

    long fsize = -1;
    if (test_f) {
        fseek(test_f, 0, SEEK_END);
        fsize = ftell(test_f);
        fclose(test_f);
    }

    api->c_reset(ctx);
    if (fsize >= 0) {
        char status_msg[512];
        snprintf(status_msg, sizeof(status_msg), "⏳ **Отправка файла из кэша (%ld байт)...**\n> `%s`", fsize, target);
        api->c_markdown(ctx, status_msg);
    } else {
        char status_msg[512];
        snprintf(status_msg, sizeof(status_msg), "❌ **Не удалось подготовить файл на диске:** `%s`", target);
        api->c_markdown(ctx, status_msg);
        api->c_edit(ctx, 0);
        return;
    }
    api->c_edit(ctx, 0);

    api->c_reset(ctx);
    api->c_markdown(ctx, "✅ **Тестовый файл успешно отправлен:**\n> ");
    api->c_text(ctx, label);

    api->c_send_file(ctx, cid, target, 0);
}

// ─── Команда .like / .favs ──────────────────────────────────────────────────
static void cmd_like(koto_ctx* ctx, const koto_api* api) {
    const char* args = api->cmd_args(ctx);
    if (!args || !*args) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "⚠️ Использование: ", KOTO_ENT_BOLD);
        api->c_fmt(ctx, ".like <номер из .find>", KOTO_ENT_CODE);
        api->c_edit(ctx, KOTO_FMT_PLAIN);
        return;
    }

    int idx = 0;
    try { idx = std::stoi(trim(args)) - 1; } catch (...) { return; }

    long long chat_id = api->chat_id(ctx);
    std::string query;
    int page = 1, per_page = 5;
    std::vector<TrackInfo> tracks;

    if (!load_search_session(api, ctx, chat_id, query, page, per_page, tracks) ||
        idx < 0 || idx >= static_cast<int>(tracks.size())) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "⚠️ Трек не найден в результатах поиска.", KOTO_ENT_BOLD);
        api->c_edit(ctx, KOTO_FMT_PLAIN);
        return;
    }

    const auto& t = tracks[idx];
    std::string favs_str = db_get_str(api, ctx, "liked_tracks", "[]");
    json fj = json::parse(favs_str, nullptr, false);
    if (!fj.is_array()) fj = json::array();

    json item;
    item["id"] = t.id;
    item["title"] = t.title;
    item["artist"] = t.artist;
    item["duration_sec"] = t.duration_sec;
    fj.push_back(item);

    db_set_str(api, ctx, "liked_tracks", fj.dump());

    api->c_reset(ctx);
    api->c_fmt(ctx, "❤️ Трек добавлен в Избранное:\n", KOTO_ENT_BOLD);
    api->c_fmt(ctx, t.title.c_str(), KOTO_ENT_BOLD);
    api->c_text(ctx, " — ");
    api->c_fmt(ctx, t.artist.c_str(), KOTO_ENT_ITALIC);
    api->c_edit(ctx, KOTO_FMT_PLAIN);
}

static void cmd_favs(koto_ctx* ctx, const koto_api* api) {
    std::string favs_str = db_get_str(api, ctx, "liked_tracks", "[]");
    json fj = json::parse(favs_str, nullptr, false);

    api->c_reset(ctx);
    api->c_fmt(ctx, "⭐️ Избранные треки (@Diezdd):\n\n", KOTO_ENT_BOLD);

    if (!fj.is_array() || fj.empty()) {
        api->c_text(ctx, "Список избранного пуст. Добавляйте треки командой .like <номер>");
        api->c_edit(ctx, KOTO_FMT_PLAIN);
        return;
    }

    int count = 1;
    for (const auto& item : fj) {
        std::string title = item.value("title", "Unknown");
        std::string artist = item.value("artist", "Unknown");
        long long dur = item.value("duration_sec", 0LL);

        std::string line = std::to_string(count++) + ". " + title + " — " + artist +
                           " [" + format_duration(dur) + "]\n";
        api->c_text(ctx, line.c_str());
        if (count > 25) break;
    }

    api->c_edit(ctx, KOTO_FMT_PLAIN);
}

// ─── Команда .musicinfo ─────────────────────────────────────────────────────
static void cmd_musicinfo(koto_ctx* ctx, const koto_api* api) {
    std::string cid = sc_get_cid(api, ctx);

    api->c_reset(ctx);
    api->c_fmt(ctx, "🎵 Music Downloader SDK (C++)\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "• Версия: ");
    api->c_fmt(ctx, "1.2.7", KOTO_ENT_CODE);
    api->c_text(ctx, "\n• Автор: ");
    api->c_fmt(ctx, "@Diezdd", KOTO_ENT_CODE);
    api->c_text(ctx, "\n• Сервис по умолчанию: ");
    api->c_fmt(ctx, "SoundCloud ☁️", KOTO_ENT_CODE);
    api->c_text(ctx, "\n• SoundCloud Client ID: ");
    api->c_fmt(ctx, (cid.substr(0, 8) + "...").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n• Команды:\n");
    api->c_text(ctx, "  - .find <запрос> — поиск треков\n");
    api->c_text(ctx, "  - .dlf <номер> (или dlf <номер>) — скачать трек\n");
    api->c_text(ctx, "  - .download <запрос> — быстрый поиск + загрузка\n");
    api->c_text(ctx, "  - .favs — список избранного\n");
    api->c_text(ctx, "  - .like <номер> — лайкнуть трек");

    api->c_edit(ctx, KOTO_FMT_PLAIN);
}

// ─── Обработчик Inline Callback Кнопок (on_callback) ───────────────────────
static void music_dl_callback(koto_ctx* ctx, const koto_api* api) {
    const char* data = api->cb_data(ctx);
    if (!data || strncmp(data, "mdl:", 4) != 0) return;

    long long mid = api->cb_message_id(ctx);
    long long chat_id = api->chat_id(ctx);

    // Извлекаем возможный chat_id из callback_data (только если передан реальный ID чата в конце)
    std::string sdata(data);
    std::vector<std::string> parts = split(sdata, ':');
    if (parts.size() >= 4) {
        try {
            long long parsed_cid = std::stoll(parts.back());
            if (std::abs(parsed_cid) > 100 && std::abs(chat_id) <= 100) {
                chat_id = parsed_cid;
                KOTO_LOGI("music_dl", "Восстановлен chat_id из callback_data: %lld", chat_id);
            }
        } catch (...) {}
    }

    if (std::abs(chat_id) <= 100 && g_last_session.chat_id != 0) {
        chat_id = g_last_session.chat_id;
        KOTO_LOGI("music_dl", "Восстановлен chat_id из сессии памяти: %lld", chat_id);
    }
    if (std::abs(chat_id) <= 100) {
        chat_id = db_get_int64(api, ctx, "last_search_chat_id", 0);
        if (std::abs(chat_id) > 100) {
            KOTO_LOGI("music_dl", "Восстановлен chat_id из базы данных: %lld", chat_id);
        }
    }
    if (std::abs(chat_id) <= 100 && api && api->get_me) {
        chat_id = api->get_me(ctx);
        if (std::abs(chat_id) > 100) {
            KOTO_LOGI("music_dl", "Фолбэк chat_id на get_me: %lld", chat_id);
        }
    }

    if (strcmp(data, "mdl:noop") == 0) {
        api->cb_answer(ctx, "Текущая страница", 0);
        return;
    }

    if (strncmp(data, "mdl:close", 9) == 0) {
        long long sender = api->sender_id ? api->sender_id(ctx) : 0;
        long long me = api->get_me ? api->get_me(ctx) : 0;
        int level = api->user_level ? api->user_level(ctx) : KOTO_LEVEL_ALL;
        if (sender != 0 && me != 0 && sender != me && level < KOTO_LEVEL_TRUSTED) {
            api->cb_answer(ctx, "⛔️ Только автор может закрыть поиск", 1);
            return;
        }
        api->cb_answer(ctx, "Поиск закрыт", 0);
        if (api->delete_msg) api->delete_msg(ctx);
        if (chat_id != 0 && mid != 0 && api->delete_message) {
            api->delete_message(ctx, chat_id, mid);
        }
        api->c_reset(ctx);
        api->c_markdown(ctx, "✖️ *Поиск музыки закрыт.*");
        api->c_edit(ctx, 0);
        return;
    }

    if (strncmp(data, "mdl:favs", 8) == 0) {
        api->cb_answer(ctx, "⭐ Открываю избранное...", 0);
        cmd_favs(ctx, api);
        return;
    }

    // ── Перелистывание страниц ──
    if (strncmp(data, "mdl:pg:", 7) == 0) {
        std::string payload = data + 7;
        std::string action;
        size_t cpos = payload.find(':');
        if (cpos != std::string::npos) {
            action = payload.substr(0, cpos);
            try {
                long long btn_cid = std::stoll(payload.substr(cpos + 1));
                if (btn_cid != 0) chat_id = btn_cid;
            } catch (...) {}
        } else {
            action = payload;
        }

        std::string query;
        int page = 1;
        int per_page = 5;
        std::vector<TrackInfo> tracks;

        if (!load_search_session(api, ctx, chat_id, query, page, per_page, tracks)) {
            api->cb_answer(ctx, "⚠️ Сессия поиска устарела. Начните поиск заново.", 1);
            return;
        }

        int total_pages = (static_cast<int>(tracks.size()) + per_page - 1) / per_page;
        if (action == "next") {
            if (page < total_pages) page++;
            else {
                api->cb_answer(ctx, "ℹ️ Вы уже на последней странице.", 0);
                return;
            }
        } else if (action == "prev") {
            if (page > 1) page--;
            else {
                api->cb_answer(ctx, "ℹ️ Вы уже на первой странице.", 0);
                return;
            }
        }

        save_search_session(api, ctx, chat_id, query, page, per_page, tracks);
        api->cb_answer(ctx, ("📄 Страница " + std::to_string(page) + " из " + std::to_string(total_pages)).c_str(), 0);
        render_page(ctx, api, query, page, per_page, tracks, chat_id);
        api->c_edit_message(ctx, chat_id, mid, KOTO_FMT_PLAIN);
        return;
    }

    // ── Скачивание по кнопке "🎧 <номер>" ──
    if (strncmp(data, "mdl:dl:", 7) == 0) {
        int num = 0;
        long long btn_cid = 0;
        std::string payload = data + 7;
        size_t cpos = payload.find(':');
        try {
            if (cpos != std::string::npos) {
                num = std::stoi(payload.substr(0, cpos));
                btn_cid = std::stoll(payload.substr(cpos + 1));
            } else {
                num = std::stoi(payload);
            }
        } catch (...) { return; }

        if (btn_cid != 0) {
            chat_id = btn_cid;
        }
        if (std::abs(chat_id) <= 100 && g_last_session.chat_id != 0) {
            chat_id = g_last_session.chat_id;
        }
        if (std::abs(chat_id) <= 100) {
            chat_id = db_get_int64(api, ctx, "last_search_chat_id", 0);
        }
        if (std::abs(chat_id) <= 100 && api && api->get_me) {
            chat_id = api->get_me(ctx);
        }

        std::string query;
        int page = 1;
        int per_page = 5;
        std::vector<TrackInfo> tracks;

        if (!load_search_session(api, ctx, chat_id, query, page, per_page, tracks)) {
            api->cb_answer(ctx, "⚠️ Сессия поиска устарела. Сделайте .find заново.", 1);
            return;
        }

        int idx = num - 1;
        if (idx < 0 || idx >= static_cast<int>(tracks.size())) {
            api->cb_answer(ctx, "⚠️ Неверный номер трека.", 1);
            return;
        }

        const auto& track = tracks[idx];
        KOTO_LOGI("music_dl", "Клик по кнопке скачивания трека #%d: '%s' (ID: %s, chat_id: %lld)", idx + 1, track.title.c_str(), track.id.c_str(), chat_id);

        if (track.has_drm) {
            api->cb_answer(ctx, "⚠️ Этот трек защищён DRM (SoundCloud Go+) и недоступен для скачивания.", 1);
            return;
        }

        // Отвечаем на коллбэк toast-уведомлением и сразу шлём аудио в чат
        api->cb_answer(ctx, ("⏳ Загружаю: " + track.title).c_str(), 0);

        std::string err;
        bool ok = download_and_send_track(ctx, api, track, chat_id, num, &err);
        if (!ok) {
            std::string fail_msg = "❌ Не удалось скачать трек: " + (err.empty() ? "Ошибка потока" : err);
            api->cb_answer(ctx, fail_msg.c_str(), 1);
        }
        return;
    }
}

// ─── Команда .musiclog (логи разработки) ───────────────────────────────────
static void cmd_musiclog(koto_ctx* ctx, const koto_api* api) {
    const char* args = api->cmd_args(ctx);
    if (args && strcmp(trim(args).c_str(), "clear") == 0) {
        Logger::instance().clear();
        api->c_reset(ctx);
        api->c_fmt(ctx, "🧹 Логи модуля music_dl успешно очищены.", KOTO_ENT_BOLD);
        api->c_edit(ctx, KOTO_FMT_PLAIN);
        return;
    }

    auto entries = Logger::instance().get_entries();
    api->c_reset(ctx);
    api->c_emoji(ctx, "📋", 5188621441926438751LL);
    api->c_fmt(ctx, " Логи разработки music_dl (последние ", KOTO_ENT_BOLD);
    api->c_text(ctx, std::to_string(entries.size()).c_str());
    api->c_fmt(ctx, " записей):\n\n", KOTO_ENT_BOLD);

    if (entries.empty()) {
        api->c_fmt(ctx, "ℹ️ Логи пока пусты. Выполните поиск (.find) или скачивание (.dlf), чтобы зафиксировать события.", KOTO_ENT_ITALIC);
    } else {
        std::string log_text;
        for (const auto& e : entries) {
            log_text += "[" + e.time_str + "] [" + e.level + "][" + e.tag + "] " + e.msg + "\n";
        }
        api->c_fmt(ctx, log_text.c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, "\n\n💡 Очистить логи: ", KOTO_ENT_ITALIC);
        api->c_fmt(ctx, ".musiclog clear", KOTO_ENT_CODE);
    }
    api->c_edit(ctx, KOTO_FMT_PLAIN);
}

// ─── Команда .mclean: очистка кэша треков ─────────────────────────────────
static void cmd_mclean(koto_ctx* ctx, const koto_api* api) {
    char cache[512] = {0};
    api->cache_dir(ctx, cache, sizeof(cache));
    
    int count = 0;
    DIR* d = opendir(cache);
    if (d) {
        struct dirent* dir;
        while ((dir = readdir(d)) != NULL) {
            std::string name = dir->d_name;
            if (name.size() > 4 && name.rfind(".mp3") == name.size() - 4) {
                std::string full = std::string(cache) + "/" + name;
                unlink(full.c_str());
                count++;
            }
        }
        closedir(d);
    }
    
    api->c_reset(ctx);
    api->c_fmt(ctx, "🧹 Кэш треков очищен: удалено ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, std::to_string(count).c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " файлов mp3.");
    api->c_edit(ctx, KOTO_FMT_PLAIN);
}

// ─── Таблица команд ────────────────────────────────────────────────────────
static const koto_command COMMANDS[] = {
    {
        "find",
        &cmd_find,
        KOTO_LEVEL_ALL,
        "Поиск музыки по названию или исполнителю",
        "<запрос>"
    },
    {
        "fc",
        &cmd_fc,
        KOTO_LEVEL_ALL,
        "Перелистывание страниц результатов поиска (next/prev)",
        "[next|prev]"
    },
    {
        "dlf",
        &cmd_dlf,
        KOTO_LEVEL_ALL,
        "Скачать трек по номеру из результатов поиска",
        "<номер>"
    },
    {
        "download",
        &cmd_download,
        KOTO_LEVEL_ALL,
        "Быстрый поиск и скачивание первого трека",
        "<запрос>"
    },
    {
        "msc",
        &cmd_download,
        KOTO_LEVEL_ALL,
        "Короткий алиас для команды .download",
        "<запрос>"
    },
    {
        "like",
        &cmd_like,
        KOTO_LEVEL_ALL,
        "Добавить трек из текущего поиска в избранное",
        "<номер>"
    },
    {
        "favs",
        &cmd_favs,
        KOTO_LEVEL_ALL,
        "Показать список избранных треков",
        NULL
    },
    {
        "musicinfo",
        &cmd_musicinfo,
        KOTO_LEVEL_ALL,
        "Информация о модуле и сервисе скачивания музыки",
        NULL
    },
    {
        "musiclog",
        &cmd_musiclog,
        KOTO_LEVEL_ALL,
        "Просмотр логов разработки и отладки модуля music_dl",
        "[clear]"
    },
    {
        "mdebug",
        &cmd_musiclog,
        KOTO_LEVEL_ALL,
        "Алиас для .musiclog (логи отладки)",
        "[clear]"
    },
    {
        "mclean",
        &cmd_mclean,
        KOTO_LEVEL_ALL,
        "Очистить кэш скачанных треков .mp3 на устройстве",
        ""
    },
    {
        "mfile",
        &cmd_mfile,
        KOTO_LEVEL_ALL,
        "Тест отправки локальных файлов из кэша приложения (mp3 или jpg)",
        "[mp3|jpg|<путь>]"
    },
    {
        "musictag",
        &cmd_musictag,
        KOTO_LEVEL_ALL,
        "Установить или посмотреть юзернейм владельца для тегов в треках",
        "[@username]"
    }
};

// ─── Настройки .cfg ────────────────────────────────────────────────────────
static const koto_setting_field SETTINGS[] = {
    {
        "per_page",
        "Треков на страницу поиска",
        KOTO_FIELD_INT,
        "5",
        NULL
    },
    {
        "owner_username",
        "Юзернейм владельца (тег исполнителя в аудиофайле)",
        KOTO_FIELD_STR,
        "@Diezdd",
        NULL
    },
    {
        "sc_cid",
        "Пользовательский SoundCloud Client ID (опц.)",
        KOTO_FIELD_STR,
        "",
        NULL
    }
};

// ─── Экспорт модуля ────────────────────────────────────────────────────────
static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "music_dl",
    "Музыкальный поисковик и загрузчик из SoundCloud с распознаванием DRM (@Diezdd)",
    "1.6.2",
    24, // KoteLoader v0.2.7+ (поддержка via-bot кнопок)
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    &music_dl_watch,
    KOTO_WATCH_OUTGOING,
    &music_dl_callback, // on_callback: интерактивные кнопки скачивания, пагинации и закрытия
    SETTINGS,
    sizeof(SETTINGS) / sizeof(SETTINGS[0]),
    "https://github.com/Skwiziii/kt-Modules"
};

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
