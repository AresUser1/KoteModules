#include "module_abi.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include <map>
#include <string>
#include <vector>
struct Entity{int type;std::string url;};
struct Output{long long chat;std::string text;};
struct koto_ctx {
    std::map<std::string,std::string> db,cfg{{"enabled","1"},{"silent","1"},{"log_group_id","-1009999999999"}};
    std::string text="hello",username="MyCat",args,callback,composed,twins="[]",twin_list,user_json="{}",chat_json="{}",first="Name",last;
    long long me=12,sender=25,chat=2486593427LL,reply=0;
    int type=KOTO_CHAT_CHANNEL,reads=0,outputs=0;
    bool out=false,mention=false,pin=false,permission=true;
    std::vector<std::string> operations;std::vector<Entity> entities;std::vector<Output> sent;std::vector<std::string> urls;
};
static int checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static int write(const std::string& value,char* buf,int cap){if(cap>0){auto n=std::min(value.size(),size_t(cap-1));std::memcpy(buf,value.data(),n);buf[n]=0;}return int(value.size());}
int main(int argc,char** argv){
    CHECK(argc==2);auto lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);CHECK(lib);
    auto load=reinterpret_cast<const koto_module*(*)()>(dlsym(lib,"koto_module_register"));CHECK(load);auto mod=load();
    CHECK(std::string(mod->version)=="2.9.9" && mod->min_host_version==60);
    CHECK(mod->flags & KOTO_MOD_TWINS);CHECK(mod->watch_flags & KOTO_WATCH_INCOMING);
    for(int i=0;i<mod->command_count;++i) {
        CHECK(mod->commands[i].min_level == KOTO_LEVEL_OWNER);
    }
    koto_api api{};
    api.chat_id=[](koto_ctx*c){return c->chat;};api.chat_type=[](koto_ctx*c){return c->type;};
    api.sender_id=[](koto_ctx*c){return c->sender;};api.get_me=[](koto_ctx*c){return c->me;};api.msg_id=[](koto_ctx*)->long long{return 42;};
    api.is_outgoing=[](koto_ctx*c){return int(c->out);};api.msg_text=[](koto_ctx*c){return c->text.c_str();};
    api.msg_is_mentioned=[](koto_ctx*c){return int(c->mention);};api.msg_is_pinned=[](koto_ctx*c){return int(c->pin);};
    api.reply_sender_id=[](koto_ctx*c){return c->reply;};api.cmd_args=[](koto_ctx*c){return c->args.c_str();};
    api.cfg_get=[](koto_ctx*c,const char*k,char*b,int n){auto i=c->cfg.find(k);return i==c->cfg.end()?-1:write(i->second,b,n);};
    api.db_get=[](koto_ctx*c,const char*k,char*b,int n){auto i=c->db.find(k);return i==c->db.end()?-1:write(i->second,b,n);};
    api.cfg_set=[](koto_ctx*c,const char*k,const char*v){c->cfg[k]=v;};api.db_set=[](koto_ctx*c,const char*k,const char*v){c->db[k]=v;};
    api.db_get_int=[](koto_ctx*c,const char*k,long long d){auto i=c->db.find(k);return i==c->db.end()?d:std::strtoll(i->second.c_str(),nullptr,10);};
    api.db_set_int=[](koto_ctx*c,const char*k,long long v){c->db[k]=std::to_string(v);};
    api.get_twins_json=[](koto_ctx*c,char*b,int n){return write(c->twins,b,n);};api.twin_list=[](koto_ctx*c,char*b,int n){return write(c->twin_list,b,n);};
    api.user_username=[](koto_ctx*c,long long,char*b,int n){return write(c->username,b,n);};
    api.user_first_name=[](koto_ctx*c,long long,char*b,int n){return write(c->first,b,n);};
    api.user_last_name=[](koto_ctx*c,long long,char*b,int n){return write(c->last,b,n);};
    api.user_name=[](koto_ctx*c,long long,char*b,int n){return write(c->first,b,n);};
    api.user_full_json=[](koto_ctx*c,long long,char*b,int n){return write(c->user_json,b,n);};
    api.chat_full_json=[](koto_ctx*c,long long,char*b,int n){return write(c->chat_json,b,n);};
    api.msg_entity_count=[](koto_ctx*c){return int(c->entities.size());};
    api.msg_entity=[](koto_ctx*c,int i,int*t,int*o,int*l,long long*d,char*b,int n){auto&e=c->entities.at(i);*t=e.type;*o=0;*l=1;*d=0;write(e.url,b,n);return 1;};
    api.read_history=[](koto_ctx*c,long long,long long){++c->reads;c->operations.push_back("read");};api.read_mentions=[](koto_ctx*c,long long){++c->reads;c->operations.push_back("read");};
    api.c_reset=[](koto_ctx*c){c->composed.clear();};api.c_text=[](koto_ctx*c,const char*s){c->composed+=s;};
    api.c_fmt=[](koto_ctx*c,const char*s,int){c->composed+=s;};api.c_emoji=[](koto_ctx*c,const char*s,long long){c->composed+=s;};
    api.c_url=[](koto_ctx*c,const char*s,const char*u){c->composed+=s;c->urls.push_back(u);};
    api.c_mark=[](koto_ctx*c){return int(c->composed.size());};api.c_wrap=[](koto_ctx*,int,int,int){};
    api.c_send=[](koto_ctx*c,long long chat,int){c->sent.push_back({chat,c->composed});c->operations.push_back("send");};
    api.c_edit=[](koto_ctx*c,int){++c->outputs;};api.c_reply=[](koto_ctx*c,int){++c->outputs;};
    api.c_edit_message=[](koto_ctx*c,long long,long long,int){++c->outputs;};
    api.check_permission=[](koto_ctx*c,int){return int(c->permission);};
    api.cb_data=[](koto_ctx*c){return c->callback.c_str();};api.cb_message_id=[](koto_ctx*)->long long{return 1;};
    api.cb_answer=[](koto_ctx*c,const char*s,int){++c->outputs;c->composed=s;};
    api.c_button=[](koto_ctx*,const char*,const char*,int){};
    auto watch=[&](koto_ctx& c){c.reads=0;c.sent.clear();c.operations.clear();mod->on_watch(&c,&api);};
    auto cmd=[&](koto_ctx& c,const char* name,const char* args){c.args=args;auto i=std::find_if(mod->commands,mod->commands+mod->command_count,[&](const auto& d){return std::string(d.name)==name;});CHECK(i!=mod->commands+mod->command_count);i->handler(&c,&api);};
    {koto_ctx c;c.mention=true;watch(c);CHECK(c.reads==2 && c.sent.size()==1);CHECK(c.operations==std::vector<std::string>({"send","read","read"}));c.out=true;watch(c);CHECK(!c.reads && c.sent.empty());c.out=false;c.cfg["enabled"]="0";watch(c);CHECK(!c.reads);}
    for(const char* text:{"@MyCat","hello @mYcAt!","(@MYCAT)","@other @mycat"}){koto_ctx c;c.text=text;watch(c);CHECK(c.reads==2 && c.sent.size()==1);}
    for(const char* text:{"@MyCat123","@MyCat_","user@mycat","@@mycat","@Other","ordinary message"}){koto_ctx c;c.text=text;watch(c);CHECK(!c.reads && c.sent.empty());}
    for(const auto& [url,yes]:std::vector<std::pair<std::string,bool>>{{"tg://user?id=12",true},{"TG://USER?id=12&x=1",true},{"tg://user?id=123",false},{"tg://user?id=12junk",false},{"https://example/tg://user?id=12",false}}){koto_ctx c;c.entities.push_back({KOTO_ENT_URL,url});watch(c);CHECK((c.reads==2)==yes);}
    {koto_ctx c;c.reply=c.me;watch(c);CHECK(c.reads==2);c.reply=99;watch(c);CHECK(!c.reads);c.pin=true;c.db["ignore_users"]="25";watch(c);CHECK(c.reads==2);}
    {koto_ctx c;c.mention=true;c.user_json="{\"about\":\""+std::string(5000,'x')+"\", \"is_bot\": true}";c.cfg["process_bots"]="0";watch(c);CHECK(!c.reads);c.cfg["process_bots"]="1";watch(c);CHECK(c.reads==2);}
    {koto_ctx c;c.mention=true;c.user_json="{\"about\":\"is_bot:true\",\"is_bot\":false}";c.cfg["process_bots"]="0";watch(c);CHECK(c.reads==2);}
    {koto_ctx c;c.mention=true;c.twins="[{\"uid\":123,\"is_main\":false,\"name\":\"Other\"}]";watch(c);CHECK(c.reads==2);c.me=123;watch(c);CHECK(!c.reads);c.db["twin_modes"]="123\tall\n";watch(c);CHECK(c.reads==2);CHECK(c.sent.size()==1 && c.sent.back().text.find("Other")!=std::string::npos);c.cfg["silent"]="0";watch(c);CHECK(c.reads==2 && c.sent.size()==2);}
    {koto_ctx c;c.mention=true;c.me=123;c.twins="[ { \"name\": \"Twin \\\"quoted\\\"\", \"is_main\": false, \"uid\": 123 } ]";watch(c);CHECK(!c.reads);c.db["twin_modes"]="123\tsemi\n";watch(c);CHECK(c.reads==2);c.type=KOTO_CHAT_PRIVATE;watch(c);CHECK(!c.reads);c.db["twin_modes"]="123\tall\n";watch(c);CHECK(c.reads==2);c.cfg["enabled"]="0";watch(c);CHECK(!c.reads);}
    {koto_ctx c;c.mention=true;c.me=123;c.twins="bad json";c.twin_list="123\tFallback\t+000\n";watch(c);CHECK(!c.reads);c.db["twin_modes"]="123\tall\n";watch(c);CHECK(c.reads==2);}
    {koto_ctx c;c.mention=true;c.twins="[{\"uid\":124,\"is_main\":false,\"name\":\""+std::string(20000,'x')+"\"},{\"uid\":12,\"is_main\":false,\"name\":\"Tail\"}]";watch(c);CHECK(!c.reads);c.db["twin_modes"]="12\tall\n";watch(c);CHECK(c.reads==2);}
    {koto_ctx c;c.mention=true;c.db["ignore_chats"]="-1002486593427";watch(c);CHECK(!c.reads);c.db["ignore_chats"]="2486593427";watch(c);CHECK(!c.reads);c.db["ignore_chats"]="-2486593427";watch(c);CHECK(c.reads==2);}
    {koto_ctx c;c.mention=true;c.chat=1007654321;c.db["ignore_chats"]="7654321";watch(c);CHECK(c.reads==2);CHECK(c.urls.back()=="https://t.me/c/1007654321/42");}
    {koto_ctx c;c.mention=true;c.chat=777;c.type=KOTO_CHAT_GROUP;c.db["ignore_chats"]="-777";watch(c);CHECK(!c.reads);c.type=KOTO_CHAT_PRIVATE;c.cfg["process_pm"]="1";watch(c);CHECK(c.reads==2);}
    {koto_ctx c;c.mention=true;for(int i=10000;i<14000;++i)c.db["ignore_users"]+=std::to_string(i)+" ";c.db["ignore_users"]+="25";watch(c);CHECK(!c.reads);}
    {koto_ctx c;c.mention=true;c.chat_json="{\"about\":\""+std::string(6000,'x')+"\",\"title\":\"A \\\"quoted\\\" chat\"}";watch(c);CHECK(c.reads==2);CHECK(c.sent[0].text.find("A \"quoted\" chat")!=std::string::npos);}
    {koto_ctx c;c.mention=true;c.text=std::string(499,'a')+"😸more";c.first=std::string(300,'a')+"😸";watch(c);CHECK(c.reads==2);CHECK(c.sent[0].text.find(std::string(499,'a')+"...")!=std::string::npos);bool valid=true;try{(void)nlohmann::json(c.sent[0].text).dump();}catch(...){valid=false;}CHECK(valid);CHECK(c.sent[0].text.find(c.first)!=std::string::npos);}
    {koto_ctx c;cmd(c,"stconfig","ignore_chats add this");CHECK(c.db["ignore_chats"].find("-1002486593427")!=std::string::npos);c.db["ignore_chats"]+="2486593427 ";cmd(c,"stconfig","ignore_chats del this");CHECK(c.db["ignore_chats"].empty());}
    for(const char* args:{"silent nonsense","ignore_chats add 123garbage","ignore_users add -9223372036854775808","set_log_group garbage"}){koto_ctx c;auto db=c.db,cfg=c.cfg;cmd(c,"stconfig",args);CHECK(c.db==db && c.cfg==cfg);}
    {koto_ctx c;cmd(c,"stconfig","silent ON");CHECK(c.cfg["silent"]=="1");cmd(c,"stconfig","silent OFF");CHECK(c.cfg["silent"]=="0");}
    for(const char* callback:{"sttwins_set:123:nope","sttwins_set:999:all","sttwins_set:123bad:all"}){koto_ctx c;c.twins="[{\"uid\":123,\"is_main\":false}]";c.callback=callback;auto before=c.db;mod->on_callback(&c,&api);CHECK(c.db==before);}
    {koto_ctx c;c.twins="[{\"uid\":123,\"is_main\":false}]";c.callback="sttwins_set:123:all";mod->on_callback(&c,&api);CHECK(c.db["twin_modes"]=="123\tall\n");}
    {
        koto_ctx c;
        c.me=123;
        c.twins="[{\"uid\":123,\"is_main\":false}]";
        c.out=false;
        c.permission=true;
        auto cfg_before=c.cfg;
        auto db_before=c.db;
        cmd(c,"stags","on");
        CHECK(c.outputs==0 && c.sent.empty() && c.cfg==cfg_before && c.db==db_before);
        cmd(c,"stconfig","silent on");
        CHECK(c.outputs==0 && c.sent.empty() && c.cfg==cfg_before && c.db==db_before);
        cmd(c,"sttwins","");
        CHECK(c.outputs==0 && c.sent.empty() && c.cfg==cfg_before && c.db==db_before);
        c.callback="sttwins_set:123:all";
        mod->on_callback(&c,&api);
        CHECK(c.outputs==0 && c.sent.empty() && c.cfg==cfg_before && c.db==db_before);
    }
    std::printf("Silent Tags: %d checks passed\n",checks);
}
