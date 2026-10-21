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

struct MockButton {
    std::string text;
    std::string data;
    int row;
};

struct koto_ctx {
    std::map<std::string, std::string> db;
    std::string text;
    std::string args;
    std::string cb_data_str;
    std::string cb_answer_toast;
    int cb_answer_alert = 0;
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
    std::vector<MockButton> buttons;
    int edited = 0;
    int replied = 0;
    int deleted = 0;
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
    CHECK(std::string(mod->version) == "2.2.0");
    CHECK(mod->command_count == 7);
    CHECK(std::string(mod->commands[0].name) == "wtr");
    CHECK(std::string(mod->commands[1].name) == "mtr");
    CHECK(std::string(mod->commands[2].name) == "mtrcfg");
    CHECK(std::string(mod->commands[3].name) == "mtrtime");
    CHECK(std::string(mod->commands[4].name) == "mtrreason");
    CHECK(std::string(mod->commands[5].name) == "mtrleave");
    CHECK(std::string(mod->commands[6].name) == "mtrsettings");
    CHECK(mod->on_watch != nullptr);
    CHECK(mod->watch_flags & KOTO_WATCH_INCOMING);
    CHECK(mod->on_callback != nullptr);
    CHECK(mod->settings != nullptr);
    CHECK(mod->settings_count == 3);

