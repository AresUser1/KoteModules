// Administration port for Kotogram. Real operations are executed by host 60+.
#include "module_abi.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <charconv>
#include <ctime>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
using Json=nlohmann::json;
namespace {
struct Right { const char* key; const char* title; };
const Right RIGHTS[]={
    {"info","Инфо"},{"post","Посты"},{"edit","Редактирование"},
    {"delete","Удаление"},{"ban","Бан"},{"invite","Приглашения"},
    {"pin","Закрепление"},{"call","Звонки"},{"add_admins","Назначение админов"},
    {"anon","Анонимность"},{"poststory","Публикация историй"},
    {"editstory","Редактирование историй"},{"delstory","Удаление историй"}
};
constexpr const char* DEFAULT_HASH="0001111100000";
std::mutex panel_mutex;
std::string trim(std::string s) {
    size_t first=s.find_first_not_of(" \t\r\n");
    if (first==std::string::npos) return {};
    return s.substr(first,s.find_last_not_of(" \t\r\n")-first+1);
}
bool integer(const std::string& s,long long& value) {
    if (s.empty() || s.find_first_not_of("0123456789")!=std::string::npos) return false;
    auto parsed=std::from_chars(s.data(),s.data()+s.size(),value);
    return parsed.ec==std::errc{} && parsed.ptr==s.data()+s.size() && value>0;
}
bool valid_hash(const std::string& s) { return s.size()==13 && s.find_first_not_of("01")==std::string::npos; }
size_t characters(const std::string& s) {
    return (size_t)std::count_if(s.begin(),s.end(),[](unsigned char c){return (c&0xc0)!=0x80;});
}
void send(koto_ctx* ctx,const koto_api* api) {
    if (api->is_outgoing(ctx)) api->c_edit(ctx,KOTO_FMT_PLAIN);
    else api->c_reply(ctx,KOTO_FMT_PLAIN);
}
void say(koto_ctx* ctx,const koto_api* api,const std::string& text) {
    api->c_reset(ctx); api->c_text(ctx,text.c_str()); send(ctx,api);
}
bool allowed(koto_ctx* ctx,const koto_api* api,bool group=false) {
    if (!api->check_permission(ctx,KOTO_LEVEL_TRUSTED)) return false;
    if (api->host_version(ctx)<60) {
        say(ctx,api,"❌ Для реального управления администраторами требуется ядро versionCode 60+. Старый порт не выполнял эти запросы."); return false;
    }
    if (group) {
        int type=api->chat_type(ctx);
        if (type!=KOTO_CHAT_GROUP && type!=KOTO_CHAT_CHANNEL) {
            say(ctx,api,type==KOTO_CHAT_UNKNOWN?"❌ Не удалось определить тип чата. Запрос не отправлен.":"❌ Эта команда работает только в группах и каналах."); return false;
        }
    }
    return true;
}
std::string read_db(koto_ctx* ctx,const koto_api* api,const std::string& key) {
    std::vector<char> value(8192);
    int size=api->db_get(ctx,key.c_str(),value.data(),(int)value.size());
    if (size<0) return {};
    if (size>65536) return {};
    if (size>=(int)value.size()) {
        value.resize((size_t)size+1);
        size=api->db_get(ctx,key.c_str(),value.data(),(int)value.size());
    }
    if (size<0 || size>=(int)value.size()) return {};
    return std::string(value.data(),(size_t)size);
}
std::string current_hash(koto_ctx* ctx,const koto_api* api) {
    auto hash=read_db(ctx,api,"admin_rights_template");
    return valid_hash(hash)?hash:DEFAULT_HASH;
}
std::map<std::string,std::string> configs(koto_ctx* ctx,const koto_api* api) {
    std::map<std::string,std::string> result;
    std::istringstream lines(read_db(ctx,api,"saved_admin_configs"));
    for (std::string line;std::getline(lines,line);) {
        auto tab=line.find('\t');
        if (tab!=std::string::npos && valid_hash(line.substr(tab+1))) result[line.substr(0,tab)]=line.substr(tab+1);
    }
    return result;
}
void save_configs(koto_ctx* ctx,const koto_api* api,const std::map<std::string,std::string>& values) {
    std::string text;
    for (auto& [name,hash]:values) text+=name+"\t"+hash+"\n";
    api->db_set(ctx,"saved_admin_configs",text.c_str());
}
std::string panel_key(long long token) { return "admin_panel_"+std::to_string(token%64); }
Json new_panel(koto_ctx* ctx,const koto_api* api,const char* kind) {
    long long seq=api->db_get_int(ctx,"admin_panel_seq",0);
    if (seq<0 || seq>=9000000000000000000LL) seq=0;
    ++seq; api->db_set_int(ctx,"admin_panel_seq",seq);
    return Json{{"token",seq},{"revision",1},{"owner",api->sender_id(ctx)},
                {"chat",api->chat_id(ctx)},{"kind",kind},{"expires",(long long)std::time(nullptr)+86400},
                {"hash",current_hash(ctx,api)},{"uid",0},{"rank",""}};
}
void save_panel(koto_ctx* ctx,const koto_api* api,const Json& panel) {
    std::string text=panel.dump();
    api->db_set(ctx,panel_key(panel.at("token").get<long long>()).c_str(),text.c_str());
}
void button(koto_ctx* ctx,const koto_api* api,const Json& p,const std::string& text,
            const char* action,int arg=-1,int row=1,int color=0) {
    std::string data="adm:"+std::to_string(p.at("token").get<long long>())+":"+
        std::to_string(p.at("revision").get<long long>())+":"+action;
    if (arg>=0) data+=":"+std::to_string(arg);
    if (api->c_button_ex) api->c_button_ex(ctx,text.c_str(),data.c_str(),row,color,0);
    else api->c_button(ctx,text.c_str(),data.c_str(),row);
}
void render(koto_ctx* ctx,const koto_api* api,const Json& p) {
    api->c_reset(ctx);
    std::string kind=p.at("kind").get<std::string>();
    if (kind=="saved") {
        api->c_text(ctx,"📋 Сохранённые шаблоны прав\nВыберите шаблон для загрузки.\n");
        auto names=p.at("names").get<std::vector<std::string>>();
        for (size_t i=0;i<names.size();++i) button(ctx,api,p,names[i],"load",(int)i);
    } else {
        std::string hash=p.at("hash").get<std::string>();
        api->c_text(ctx,kind=="user"?"🛡 Назначение администратора\n":"⚙ Шаблон прав администратора\n");
        if (kind=="user") {
            std::string line="Пользователь: "+std::to_string(p.at("uid").get<long long>())+"\n";
            auto rank=p.at("rank").get<std::string>();
            if (!rank.empty()) line+="Ранг: "+rank+"\n";
            api->c_text(ctx,line.c_str());
        }
        for (int i=0;i<13;++i) {
            bool on=hash[(size_t)i]=='1';
            button(ctx,api,p,std::string(on?"✅ ":"▫ ")+RIGHTS[i].title,"toggle",i,i%3==0,on?KOTO_BTN_SUCCESS:0);
        }
        if (kind=="user") button(ctx,api,p,"✅ Применить права","confirm",-1,1,KOTO_BTN_SUCCESS);
    }
    button(ctx,api,p,"✖ Закрыть","close");
}
long long target(koto_ctx* ctx,const koto_api* api,std::string& rest) {
    std::string args=trim(api->cmd_args(ctx)?api->cmd_args(ctx):"");
    long long reply=api->reply_sender_id(ctx);
    if (reply>0) { rest=args; return reply; }
    std::istringstream input(args); std::string token;
    input>>token; std::getline(input,rest); rest=trim(rest);
    long long uid=0;
    if (integer(token,uid)) return uid;
    if (!token.empty() && token[0]=='@' && api->resolve) return api->resolve(ctx,token.substr(1).c_str());
    return 0;
}
void cmd_admin(koto_ctx* ctx,const koto_api* api) {
    if (!allowed(ctx,api,true)) return;
    std::string rank; long long uid=target(ctx,api,rank); // Resolve before locking panel storage.
    if (!uid && !trim(api->cmd_args(ctx)).empty()) { say(ctx,api,"❌ Пользователь не найден. Укажите @user, числовой ID или ответьте на сообщение."); return; }
    if (characters(rank)>16) { say(ctx,api,"❌ Ранг не может быть длиннее 16 символов."); return; }
    std::lock_guard<std::mutex> lock(panel_mutex);
    auto p=new_panel(ctx,api,uid?"user":"template"); p["uid"]=uid; p["rank"]=rank;
    save_panel(ctx,api,p); render(ctx,api,p); send(ctx,api);
}
void request(koto_ctx* ctx,const koto_api* api,int op) {
    if (!allowed(ctx,api,true)) return;
    std::string rank; long long uid=target(ctx,api,rank);
    if (uid<=0) { say(ctx,api,"❌ Пользователь не найден. Укажите @user, числовой ID или ответьте на сообщение."); return; }
    if (op==KOTO_ADMIN_RANK && (rank.empty() || characters(rank)>16)) { say(ctx,api,"❌ Укажите ранг длиной от 1 до 16 символов."); return; }
    // No success message here: only the host knows whether Telegram accepted it.
    api->admin_action(ctx,op,api->chat_id(ctx),uid,"",op==KOTO_ADMIN_RANK?rank.c_str():"");
}
void cmd_unadmin(koto_ctx* c,const koto_api* a) { request(c,a,KOTO_ADMIN_DEMOTE); }
void cmd_unrank(koto_ctx* c,const koto_api* a) { request(c,a,KOTO_ADMIN_UNRANK); }
void cmd_rank(koto_ctx* c,const koto_api* a) { request(c,a,KOTO_ADMIN_RANK); }
void cmd_untag(koto_ctx* ctx,const koto_api* api) {
    if (!allowed(ctx,api,true)) return;
    api->admin_action(ctx,KOTO_ADMIN_CLEAR_TAGS,api->chat_id(ctx),0,"","");
}
void cmd_adminsettings(koto_ctx* ctx,const koto_api* api) {
    if (!allowed(ctx,api)) return;
    std::lock_guard<std::mutex> lock(panel_mutex);
    auto p=new_panel(ctx,api,"template"); save_panel(ctx,api,p); render(ctx,api,p); send(ctx,api);
}
void cmd_admincfg(koto_ctx* ctx,const koto_api* api) {
    if (!allowed(ctx,api)) return;
    std::istringstream input(trim(api->cmd_args(ctx))); std::string action,name;
    input>>action; std::getline(input,name); name=trim(name);
    if (action.empty()) { cmd_adminsettings(ctx,api); return; }
    std::lock_guard<std::mutex> lock(panel_mutex);
    auto saved=configs(ctx,api);
    if (action=="list") {
        if (saved.empty()) { say(ctx,api,"📋 Сохранённых шаблонов нет."); return; }
        auto p=new_panel(ctx,api,"saved"); std::vector<std::string> names;
        for (auto& [key,value]:saved) { (void)value; if (names.size()<64) names.push_back(key); }
        p["names"]=names; save_panel(ctx,api,p); render(ctx,api,p); send(ctx,api); return;
    }
    if (name.empty() || characters(name)>32 || name.find_first_of("\n\r\t")!=std::string::npos) { say(ctx,api,"❌ Укажите имя конфигурации: от 1 до 32 символов без переноса строк."); return; }
    if (action=="save") {
        if (saved.size()>=64 && !saved.count(name)) { say(ctx,api,"❌ Лимит 64 шаблона. Сначала удалите ненужный."); return; }
        saved[name]=current_hash(ctx,api); save_configs(ctx,api,saved); say(ctx,api,"✅ Шаблон сохранён: "+name);
    } else if (action=="load" && saved.count(name)) {
        api->db_set(ctx,"admin_rights_template",saved.at(name).c_str()); say(ctx,api,"✅ Шаблон загружен: "+name);
    } else if (action=="del" && saved.erase(name)) { save_configs(ctx,api,saved); say(ctx,api,"✅ Шаблон удалён: "+name);
    } else say(ctx,api,"❌ Неизвестное действие или шаблон. Используйте .admincfg save/del/load/list [имя].");
}
void cmd_adminhelp(koto_ctx* ctx,const koto_api* api) {
    if (!allowed(ctx,api)) return;
    std::string text="🛡 Администрирование\n\n.admin [@user/ID] [Ранг] — панель назначения. Без цели — шаблон.\n"
        ".unadmin / .unrank [@user/ID] — снять права и тег участника.\n.rank [@user/ID] <Ранг> — установить ранг; существующие права сохраняются.\n"
        ".untag — снять теги обычных участников, не трогая администраторов и ботов.\n"
        ".adminsettings — шаблон прав.\n.admincfg save/del/load/list [имя] — сохранённые шаблоны.\n\n"
        "Цель можно указать ответом на сообщение. В обычных группах доступны назначение/снятие роли без гранулярных прав; ранги требуют супергруппу.\n\n";
    for (auto& right:RIGHTS) text+=std::string(right.key)+" — "+right.title+"\n";
    say(ctx,api,text);
}
void on_callback(koto_ctx* ctx,const koto_api* api) {
    std::string data=api->cb_data(ctx)?api->cb_data(ctx):"";
    if (data.rfind("adm:",0)!=0) return; // Never consume another module's callback.
    if (!api->check_permission(ctx,KOTO_LEVEL_TRUSTED)) { api->cb_answer(ctx,"Недостаточно прав для этой панели.",1); return; }
    std::vector<std::string> fields; std::istringstream input(data.substr(4));
    for (std::string token;std::getline(input,token,':');) fields.push_back(token);
    long long token=0,revision=0;
    if (fields.size()<3 || fields.size()>4 || !integer(fields[0],token) || !integer(fields[1],revision)) { api->cb_answer(ctx,"Некорректная кнопка.",1); return; }
    std::lock_guard<std::mutex> lock(panel_mutex);
    auto raw=read_db(ctx,api,panel_key(token));
    auto p=Json::parse(raw,nullptr,false);
    if (!p.is_object() || p.value("token",0LL)!=token || p.value("expires",0LL)<(long long)std::time(nullptr)) { api->cb_answer(ctx,"Панель устарела. Откройте новую командой .admin.",0); return; }
    long long sender=api->sender_id(ctx),chat=api->chat_id(ctx);
    if ((sender!=p.value("owner",0LL) && sender!=api->get_me(ctx)) || (chat && chat!=p.value("chat",0LL))) { api->cb_answer(ctx,"Это чужая панель.",1); return; }
    if (p.value("revision",0LL)!=revision) { api->cb_answer(ctx,"Панель обновилась. Нажмите актуальную кнопку.",0); return; }
    std::string kind=p.value("kind","");
    if (fields[2]=="close") {
        api->db_del(ctx,panel_key(token).c_str()); api->c_reset(ctx); api->c_text(ctx,"Панель закрыта.");
    } else if (fields[2]=="toggle" && fields.size()==4 && (kind=="template" || kind=="user")) {
        long long index=0;
        if (fields[3]=="0") index=0;
        else if (!integer(fields[3],index)) { api->cb_answer(ctx,"Некорректное право.",1); return; }
        auto hash=p.value("hash","");
        if (index>=13 || !valid_hash(hash)) { api->cb_answer(ctx,"Некорректное право.",1); return; }
        hash[(size_t)index]=hash[(size_t)index]=='1'?'0':'1'; p["hash"]=hash;
        if (kind=="template") api->db_set(ctx,"admin_rights_template",hash.c_str());
        p["revision"]=revision+1; save_panel(ctx,api,p); render(ctx,api,p);
    } else if (fields[2]=="load" && fields.size()==4 && kind=="saved") {
        long long index=0;
        if (fields[3]!="0" && !integer(fields[3],index)) { api->cb_answer(ctx,"Некорректный шаблон.",1); return; }
        auto names=p.at("names").get<std::vector<std::string>>(); auto saved=configs(ctx,api);
        if ((size_t)index>=names.size() || !saved.count(names[(size_t)index])) { api->cb_answer(ctx,"Шаблон уже удалён.",0); return; }
        api->db_set(ctx,"admin_rights_template",saved.at(names[(size_t)index]).c_str());
        api->db_del(ctx,panel_key(token).c_str()); api->c_reset(ctx); api->c_text(ctx,"✅ Шаблон прав загружен.");
    } else if (fields[2]=="confirm" && kind=="user") {
        api->db_del(ctx,panel_key(token).c_str()); // Prevent double application.
        auto hash=p.at("hash").get<std::string>(),rank=p.at("rank").get<std::string>();
        api->admin_action(ctx,KOTO_ADMIN_PROMOTE,p.at("chat").get<long long>(),p.at("uid").get<long long>(),hash.c_str(),rank.c_str());
        api->cb_answer(ctx,"Выполняю запрос Telegram…",0); return;
    } else { api->cb_answer(ctx,"Это действие недоступно.",0); return; }
    api->c_edit_message(ctx,api->chat_id(ctx),api->cb_message_id(ctx),KOTO_FMT_PLAIN);
    api->cb_answer(ctx,"",0);
}
const koto_command COMMANDS[]={
    {"admin",cmd_admin,KOTO_LEVEL_TRUSTED,"Назначить администратора / настроить шаблон","[@user/ID] [Ранг]"},
    {"unadmin",cmd_unadmin,KOTO_LEVEL_TRUSTED,"Снять права и тег участника","[@user/ID]"},
    {"unrank",cmd_unrank,KOTO_LEVEL_TRUSTED,"Снять права и тег участника","[@user/ID]"},
    {"untag",cmd_untag,KOTO_LEVEL_TRUSTED,"Снять теги обычных участников",nullptr},
    {"rank",cmd_rank,KOTO_LEVEL_TRUSTED,"Установить ранг, сохранив права","[@user/ID] <Ранг>"},
    {"adminhelp",cmd_adminhelp,KOTO_LEVEL_TRUSTED,"Справка по администрированию",nullptr},
    {"admincfg",cmd_admincfg,KOTO_LEVEL_TRUSTED,"Сохранённые шаблоны прав","[save|load|del|list] [имя]"},
    {"adminsettings",cmd_adminsettings,KOTO_LEVEL_TRUSTED,"Открыть шаблон прав",nullptr}
};
const koto_module MODULE={KOTO_MODULE_ABI,"administration","Управление администраторами через реальные MTProto-запросы, шаблоны и панели",
    "1.2.1",60,0,COMMANDS,(int)(sizeof(COMMANDS)/sizeof(COMMANDS[0])),nullptr,0,on_callback,nullptr,0,
    "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/administration.so"};
}
extern "C" const koto_module* koto_module_register(void) { return &MODULE; }
