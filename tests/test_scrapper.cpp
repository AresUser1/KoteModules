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
    std::string sent_file_name;
    std::string sent_file_content;
    std::map<std::string, std::string> db;
    long long chat = -100123456789LL;
    long long sender = 777000LL;
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

    CHECK(std::string(mod->name) == "scrapper");
    CHECK(std::string(mod->version) == "1.0.4");
    CHECK(mod->command_count == 5);
    CHECK(std::string(mod->commands[0].name) == "scrap");
    CHECK(std::string(mod->commands[1].name) == "scrapper");
    CHECK(std::string(mod->commands[2].name) == "scrapcfg");
    CHECK(std::string(mod->commands[3].name) == "stopscrap");
    CHECK(std::string(mod->commands[4].name) == "killscrap");

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

    api.c_send_bytes = [](koto_ctx* c, long long, const char* name, const unsigned char* data, int len, int) {
        c->sent_file_name = name ? name : "";
        if (data && len > 0) {
            c->sent_file_content.assign(reinterpret_cast<const char*>(data), len);
        }
    };

    api.chat_full_json = [](koto_ctx*, long long cid, char* buf, int buflen) -> int {
        if (cid == -100123456789LL) {
            std::string j = "{\"id\":-100123456789,\"title\":\"Test Group\",\"username\":\"testgroup\","
                            "\"participant_ids\":[111,222,333,444],\"admin_ids\":[111],\"bot_ids\":[222]}";
            int to_copy = std::min((int)j.size(), buflen - 1);
            std::memcpy(buf, j.data(), to_copy);
            buf[to_copy] = '\0';
            return to_copy;
        }
        return 0;
    };

    api.user_username = [](koto_ctx*, long long uid, char* buf, int buflen) -> int {
        std::string u = "";
        if (uid == 333) u = "user333";
        if (uid == 444) u = "user444";
        if (u.empty()) return 0;
        int to_copy = std::min((int)u.size(), buflen - 1);
        std::memcpy(buf, u.data(), to_copy);
        buf[to_copy] = '\0';
        return to_copy;
    };

    koto_ctx ctx;

    // 1. Проверка .scrapcfg без аргументов
    ctx.args = "";
    mod->commands[2].handler(&ctx, &api); // scrapcfg
    CHECK(ctx.composed.find("Настройки скраппера") != std::string::npos);
    CHECK(ctx.composed.find("MEMBERS") != std::string::npos);

    // 2. Изменение настроек
    ctx.args = "limit 100";
    ctx.composed.clear();
    mod->commands[2].handler(&ctx, &api);
    CHECK(ctx.composed.find("Лимит сбора установлен на: 100") != std::string::npos);

    // 3. Выполнение .scrap с фильтрацией (admin 111 и bot 222 должны быть пропущены)
    ctx.args = "";
    ctx.composed.clear();
    mod->commands[0].handler(&ctx, &api); // scrap
    CHECK(ctx.composed.find("Сбор участников успешно завершён") != std::string::npos);
    CHECK(ctx.composed.find("Собрано: 2") != std::string::npos); // 333 и 444
    CHECK(!ctx.sent_file_name.empty());
    CHECK(ctx.sent_file_content.find("@user333") != std::string::npos);
    CHECK(ctx.sent_file_content.find("@user444") != std::string::npos);
    CHECK(ctx.sent_file_content.find("111") == std::string::npos); // админ пропущен
    CHECK(ctx.sent_file_content.find("222") == std::string::npos); // бот пропущен

    api.get_twins_json = [](koto_ctx*, char* buf, int buflen) -> int {
        std::string j = "[{\"uid\":7767394972,\"name\":\"Umbra\",\"is_main\":false,\"online\":true}]";
        int to_copy = std::min((int)j.size(), buflen - 1);
        std::memcpy(buf, j.data(), to_copy);
        buf[to_copy] = '\0';
        return to_copy;
    };

    api.twin_get_history_json = [](koto_ctx*, long long twin_uid, long long, int, int, int offset, char* buf, int buflen) -> int {
        if (twin_uid == 7767394972LL && offset == 0) {
            std::string j = "[{\"id\":1001,\"sender_id\":333,\"text\":\"hi\"},"
                            "{\"id\":1002,\"sender_id\":333,\"text\":\"msg2\"},"
                            "{\"id\":1003,\"sender_id\":444,\"text\":\"msg3 hello @cooltag\",\"reactors\":[555]}]";
            int to_copy = std::min((int)j.size(), buflen - 1);
            std::memcpy(buf, j.data(), to_copy);
            buf[to_copy] = '\0';
            return to_copy;
        }
        std::string empty = "[]";
        int to_copy = std::min((int)empty.size(), buflen - 1);
        std::memcpy(buf, empty.data(), to_copy);
        buf[to_copy] = '\0';
        return to_copy;
    };

    // 4. Проверка .stopscrap
    ctx.composed.clear();
    mod->commands[3].handler(&ctx, &api);
    CHECK(ctx.composed.find("остановлен") != std::string::npos);

    // 5. Проверка ACTIVITY mode с воркером Umbra, реакциями и тегами
    ctx.args = "method activity";
    mod->commands[2].handler(&ctx, &api);
    CHECK(ctx.composed.find("ACTIVITY") != std::string::npos);

    ctx.args = "reactions off";
    mod->commands[2].handler(&ctx, &api);
    CHECK(ctx.composed.find("Сбор реакций: ВЫКЛ") != std::string::npos);

    ctx.args = "reactions on";
    mod->commands[2].handler(&ctx, &api);
    CHECK(ctx.composed.find("Сбор реакций: ВКЛ") != std::string::npos);

    ctx.args = "tags off";
    mod->commands[2].handler(&ctx, &api);
    CHECK(ctx.composed.find("Сбор тегов: ВЫКЛ") != std::string::npos);

    ctx.args = "tags on";
    mod->commands[2].handler(&ctx, &api);
    CHECK(ctx.composed.find("Сбор тегов: ВКЛ") != std::string::npos);

    ctx.args = "worker Umbra";
    mod->commands[2].handler(&ctx, &api);
    CHECK(ctx.composed.find("Umbra") != std::string::npos);

    ctx.args = "";
    ctx.composed.clear();
    ctx.sent_file_name.clear();
    ctx.sent_file_content.clear();
    mod->commands[0].handler(&ctx, &api); // scrap
    CHECK(ctx.composed.find("Сбор участников успешно завершён") != std::string::npos);
    CHECK(ctx.sent_file_content.find("WORKER: Umbra (ID: 7767394972)") != std::string::npos);
    CHECK(ctx.sent_file_content.find("@user333 | Сообщений: 2") != std::string::npos);
    CHECK(ctx.sent_file_content.find("@user444 | Сообщений: 1") != std::string::npos);
    CHECK(ctx.sent_file_content.find("555 | Реакций: 1") != std::string::npos);
    CHECK(ctx.sent_file_content.find("@cooltag | Упоминаний в чате: 1") != std::string::npos);

    // 6. Проверка указания воркера по числовому ID напрямую
    ctx.args = "worker 7767394972";
    mod->commands[2].handler(&ctx, &api);
    CHECK(ctx.composed.find("7767394972") != std::string::npos);

    ctx.args = "";
    ctx.composed.clear();
    ctx.sent_file_name.clear();
    ctx.sent_file_content.clear();
    mod->commands[0].handler(&ctx, &api); // scrap
    CHECK(ctx.composed.find("Сбор участников успешно завершён") != std::string::npos);
    CHECK(ctx.sent_file_content.find("WORKER: Umbra (ID: 7767394972)") != std::string::npos);

    // 7. Проверка неизвестного воркера
    ctx.args = "worker UnknownTwin";
    mod->commands[2].handler(&ctx, &api);

    ctx.args = "";
    ctx.composed.clear();
    mod->commands[0].handler(&ctx, &api);
    CHECK(ctx.composed.find("не найден или не активен") != std::string::npos);

    std::printf("Scrapper tests: %d checks passed successfully!\n", checks);
    return 0;
}