    koto_api api{};
    api.chat_id = [](koto_ctx* c) -> long long { return c->chat; };
    api.sender_id = [](koto_ctx* c) -> long long { return c->sender; };
    api.get_me = [](koto_ctx* c) -> long long { return c->me; };
    api.is_outgoing = [](koto_ctx* c) -> int { return c->out ? 1 : 0; };
    api.msg_text = [](koto_ctx* c) -> const char* { return c->text.c_str(); };
    api.cmd_args = [](koto_ctx* c) -> const char* { return c->args.c_str(); };

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
        return 1;
    };

    api.c_reset = [](koto_ctx* c) { c->composed.clear(); c->buttons.clear(); };
    api.c_text = [](koto_ctx* c, const char* s) { if (s) c->composed += s; };
    api.c_fmt = [](koto_ctx* c, const char* s, int) { if (s) c->composed += s; };
    api.c_send = [](koto_ctx* c, long long chat, int) { c->sent.push_back({chat, c->composed}); };
    api.c_edit = [](koto_ctx* c, int) { c->edited++; };
    api.c_reply = [](koto_ctx* c, int) { c->replied++; };
    api.c_button = [](koto_ctx* c, const char* text, const char* cb, int row) {
        c->buttons.push_back({text ? text : "", cb ? cb : "", row});
    };
    api.cb_data = [](koto_ctx* c) -> const char* { return c->cb_data_str.c_str(); };
    api.cb_answer = [](koto_ctx* c, const char* text, int alert) {
        c->cb_answer_toast = text ? text : "";
        c->cb_answer_alert = alert;
    };
    api.delete_msg = [](koto_ctx* c) { c->deleted++; };
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

    // 3. Проверка .mtrcfg без аргументов (карточка настроек)
    ctx.args = "";
    mod->commands[2].handler(&ctx, &api);
    CHECK(ctx.composed.find("Настройки MafiaTriggers v2.2.0") != std::string::npos);
    CHECK(ctx.composed.find("Длительность мута: 2h") != std::string::npos);
    CHECK(ctx.composed.find("Причина за AFK: сон") != std::string::npos);

    // 4. Проверка .mtrcfg time 30m / .mtrcfg reason 1.2 АФК
    ctx.args = "time 30m";
    mod->commands[2].handler(&ctx, &api);
    CHECK(ctx.db["cfg_mute_time"] == "30m");

    ctx.args = "reason 1.2 АФК";
    mod->commands[2].handler(&ctx, &api);
    CHECK(ctx.db["cfg_afk_reason"] == "1.2 АФК");

    ctx.args = "leave 1.3 Лив";
    mod->commands[2].handler(&ctx, &api);
    CHECK(ctx.db["cfg_leave_reason"] == "1.3 Лив");

    // 5. Проверка шорткатов .mtrtime, .mtrreason, .mtrleave
    ctx.args = "4h";
    mod->commands[3].handler(&ctx, &api);
    CHECK(ctx.db["cfg_mute_time"] == "4h");

    ctx.args = "сон во время игры";
    mod->commands[4].handler(&ctx, &api);
    CHECK(ctx.db["cfg_afk_reason"] == "сон во время игры");

    ctx.args = "выход из игры";
    mod->commands[5].handler(&ctx, &api);
    CHECK(ctx.db["cfg_leave_reason"] == "выход из игры");

    // 6. Проверка .mtrsettings (интерактивная панель с кнопками)
    mod->commands[6].handler(&ctx, &api);
    CHECK(ctx.composed.find("Панель настроек MafiaTriggers v2.2.0") != std::string::npos);
    CHECK(!ctx.buttons.empty());

    // 7. Проверка on_callback
    ctx.cb_data_str = "mtr:time:1d";
    mod->on_callback(&ctx, &api);
    CHECK(ctx.db["cfg_mute_time"] == "1d");
    CHECK(ctx.cb_answer_toast.find("1d") != std::string::npos);

    ctx.cb_data_str = "mtr:afk:1.2 АФК";
    mod->on_callback(&ctx, &api);
    CHECK(ctx.db["cfg_afk_reason"] == "1.2 АФК");

    ctx.cb_data_str = "mtr:close";
    mod->on_callback(&ctx, &api);
    CHECK(ctx.deleted == 1);

    // 8. Сообщение от Mafia Baku Black с кастомным временем мута и кастомной причиной
    ctx.text = "Комиссар Каттани 𝙺𝚝😸 Exile ♱ пообещал больше не спать во время игры, но умер.\n\n"
               "Дон 𝙺𝚝😸 Wraith ♱ пообещал больше не спать во время игры, но умер.";
    ctx.entities = {
        {KOTO_ENT_URL, 18, 14, "tg://user?id=11111"},
        {KOTO_ENT_URL, 88, 15, "tg://user?id=22222"}
    };

    ctx.sent.clear();
    mod->on_watch(&ctx, &api);

    // Должно быть отправлено ровно 2 мута с кастомным временем (1d) и кастомной причиной (1.2 АФК)
    CHECK(ctx.sent.size() == 2);
    CHECK(ctx.sent[0].text == "!mute 11111 1d 1.2 АФК");
    CHECK(ctx.sent[1].text == "!mute 22222 1d 1.2 АФК");

    // 9. Проверяем также кастомную причину при ливе
    ctx.text = "Дон 𝙺𝚝😸 Wraith ♱ не выдержал гнетущей тишины и покинул игру.";
    ctx.entities = {
        {KOTO_ENT_URL, 4, 15, "tg://user?id=22222"}
    };
    ctx.sent.clear();
    mod->on_watch(&ctx, &api);
    CHECK(ctx.sent.size() == 1);
    CHECK(ctx.sent[0].text == "!mute 22222 1d выход из игры");

    // 10. Сброс настроек на умолчания (.mtrcfg reset)
    ctx.args = "reset";
    mod->commands[2].handler(&ctx, &api);
    CHECK(ctx.db["cfg_mute_time"] == "2h");
    CHECK(ctx.db["cfg_afk_reason"] == "сон");
    CHECK(ctx.db["cfg_leave_reason"] == "выход");

    // Проверяем что после сброса используются стандартные 2h и сон
    ctx.text = "Дон 𝙺𝚝😸 Wraith ♱ пообещал больше не спать во время игры, но умер.";
    ctx.entities = {
        {KOTO_ENT_URL, 4, 15, "tg://user?id=22222"}
    };
    ctx.sent.clear();
    mod->on_watch(&ctx, &api);
    CHECK(ctx.sent.size() == 1);
    CHECK(ctx.sent[0].text == "!mute 22222 2h сон");

    dlclose(lib);
    std::printf("Mafia Triggers test: %d checks passed successfully!\n", checks);
    return 0;
}
