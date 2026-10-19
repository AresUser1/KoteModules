// kotserv.cpp — Модуль информации о сервере и устройстве (C++ ABI 3).
// Портирован из Python-модуля kotserv.py (v2.0.3).
// Автор: Kote

#include "module_abi.h"
#include "koto_utils.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <iomanip>

namespace {

static constexpr long long ICON_LAPTOP = 5172685498350567983LL; // 💻

struct ServerStats {
    int cpu_cores;
    double cpu_load;
    long long ram_used_mb;
    long long ram_total_mb;
    double ram_percent;
    std::string kernel;
    std::string arch;
    std::string os_name;
    long long uptime_sec;
};

// Чтение реальных системных данных Linux / Android
ServerStats get_real_stats() {
    ServerStats s;
    s.cpu_cores = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    if (s.cpu_cores < 1) s.cpu_cores = 1;

    // CPU load из /proc/loadavg
    s.cpu_load = 0.0;
    std::ifstream la_file("/proc/loadavg");
    if (la_file.is_open()) {
        double l1 = 0.0, l5 = 0.0, l15 = 0.0;
        if (la_file >> l1 >> l5 >> l15) {
            s.cpu_load = (l1 / static_cast<double>(s.cpu_cores)) * 100.0;
            if (s.cpu_load > 100.0) s.cpu_load = 100.0;
            if (s.cpu_load < 0.0) s.cpu_load = 0.0;
        }
    }

    // RAM из /proc/meminfo
    s.ram_used_mb = 0;
    s.ram_total_mb = 0;
    s.ram_percent = 0.0;
    std::ifstream mem_file("/proc/meminfo");
    if (mem_file.is_open()) {
        std::string line;
        long long total_kb = 0, avail_kb = 0, free_kb = 0, cached_kb = 0, buffers_kb = 0;
        while (std::getline(mem_file, line)) {
            if (line.rfind("MemTotal:", 0) == 0) sscanf(line.c_str(), "MemTotal: %lld kB", &total_kb);
            else if (line.rfind("MemAvailable:", 0) == 0) sscanf(line.c_str(), "MemAvailable: %lld kB", &avail_kb);
            else if (line.rfind("MemFree:", 0) == 0) sscanf(line.c_str(), "MemFree: %lld kB", &free_kb);
            else if (line.rfind("Cached:", 0) == 0) sscanf(line.c_str(), "Cached: %lld kB", &cached_kb);
            else if (line.rfind("Buffers:", 0) == 0) sscanf(line.c_str(), "Buffers: %lld kB", &buffers_kb);
        }
        if (avail_kb == 0) avail_kb = free_kb + cached_kb + buffers_kb;
        s.ram_total_mb = total_kb / 1024;
        s.ram_used_mb = (total_kb - avail_kb) / 1024;
        if (s.ram_used_mb < 0) s.ram_used_mb = 0;
        if (s.ram_total_mb > 0) {
            s.ram_percent = static_cast<double>(s.ram_used_mb) * 100.0 / static_cast<double>(s.ram_total_mb);
        }
    }

    // Kernel & Arch
    struct utsname u;
    if (uname(&u) == 0) {
        s.kernel = u.release;
        s.arch = u.machine;
        s.os_name = u.sysname;
    } else {
        s.kernel = "Linux";
        s.arch = "aarch64";
        s.os_name = "Linux/Android";
    }

    // Uptime
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        s.uptime_sec = si.uptime;
    } else {
        s.uptime_sec = 0;
    }

    return s;
}

