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
    std::string args,text;
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
    CHECK(std::string(manager->version)=="1.2.1" && manager->min_host_version==61);
    CHECK(std::string(chat->version)=="1.0.6" && chat->min_host_version==61);
    koto_api api{};
    api.check_permission=[](koto_ctx*,int){return 1;};
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
    api.c_edit=[](koto_ctx*,int){};api.c_reply=[](koto_ctx*,int){};
    api.read_history=[](koto_ctx* c,long long cid,long long mid){c->reads.push_back({cid,mid,50});};
    api.read_mentions=[](koto_ctx* c,long long cid){c->reads.push_back({cid,0,51});};
    api.kick_user=[](koto_ctx* c,long long cid,long long uid){c->reads.push_back({cid,uid,42});};
    auto command=[&](const koto_module* module,koto_ctx& c,const char* name,const char* args=""){
        c.args=args;c.reads.clear();auto begin=module->commands,end=begin+module->command_count;
        auto it=std::find_if(begin,end,[&](const auto& item){return std::string(item.name)==name;});CHECK(it!=end);it->handler(&c,&api);
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
