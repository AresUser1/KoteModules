// sysinfo.cpp — Новый системный модуль мониторинга и метрик (C++ ABI 3).
// Предоставляет точные и достоверные аппаратные данные сервера/устройства Linux & Android.
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <cstring>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <unistd.h>
#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

namespace {

static constexpr long long ICON_LAPTOP = 5172685498350567983LL;
static constexpr long long ICON_GEAR   = 5253952855185829086LL;
static constexpr long long ICON_CHART  = 5364265190353286344LL;
static constexpr long long ICON_ROCKET = 5445284980978621387LL;

struct SysMetrics {
    std::string hostname;
    std::string os_name;
    std::string kernel;
    std::string arch;
    long long uptime_sec = 0;

    std::string cpu_model;
    int cpu_cores = 1;
    bool has_loadavg = false;
    double load_1m = 0.0;
    double load_5m = 0.0;
    double load_15m = 0.0;
    double cpu_percent = 0.0;

    long long ram_total_mb = 0;
    long long ram_used_mb = 0;
    long long ram_avail_mb = 0;
    long long ram_free_mb = 0;
    long long ram_cached_mb = 0;
    double ram_percent = 0.0;

    long long swap_total_mb = 0;
    long long swap_used_mb = 0;
    double swap_percent = 0.0;

    double disk_total_gb = 0.0;
    double disk_used_gb = 0.0;
    double disk_free_gb = 0.0;
    double disk_percent = 0.0;

    double proc_rss_mb = 0.0;
    int proc_threads = 0;

    double net_rx_mb = 0.0;
    double net_tx_mb = 0.0;
};

static std::string get_prop(const char* name) {
#if defined(__ANDROID__)
    char val[PROP_VALUE_MAX] = {0};
    if (__system_property_get(name, val) > 0) {
        return val;
    }
#endif
    return "";
}

std::string make_bar(double percent, int width = 10) {
    if (percent < 0.0) percent = 0.0;
    if (percent > 100.0) percent = 100.0;
    int filled = static_cast<int>(std::round(percent / 100.0 * width));
    std::string bar;
    for (int i = 0; i < filled; ++i) bar += "▰";
    for (int i = filled; i < width; ++i) bar += "▱";
    return bar;
}

std::string format_uptime(long long sec) {
    if (sec <= 0) return "0 сек.";
    long long days = sec / 86400;
    long long hours = (sec % 86400) / 3600;
    long long mins = (sec % 3600) / 60;
    long long s = sec % 60;

    std::ostringstream ss;
    if (days > 0) ss << days << " дн. ";
    if (hours > 0 || days > 0) ss << hours << " ч. ";
    ss << mins << " мин. " << s << " сек.";
    return ss.str();
}

SysMetrics collect_metrics() {
    SysMetrics m;

    // 1. Hostname, Kernel, Arch
    char hbuf[256] = {0};
    if (gethostname(hbuf, sizeof(hbuf) - 1) == 0) m.hostname = hbuf;
    else m.hostname = "localhost";

    struct utsname u;
    if (uname(&u) == 0) {
        m.kernel = u.release;
        m.arch = u.machine;
    } else {
        m.kernel = "Linux";
        m.arch = "aarch64";
    }

    // 2. OS Name (из Android properties или /etc/os-release)
    std::string android_rel = get_prop("ro.build.version.release");
    if (!android_rel.empty()) {
        std::string market = get_prop("ro.product.marketname");
        if (market.empty()) market = get_prop("ro.product.odm.marketname");
        std::string brand = get_prop("ro.product.brand");
        std::string model = get_prop("ro.product.model");
        std::string dev;
        if (!market.empty()) dev = market;
        else if (!brand.empty() && !model.empty()) dev = brand + " " + model;
        else if (!model.empty()) dev = model;

        m.os_name = "Android " + android_rel + (dev.empty() ? "" : " (" + dev + ")");
    }

    if (m.os_name.empty()) {
        std::ifstream os_file("/etc/os-release");
        if (os_file.is_open()) {
            std::string line;
            while (std::getline(os_file, line)) {
                if (line.rfind("PRETTY_NAME=", 0) == 0) {
                    std::string val = line.substr(12);
                    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
                        val = val.substr(1, val.size() - 2);
                    }
                    m.os_name = val;
                    break;
                }
            }
        }
    }
    if (m.os_name.empty()) m.os_name = "Linux/GNU";