// Загрузка статистики (с проверкой на fake режим)
ServerStats load_stats(const koto_api* api, koto_ctx* ctx) {
    long long use_fake = koto_utils::db_get_int64(api, ctx, "use_fake", 0);
    if (!use_fake) {
        return get_real_stats();
    }

    ServerStats s;
    s.cpu_cores = static_cast<int>(koto_utils::db_get_int64(api, ctx, "fake_cpu", 8));
    int cmin = static_cast<int>(koto_utils::db_get_int64(api, ctx, "fake_cpu_min", 15));
    int cmax = static_cast<int>(koto_utils::db_get_int64(api, ctx, "fake_cpu_max", 45));
    if (cmax <= cmin) cmax = cmin + 1;
    s.cpu_load = cmin + (rand() % (cmax - cmin));

    s.ram_total_mb = koto_utils::db_get_int64(api, ctx, "fake_ram_total", 16384);
    int rmin = static_cast<int>(koto_utils::db_get_int64(api, ctx, "fake_ram_min", 25));
    int rmax = static_cast<int>(koto_utils::db_get_int64(api, ctx, "fake_ram_max", 50));
    if (rmax <= rmin) rmax = rmin + 1;
    s.ram_percent = rmin + (rand() % (rmax - rmin));
    s.ram_used_mb = static_cast<long long>(s.ram_total_mb * s.ram_percent / 100.0);

    s.kernel = koto_utils::db_get_str(api, ctx, "fake_kernel", "5.15.0-94-generic");
    s.arch = koto_utils::db_get_str(api, ctx, "fake_arch", "64bit");
    s.os_name = koto_utils::db_get_str(api, ctx, "fake_os", "Ubuntu 22.04.3 LTS");
    s.uptime_sec = 86400 * 5 + 3600 * 12;

    return s;
}

