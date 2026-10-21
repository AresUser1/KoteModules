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

struct MockEntity {
    int type;
    int off;
    int len;
    std::string url;
};

struct MockOutput {
    long long chat;
    std::string text;
};

struct koto_ctx {
    std::map<std::string, std::string> db;
    std::string text;
    std::string username = "MafiaBakuBlackBot";
    std::string first_name = "Mafia Baku Black";
    std::string composed;
    long long me = 99999;
    long long sender = 1044037207LL; // Mafia Baku Black ID
    long long chat = -1002404766570LL;
    bool out = false;
    bool permission = true;
    std::vector<MockEntity> entities;
    std::vector<MockOutput> sent;
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

static int write_str(const std::string& value, char* buf, int cap) {
    if (cap > 0) {
        size_t n = std::min(value.size(), size_t(cap - 1));
        std::memcpy(buf, value.data(), n);
        buf[n] = '\0';
    }
    return (int)value.size();
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    CHECK(lib != nullptr);

    auto load = reinterpret_cast<const koto_module*(*)()>(dlsym(lib, "koto_module_register"));
    CHECK(load != nullptr);
    const koto_module* mod = load();
    CHECK(mod != nullptr);

    CHECK(std::string(mod->name) == "mafia_triggers");
    CHECK(std::string(mod->version) == "2.1.4");
    CHECK(mod->command_count == 2);
    CHECK(std::string(mod->commands[0].name) == "wtr");
    CHECK(std::string(mod->commands[1].name) == "mtr");
    CHECK(mod->on_watch != nullptr);
    CHECK(mod->watch_flags & KOTO_WATCH_INCOMING);

    koto_api api{};
    api.chat_id = [](koto_ctx* c) -> long long { return c->chat; };
    api.sender_id = [](koto_ctx* c) -> long long { return c->sender; };
    api.get_me = [](koto_ctx* c) -> long long { return c->me; };
    api.is_outgoing = [](koto_ctx* c) -> int { return c->out ? 1 : 0; };
    api.msg_text = [](koto_ctx* c) -> const char* { return c->text.c_str(); };

    api.db_get = [](koto_ctx* c, const char* k, char* b, int n) -> int {
        auto it = c->db.find(k ? k : "");
        if (it == c->db.end()) return -1;
        return write_str(it->second, b, n);
    };
    api.db_set = [](koto_ctx* c, const char* k, const char* v) {
        if (k && v) c->db[k] = v;
    };

    api.user_username = [](koto_ctx* c, long long uid, char* b, int n) -> int {
        if (uid == c->sender) return write_str(c->username, b, n);
        return 0;
    };
    api.user_first_name = [](koto_ctx* c, long long uid, char* b, int n) -> int {
        if (uid == c->sender) return write_str(c->first_name, b, n);
        return 0;
    };
    api.resolve = [](koto_ctx*, const char* uname) -> long long {
        if (uname && std::string(uname) == "Wraith") return 22222LL;
        if (uname && std::string(uname) == "Exile") return 11111LL;
        if (uname && std::string(uname) == "MafiaBakuBlackBot") return 1044037207LL;
        return 0LL;
    };

    api.msg_entity_count = [](koto_ctx* c) -> int {
        return (int)c->entities.size();
    };
    api.msg_entity = [](koto_ctx* c, int i, int* t, int* o, int* l, long long*, char* b, int n) -> int {
        if (i < 0 || i >= (int)c->entities.size()) return 0;
        const auto& e = c->entities[i];
        if (t) *t = e.type;
        if (o) *o = e.off;
        if (l) *l = e.len;
        write_str(e.url, b, n);
        return 1; // Возвращает 1 при успехе!
    };

    api.c_reset = [](koto_ctx* c) { c->composed.clear(); };
    api.c_text = [](koto_ctx* c, const char* s) { if (s) c->composed += s; };
    api.c_fmt = [](koto_ctx* c, const char* s, int) { if (s) c->composed += s; };
    api.c_send = [](koto_ctx* c, long long chat, int) { c->sent.push_back({chat, c->composed}); };
    api.c_edit = [](koto_ctx* c, int) { c->edited++; };
    api.c_reply = [](koto_ctx* c, int) { c->replied++; };
    api.check_permission = [](koto_ctx* c, int) -> int { return c->permission ? 1 : 0; };

    koto_ctx ctx;

    // 1. Проверка .wtr (активация и деактивация)
    mod->commands[0].handler(&ctx, &api);
    CHECK(ctx.db["warn_chats"].find("-1002404766570") != std::string::npos);
    CHECK(ctx.composed.find("Активирован (Warn)") != std::string::npos);

    mod->commands[0].handler(&ctx, &api);
    CHECK(ctx.db["warn_chats"].empty());
    CHECK(ctx.composed.find("Деактивирован (Warn)") != std::string::npos);

    // 2. Проверка .mtr (активация)
    mod->commands[1].handler(&ctx, &api);
    CHECK(ctx.db["mute_chats"].find("-1002404766570") != std::string::npos);
    CHECK(ctx.composed.find("Активирован (Mute)") != std::string::npos);

    // 3. Сообщение от Mafia Baku Black с двумя уснувшими игроками (Exile и Wraith)
    // Текст 1 в 1 как из реального лога пользователя
    ctx.text = "Комиссар Каттани 𝙺𝚝😸 Exile ♱ пообещал больше не спать во время игры, но умер.\n\n"
               "Дон 𝙺𝚝😸 Wraith ♱ пообещал больше не спать во время игры, но умер.";

    // Entity 0: Exile (tg://user?id=11111)
    // Entity 1: Wraith (tg://user?id=22222)
    ctx.entities = {
        {KOTO_ENT_URL, 18, 14, "tg://user?id=11111"},
        {KOTO_ENT_URL, 88, 15, "tg://user?id=22222"}
    };

    ctx.sent.clear();
    mod->on_watch(&ctx, &api);

    // Должно быть отправлено ровно 2 мута: для Exile (11111) и Wraith (22222)
    CHECK(ctx.sent.size() == 2);
    CHECK(ctx.sent[0].text == "!mute 11111 2h сон");
    CHECK(ctx.sent[1].text == "!mute 22222 2h сон");

    // 4. Проверяем также включение Warn-триггера (активированы оба: Mute + Warn)
    mod->commands[0].handler(&ctx, &api); // Включаем Warn
    ctx.sent.clear();
    mod->on_watch(&ctx, &api);

    // При обоих включенных должны сработать оба наказания для обоих нарушителей
    CHECK(ctx.sent.size() == 4);
    CHECK(ctx.sent[0].text == "!warn 11111 сон");
    CHECK(ctx.sent[1].text == "!mute 11111 2h сон");
    CHECK(ctx.sent[2].text == "!warn 22222 сон");
    CHECK(ctx.sent[3].text == "!mute 22222 2h сон");

    // 5. Изоляция контекста: один игрок убит мафией, второй уснул (AFK)
    ctx.text = "Мирный Житель 𝙺𝚝😸 Antichrist погиб от пули мафии этой ночью.\n\n"
               "Дон 𝙺𝚝😸 Wraith ♱ пообещал больше не спать во время игры, но умер.";
    ctx.entities = {
        {KOTO_ENT_URL, 15, 18, "tg://user?id=33333"},
        {KOTO_ENT_URL, 85, 15, "tg://user?id=22222"}
    };

    ctx.sent.clear();
    mod->on_watch(&ctx, &api);

    // Только Wraith должен быть наказан! Antichrist не должен наказываться!
    CHECK(ctx.sent.size() == 2); // 1 warn + 1 mute для Wraith
    CHECK(ctx.sent[0].text == "!warn 22222 сон");
    CHECK(ctx.sent[1].text == "!mute 22222 2h сон");

    // 6. Триггер выхода (Leave)
    ctx.text = "Дон 𝙺𝚝😸 Wraith ♱ не выдержал гнетущей тишины и покинул игру.";
    ctx.entities = {
        {KOTO_ENT_URL, 4, 15, "tg://user?id=22222"}
    };
    ctx.sent.clear();
    mod->on_watch(&ctx, &api);
    CHECK(ctx.sent.size() == 2);
    CHECK(ctx.sent[0].text == "!warn 22222 выход");
    CHECK(ctx.sent[1].text == "!mute 22222 2h выход");

    // 7. Упоминание через @username (KOTO_ENT_MENTION)
    ctx.text = "Дон @Wraith пообещал больше не спать во время игры, но умер.";
    ctx.entities = {
        {KOTO_ENT_MENTION, 4, 7, ""}
    };
    ctx.sent.clear();
    mod->on_watch(&ctx, &api);
    CHECK(ctx.sent.size() == 2);
    CHECK(ctx.sent[0].text == "!warn 22222 сон");
    CHECK(ctx.sent[1].text == "!mute 22222 2h сон");

    // 8. Игнорирование сообщений от обычных пользователей
    ctx.sender = 55555LL; // Не бот
    ctx.username = "user55555";
    ctx.first_name = "Regular User";
    ctx.sent.clear();
    mod->on_watch(&ctx, &api);
    CHECK(ctx.sent.empty());

    dlclose(lib);
    std::printf("Mafia Triggers test: %d checks passed successfully!\n", checks);
    return 0;
}
