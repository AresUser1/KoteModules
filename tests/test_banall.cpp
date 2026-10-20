// Load the actual module through its exported ABI; no Telegram calls are made.
#include "module_abi.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <vector>
struct Request { long long chat, user; bool kick; };
struct koto_ctx {
    std::string snapshot = R"({"participant_ids":[1,2,3],"admin_ids":[1,2,99],"participants_count":3})";
    std::string text;
    std::vector<Request> requests;
    long long me=1;
    int type=KOTO_CHAT_GROUP, reads=0, edits=0, replies=0;
    bool allowed=true, outgoing=true;
};
static int checks;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(int argc,char** argv) {
    CHECK(argc==2);
    auto handle=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);CHECK(handle);
    auto entry=reinterpret_cast<const koto_module*(*)()>(dlsym(handle,"koto_module_register"));CHECK(entry);
    auto module=entry();CHECK(module && std::string(module->version)=="1.0.2" && module->min_host_version==60);
    koto_api api{};
    api.check_permission=[](koto_ctx* c,int level){assert(level==KOTO_LEVEL_OWNER);return int(c->allowed);};
    api.chat_type=[](koto_ctx* c){return c->type;};
    api.chat_id=[](koto_ctx*)->long long{return 777;};
    api.get_me=[](koto_ctx* c){return c->me;};
    api.is_outgoing=[](koto_ctx* c){return int(c->outgoing);};
    api.chat_full_json=[](koto_ctx* c,long long id,char* buffer,int capacity){
        assert(id==777);++c->reads;auto length=c->snapshot.size();
        if(capacity>0){auto n=std::min(length,size_t(capacity-1));std::memcpy(buffer,c->snapshot.data(),n);buffer[n]=0;}
        return int(length);
    };
    api.c_reset=[](koto_ctx* c){c->text.clear();};
    api.c_text=[](koto_ctx* c,const char* value){c->text+=value;};
    api.c_edit=[](koto_ctx* c,int){++c->edits;};
    api.c_reply=[](koto_ctx* c,int){++c->replies;};
    api.ban_user=[](koto_ctx* c,long long chat,long long user,int until){assert(until==0);c->requests.push_back({chat,user,false});};
    api.kick_user=[](koto_ctx* c,long long chat,long long user){c->requests.push_back({chat,user,true});};
    auto run=[&](koto_ctx& c,const char* command="banall") {
        auto it=std::find_if(module->commands,module->commands+module->command_count,[&](const auto& item){return std::string(item.name)==command;});
        CHECK(it!=module->commands+module->command_count);it->handler(&c,&api);
    };
    auto contains=[](const koto_ctx& c,const char* text){return c.text.find(text)!=std::string::npos;};
    {
        koto_ctx c;c.snapshot=R"({"participant_ids":[1],"admin_ids":[1,99],"participants_count":2})";run(c);
        CHECK(c.requests.empty());CHECK(contains(c,"Получено участников: 1"));
        CHECK(contains(c,"Пропущен текущий аккаунт: 1"));CHECK(contains(c,"Пропущены администраторы/владелец: 0"));
        CHECK(contains(c,"ID 1 — текущий аккаунт"));CHECK(!contains(c,"ID 99"));
        CHECK(contains(c,"Список неполный: получено 1 из 2"));CHECK(contains(c,"через минуту"));
        CHECK(!contains(c,"🎉") && !contains(c,"админы/ошибки"));CHECK(c.reads==1 && c.edits==1);
        std::puts("Example for an incomplete, self-only participant snapshot:");std::puts(c.text.c_str());
    }
    {
        koto_ctx c;c.snapshot=R"({"participant_ids":[1,2,3,3,0,-5,"x",18446744073709551615],"admin_ids":[1,2,99],"creator_id":7})";run(c);
        CHECK(c.requests.size()==1 && c.requests[0].user==3 && c.requests[0].chat==777 && !c.requests[0].kick);
        CHECK(contains(c,"Запросов на бан: 1"));CHECK(contains(c,"Пропущены администраторы/владелец: 1"));
        CHECK(contains(c,"ID 2 — администратор или владелец"));CHECK(contains(c,"Некорректных ID в списке: 4"));
        CHECK(contains(c,"Повторных записей исключено: 1"));CHECK(c.reads==1);
    }
    {
        koto_ctx c;c.outgoing=false;run(c,"kickall");
        CHECK(c.requests.size()==1 && c.requests[0].kick && c.requests[0].user==3);
        CHECK(contains(c,"Запросов на кик: 1"));CHECK(!contains(c,"Кикнуто"));CHECK(c.replies==1 && c.edits==0);
    }
    for(const char* data:{"broken","{}","[]",R"({"participant_ids":false,"admin_ids":[]})"}) {
        koto_ctx c;c.snapshot=data;run(c);CHECK(c.requests.empty());CHECK(contains(c,"participant_ids"));CHECK(contains(c,"Запросы не отправлены"));
    }
    {
        koto_ctx c;c.snapshot=R"({"participant_ids":[3]})";run(c);CHECK(c.requests.empty());CHECK(contains(c,"admin_ids"));
    }
    {
        koto_ctx c;c.snapshot=R"({"participant_ids":[],"admin_ids":[1,99]})";run(c);
        CHECK(c.requests.empty());CHECK(contains(c,"Получено участников: 0"));CHECK(contains(c,"Пропущены администраторы/владелец: 0"));
    }
    {
        koto_ctx c;c.snapshot=R"({"participant_ids":[7,3],"admin_ids":[],"creator_id":7,"participants_hidden":true})";run(c);
        CHECK(c.requests.size()==1 && c.requests[0].user==3);CHECK(contains(c,"ID 7 — администратор или владелец"));
        CHECK(contains(c,"Telegram ограничивает просмотр"));
    }
    for(int type:{KOTO_CHAT_PRIVATE,KOTO_CHAT_UNKNOWN}) {
        koto_ctx c;c.type=type;run(c);CHECK(c.reads==0 && c.requests.empty());CHECK(contains(c,"только в группах"));
    }
    {
        koto_ctx c;c.me=0;run(c);CHECK(c.reads==0 && c.requests.empty());CHECK(contains(c,"ID текущего аккаунта"));
    }
    {
        koto_ctx c;c.allowed=false;run(c);CHECK(c.reads==0 && c.requests.empty() && c.text.empty());
    }
    {
        auto original=api.ban_user;api.ban_user=nullptr;koto_ctx c;run(c);api.ban_user=original;
        CHECK(c.requests.empty());CHECK(contains(c,"не предоставляет ban_user"));CHECK(contains(c,"Не отправлены запросы для участников: 1"));
    }
    {
        koto_ctx c;c.snapshot="{\"participant_ids\":[1],\"admin_ids\":[1],\"about\":\""+std::string(5000,'x')+"\"}";run(c);
        CHECK(c.requests.empty() && c.reads==2);CHECK(contains(c,"ID 1 — текущий аккаунт"));
    }
    {
        koto_ctx c;
        std::string ids;
        for(int id=1;id<=150;++id){if(!ids.empty())ids+=",";ids+=std::to_string(id);}
        c.snapshot="{\"participant_ids\":["+ids+"],\"admin_ids\":["+ids+"]}";
        run(c);CHECK(c.requests.empty());CHECK(contains(c,"Пропущены администраторы/владелец: 149"));
        CHECK(contains(c,"ещё 138"));CHECK(c.text.size()<4096);
    }
    dlclose(handle);std::printf("Banall diagnostics: %d checks passed\n",checks);
}
