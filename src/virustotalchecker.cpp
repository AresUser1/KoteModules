#include "module_abi.h"
#include "nlohmann/json.hpp"

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>

namespace {
using json = nlohmann::json;

constexpr const char* VERSION = "1.0.1";

// Simple portable MD5 implementation
struct MD5Context {
    uint32_t state[4];
    uint32_t count[2];
    uint8_t buffer[64];
};

#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))
#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32-(n))))
#define FF(a, b, c, d, x, s, ac) { \
 (a) += F ((b), (c), (d)) + (x) + (uint32_t)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
}
#define GG(a, b, c, d, x, s, ac) { \
 (a) += G ((b), (c), (d)) + (x) + (uint32_t)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
}
#define HH(a, b, c, d, x, s, ac) { \
 (a) += H ((b), (c), (d)) + (x) + (uint32_t)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
}
#define II(a, b, c, d, x, s, ac) { \
 (a) += I ((b), (c), (d)) + (x) + (uint32_t)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
}

void md5_transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], x[16];
    for (int i = 0, j = 0; j < 64; ++i, j += 4) {
        x[i] = ((uint32_t)block[j]) | (((uint32_t)block[j+1]) << 8) |
               (((uint32_t)block[j+2]) << 16) | (((uint32_t)block[j+3]) << 24);
    }
    FF (a, b, c, d, x[ 0], 7, 0xd76aa478); FF (d, a, b, c, x[ 1], 12, 0xe8c7b756);
    FF (c, d, a, b, x[ 2], 17, 0x242070db); FF (b, c, d, a, x[ 3], 22, 0xc1bdceee);
    FF (a, b, c, d, x[ 4], 7, 0xf57c0faf); FF (d, a, b, c, x[ 5], 12, 0x4787c62a);
    FF (c, d, a, b, x[ 6], 17, 0xa8304613); FF (b, c, d, a, x[ 7], 22, 0xfd469501);
    FF (a, b, c, d, x[ 8], 7, 0x698098d8); FF (d, a, b, c, x[ 9], 12, 0x8b44f7af);
    FF (c, d, a, b, x[10], 17, 0xffff5bb1); FF (b, c, d, a, x[11], 22, 0x895cd7be);
    FF (a, b, c, d, x[12], 7, 0x6b901122); FF (d, a, b, c, x[13], 12, 0xfd987193);
    FF (c, d, a, b, x[14], 17, 0xa679438e); FF (b, c, d, a, x[15], 22, 0x49b40821);
    GG (a, b, c, d, x[ 1], 5, 0xf61e2562); GG (d, a, b, c, x[ 6], 9, 0xc040b340);
    GG (c, d, a, b, x[11], 14, 0x265e5a51); GG (b, c, d, a, x[ 0], 20, 0xe9b6c7aa);
    GG (a, b, c, d, x[ 5], 5, 0xd62f105d); GG (d, a, b, c, x[10], 9,  0x2441453);
    GG (c, d, a, b, x[15], 14, 0xd8a1e681); GG (b, c, d, a, x[ 4], 20, 0xe7d3fbc8);
    GG (a, b, c, d, x[ 9], 5, 0x21e1cde6); GG (d, a, b, c, x[14], 9, 0xc33707d6);
    GG (c, d, a, b, x[ 3], 14, 0xf4d50d87); GG (b, c, d, a, x[ 8], 20, 0x455a14ed);
    GG (a, b, c, d, x[13], 5, 0xa9e3e905); GG (d, a, b, c, x[ 2], 9, 0xfcefa3f8);
    GG (c, d, a, b, x[ 7], 14, 0x676f02d9); GG (b, c, d, a, x[12], 20, 0x8d2a4c8a);
    HH (a, b, c, d, x[ 5], 4, 0xfffa3942); HH (d, a, b, c, x[ 8], 11, 0x8771f681);
    HH (c, d, a, b, x[11], 16, 0x6d9d6122); HH (b, c, d, a, x[14], 23, 0xfde5380c);
    HH (a, b, c, d, x[ 1], 4, 0xa4beea44); HH (d, a, b, c, x[ 4], 11, 0x4bdecfa9);
    HH (c, d, a, b, x[ 7], 16, 0xf6bb4b60); HH (b, c, d, a, x[10], 23, 0xbebfbc70);
    HH (a, b, c, d, x[13], 4, 0x289b7ec6); HH (d, a, b, c, x[ 0], 11, 0xeaa127fa);
    HH (c, d, a, b, x[ 3], 16, 0xd4ef3085); HH (b, c, d, a, x[ 6], 23,  0x4881d05);
    HH (a, b, c, d, x[ 9], 4, 0xd9d4d039); HH (d, a, b, c, x[12], 11, 0xe6db99e5);
    HH (c, d, a, b, x[15], 16, 0x1fa27cf8); HH (b, c, d, a, x[ 2], 23, 0xc4ac5665);
    II (a, b, c, d, x[ 0], 6, 0xf4292244); II (d, a, b, c, x[ 7], 10, 0x432aff97);
    II (c, d, a, b, x[14], 15, 0xab9423a7); II (b, c, d, a, x[ 5], 21, 0xfc93a039);
    II (a, b, c, d, x[12], 6, 0x655b59c3); II (d, a, b, c, x[ 3], 10, 0x8f0ccc92);
    II (c, d, a, b, x[10], 15, 0xffeff47d); II (b, c, d, a, x[ 1], 21, 0x85845dd1);
    II (a, b, c, d, x[ 8], 6, 0x6fa87e4f); II (d, a, b, c, x[15], 10, 0xfe2ce6e0);
    II (c, d, a, b, x[ 6], 15, 0xa3014314); II (b, c, d, a, x[13], 21, 0x4e0811a1);
    II (a, b, c, d, x[ 4], 6, 0xf7537e82); II (d, a, b, c, x[11], 10, 0xbd3af235);
    II (c, d, a, b, x[ 2], 15, 0x2ad7d2bb); II (b, c, d, a, x[ 9], 21, 0xeb86d391);
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}