    // 3. Uptime
    std::ifstream up_file("/proc/uptime");
    if (up_file.is_open()) {
        double up = 0.0;
        if (up_file >> up) m.uptime_sec = static_cast<long long>(up);
    }
    if (m.uptime_sec == 0) {
        struct sysinfo si;
        if (sysinfo(&si) == 0) m.uptime_sec = si.uptime;
    }

    // 4. CPU Cores & Model & LoadAvg
    m.cpu_cores = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    if (m.cpu_cores < 1) m.cpu_cores = 1;

    // Проверяем SoC свойства Android
    std::string soc_manuf = get_prop("ro.soc.manufacturer");
    std::string soc_model = get_prop("ro.soc.model");
    if (soc_model.empty()) soc_model = get_prop("ro.board.platform");
    if (soc_model.empty()) soc_model = get_prop("ro.hardware");
    if (soc_model.empty()) soc_model = get_prop("ro.chipname");

    if (!soc_manuf.empty() && !soc_model.empty()) {
        m.cpu_model = soc_manuf + " " + soc_model;
    } else if (!soc_model.empty()) {
        m.cpu_model = soc_model;
    }

    // Проверяем /proc/cpuinfo
    if (m.cpu_model.empty()) {
        std::ifstream cpu_file("/proc/cpuinfo");
        if (cpu_file.is_open()) {
            std::string line;
            while (std::getline(cpu_file, line)) {
                if (line.rfind("model name", 0) == 0 || line.rfind("Hardware", 0) == 0) {
                    auto colon = line.find(':');
                    if (colon != std::string::npos && colon + 2 < line.size()) {
                        m.cpu_model = koto_utils::trim(line.substr(colon + 1));
                        break;
                    }
                }
            }
        }
    }

    // Частота CPU (sysfs)
    long long max_khz = 0;
    for (int i = 0; i < m.cpu_cores; ++i) {
        std::string p = "/sys/devices/system/cpu/cpu" + std::to_string(i) + "/cpufreq/cpuinfo_max_freq";
        std::ifstream ff(p);
        long long k = 0;
        if (ff >> k && k > max_khz) max_khz = k;
    }
    if (max_khz == 0) {
        std::ifstream ff("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq");
        ff >> max_khz;
    }
    if (max_khz > 0) {
        double ghz = static_cast<double>(max_khz) / 1000000.0;
        std::ostringstream fss;
        fss << std::fixed << std::setprecision(2) << ghz << " GHz";
        if (!m.cpu_model.empty()) m.cpu_model += " @ " + fss.str();
        else m.cpu_model = fss.str();
    }

    if (m.cpu_model.empty()) {
        m.cpu_model = m.arch + " Processor (" + std::to_string(m.cpu_cores) + " Cores)";
    }

    // Чтение LoadAvg
    std::ifstream la_file("/proc/loadavg");
    if (la_file.is_open()) {
        double l1 = 0.0, l5 = 0.0, l15 = 0.0;
        if (la_file >> l1 >> l5 >> l15) {
            if (l1 > 0.001 || l5 > 0.001 || l15 > 0.001) {
                m.has_loadavg = true;
                m.load_1m = l1;
                m.load_5m = l5;
                m.load_15m = l15;
                m.cpu_percent = (m.load_1m / static_cast<double>(m.cpu_cores)) * 100.0;
                if (m.cpu_percent > 100.0) m.cpu_percent = 100.0;
                if (m.cpu_percent < 0.0) m.cpu_percent = 0.0;
            }
        }
    }

