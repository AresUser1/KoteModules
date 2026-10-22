// sysinfo.cpp — system and host-process metrics (C ABI 3, host 63+).
#include "module_abi.h"
#include "koto_utils.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>
#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

namespace {
using Number = std::optional<double>;
using FileReader = std::function<std::optional<std::string>(const std::string&)>;
constexpr double MiB=1024.0*1024.0, GiB=1024.0*1024.0*1024.0;
constexpr long long ICON_LAPTOP=5172685498350567983LL;
struct SysMetrics {
    std::string hostname="недоступно",os_name="недоступно",kernel="недоступно",arch="недоступно";
    std::string cpu_model="недоступно",disk_path,process_scope="Процесс хоста";
    std::string network_scope="Сумма интерфейсов, кроме lo (с загрузки устройства)";
    std::optional<long long> uptime_sec;
    std::optional<int> cpu_online,cpu_configured,pid,uid,threads;
    Number cpu_percent,load_1m,load_5m,load_15m;
    Number ram_total,ram_available,ram_free,ram_cached,ram_used,ram_percent;
    bool ram_available_estimated=false;
    Number swap_total,swap_used,swap_percent;
    Number disk_total,disk_used,disk_available,disk_reserved,disk_percent;
    Number rss,cpu_time_ms,net_rx,net_tx;
};
std::optional<std::string> read_text(const std::string& path) {
    std::ifstream in(path);
    if(!in) return {};
    std::ostringstream content;content<<in.rdbuf();
    if(in.bad()) return {};
    return content.str();
}
std::string property(const char* key) {
#if defined(__ANDROID__)
    char value[PROP_VALUE_MAX]={};
    if(__system_property_get(key,value)>0) return value;
#endif
    return {};
}
std::string quantity(Number value,double divisor=1,int precision=1,const char* unit="") {
    if(!value || !std::isfinite(*value) || *value<0) return "недоступно";
    std::ostringstream out;out<<std::fixed<<std::setprecision(precision)<<(*value/divisor)<<unit;return out.str();
}
std::string integer(std::optional<int> value) {return value?std::to_string(*value):"недоступно";}
std::string bar(double value) {
    int filled=static_cast<int>(std::round(std::clamp(value,0.0,100.0)/10));
    std::string out;for(int i=0;i<10;++i)out+=i<filled?"▰":"▱";return out;
}
std::string uptime(std::optional<long long> value) {
    if(!value) return "недоступно";
    auto sec=*value;std::ostringstream out;
    if(sec>=86400)out<<sec/86400<<" дн. ";
    if(sec>=3600)out<<(sec/3600)%24<<" ч. ";
    out<<(sec/60)%60<<" мин. "<<sec%60<<" сек.";return out.str();
}
void parse_load(const std::string& text,SysMetrics& m) {
    std::istringstream in(text);double a,b,c;
    if(in>>a>>b>>c && std::isfinite(a)&&std::isfinite(b)&&std::isfinite(c)&&a>=0&&b>=0&&c>=0) {
        m.load_1m=a;m.load_5m=b;m.load_15m=c; // Real idle zero is valid.
    }
}
void parse_memory(const std::string& text,SysMetrics& m) {
    std::istringstream in(text);std::string line;
    Number total,available,free,cached,buffers,reclaimable,shmem,swap_total,swap_free;
    while(std::getline(in,line)) {
        std::istringstream row(line);std::string key,unit;double value;
        if(!(row>>key>>value>>unit)||unit!="kB"||!std::isfinite(value)||value<0)continue;
        value*=1024;
        if(key=="MemTotal:")total=value;else if(key=="MemAvailable:")available=value;
        else if(key=="MemFree:")free=value;else if(key=="Cached:")cached=value;
        else if(key=="Buffers:")buffers=value;else if(key=="SReclaimable:")reclaimable=value;
        else if(key=="Shmem:")shmem=value;else if(key=="SwapTotal:")swap_total=value;
        else if(key=="SwapFree:")swap_free=value;
    }
    if(total && *total>0) {
        m.ram_total=total;m.ram_free=free;m.ram_cached=cached;
        if(!available && free && cached && buffers) {
            available=std::max(0.0,*free+*cached+*buffers+reclaimable.value_or(0)-shmem.value_or(0));
            m.ram_available_estimated=true;
        }
        if(available && *available<=*total) {
            m.ram_available=available;m.ram_used=*total-*available;
            m.ram_percent=*m.ram_used*100.0/ *total;
        }
    }
    if(swap_total && swap_free && *swap_free<=*swap_total) {
        m.swap_total=swap_total;m.swap_used=*swap_total-*swap_free;
        if(*swap_total>0)m.swap_percent=*m.swap_used*100/ *swap_total;
    }
}
void parse_process(const std::string& text,SysMetrics& m) {
    std::istringstream in(text);std::string line;
    while(std::getline(in,line)) {
        std::istringstream row(line);std::string key,unit;long long value;
        if(!(row>>key>>value)||value<0)continue;
        if(key=="VmRSS:" && row>>unit && unit=="kB")m.rss=double(value)*1024;
        if(key=="Threads:" && value>0 && value<=std::numeric_limits<int>::max())m.threads=static_cast<int>(value);
    }
}
void parse_network(const std::string& text,SysMetrics& m) {
    std::istringstream in(text);std::string line;long double rx_total=0,tx_total=0;bool valid=false;
    while(std::getline(in,line)) {
        auto colon=line.find(':');if(colon==std::string::npos)continue;
        auto iface=koto_utils::trim(line.substr(0,colon));
        std::istringstream row(line.substr(colon+1));long long fields[16];bool ok=!iface.empty();
        for(auto& field:fields)if(!(row>>field)||field<0){ok=false;break;}
        if(!ok)continue;valid=true;
        if(iface!="lo"){rx_total+=fields[0];tx_total+=fields[8];}
    }
    if(valid){m.net_rx=double(rx_total);m.net_tx=double(tx_total);}
}
struct CpuTicks {long double total,idle;};
std::optional<CpuTicks> parse_cpu(const std::string& text) {
    std::istringstream in(text);std::string label;long long ticks[8];
    if(!(in>>label)||label!="cpu")return {};
    // guest and guest_nice are already included in user/nice; do not add twice.
    for(auto& value:ticks)if(!(in>>value)||value<0)return {};
    long double sum=0;for(auto value:ticks)sum+=value;
    return CpuTicks{sum,static_cast<long double>(ticks[3])+ticks[4]};
}
Number cpu_usage(const CpuTicks& a,const CpuTicks& b) {
    auto total=b.total-a.total,idle=b.idle-a.idle;
    if(total<=0 || idle<0 || idle>total)return {};
    return double((total-idle)*100/total);
}
Number json_counter(const nlohmann::json& j,const char* key) {
    auto it=j.find(key);if(it==j.end() || !it->is_number_integer())return {};
    double value=it->get<double>();if(!std::isfinite(value)||value<0)return {};return value;
}
bool apply_host_metrics(const std::string& text,SysMetrics& m) {
    auto j=nlohmann::json::parse(text,nullptr,false);
    if(!j.is_object()||!j.contains("schema")||j["schema"]!=1)return false;
    m.process_scope="Процесс Kotogram (хост)";
    m.network_scope="Трафик приложения (UID, с загрузки устройства)";
    m.rss=json_counter(j,"process_rss_bytes");m.cpu_time_ms=json_counter(j,"process_cpu_time_ms");
    m.net_rx=json_counter(j,"app_rx_bytes");m.net_tx=json_counter(j,"app_tx_bytes");
    auto int_counter=[&](const char* name)->std::optional<int>{auto n=json_counter(j,name);return n&&*n>0&&*n<=std::numeric_limits<int>::max()?std::optional<int>(static_cast<int>(*n)):std::nullopt;};
    m.pid=int_counter("process_pid");m.uid=int_counter("app_uid");m.threads=int_counter("process_threads");
    return true;
}
SysMetrics collect_metrics(koto_ctx* ctx,const koto_api* api,const FileReader& read=read_text) {
    SysMetrics m;
#if defined(__ANDROID__)
    m.process_scope="Процесс модуля";
    m.network_scope="Трафик приложения (UID, с загрузки устройства)";
#endif
    char hostname[256]={};if(gethostname(hostname,sizeof(hostname)-1)==0)m.hostname=hostname;
    struct utsname un{};if(uname(&un)==0){m.kernel=un.release;m.arch=un.machine;}
    auto android=property("ro.build.version.release");
    if(!android.empty()) {
        auto brand=property("ro.product.brand"),model=property("ro.product.model");
        m.os_name="Android "+android+" ("+brand+" "+model+")";
    } else if(auto file=read("/etc/os-release")) {
        std::istringstream in(*file);std::string line;
        while(std::getline(in,line))if(line.rfind("PRETTY_NAME=",0)==0){m.os_name=line.substr(12);if(m.os_name.size()>=2&&m.os_name.front()=='"'&&m.os_name.back()=='"')m.os_name=m.os_name.substr(1,m.os_name.size()-2);break;}
    }
    if(auto file=read("/proc/uptime")){std::istringstream in(*file);double sec;if(in>>sec&&std::isfinite(sec)&&sec>=0&&sec<double(std::numeric_limits<long long>::max()))m.uptime_sec=static_cast<long long>(sec);}
    if(!m.uptime_sec){struct sysinfo si{};if(sysinfo(&si)==0&&si.uptime>=0)m.uptime_sec=si.uptime;}
    auto online=sysconf(_SC_NPROCESSORS_ONLN),configured=sysconf(_SC_NPROCESSORS_CONF);
    if(online>0&&online<=4096)m.cpu_online=static_cast<int>(online);
    if(configured>0&&configured<=4096)m.cpu_configured=static_cast<int>(configured);
    auto model=property("ro.soc.model");if(model.empty())model=property("ro.board.platform");
    auto manufacturer=property("ro.soc.manufacturer");
    if(!model.empty())m.cpu_model=(manufacturer.empty()?"":manufacturer+" ")+model;
    else if(auto file=read("/proc/cpuinfo")) {
        std::istringstream in(*file);std::string line;
        while(std::getline(in,line))if(line.rfind("model name",0)==0||line.rfind("Hardware",0)==0){auto p=line.find(':');if(p!=std::string::npos)m.cpu_model=koto_utils::trim(line.substr(p+1));break;}
    }
    long long max_khz=0;
    for(int i=0;i<m.cpu_configured.value_or(m.cpu_online.value_or(1));++i) {
        auto base="/sys/devices/system/cpu/cpu"+std::to_string(i)+"/cpufreq/";
        auto file=read(base+"cpuinfo_max_freq");if(!file)file=read(base+"scaling_max_freq");
        if(file){std::istringstream in(*file);long long n;if(in>>n&&n>max_khz)max_khz=n;}
    }
    if(max_khz>0)m.cpu_model+=" (макс. "+quantity(double(max_khz),1000000,2," GHz")+")";
    if(auto file=read("/proc/loadavg"))parse_load(*file,m);
    if(auto file=read("/proc/stat"))if(auto first=parse_cpu(*file)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        if(auto next=read("/proc/stat"))if(auto second=parse_cpu(*next))m.cpu_percent=cpu_usage(*first,*second);
    }
    if(auto file=read("/proc/meminfo"))parse_memory(*file,m);
    // sysinfo is available even when the textual proc file is blocked.
    if(!m.ram_total) {
        struct sysinfo si{};if(sysinfo(&si)==0&&si.totalram>0) {
            m.ram_total=double(si.totalram)*si.mem_unit;m.ram_free=double(si.freeram)*si.mem_unit;
            // freeram alone is not MemAvailable; do not fabricate used/available.
            m.swap_total=double(si.totalswap)*si.mem_unit;
            if(si.freeswap<=si.totalswap)m.swap_used=double(si.totalswap-si.freeswap)*si.mem_unit;
        }
    }
    char cache[4096]={};std::string path;
    if(api&&api->cache_dir&&api->cache_dir(ctx,cache,sizeof(cache))>0)path=cache;
    struct statvfs sv{};
    for(const auto& candidate:{path,std::string("/data"),std::string("/")}) {
        if(candidate.empty()||statvfs(candidate.c_str(),&sv)!=0||sv.f_blocks==0)continue;
        double size=sv.f_frsize?sv.f_frsize:sv.f_bsize;
        if(sv.f_bfree>sv.f_blocks||sv.f_bavail>sv.f_bfree)continue;
        m.disk_path=candidate;m.disk_total=double(sv.f_blocks)*size;
        m.disk_used=double(sv.f_blocks-sv.f_bfree)*size;m.disk_available=double(sv.f_bavail)*size;
        m.disk_reserved=double(sv.f_bfree-sv.f_bavail)*size;m.disk_percent=*m.disk_used*100/ *m.disk_total;break;
    }
    m.pid=getpid();if(auto file=read("/proc/self/status"))parse_process(*file,m);
#if !defined(__ANDROID__)
    if(auto file=read("/proc/net/dev"))parse_network(*file,m);
#endif
    if(api && api->host_version && api->host_version(ctx)>=63 && api->system_metrics_json) {
        char json[8192]={};int n=api->system_metrics_json(ctx,json,sizeof(json));
        if(n>0&&n<int(sizeof(json)))apply_host_metrics(std::string(json,n),m);
    }
    return m;
}
std::string load_text(const SysMetrics& m) {
    if(!m.load_1m || !m.load_5m || !m.load_15m)return "недоступно (нет данных /proc/loadavg)";
    return quantity(m.load_1m,1,2)+", "+quantity(m.load_5m,1,2)+", "+quantity(m.load_15m,1,2);
}
std::string memory_text(const SysMetrics& m,bool details=false) {
    std::ostringstream out;
    if(m.ram_percent)out<<bar(*m.ram_percent)<<" "<<quantity(m.ram_percent,1,1,"%")<<"\n";
    out<<"Использовано / всего: "<<quantity(m.ram_used,MiB,0)<<" / "<<quantity(m.ram_total,MiB,0)<<" МиБ\n"
       <<"Доступно"<<(m.ram_available_estimated?" (оценка)":"")<<": "<<quantity(m.ram_available,MiB,0," МиБ")<<"\n";
    if(details)out<<"Свободно без кеша: "<<quantity(m.ram_free,MiB,0," МиБ")<<"\nКеш: "<<quantity(m.ram_cached,MiB,0," МиБ")<<"\n";
    out<<"Swap: "<<quantity(m.swap_used,MiB,0)<<" / "<<quantity(m.swap_total,MiB,0)<<" МиБ\n";return out.str();
}
std::string disk_text(const SysMetrics& m) {
    std::ostringstream out;
    if(m.disk_percent)out<<bar(*m.disk_percent)<<" "<<quantity(m.disk_percent,1,1,"%")<<"\n";
    out<<"Использовано / всего: "<<quantity(m.disk_used,GiB,2)<<" / "<<quantity(m.disk_total,GiB,2)<<" ГиБ\n"
       <<"Доступно приложению: "<<quantity(m.disk_available,GiB,2," ГиБ")<<"\n";
    if(m.disk_reserved&&*m.disk_reserved>0)out<<"Резерв файловой системы: "<<quantity(m.disk_reserved,GiB,2," ГиБ")<<"\n";
    return out.str();
}
std::string process_text(const SysMetrics& m) {
    return "PID: "+integer(m.pid)+"\nRSS: "+quantity(m.rss,MiB,1," МиБ")+"\nПотоков: "+integer(m.threads)+
        "\nCPU-время процесса: "+quantity(m.cpu_time_ms,1000,2," с")+"\n";
}
std::string network_text(const SysMetrics& m) {
    return m.network_scope+"\nПолучено: "+quantity(m.net_rx,MiB,2," МиБ")+"\nОтправлено: "+quantity(m.net_tx,MiB,2," МиБ")+"\n";
}
void render_panel(koto_ctx* ctx,const koto_api* api,const SysMetrics& m,bool details=false) {
    api->c_reset(ctx);api->c_emoji(ctx,"💻",ICON_LAPTOP);
    api->c_fmt(ctx,details?" Детальные метрики Kotogram\n\n":" Системная информация Kotogram\n\n",KOTO_ENT_BOLD);
    std::ostringstream out;
    out<<"⚙️ Система и устройство\nХост: "<<m.hostname<<"\nОС: "<<m.os_name<<"\nЯдро: "<<m.kernel<<" ("<<m.arch<<")\nАптайм устройства: "<<uptime(m.uptime_sec)<<"\n\n"
       <<"🚀 CPU\nМодель: "<<m.cpu_model<<"\nЯдер онлайн / настроено: "<<integer(m.cpu_online)<<" / "<<integer(m.cpu_configured)<<"\n"
       <<"Загрузка CPU (замер): "<<quantity(m.cpu_percent,1,1,"%")<<"\n"
       <<"Load Average (1/5/15 мин): "<<load_text(m)<<"\n\n"
       <<"📊 RAM\n"<<memory_text(m,details)<<"\n"
       <<"💾 Накопитель"<<(details&&!m.disk_path.empty()?" ("+m.disk_path+")":"")<<"\n"<<disk_text(m)<<"\n"
       <<"🐾 "<<m.process_scope<<"\n"<<process_text(m)<<"\n🌐 "<<network_text(m);
    api->c_text(ctx,out.str().c_str());
    api->c_button(ctx,"🔄 Обновить",details?"sys_details":"sys_refresh",0);
    api->c_button(ctx,details?"⬅️ Назад":"📊 Подробно",details?"sys_main":"sys_details",0);
    api->c_button(ctx,"🚫 Закрыть","sys_close",1);
}
void render_neofetch(koto_ctx* ctx,const koto_api* api,const SysMetrics& m) {
    api->c_reset(ctx);std::ostringstream out;
    out<<"   /\\_/\\     Kotogram\n  ( o.o )\n   > ^ <\n"
       <<"OS: "<<m.os_name<<"\nHost: "<<m.hostname<<"\nKernel: "<<m.kernel<<"\nArch: "<<m.arch
       <<"\nUptime: "<<uptime(m.uptime_sec)<<"\nCPU: "<<m.cpu_model<<"\nLoad Average: "<<load_text(m)<<"\n\nRAM\n"<<memory_text(m)
       <<"\nDisk\n"<<disk_text(m)<<"\n"<<m.process_scope<<"\n"<<process_text(m)<<"\n"<<network_text(m);
    api->c_fmt(ctx,out.str().c_str(),KOTO_ENT_CODE);
    api->c_button(ctx,"🔄 Обновить","sys_neofetch",0);api->c_button(ctx,"💻 Панель","sys_main",0);api->c_button(ctx,"🚫 Закрыть","sys_close",1);
}
void cmd_sys(koto_ctx* ctx,const koto_api* api) {
    if(!api->check_permission(ctx,KOTO_LEVEL_OWNER))return;
    render_panel(ctx,api,collect_metrics(ctx,api));if(api->is_outgoing(ctx))api->c_edit(ctx,0);else api->c_reply(ctx,0);
}
void cmd_neofetch(koto_ctx* ctx,const koto_api* api) {
    if(!api->check_permission(ctx,KOTO_LEVEL_OWNER))return;
    render_neofetch(ctx,api,collect_metrics(ctx,api));if(api->is_outgoing(ctx))api->c_edit(ctx,0);else api->c_reply(ctx,0);
}
void on_callback(koto_ctx* ctx,const koto_api* api) {
    const char* raw=api->cb_data?api->cb_data(ctx):nullptr;
    if(!raw || (strncmp(raw, "sys_", 4) != 0 && strncmp(raw, "sysinfo:", 8) != 0)) return;
    if(!api->check_permission(ctx,KOTO_LEVEL_OWNER)){if(api->cb_answer)api->cb_answer(ctx,"❌ Только для владельца",1);return;}
    std::string data=raw;
    if(data=="sys_close"){if(api->cb_answer)api->cb_answer(ctx,"Закрыто",0);if(api->delete_msg)api->delete_msg(ctx);return;}
    if(data!="sys_refresh"&&data!="sys_main"&&data!="sys_details"&&data!="sys_neofetch")return;
    auto m=collect_metrics(ctx,api);
    if(data=="sys_neofetch")render_neofetch(ctx,api,m);else render_panel(ctx,api,m,data=="sys_details");
    if(api->cb_answer)api->cb_answer(ctx,"Данные обновлены",0);api->c_edit(ctx,0);
}
const koto_command COMMANDS[]={
    {"sys",cmd_sys,KOTO_LEVEL_OWNER,"Метрики устройства и процесса хоста; недоступные данные отмечены явно.",nullptr},
    {"sysinfo",cmd_sys,KOTO_LEVEL_OWNER,"Система, RAM, накопитель и трафик приложения.",nullptr},
    {"neofetch",cmd_neofetch,KOTO_LEVEL_OWNER,"Характеристики устройства с ASCII-котиком.",nullptr}
};
const koto_module MODULE={KOTO_MODULE_ABI,"sysinfo","Метрики устройства и хоста; Android TrafficStats и явная доступность данных.","1.1.1",63,0,COMMANDS,3,nullptr,0,on_callback,nullptr,0,"https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/sysinfo.so"};
}
extern "C" const koto_module* koto_module_register(){return &MODULE;}
