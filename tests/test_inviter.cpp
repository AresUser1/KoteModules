#include "module_abi.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <map>
#include <string>
#include <vector>

struct koto_ctx {
    std::string args;
    std::string composed;
    std::map<std::string, std::string> db;
    long long chat = -100123456789LL;
    long long sender = 777000LL;
    int edited = 0;
    int replied = 0;
    int sent_msgs = 0;
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

    CHECK(std::string(mod->name) == "inviter");
    CHECK(std::string(mod->version) == "1.0.1");
    CHECK(mod->command_count == 7);
    CHECK(std::string(mod->commands[0].name) == "invite");
    CHECK(std::string(mod->commands[1].name) == "inviter");
    CHECK(std::string(mod->commands[2].name) == "invsetmsg");
    CHECK(std::string(mod->commands[3].name) == "invstatus");
    CHECK(std::string(mod->commands[4].name) == "invcfg");
    CHECK(std::string(mod->commands[5].name) == "invclear");
    CHECK(std::string(mod->commands[6].name) == "invstop");

    koto_api api{};
    api.cmd_args = [](koto_ctx* c) -> const char* { return c->args.c_str(); };
    api.is_outgoing = [](koto_ctx*) -> int { return 1; };
    api.check_permission = [](koto_ctx*, int) -> int { return 1; };
    api.chat_id = [](koto_ctx* c) -> long long { return c->chat; };
    api.sender_id = [](koto_ctx* c) -> long long { return c->sender; };
    api.setting_get = [](koto_ctx*, const char*, char* b, int n) -> int {
        std::strncpy(b, ".", n);
        return 1;
    };

    api.db_get = [](koto_ctx* c, const char* k, char* buf, int buflen) -> int {
        auto it = c->db.find(k ? k : "");
        if (it == c->db.end()) return 0;
        int to_copy = std::min((int)it->second.size(), buflen - 1);
        std::memcpy(buf, it->second.data(), to_copy);
        buf[to_copy] = '\0';
        return to_copy;
    };
    api.db_set = [](koto_ctx* c, const char* k, const char* val) {
        c->db[k ? k : ""] = val ? val : "";
    };
    api.db_del = [](koto_ctx* c, const char* k) {
        c->db.erase(k ? k : "");
    };
    api.db_get_int = [](koto_ctx* c, const char* k, long long def) -> long long {
        auto it = c->db.find(k ? k : "");
        if (it == c->db.end()) return def;
        try { return std::stoll(it->second); } catch (...) { return def; }
    };
    api.db_set_int = [](koto_ctx* c, const char* k, long long val) {
        c->db[k ? k : ""] = std::to_string(val);
    };

    api.c_reset = [](koto_ctx* c) { c->composed.clear(); };
    api.c_text = [](koto_ctx* c, const char* s) { if (s) c->composed += s; };
    api.c_fmt = [](koto_ctx* c, const char* s, int) { if (s) c->composed += s; };
    api.c_emoji = [](koto_ctx* c, const char* s, long long) { if (s) c->composed += s; };
    api.c_edit = [](koto_ctx* c, int) { c->edited++; };
    api.c_reply = [](koto_ctx* c, int) { c->replied++; };
    api.c_send = [](koto_ctx* c, long long, int) { c->sent_msgs++; };

    api.chat_full_json = [](koto_ctx*, long long cid, char* buf, int buflen) -> int {
        if (cid == -100123456789LL) {
            std::string j = "{\"id\":-100123456789,\"title\":\"Target Chat\",\"participant_ids\":[10,20,30,40],\"admin_ids\":[10],\"bot_ids\":[20]}";
            int to_copy = std::min((int)j.size(), buflen - 1);
            std::memcpy(buf, j.data(), to_copy);
            buf[to_copy] = '\0';
            return to_copy;
        }
        return 0;
    };

    koto_ctx ctx;

    // 1. Установка шаблона сообщения (.invsetmsg)
    ctx.args = "Привет! Заходи в наш чат: @kotobas";
    mod->commands[2].handler(&ctx, &api);
    CHECK(ctx.composed.find("Шаблон сообщения сохранён") != std::string::npos);
    CHECK(ctx.db["msg_template"] == "Привет! Заходи в наш чат: @kotobas");

    // 2. Настройка параметров (.invcfg)
    ctx.args = "delay 20 45";
    ctx.composed.clear();
    mod->commands[4].handler(&ctx, &api); // invcfg
    CHECK(ctx.composed.find("20–45") != std::string::npos);
    CHECK(ctx.db["delay_min"] == "20");
    CHECK(ctx.db["delay_max"] == "45");

    // 3. Формирование очереди через .invite
    ctx.args = "";
    ctx.composed.clear();
    mod->commands[0].handler(&ctx, &api); // invite
    CHECK(ctx.composed.find("Очередь Inviter сформирована") != std::string::npos);
    CHECK(ctx.composed.find("Target Chat") != std::string::npos);
    CHECK(ctx.sent_msgs > 0);

    // 4. Проверка статуса (.invstatus)
    ctx.args = "";
    ctx.composed.clear();
    mod->commands[3].handler(&ctx, &api); // invstatus
    CHECK(ctx.composed.find("Статус Inviter Pro") != std::string::npos);
    CHECK(ctx.composed.find("Успешно отправлено: 1") != std::string::npos);

    // 5. Очистка очереди (.invclear)
    ctx.composed.clear();
    mod->commands[5].handler(&ctx, &api); // invclear
    CHECK(ctx.composed.find("очищены") != std::string::npos);
    CHECK(ctx.db.find("queue") == ctx.db.end());

    std::printf("Inviter tests: %d checks passed successfully!\n", checks);
    return 0;
}
