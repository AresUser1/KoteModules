// Exercise the actual command parser without invoking Telegram.
#include "../src/spam_ban.cpp"
#include <cassert>
#include <cstdio>
struct koto_ctx { const char* args; int type=KOTO_CHAT_PRIVATE; };
static int requests;
static long long target;
int main() {
    koto_api api{};
    api.check_permission=[](koto_ctx*,int){return 1;};
    api.cmd_args=[](koto_ctx* c){return c->args;};
    api.resolve=[](koto_ctx*,const char* name)->long long{return std::strcmp(name,"valid")==0?43:0;};
    api.reply_sender_id=[](koto_ctx*)->long long{return 0;};
    api.chat_type=[](koto_ctx* c){return c->type;};
    api.chat_id=[](koto_ctx*)->long long{return 777;};
    api.is_outgoing=[](koto_ctx*){return 1;};
    api.c_reset=[](koto_ctx*){};
    api.c_text=[](koto_ctx*,const char*){};
    api.c_edit=[](koto_ctx*,int){};
    api.ban_user=[](koto_ctx*,long long,long long id,int mode){assert(mode==KOTO_BAN_REPORT_SPAM);++requests;target=id;};
    for (const char* input:{"42junk","42 extra","-42","0","999999999999999999999999999999","@missing"}) {
        koto_ctx ctx{input};requests=0;cmd_spamban(&ctx,&api);assert(requests==0);
    }
    for (auto item:{std::pair<const char*,long long>{"42",42},{"@valid",43},{"",777}}) {
        koto_ctx ctx{item.first};requests=0;cmd_spamban(&ctx,&api);assert(requests==1 && target==item.second);
    }
    koto_ctx group{"",KOTO_CHAT_GROUP};requests=0;cmd_spamban(&group,&api);assert(requests==0);
    std::puts("Spam-ban inputs: 10 cases passed; no partial, overflowing or group IDs accepted");
}