void render_stats(koto_ctx* ctx, const koto_api* api, const ServerStats& st) {
    api->c_reset(ctx);

    api->c_emoji(ctx, "💻", ICON_LAPTOP);
    api->c_fmt(ctx, " Информация о сервере:\n\n", KOTO_ENT_BOLD);

    // CPU
    api->c_emoji(ctx, "💻", ICON_LAPTOP);
    std::ostringstream cpu_oss;
    cpu_oss << " CPU: " << st.cpu_cores << " ядер(-ро) " << std::fixed << std::setprecision(1) << st.cpu_load << "%\n";
    api->c_fmt(ctx, cpu_oss.str().c_str(), KOTO_ENT_BOLD);

    // RAM
    api->c_emoji(ctx, "💻", ICON_LAPTOP);
    std::ostringstream ram_oss;
    ram_oss << " RAM: " << st.ram_used_mb << " / " << st.ram_total_mb << "MB (" << std::fixed << std::setprecision(1) << st.ram_percent << "%)\n\n";
    api->c_fmt(ctx, ram_oss.str().c_str(), KOTO_ENT_BOLD);

    // Kernel & Arch & OS
    api->c_emoji(ctx, "💻", ICON_LAPTOP);
    api->c_fmt(ctx, " Kernel: ", KOTO_ENT_BOLD);
    api->c_text(ctx, (st.kernel + "\n").c_str());

    api->c_emoji(ctx, "💻", ICON_LAPTOP);
    api->c_fmt(ctx, " Arch: ", KOTO_ENT_BOLD);
    api->c_text(ctx, (st.arch + "\n").c_str());

    api->c_emoji(ctx, "💻", ICON_LAPTOP);
    api->c_fmt(ctx, " OS: ", KOTO_ENT_BOLD);
    api->c_text(ctx, (st.os_name + "\n").c_str());

    // Uptime
    api->c_emoji(ctx, "💻", ICON_LAPTOP);
    api->c_fmt(ctx, " Uptime: ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, (koto_utils::format_duration(st.uptime_sec) + "\n").c_str(), KOTO_ENT_CODE);

    // Инлайн кнопки
    api->c_button(ctx, "🔄 Обновить", "ks_refresh", 0);
    api->c_button(ctx, "🚫 Закрыть", "ks_close", 0);
}

void cmd_serv(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) return;

    ServerStats st = load_stats(api, ctx);
    render_stats(ctx, api, st);

    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

void cmd_kscfg(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) return;

    const char* raw = api->cmd_args(ctx);
    std::string args_str = raw ? koto_utils::trim(raw) : "";

    if (args_str.empty()) {
        char pbuf[32] = ".";
        api->setting_get(ctx, "prefix", pbuf, sizeof(pbuf));
        std::string p = pbuf;

        api->c_reset(ctx);
        api->c_fmt(ctx, "⚙️ KOTserv Config\n\n", KOTO_ENT_BOLD);
        api->c_fmt(ctx, (p + "kscfg set use_fake <on|off>\n").c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, (p + "kscfg set fake_cpu <ядра>\n").c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, (p + "kscfg set fake_ram_total <MB>\n\n").c_str(), KOTO_ENT_CODE);
        api->c_fmt(ctx, "Текущий режим: ", KOTO_ENT_BOLD);
        long long fake = koto_utils::db_get_int64(api, ctx, "use_fake", 0);
        api->c_fmt(ctx, fake ? "Фейковые данные 🎭" : "Реальные данные ⚙️", KOTO_ENT_CODE);

        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    std::vector<std::string> parts = koto_utils::split(args_str, ' ');
    if (parts.size() >= 3 && parts[0] == "set") {
        std::string key = parts[1];
        std::string val = parts[2];

        if (key == "use_fake") {
            bool on = (val == "on" || val == "true" || val == "1");
            koto_utils::db_set_int64(api, ctx, "use_fake", on ? 1 : 0);
            api->c_reset(ctx);
            api->c_fmt(ctx, on ? "✅ Фейковый режим включен" : "✅ Реальный режим включен", KOTO_ENT_BOLD);
        } else if (key == "fake_cpu" || key == "fake_ram_total" || key == "fake_cpu_min" || key == "fake_cpu_max") {
            long long num = std::stoll(val);
            koto_utils::db_set_int64(api, ctx, key, num);
            api->c_reset(ctx);
            api->c_fmt(ctx, ("✅ " + key + " = " + val).c_str(), KOTO_ENT_BOLD);
        } else {
            koto_utils::db_set_str(api, ctx, key, val);
            api->c_reset(ctx);
            api->c_fmt(ctx, ("✅ " + key + " = " + val).c_str(), KOTO_ENT_BOLD);
        }

        if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
        else api->c_reply(ctx, 0);
        return;
    }

    api->c_reset(ctx);
    api->c_text(ctx, "❌ Неверные параметры команды.");
    if (api->is_outgoing(ctx)) api->c_edit(ctx, 0);
    else api->c_reply(ctx, 0);
}

void on_callback(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) {
        if (api->cb_answer) api->cb_answer(ctx, "❌ Доступ запрещен! Только для владельца.", 1);
        return;
    }

    const char* data = api->cb_data(ctx);
    if (!data) return;
    std::string d = data;

    if (d == "ks_close") {
        if (api->cb_answer) api->cb_answer(ctx, "Закрыто", 0);
        api->delete_msg(ctx);
        return;
    }

    if (d == "ks_refresh") {
        if (api->cb_answer) api->cb_answer(ctx, "Обновлено", 0);
        ServerStats st = load_stats(api, ctx);
        render_stats(ctx, api, st);
        api->c_edit(ctx, 0);
        return;
    }
}

const koto_command COMMANDS[] = {
    {
        "serv",
        &cmd_serv,
        KOTO_LEVEL_OWNER,
        "Показывает нагрузку CPU, RAM и системную информацию (Инлайн).",
        nullptr
    },
    {
        "kscfg",
        &cmd_kscfg,
        KOTO_LEVEL_OWNER,
        "Настройка параметров отображения модуля kotserv.",
        "[set <key> <val>]"
    }
};

const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "kotserv",
    "Информация о сервере, нагрузке CPU/RAM и ОС с инлайн кнопками (C++ ABI 3).",
    "2.0.3",
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