    // 5. RAM & Swap из /proc/meminfo
    std::ifstream mem_file("/proc/meminfo");
    if (mem_file.is_open()) {
        std::string line;
        long long total_kb = 0, avail_kb = 0, free_kb = 0, cached_kb = 0, buffers_kb = 0;
        long long sw_total_kb = 0, sw_free_kb = 0;
        while (std::getline(mem_file, line)) {
            if (line.rfind("MemTotal:", 0) == 0) sscanf(line.c_str(), "MemTotal: %lld kB", &total_kb);
            else if (line.rfind("MemAvailable:", 0) == 0) sscanf(line.c_str(), "MemAvailable: %lld kB", &avail_kb);
            else if (line.rfind("MemFree:", 0) == 0) sscanf(line.c_str(), "MemFree: %lld kB", &free_kb);
            else if (line.rfind("Cached:", 0) == 0) sscanf(line.c_str(), "Cached: %lld kB", &cached_kb);
            else if (line.rfind("Buffers:", 0) == 0) sscanf(line.c_str(), "Buffers: %lld kB", &buffers_kb);
            else if (line.rfind("SwapTotal:", 0) == 0) sscanf(line.c_str(), "SwapTotal: %lld kB", &sw_total_kb);
            else if (line.rfind("SwapFree:", 0) == 0) sscanf(line.c_str(), "SwapFree: %lld kB", &sw_free_kb);
        }
        if (avail_kb == 0) avail_kb = free_kb + cached_kb + buffers_kb;

        m.ram_total_mb = total_kb / 1024;
        m.ram_avail_mb = avail_kb / 1024;
        m.ram_free_mb = free_kb / 1024;
        m.ram_cached_mb = (cached_kb + buffers_kb) / 1024;
        m.ram_used_mb = m.ram_total_mb - m.ram_avail_mb;
        if (m.ram_used_mb < 0) m.ram_used_mb = 0;
        if (m.ram_total_mb > 0) {
            m.ram_percent = static_cast<double>(m.ram_used_mb) * 100.0 / static_cast<double>(m.ram_total_mb);
        }

        m.swap_total_mb = sw_total_kb / 1024;
        long long sw_free_mb = sw_free_kb / 1024;
        m.swap_used_mb = m.swap_total_mb - sw_free_mb;
        if (m.swap_used_mb < 0) m.swap_used_mb = 0;
        if (m.swap_total_mb > 0) {
            m.swap_percent = static_cast<double>(m.swap_used_mb) * 100.0 / static_cast<double>(m.swap_total_mb);
        }
    }

    // 6. Disk space (statvfs)
    struct statvfs sv;
    const char* path_to_check = "/data";
    if (statvfs(path_to_check, &sv) != 0) path_to_check = "/";
    if (statvfs(path_to_check, &sv) == 0) {
        double bsize = static_cast<double>(sv.f_frsize ? sv.f_frsize : sv.f_bsize);
        double total_b = static_cast<double>(sv.f_blocks) * bsize;
        double free_b = static_cast<double>(sv.f_bavail) * bsize;
        double used_b = total_b - free_b;
        if (used_b < 0) used_b = 0;

        m.disk_total_gb = total_b / (1024.0 * 1024.0 * 1024.0);
        m.disk_free_gb = free_b / (1024.0 * 1024.0 * 1024.0);
        m.disk_used_gb = used_b / (1024.0 * 1024.0 * 1024.0);
        if (m.disk_total_gb > 0) {
            m.disk_percent = (m.disk_used_gb / m.disk_total_gb) * 100.0;
        }
    }

    // 7. Process metrics (VmRSS & Threads)
    std::ifstream stat_self("/proc/self/status");
    if (stat_self.is_open()) {
        std::string line;
        while (std::getline(stat_self, line)) {
            if (line.rfind("VmRSS:", 0) == 0) {
                long long rss_kb = 0;
                sscanf(line.c_str(), "VmRSS: %lld kB", &rss_kb);
                m.proc_rss_mb = static_cast<double>(rss_kb) / 1024.0;
            } else if (line.rfind("Threads:", 0) == 0) {
                sscanf(line.c_str(), "Threads: %d", &m.proc_threads);
            }
        }
    }

