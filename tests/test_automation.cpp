#include "module_abi.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <map>
#include <string>
#include <vector>
struct Read {long long chat,id;int kind;};
struct koto_ctx {
    std::map<std::string,std::string> db;
    std::string args,text,twins,twins_json="[]",callback;
    bool trusted=true;
    int outputs=0;
    std::vector<Read> reads;
    long long chat=2486593427LL,me=1;
    int type=KOTO_CHAT_CHANNEL;
    bool outgoing=false;
};
static int checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(int argc,char** argv) {
    CHECK(argc==3);auto first=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);CHECK(first);
    auto second=dlopen(argv[2],RTLD_NOW|RTLD_LOCAL);CHECK(second);
    auto load=[](void* handle){auto fn=reinterpret_cast<const koto_module*(*)()>(dlsym(handle,"koto_module_register"));assert(fn);return fn();};
    auto manager=load(first);auto chat=load(second);
    CHECK(std::string(manager->version)=="1.2.5" && manager->min_host_version==64);
    CHECK(std::string(chat->version)=="1.0.6" && chat->min_host_version==61);
    CHECK((manager->watch_flags & KOTO_WATCH_UNREAD_SYNC) != 0);
    CHECK((manager->flags & KOTO_MOD_TWINS) != 0); // Android router requires this before calling the watcher.
    koto_api api{};
    api.check_permission=[](koto_ctx* c,int){return int(c->trusted);};
    api.is_outgoing=[](koto_ctx* c){return int(c->outgoing);};
    api.chat_id=[](koto_ctx* c){return c->chat;};api.get_me=[](koto_ctx* c){return c->me;};
    api.chat_type=[](koto_ctx* c){return c->type;};
    api.msg_id=[](koto_ctx*)->long long{return 10;};
    api.cmd_args=[](koto_ctx* c){return c->args.c_str();};
    api.db_get=[](koto_ctx* c,const char* key,char* buf,int cap){auto it=c->db.find(key);if(it==c->db.end())return -1;auto n=std::min(it->second.size(),size_t(cap>0?cap-1:0));if(cap>0){std::memcpy(buf,it->second.data(),n);buf[n]=0;}return int(it->second.size());};
    api.db_set=[](koto_ctx* c,const char* k,const char* v){c->db[k]=v;};
    api.c_reset=[](koto_ctx* c){c->text.clear();};api.c_text=[](koto_ctx* c,const char* s){c->text+=s;};
    api.c_fmt=[](koto_ctx* c,const char* s,int){c->text+=s;};
    api.c_emoji=[](koto_ctx* c,const char* s,long long){c->text+=s;};
    api.c_edit=[](koto_ctx* c,int){++c->outputs;};api.c_reply=[](koto_ctx* c,int){++c->outputs;};
    api.read_history=[](koto_ctx* c,long long cid,long long mid){c->reads.push_back({cid,mid,50});};
    api.read_mentions=[](koto_ctx* c,long long cid){c->reads.push_back({cid,0,51});};
    api.twin_list=[](koto_ctx* c,char* b,int n){std::snprintf(b,n,"%s",c->twins.c_str());return int(c->twins.size());};
    api.get_twins_json=[](koto_ctx* c,char* b,int n){std::snprintf(b,n,"%s",c->twins_json.c_str());return int(c->twins_json.size());};
    api.c_button=[](koto_ctx* c,const char*,const char*,int){++c->outputs;};
    api.cb_data=[](koto_ctx* c){return c->callback.c_str();};
    api.cb_answer=[](koto_ctx* c,const char*,int){++c->outputs;};
    api.delete_msg=[](koto_ctx* c){++c->outputs;};
    api.kick_user=[](koto_ctx* c,long long cid,long long uid){c->reads.push_back({cid,uid,42});};
    auto command=[&](const koto_module* module,koto_ctx& c,const char* name,const char* args=""){
        c.args=args;c.reads.clear();auto begin=module->commands,end=begin+module->command_count;
        auto it=std::find_if(begin,end,[&](const auto& item){return std::string(item.name)==name;});CHECK(it!=end);it->handler(&c,&api);
    };
    auto main_command=[&](koto_ctx& c,const char* name,const char* args=""){
        auto account=c.me;c.me=1;command(manager,c,name,args);c.me=account;
    };
    auto watch=[&](koto_ctx& c){c.reads.clear();manager->on_watch(&c,&api);};
    {
        koto_ctx c;command(manager,c,"autread","on");watch(c);CHECK(c.reads.size()==2 && c.reads[0].chat==2486593427LL);
        command(manager,c,"autread","off this");CHECK(c.db["autoread_excluded"].find("-1002486593427")!=std::string::npos);
        watch(c);CHECK(c.reads.empty());
        command(manager,c,"autreadlist");CHECK(c.text.find("Исключения")!=std::string::npos && c.text.find("-1002486593427")!=std::string::npos);
        c.chat=5422122659LL;c.type=KOTO_CHAT_GROUP;watch(c);CHECK(c.reads.size()==2);
        command(manager,c,"autread","off this");watch(c);CHECK(c.reads.empty());
        c.type=KOTO_CHAT_PRIVATE;watch(c);CHECK(c.reads.size()==2); // same number, distinct peer domain
        c.type=KOTO_CHAT_GROUP;command(manager,c,"autread","on this");watch(c);CHECK(c.reads.size()==2);
        command(manager,c,"autread","off");watch(c);CHECK(c.reads.empty());
        c.chat=2486593427LL;c.type=KOTO_CHAT_CHANNEL;command(manager,c,"autread","on");watch(c);CHECK(c.reads.empty()); // exclusions survive
        command(manager,c,"autread","on this");watch(c);CHECK(c.reads.size()==2);
    }
    {
        koto_ctx c;c.db["autoread_chats"]="0\n";c.chat=999;c.type=KOTO_CHAT_PRIVATE;
        command(manager,c,"autread","off -1002486593427");c.chat=2486593427LL;c.type=KOTO_CHAT_CHANNEL;watch(c);CHECK(c.reads.empty());
        c.chat=999;c.type=KOTO_CHAT_PRIVATE;command(manager,c,"autread","off -5422122659");
        c.chat=-5422122659LL;c.type=KOTO_CHAT_GROUP;watch(c);CHECK(c.reads.empty());
    }
    for(const char* args:{"","off this garbage","on nope","off -100bad","off 0","on this extra"}) {
        koto_ctx c;c.db["autoread_chats"]="0\n";auto before=c.db;command(manager,c,"autread",args);CHECK(c.db==before);CHECK(c.text.find("Использование")!=std::string::npos);
    }
    {
        koto_ctx c;c.db["autoread_chats"]="2486593427\n";watch(c);CHECK(c.reads.size()==2);
        command(manager,c,"autread","off this");watch(c);CHECK(c.reads.empty());
        CHECK(c.db["autoread_chats"].find("2486593427")==std::string::npos);
    }
    {
        koto_ctx c;c.db["autoread_chats"]="0\n";c.outgoing=true;watch(c);CHECK(c.reads.empty());
        c.outgoing=false;c.type=KOTO_CHAT_UNKNOWN;watch(c);CHECK(c.reads.empty());
    }
    {
        koto_ctx c;command(manager,c,"autreadnow");CHECK(c.reads.size()==2 && c.reads[0].kind==50 && c.reads[1].kind==51);
    }
    {
        koto_ctx c;c.twins="1877549535\tTwin\t+000\n";
        command(manager,c,"amtwins");CHECK(c.text.find("1877549535")!=std::string::npos && c.text.find("❌ Twin")!=std::string::npos);
        command(manager,c,"amtwins","on 1877549535");CHECK(c.db["twin_mode:1877549535"]=="1");
        CHECK(c.text.find("✅ Twin")!=std::string::npos);
        c.me=1877549535;watch(c);CHECK(c.reads.size()==2); // No global main-account mode required.
        main_command(c,"amtwins","off 1877549535");watch(c);CHECK(c.reads.empty());
        c.db["autoread_chats"]="0\n";watch(c);CHECK(c.reads.empty()); // Disabled twin must not inherit main default.
        main_command(c,"amtwins","on 1877549535");
        main_command(c,"autread","off this");watch(c);CHECK(c.reads.empty());
        main_command(c,"autread","on this");watch(c);CHECK(c.reads.size()==2);
        c.outgoing=true;watch(c);CHECK(c.reads.empty());c.outgoing=false;
        c.me=1;watch(c);CHECK(c.reads.size()==2); // Return to main account.
        c.db["autoread_chats"]="";c.me=555;
        c.twins+="555\tAdded\t+000\n";c.db["twin_mode:555"]="1";
        watch(c);CHECK(c.reads.size()==2); // New twin is recognized immediately.
        c.twins="1877549535\tTwin\t+000\n";watch(c);CHECK(c.reads.empty());
        for(const char* args:{"on 999","on -1","on 1877549535 extra","off nope","enable 1877549535","on"}) {
            auto old=c.db;main_command(c,"amtwins",args);CHECK(c.db==old);CHECK(c.text.find("❌")!=std::string::npos);
        }
        c.me=1;c.trusted=false;auto old=c.db;main_command(c,"amtwins","off 1877549535");CHECK(c.db==old);
        c.callback="amtwins_toggle:1877549535";manager->on_callback(&c,&api);CHECK(c.db==old);
        c.trusted=true;c.callback="amtwins_toggle:999";manager->on_callback(&c,&api);CHECK(c.db==old);
        c.callback="amtwins_toggle:1877549535bad";manager->on_callback(&c,&api);CHECK(c.db==old);
        c.callback="amtwins_toggle:1877549535";manager->on_callback(&c,&api);CHECK(c.db["twin_mode:1877549535"]=="0");
    }
    {
        koto_ctx c;c.me=1877549535;
        c.twins_json="[{\"uid\":1,\"name\":\"Main\",\"is_main\":true},{\"uid\":1877549535,\"name\":\"Fallback\",\"is_main\":false}]";
        c.db["twin_mode:1877549535"]="1";watch(c);CHECK(c.reads.size()==2);
        main_command(c,"amtwins");CHECK(c.text.find("Fallback")!=std::string::npos);
    }
    for (bool outgoing:{false,true}) {
        koto_ctx c;c.me=1877549535;c.twins="1877549535\tTwin\t+000\n";
        c.outgoing=outgoing;c.db["twin_mode:1877549535"]="1";
        c.text="untouched";
        for (int i=0;i<manager->command_count;++i) {
            auto before=c.db;c.outputs=0;
            command(manager,c,manager->commands[i].name,"off");
            CHECK(c.outputs==0 && c.text=="untouched");
            CHECK(c.db==before && c.reads.empty());
        }
        for (const char* callback:{"am_noop","am_close","amtwins_toggle:1877549535"}) {
            auto before=c.db;c.callback=callback;c.outputs=0;
            manager->on_callback(&c,&api);
            CHECK(c.outputs==0 && c.text=="untouched" && c.db==before);
        }
        c.outputs=0;watch(c);CHECK(c.outputs==0);
        CHECK(outgoing ? c.reads.empty() : c.reads.size()==2);
    }
    {
        koto_ctx c;c.me=1877549535;
        c.twins_json="[{\"uid\":1,\"is_main\":true},{\"uid\":1877549535,\"is_main\":false}]";
        auto before=c.db;command(manager,c,"amtwins","on 1877549535");
        CHECK(c.outputs==0 && c.text.empty() && c.db==before);
    }
    for(int type:{KOTO_CHAT_GROUP,KOTO_CHAT_CHANNEL}) {
        koto_ctx c;c.type=type;c.chat=type==KOTO_CHAT_GROUP?-5422122659LL:-1002486593427LL;
        command(chat,c,"kickme","тест");CHECK(c.reads.size()==1 && c.reads[0].kind==42 && c.reads[0].chat==c.chat && c.reads[0].id==1);
        CHECK(c.text.find("До связи")!=std::string::npos);
    }
    {
        koto_ctx c;c.type=KOTO_CHAT_PRIVATE;command(chat,c,"kickme");CHECK(c.reads.empty());CHECK(c.text.find("До связи")==std::string::npos);
        c.type=KOTO_CHAT_GROUP;c.me=0;command(chat,c,"kickme");CHECK(c.reads.empty());
    }
    dlclose(second);dlclose(first);std::printf("Autoread exclusions and kickme: %d checks passed\n",checks);
}