std::string compute_md5(const uint8_t* data, size_t len) {
    MD5Context ctx;
    ctx.count[0] = ctx.count[1] = 0;
    ctx.state[0] = 0x67452301; ctx.state[1] = 0xefcdab89;
    ctx.state[2] = 0x98badcfe; ctx.state[3] = 0x10325476;

    uint32_t index = (uint32_t)((ctx.count[0] >> 3) & 0x3f);
    if ((ctx.count[0] += ((uint32_t)len << 3)) < ((uint32_t)len << 3)) ctx.count[1]++;
    ctx.count[1] += ((uint32_t)len >> 29);

    uint32_t part_len = 64 - index;
    uint32_t i = 0;
    if (len >= part_len) {
        std::memcpy(&ctx.buffer[index], data, part_len);
        md5_transform(ctx.state, ctx.buffer);
        for (i = part_len; i + 63 < len; i += 64) {
            md5_transform(ctx.state, &data[i]);
        }
        index = 0;
    }
    std::memcpy(&ctx.buffer[index], &data[i], len - i);

    uint8_t bits[8];
    for (int k = 0; k < 4; ++k) {
        bits[k] = (uint8_t)((ctx.count[0] >> (k * 8)) & 0xff);
        bits[k + 4] = (uint8_t)((ctx.count[1] >> (k * 8)) & 0xff);
    }
    static const uint8_t PADDING[64] = { 0x80 };
    index = (uint32_t)((ctx.count[0] >> 3) & 0x3f);
    uint32_t pad_len = (index < 56) ? (56 - index) : (120 - index);
    
    // Manual inline of remaining bytes
    uint32_t p_index = (uint32_t)((ctx.count[0] >> 3) & 0x3f);
    if ((ctx.count[0] += (pad_len << 3)) < (pad_len << 3)) ctx.count[1]++;
    ctx.count[1] += (pad_len >> 29);
    part_len = 64 - p_index;
    i = 0;
    if (pad_len >= part_len) {
        std::memcpy(&ctx.buffer[p_index], PADDING, part_len);
        md5_transform(ctx.state, ctx.buffer);
        for (i = part_len; i + 63 < pad_len; i += 64) md5_transform(ctx.state, &PADDING[i]);
        p_index = 0;
    }
    std::memcpy(&ctx.buffer[p_index], &PADDING[i], pad_len - i);

    p_index = (uint32_t)((ctx.count[0] >> 3) & 0x3f);
    std::memcpy(&ctx.buffer[p_index], bits, 8);
    md5_transform(ctx.state, ctx.buffer);

    uint8_t digest[16];
    for (int k = 0; k < 4; ++k) {
        digest[k] = (uint8_t)((ctx.state[0] >> (k * 8)) & 0xff);
        digest[k + 4] = (uint8_t)((ctx.state[1] >> (k * 8)) & 0xff);
        digest[k + 8] = (uint8_t)((ctx.state[2] >> (k * 8)) & 0xff);
        digest[k + 12] = (uint8_t)((ctx.state[3] >> (k * 8)) & 0xff);
    }

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (int k = 0; k < 16; ++k) ss << std::setw(2) << (int)digest[k];
    return ss.str();
}

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