    // 8. Network stats
    std::ifstream net_file("/proc/net/dev");
    if (net_file.is_open()) {
        std::string line;
        unsigned long long total_rx = 0, total_tx = 0;
        while (std::getline(net_file, line)) {
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string iface = koto_utils::trim(line.substr(0, colon));
            if (iface == "lo") continue; // игнорируем loopback

            unsigned long long rx = 0, tx = 0, dummy = 0;
            std::istringstream iss(line.substr(colon + 1));
            // Format: rx_bytes, rx_packets, errs, drop, fifo, frame, compressed, multicast, tx_bytes, ...
            if (iss >> rx >> dummy >> dummy >> dummy >> dummy >> dummy >> dummy >> dummy >> tx) {
                total_rx += rx;
                total_tx += tx;
            }
        }
        m.net_rx_mb = static_cast<double>(total_rx) / (1024.0 * 1024.0);
        m.net_tx_mb = static_cast<double>(total_tx) / (1024.0 * 1024.0);
    }

    return m;
}

void render_main_panel(koto_ctx* ctx, const koto_api* api, const SysMetrics& m) {
    api->c_reset(ctx);

    api->c_emoji(ctx, "💻", ICON_LAPTOP);
    api->c_fmt(ctx, " Системная информация Kotogram\n\n", KOTO_ENT_BOLD);

    // ── Система и устройство ──
    api->c_emoji(ctx, "⚙️", ICON_GEAR);
    api->c_fmt(ctx, " Система & Устройство:\n", KOTO_ENT_BOLD);
    api->c_text(ctx, " • Хост: ");
    api->c_fmt(ctx, (m.hostname + "\n").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " • ОС: ");
    api->c_fmt(ctx, (m.os_name + "\n").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " • Ядро: ");
    api->c_fmt(ctx, (m.kernel + " (" + m.arch + ")\n").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " • Аптайм: ");
    api->c_fmt(ctx, (format_uptime(m.uptime_sec) + "\n\n").c_str(), KOTO_ENT_CODE);

    // ── Процессор ──
    api->c_emoji(ctx, "🚀", ICON_ROCKET);
    api->c_fmt(ctx, " Процессор (CPU):\n", KOTO_ENT_BOLD);
    api->c_text(ctx, " • Модель: ");
    api->c_fmt(ctx, (m.cpu_model + "\n").c_str(), KOTO_ENT_CODE);
    api->c_text(ctx, " • Ядер: ");
    api->c_fmt(ctx, (std::to_string(m.cpu_cores) + "\n").c_str(), KOTO_ENT_CODE);

    if (m.has_loadavg) {
        std::ostringstream cpu_ss;
        cpu_ss << " • Загрузка: " << make_bar(m.cpu_percent, 10) << " "
               << std::fixed << std::setprecision(1) << m.cpu_percent << "%\n";
        api->c_text(ctx, cpu_ss.str().c_str());

        std::ostringstream la_ss;
        la_ss << " • Load Avg: " << std::fixed << std::setprecision(2)
              << m.load_1m << ", " << m.load_5m << ", " << m.load_15m << "\n\n";
        api->c_fmt(ctx, la_ss.str().c_str(), KOTO_ENT_CODE);
    } else {
        std::string cpu_status = " • Статус: Онлайн (" + std::to_string(m.cpu_cores) + " активных ядер)\n";
        api->c_text(ctx, cpu_status.c_str());
        api->c_text(ctx, " • Режим: ");
        api->c_fmt(ctx, "SELinux Protected (App Sandbox)\n\n", KOTO_ENT_CODE);
    }

    // ── Память ──
    api->c_emoji(ctx, "📊", ICON_CHART);
    api->c_fmt(ctx, " Оперативная память (RAM):\n", KOTO_ENT_BOLD);

    std::ostringstream ram_ss;
    ram_ss << " • " << make_bar(m.ram_percent, 10) << " "
           << std::fixed << std::setprecision(1) << m.ram_percent << "%\n"
           << " • Использовано: " << m.ram_used_mb << " / " << m.ram_total_mb << " MB\n"
           << " • Свободно: " << m.ram_avail_mb << " MB (Кэш: " << m.ram_cached_mb << " MB)\n";
    if (m.swap_total_mb > 0) {
        ram_ss << " • Swap: " << m.swap_used_mb << " / " << m.swap_total_mb << " MB ("
               << std::fixed << std::setprecision(1) << m.swap_percent << "%)\n\n";
    } else {
        ram_ss << "\n";
    }
    api->c_text(ctx, ram_ss.str().c_str());

    // ── Дисковое хранилище ──
    api->c_emoji(ctx, "💾", ICON_LAPTOP);
    api->c_fmt(ctx, " Накопитель:\n", KOTO_ENT_BOLD);

    std::ostringstream dsk_ss;
    dsk_ss << " • " << make_bar(m.disk_percent, 10) << " "
           << std::fixed << std::setprecision(1) << m.disk_percent << "%\n"
           << " • Использовано: " << std::fixed << std::setprecision(1) << m.disk_used_gb
           << " / " << m.disk_total_gb << " GB (Свободно: " << m.disk_free_gb << " GB)\n\n";
    api->c_text(ctx, dsk_ss.str().c_str());

    // ── Процесс Юзербота ──
    api->c_emoji(ctx, "🐾", ICON_ROCKET);
    api->c_fmt(ctx, " Процесс Kotogram:\n", KOTO_ENT_BOLD);

    std::ostringstream bot_ss;
    bot_ss << " • Память RSS: " << std::fixed << std::setprecision(1) << m.proc_rss_mb << " MB\n"
           << " • Потоков: " << m.proc_threads << "\n"
           << " • Сеть: ↓ " << std::fixed << std::setprecision(1) << m.net_rx_mb << " MB | ↑ "
           << m.net_tx_mb << " MB";
    api->c_text(ctx, bot_ss.str().c_str());

    // Кнопки управления
    api->c_button(ctx, "🔄 Обновить", "sys_refresh", 0);
    api->c_button(ctx, "📊 Подробно", "sys_details", 0);
    api->c_button(ctx, "🚫 Закрыть", "sys_close", 1);
}

