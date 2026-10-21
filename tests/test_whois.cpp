#include "module_abi.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <vector>

struct koto_ctx {
    std::string args;
    std::string composed;
    long long chat = -100123456789LL;
    long long sender = 777000LL;
    long long reply_sender = 0;
    int edited = 0;
    int replied = 0;
};

static int checks = 0;
#define CHECK(x) do { \
    ++checks; \
    if (!(x)) { \
        std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); \
        std::exit(1); \
    } \
} while(0)

int main(int argc, char** argv) {
    CHECK(argc == 2);
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    CHECK(lib != nullptr);

    auto load = reinterpret_cast<const koto_module*(*)()>(dlsym(lib, "koto_module_register"));
    CHECK(load != nullptr);
    const koto_module* mod = load();
    CHECK(mod != nullptr);

    CHECK(std::string(mod->name) == "whois");
    CHECK(std::string(mod->version) == "1.0.0");
    CHECK(mod->command_count == 3);
    CHECK(std::string(mod->commands[0].name) == "whois");
    CHECK(std::string(mod->commands[1].name) == "whoischat");
    CHECK(std::string(mod->commands[2].name) == "chatanalysis");

    koto_api api{};
    api.cmd_args = [](koto_ctx* c) -> const char* { return c->args.c_str(); };
    api.is_outgoing = [](koto_ctx*) -> int { return 1; };
    api.check_permission = [](koto_ctx*, int) -> int { return 1; };
    api.chat_id = [](koto_ctx* c) -> long long { return c->chat; };
    api.sender_id = [](koto_ctx* c) -> long long { return c->sender; };
    api.reply_sender_id = [](koto_ctx* c) -> long long { return c->reply_sender; };
    api.get_me = [](koto_ctx*) -> long long { return 777000LL; };

    api.c_reset = [](koto_ctx* c) { c->composed.clear(); };
    api.c_text = [](koto_ctx* c, const char* s) { if (s) c->composed += s; };
    api.c_fmt = [](koto_ctx* c, const char* s, int) { if (s) c->composed += s; };
    api.c_emoji = [](koto_ctx* c, const char* s, long long) { if (s) c->composed += s; };
    api.c_url = [](koto_ctx* c, const char* t, const char* u) {
        c->composed += std::string("[") + (t ? t : "") + "](" + (u ? u : "") + ")";
    };
    api.c_edit = [](koto_ctx* c, int) { c->edited++; };
    api.c_reply = [](koto_ctx* c, int) { c->replied++; };

    api.user_full_json = [](koto_ctx*, long long uid, char* buf, int buflen) -> int {
        if (uid == 777000LL) {
            std::string j = "{\"id\":777000,\"first_name\":\"Telegram\",\"last_name\":\"Notifications\","
                            "\"username\":\"telegram\",\"display_name\":\"Telegram Notifications\","
                            "\"about\":\"Official service notifications\",\"is_premium\":true,\"is_bot\":false,"
                            "\"stargifts_count\":42,\"common_chats_count\":5}";
            int to_copy = std::min((int)j.size(), buflen - 1);
            std::memcpy(buf, j.data(), to_copy);
            buf[to_copy] = '\0';
            return to_copy;
        }
        return 0;
    };

    api.chat_full_json = [](koto_ctx*, long long cid, char* buf, int buflen) -> int {
        if (cid == -100123456789LL) {
            std::string j = "{\"id\":-100123456789,\"title\":\"Kotogram Dev Chat\","
                            "\"username\":\"kotogram_dev\",\"about\":\"Official developer hub\","
                            "\"participants_count\":1500,\"admins_count\":15,\"pinned_msg_id\":42,"
                            "\"is_channel\":false,\"is_group\":true,\"participants_hidden\":false,"
                            "\"can_view_participants\":true,\"admin_ids\":[1,2,3],\"bot_ids\":[10,20]}";
            int to_copy = std::min((int)j.size(), buflen - 1);
            std::memcpy(buf, j.data(), to_copy);
            buf[to_copy] = '\0';
            return to_copy;
        }
        return 0;
    };

    api.chat_type = [](koto_ctx*) -> int { return 2; /* KOTO_CHAT_GROUP */ };

    koto_ctx ctx;

    // 1. Проверка .whois для своего аккаунта
    ctx.args = "";
    mod->commands[0].handler(&ctx, &api);
    CHECK(ctx.composed.find("Информация о пользователе") != std::string::npos);
    CHECK(ctx.composed.find("777000") != std::string::npos);
    CHECK(ctx.composed.find("Telegram") != std::string::npos);
    CHECK(ctx.composed.find("@telegram") != std::string::npos);
    CHECK(ctx.composed.find("Premium") != std::string::npos);
    CHECK(ctx.composed.find("Official service notifications") != std::string::npos);
    CHECK(ctx.composed.find("tg://user?id=777000") != std::string::npos);

    // 2. Проверка .whoischat для текущего чата
    ctx.args = "";
    ctx.composed.clear();
    mod->commands[1].handler(&ctx, &api);
    CHECK(ctx.composed.find("Анализ чата") != std::string::npos);
    CHECK(ctx.composed.find("Kotogram Dev Chat") != std::string::npos);
    CHECK(ctx.composed.find("-100123456789") != std::string::npos);
    CHECK(ctx.composed.find("Супергруппа / Группа") != std::string::npos);
    CHECK(ctx.composed.find("1500") != std::string::npos);
    CHECK(ctx.composed.find("15") != std::string::npos);
    CHECK(ctx.composed.find("2") != std::string::npos); // 2 бота
    CHECK(ctx.composed.find("#42") != std::string::npos); // pinned
    CHECK(ctx.composed.find("Открыт") != std::string::npos);

    // 3. Проверка .chatanalysis (псевдоним)
    ctx.args = "-100123456789";
    ctx.composed.clear();
    mod->commands[2].handler(&ctx, &api);
    CHECK(ctx.composed.find("Kotogram Dev Chat") != std::string::npos);

    std::printf("Whois tests: %d checks passed successfully!\n", checks);
    return 0;
}