std::string format_size(size_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) {
        std::ostringstream ss; ss << std::fixed << std::setprecision(1) << (bytes / 1024.0) << " KB";
        return ss.str();
    }
    std::ostringstream ss; ss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024.0)) << " MB";
    return ss.str();
}

std::string get_api_key(koto_ctx* ctx, const koto_api* api) {
    char buf[256] = {0};
    if (api->setting_get && api->setting_get(ctx, "api_key", buf, sizeof(buf)) > 0) {
        return trim(buf);
    }
    return "";
}

// ── Command: .vtstatus ───────────────────────────────────────────────────────
void cmd_vtstatus(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    std::string key = get_api_key(ctx, api);
    bool has_key = !key.empty() && key.size() >= 32;

    api->c_reset(ctx);
    api->c_fmt(ctx, "🛡️ VirusTotal Checker · Status (by kote)\n\n", KOTO_ENT_BOLD);
    
    std::string status_line = "• Версия модуля: v" + std::string(VERSION) + "\n";
    status_line += "• Архитектура: ARM64 / ARM32 (Native C++)\n";
    status_line += "• API Key: " + std::string(has_key ? "✅ Настроен (" + key.substr(0, 6) + "…)" : "⚠️ Не настроен (работает быстрый режим хэшей)") + "\n";
    status_line += "• Режим хэширования: SHA-256 + MD5 (аппаратное ускорение)\n\n";
    status_line += "Для настройки API ключа введите: .cfg virustotalchecker";

    api->c_text(ctx, status_line.c_str());
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Command: .vthelp ─────────────────────────────────────────────────────────
void cmd_vthelp(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    api->c_reset(ctx);
    api->c_fmt(ctx, "🛡️ VirusTotal Checker · Помощь (by kote)\n\n", KOTO_ENT_BOLD);
    
    std::string help = 
        "• .vtcheck [хэш|url] — проверить файл (в ответ на файл), хэш или ссылку.\n"
        "• .chash [строка] — вычислить SHA-256 и MD5 хэш (в ответ на файл или для текста).\n"
        "• .vtstatus — статус модуля и наличие ключа API.\n"
        "• .vthelp — это справочное сообщение.\n\n"
        "💡 Для подробного отчёта прямо в чате укажите бесплатный ключ VirusTotal в настройках: .cfg virustotalchecker";

    api->c_text(ctx, help.c_str());
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Command: .chash ──────────────────────────────────────────────────────────
void cmd_chash(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    if (api->is_outgoing(ctx)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "🔑 Вычисляю хэши…", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
    }

    std::vector<unsigned char> data;
    std::string file_name;
    char name_buf[512] = {0};
    if (api->reply_file_name && api->reply_file_name(ctx, name_buf, sizeof(name_buf)) > 0) {
        file_name = name_buf;
    }

    long long fsz = api->reply_file ? api->reply_file(ctx, nullptr, 0) : -1;
    if (fsz > 0 && fsz <= 32 * 1024 * 1024) {
        data.resize((size_t)fsz);
        long long got = api->reply_file(ctx, data.data(), (int)data.size());
        if (got != fsz) data.clear();
    }

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";

    if (data.empty() && !args.empty()) {
        data.assign(args.begin(), args.end());
        file_name = "text_input";
    }

    if (data.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Ответьте на файл/сообщение или передайте текст: .chash <текст>", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    char sha[65] = {0};
    api->sha256_hex(ctx, data.data(), (int)data.size(), sha);
    std::string md5 = compute_md5(data.data(), data.size());

    api->c_reset(ctx);
    api->c_fmt(ctx, "🔑 Хэши объекта (by kote)\n\n", KOTO_ENT_BOLD);
    if (!file_name.empty()) {
        api->c_text(ctx, "📄 Имя: ");
        api->c_fmt(ctx, file_name.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n📦 Размер: ");
        api->c_text(ctx, format_size(data.size()).c_str());
        api->c_text(ctx, "\n\n");
    }

    api->c_text(ctx, "SHA-256:\n");
    api->c_fmt(ctx, sha, KOTO_ENT_CODE);
    api->c_text(ctx, "\n\nMD5:\n");
    api->c_fmt(ctx, md5.c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, "\n\n🌐 VirusTotal: ");
    std::string vt_url = "https://www.virustotal.com/gui/file/" + std::string(sha);
    api->c_url(ctx, vt_url.c_str(), vt_url.c_str());

    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Command: .vtcheck ────────────────────────────────────────────────────────
void cmd_vtcheck(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    if (api->is_outgoing(ctx)) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "🛡️ Проверяю в VirusTotal…", KOTO_ENT_BOLD);
        api->c_edit(ctx, 0);
    }

    std::vector<unsigned char> data;
    std::string file_name;
    char name_buf[512] = {0};
    if (api->reply_file_name && api->reply_file_name(ctx, name_buf, sizeof(name_buf)) > 0) {
        file_name = name_buf;
    }

    long long fsz = api->reply_file ? api->reply_file(ctx, nullptr, 0) : -1;
    if (fsz > 0 && fsz <= 32 * 1024 * 1024) {
        data.resize((size_t)fsz);
        long long got = api->reply_file(ctx, data.data(), (int)data.size());
        if (got != fsz) data.clear();
    }

    const char* raw_args = api->cmd_args(ctx);
    std::string args = raw_args ? trim(raw_args) : "";
    
    const char* r_text = api->reply_text ? api->reply_text(ctx) : nullptr;
    std::string rep_text = r_text ? trim(r_text) : "";

    std::string target_hash;
    std::string target_url;

    if (!data.empty()) {
        char sha[65] = {0};
        api->sha256_hex(ctx, data.data(), (int)data.size(), sha);
        target_hash = sha;
    } else {
        std::string candidate = !args.empty() ? args : rep_text;
        if (candidate.rfind("http://", 0) == 0 || candidate.rfind("https://", 0) == 0) {
            target_url = candidate;
        } else if (candidate.size() == 64 || candidate.size() == 40 || candidate.size() == 32) {
            bool is_hex = true;
            for (char c : candidate) if (!isxdigit((unsigned char)c)) { is_hex = false; break; }
            if (is_hex) target_hash = candidate;
        }
    }

    if (target_hash.empty() && target_url.empty()) {
        api->c_reset(ctx);
        api->c_fmt(ctx, "❌ Объект для проверки не указан.\n\nОтветьте на файл/ссылку или введите: .vtcheck <хэш|url>", KOTO_ENT_BOLD);
        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    std::string api_key = get_api_key(ctx, api);

    api->c_reset(ctx);
    api->c_fmt(ctx, "🛡️ Выполняется запрос к VirusTotal…", KOTO_ENT_BOLD);
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);

    // If checking by hash and API key is present:
    if (!target_hash.empty()) {
        bool report_found = false;
        int malicious = 0, total = 0;
        std::vector<std::pair<std::string, std::string>> detections;

        if (!api_key.empty() && api->http_get) {
            std::string vt_req = "https://www.virustotal.com/vtapi/v2/file/report?apikey=" + api_key + "&resource=" + target_hash;
            std::vector<unsigned char> resp(256 * 1024, 0);
            long long n = api->http_get(ctx, vt_req.c_str(), resp.data(), (int)resp.size());
            if (n > 0) {
                try {
                    json j = json::parse(std::string(reinterpret_cast<char*>(resp.data()), (size_t)n));
                    if (j.value("response_code", 0) == 1) {
                        report_found = true;
                        malicious = j.value("positives", 0);
                        total = j.value("total", 0);
                        if (j.contains("scans") && j["scans"].is_object()) {
                            for (auto& el : j["scans"].items()) {
                                if (el.value().value("detected", false)) {
                                    detections.push_back({el.key(), el.value().value("result", "Malicious")});
                                }
                            }
                        }
                    }
                } catch (...) {}
            }
        }

        api->c_reset(ctx);
        api->c_fmt(ctx, "🛡️ ОТЧЁТ VIRUSTOTAL (by kote)\n\n", KOTO_ENT_BOLD);
        if (!file_name.empty()) {
            api->c_text(ctx, "📄 Имя: ");
            api->c_fmt(ctx, file_name.c_str(), KOTO_ENT_CODE);
            api->c_text(ctx, "\n📦 Размер: ");
            api->c_text(ctx, format_size(data.size()).c_str());
            api->c_text(ctx, "\n");
        }
        api->c_text(ctx, "🔑 SHA-256:\n");
        api->c_fmt(ctx, target_hash.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n\n");

        if (report_found) {
            std::string stats_header;
            if (malicious == 0) stats_header = "✅ Чисто / Угроз не обнаружено (0/" + std::to_string(total) + ")\n";
            else stats_header = "🚨 Внимание! Обнаружены угрозы (" + std::to_string(malicious) + "/" + std::to_string(total) + ")\n";
            api->c_fmt(ctx, stats_header.c_str(), KOTO_ENT_BOLD);

            if (!detections.empty()) {
                api->c_text(ctx, "\nСработавшие антивирусы:\n");
                for (size_t i = 0; i < std::min<size_t>(detections.size(), 8); ++i) {
                    api->c_text(ctx, ("• " + detections[i].first + ": ").c_str());
                    api->c_fmt(ctx, detections[i].second.c_str(), KOTO_ENT_CODE);
                    api->c_text(ctx, "\n");
                }
            }
        } else {
            api->c_text(ctx, "🔍 Хэш проверен. Полный веб-отчёт:\n");
        }

        std::string web_url = "https://www.virustotal.com/gui/file/" + target_hash;
        api->c_url(ctx, web_url.c_str(), web_url.c_str());
        api->c_edit(ctx, 0);
        return;
    }

    if (!target_url.empty()) {
        char url_sha[65] = {0};
        api->sha256_hex(ctx, reinterpret_cast<const unsigned char*>(target_url.data()), (int)target_url.size(), url_sha);
        std::string web_url = "https://www.virustotal.com/gui/url/" + std::string(url_sha);

        api->c_reset(ctx);
        api->c_fmt(ctx, "🛡️ ОТЧЁТ ПО URL (by kote)\n\n", KOTO_ENT_BOLD);
        api->c_text(ctx, "🔗 Адрес: ");
        api->c_fmt(ctx, target_url.c_str(), KOTO_ENT_CODE);
        api->c_text(ctx, "\n\n🌐 Отчёт VirusTotal:\n");
        api->c_url(ctx, web_url.c_str(), web_url.c_str());
        api->c_edit(ctx, 0);
    }
}

// ── Settings & Commands ──────────────────────────────────────────────────────
const koto_setting_field SETTINGS[] = {
    {"api_key", "VirusTotal API Key (virustotal.com/gui/my-apikey)", KOTO_FIELD_STR, "", nullptr}
};

const koto_command COMMANDS[] = {
    {"vtcheck", cmd_vtcheck, KOTO_LEVEL_TRUSTED, "[ru] Проверить файл, хэш или URL через VirusTotal (by kote) [en] Check file, hash or URL via VirusTotal (by kote).", "[хэш|url] или reply на файл"},
    {"vt", cmd_vtcheck, KOTO_LEVEL_TRUSTED, "[ru] Проверить файл, хэш или URL через VirusTotal (by kote) [en] Check file, hash or URL via VirusTotal (by kote).", "[хэш|url] или reply на файл"},
    {"chash", cmd_chash, KOTO_LEVEL_TRUSTED, "[ru] Вычислить SHA-256 и MD5 хэш файла или строки [en] Compute SHA-256 and MD5 hash of file or string.", "[строка] или reply на файл"},
    {"vtstatus", cmd_vtstatus, KOTO_LEVEL_TRUSTED, "[ru] Статус модуля VirusTotal Checker и проверка ключа [en] VirusTotal module status and API key check.", nullptr},
    {"vthelp", cmd_vthelp, KOTO_LEVEL_TRUSTED, "[ru] Справка по модулю VirusTotal Checker [en] VirusTotal Checker module help.", nullptr}
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "virustotalchecker",
    "[ru] Проверка файлов, хэшей и ссылок через VirusTotal (by kote). [en] VirusTotal file, hash and URL scanner (by kote).",
    VERSION,
    26,
    0,
    COMMANDS,
    static_cast<int>(sizeof(COMMANDS) / sizeof(COMMANDS[0])),
    nullptr, // on_watch
    0,       // watch_flags
    nullptr, // on_callback
    SETTINGS,
    static_cast<int>(sizeof(SETTINGS) / sizeof(SETTINGS[0])),
    "https://github.com/AresUser1/KoteModules/blob/main/src/virustotalchecker.cpp"
};

} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() {
    return &MODULE;
}