void render_details_panel(koto_ctx* ctx, const koto_api* api, const SysMetrics& m) {
    api->c_reset(ctx);

    api->c_emoji(ctx, "📊", ICON_CHART);
    api->c_fmt(ctx, " Детальные метрики системы:\n\n", KOTO_ENT_BOLD);

    std::ostringstream ss;
    ss << "🧠 CPU Details:\n"
       << "  Arch: " << m.arch << "\n"
       << "  Cores Online: " << m.cpu_cores << "\n"
       << "  Load (1m/5m/15m): " << std::fixed << std::setprecision(2)
       << m.load_1m << ", " << m.load_5m << ", " << m.load_15m << "\n\n"
       << "💾 Memory Breakdown:\n"
       << "  Total:     " << m.ram_total_mb << " MB\n"
       << "  Used:      " << m.ram_used_mb << " MB (" << std::fixed << std::setprecision(1) << m.ram_percent << "%)\n"
       << "  Available: " << m.ram_avail_mb << " MB\n"
       << "  Free:      " << m.ram_free_mb << " MB\n"
       << "  Cached:    " << m.ram_cached_mb << " MB\n"
       << "  Swap:      " << m.swap_used_mb << " / " << m.swap_total_mb << " MB\n\n"
       << "📦 Storage (/data):\n"
       << "  Total: " << std::fixed << std::setprecision(2) << m.disk_total_gb << " GB\n"
       << "  Used:  " << m.disk_used_gb << " GB (" << std::setprecision(1) << m.disk_percent << "%)\n"
       << "  Free:  " << m.disk_free_gb << " GB\n\n"
       << "🌐 Network (Total non-loopback):\n"
       << "  RX Received:    " << std::fixed << std::setprecision(2) << m.net_rx_mb << " MB\n"
       << "  TX Transmitted: " << m.net_tx_mb << " MB\n";

    api->c_fmt(ctx, ss.str().c_str(), KOTO_ENT_CODE);

    api->c_button(ctx, "⬅️ Назад", "sys_main", 0);
    api->c_button(ctx, "🔄 Обновить", "sys_details", 0);
    api->c_button(ctx, "🚫 Закрыть", "sys_close", 1);
}

