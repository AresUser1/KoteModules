#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
struct koto_ctx { std::string text;int sent=0;bool owner=true; };
#include "../src/sysinfo.cpp"
static int checks=0;
#define CHECK(x) do { ++checks; if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);} } while(0)
int main() {
    CHECK(std::string(koto_module_register()->version)=="1.1.1" && MODULE.min_host_version==63);
    SysMetrics missing;
    CHECK(!missing.net_rx && !missing.load_1m && !missing.ram_used && !missing.rss);
    parse_load("0.00 0.00 0.00 1/100 42",missing);
    CHECK(missing.load_1m && *missing.load_1m==0 && load_text(missing)=="0.00, 0.00, 0.00");
    SysMetrics bad;parse_load("-1 0 0",bad);CHECK(!bad.load_1m);parse_load("invalid",bad);CHECK(!bad.load_1m);
    SysMetrics mem;parse_memory("MemTotal: 10240 kB\nMemAvailable: 0 kB\nMemFree: 100 kB\nCached: 200 kB\nBuffers: 30 kB\nSwapTotal: 0 kB\nSwapFree: 0 kB\n",mem);
    CHECK(mem.ram_available && *mem.ram_available==0 && !mem.ram_available_estimated);
    CHECK(mem.ram_used==mem.ram_total && mem.ram_percent==100);
    CHECK(mem.swap_total && *mem.swap_total==0 && mem.swap_used==0);
    SysMetrics estimate;parse_memory("MemTotal: 10240 kB\nMemFree: 100 kB\nCached: 200 kB\nBuffers: 30 kB\nSReclaimable: 40 kB\nShmem: 50 kB\n",estimate);
    CHECK(estimate.ram_available==320*1024 && estimate.ram_available_estimated);
    CHECK(memory_text(estimate).find("оценка")!=std::string::npos);
    SysMetrics partial;parse_memory("MemTotal: 10240 kB\nMemFree: 100 kB\n",partial);
    CHECK(partial.ram_total && !partial.ram_used && !partial.ram_available);
    SysMetrics invalid;parse_memory("MemTotal: -100 kB\nMemAvailable: nope kB\n",invalid);CHECK(!invalid.ram_total);
    parse_memory("MemTotal: 100 kB\nMemAvailable: 1000 kB\nSwapTotal: 100 kB\nSwapFree: 101 kB\n",invalid);
    CHECK(!invalid.ram_used && !invalid.swap_used);
    SysMetrics process;parse_process("VmRSS: 2048 kB\nThreads: 17\n",process);CHECK(process.rss==2*MiB && process.threads==17);
    SysMetrics malformed;parse_process("VmRSS: -1 kB\nThreads: invalid\n",malformed);CHECK(!malformed.rss && !malformed.threads);
    SysMetrics net;parse_network("eth0: 1048576 1 0 0 0 0 0 0 2097152 2 0 0 0 0 0 0\nlo: 999 1 0 0 0 0 0 0 999 1 0 0 0 0 0 0",net);
    CHECK(net.net_rx==MiB && net.net_tx==2*MiB);
    SysMetrics zero;parse_network("eth0: 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",zero);CHECK(zero.net_rx && *zero.net_rx==0);
    SysMetrics empty;parse_network("",empty);CHECK(!empty.net_rx);parse_network("eth0: -1 0",empty);CHECK(!empty.net_rx);
    CHECK(!parse_cpu("not cpu"));CHECK(!parse_cpu("cpu 1 2 3"));CHECK(!parse_cpu("cpu 1 2 3 -4 0 0 0 0"));
    auto a=parse_cpu("cpu 100 0 50 500 10 0 0 0 999 999");auto b=parse_cpu("cpu 120 0 60 570 10 0 0 0 999 999");
    CHECK(a && b && a->total==660 && b->total==760);
    CHECK(cpu_usage(*a,*b)==30);CHECK(!cpu_usage(*a,*a));CHECK(!cpu_usage(*b,*a));
    SysMetrics host;
    CHECK(apply_host_metrics(R"({"schema":1,"app_rx_bytes":0,"app_tx_bytes":2097152,"process_rss_bytes":128974848,"process_threads":27,"process_cpu_time_ms":1500,"process_pid":777,"app_uid":10001})",host));
    CHECK(host.net_rx && *host.net_rx==0 && host.net_tx==2*MiB);
    CHECK(host.rss==123*MiB && host.threads==27 && host.pid==777 && host.uid==10001);
    CHECK(host.process_scope.find("Kotogram")!=std::string::npos && host.network_scope.find("UID")!=std::string::npos);
    CHECK(apply_host_metrics(R"({"schema":1,"app_rx_bytes":-1,"app_tx_bytes":null,"process_rss_bytes":"wrong"})",host));
    CHECK(!host.net_rx && !host.net_tx && !host.rss && !host.threads); // No stale counters or sandbox fallback.
    CHECK(!apply_host_metrics("{",host));CHECK(!apply_host_metrics(R"({"schema":2})",host));
    koto_api api{};koto_ctx ctx;
    api.c_reset=[](koto_ctx* c){c->text.clear();};
    api.c_text=[](koto_ctx* c,const char* s){c->text+=s;};api.c_fmt=[](koto_ctx* c,const char* s,int){c->text+=s;};
    api.c_emoji=[](koto_ctx* c,const char* s,long long){c->text+=s;};api.c_button=[](koto_ctx*,const char*,const char*,int){};
    api.c_edit=[](koto_ctx* c,int){++c->sent;};api.c_reply=[](koto_ctx* c,int){++c->sent;};
    api.check_permission=[](koto_ctx* c,int){return int(c->owner);};api.is_outgoing=[](koto_ctx*){return 1;};
    for(bool details:{false,true}) {
        render_panel(&ctx,&api,SysMetrics{},details);
        CHECK(ctx.text.find("Получено: недоступно")!=std::string::npos);
        CHECK(ctx.text.find("Load Average (1/5/15 мин): недоступно")!=std::string::npos);
        CHECK(ctx.text.find("0.00, 0.00, 0.00")==std::string::npos);
        CHECK(ctx.text.find("SELinux")==std::string::npos);
        CHECK(ctx.text.find("Загрузка CPU (замер): недоступно")!=std::string::npos);
    }
    render_neofetch(&ctx,&api,SysMetrics{});CHECK(ctx.text.find("Получено: недоступно")!=std::string::npos);
    ctx.owner=false;auto before=ctx.text;cmd_sys(&ctx,&api);cmd_neofetch(&ctx,&api);CHECK(ctx.sent==0 && ctx.text==before);
    api.host_version=[](koto_ctx*){return 63;};
    api.system_metrics_json=[](koto_ctx*,char* buf,int size){const char* json=R"({"schema":1,"app_rx_bytes":0,"app_tx_bytes":0,"process_pid":123,"process_rss_bytes":1048576})";std::snprintf(buf,size,"%s",json);return int(std::strlen(json));};
    FileReader inaccessible=[](const std::string&)->std::optional<std::string>{return {};};
    auto collected=collect_metrics(&ctx,&api,inaccessible);CHECK(collected.rss==MiB && collected.pid==123 && collected.net_rx==0);
    CHECK(!collected.load_1m && !collected.cpu_percent);
    api.host_version=[](koto_ctx*){return 62;};
    api.system_metrics_json=[](koto_ctx*,char*,int)->int{std::abort();};
    (void)collect_metrics(&ctx,&api,inaccessible); // Never read an appended API slot on an older host.
    std::printf("Sysinfo: %d checks passed\n",checks);
}