void render_neofetch(koto_ctx* ctx, const koto_api* api, const SysMetrics& m) {
    api->c_reset(ctx);

    std::ostringstream ss;
    ss << "   /\\_/\\     Kotogram Userbot\n"
       << "  ( o.o )    ────────────────\n"
       << "   > ^ <     OS:     " << m.os_name << "\n"
       << "             Host:   " << m.hostname << "\n"
       << "             Kernel: " << m.kernel << "\n"
       << "             Arch:   " << m.arch << "\n"
       << "             Uptime: " << format_uptime(m.uptime_sec) << "\n"
       << "             CPU:    " << m.cpu_model << " (" << m.cpu_cores << "x)\n"
       << "             RAM:    " << m.ram_used_mb << " / " << m.ram_total_mb << " MB ("
       << std::fixed << std::setprecision(1) << m.ram_percent << "%)\n"
       << "             Disk:   " << std::fixed << std::setprecision(1) << m.disk_used_gb << " / " << m.disk_total_gb << " GB\n"
       << "             RSS:    " << std::fixed << std::setprecision(1) << m.proc_rss_mb << " MB";

    api->c_fmt(ctx, ss.str().c_str(), KOTO_ENT_CODE);

    api->c_button(ctx, "🔄 Обновить", "sys_neofetch", 0);
    api->c_button(ctx, "💻 Панель", "sys_main", 0);
    api->c_button(ctx, "🚫 Закрыть", "sys_close", 1);
}

// ── Обработчики команд ──

void cmd_sys(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) return;

    SysMetrics m = collect_metrics();
    render_main_panel(ctx, api, m);

    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

void cmd_neofetch(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) return;

    SysMetrics m = collect_metrics();
    render_neofetch(ctx, api, m);

    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

// ── Обработчик инлайн-кнопок ──

void on_callback(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) {
        if (api->cb_answer) api->cb_answer(ctx, "❌ Доступ разрешен только владельцу!", 1);
        return;
    }

    const char* data = api->cb_data(ctx);
    if (!data) return;
    std::string d = data;

    if (d == "sys_close") {
        if (api->cb_answer) api->cb_answer(ctx, "Закрыто", 0);
        api->delete_msg(ctx);
        return;
    }

    if (d == "sys_refresh" || d == "sys_main") {
        if (api->cb_answer) api->cb_answer(ctx, "Данные обновлены 🔄", 0);
        SysMetrics m = collect_metrics();
        render_main_panel(ctx, api, m);
        api->c_edit(ctx, 0);
        return;
    }

    if (d == "sys_details") {
        if (api->cb_answer) api->cb_answer(ctx, "Детальные метрики 📊", 0);
        SysMetrics m = collect_metrics();
        render_details_panel(ctx, api, m);
        api->c_edit(ctx, 0);
        return;
    }

    if (d == "sys_neofetch") {
        if (api->cb_answer) api->cb_answer(ctx, "Neofetch обновлен 🐾", 0);
        SysMetrics m = collect_metrics();
        render_neofetch(ctx, api, m);
        api->c_edit(ctx, 0);
        return;
    }
}

const koto_command COMMANDS[] = {
    {
        "sys",
        &cmd_sys,
        KOTO_LEVEL_OWNER,
        "Показывает точные метрики системы, нагрузку CPU, RAM, диска и процесса.",
        nullptr
    },
    {
        "sysinfo",
        &cmd_sys,
        KOTO_LEVEL_OWNER,
        "Полная системная панель мониторинга сервера и устройства.",
        nullptr
    },
    {
        "neofetch",
        &cmd_neofetch,
        KOTO_LEVEL_OWNER,
        "Красивый ASCII Neofetch с логотипом Kotogram и характеристиками системы.",
        nullptr
    }
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "sysinfo",
    "Комплексный мониторинг сервера и устройства: реальные CPU, RAM, Disk, Uptime и Neofetch.",
    "1.0.1",
    26,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    nullptr, 0,
    &on_callback,
    nullptr, 0,
    nullptr
};

} // namespace

extern "C" const koto_module* koto_module_register(void) {
    return &MODULE;
}
