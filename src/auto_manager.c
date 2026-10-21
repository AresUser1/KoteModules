#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
// auto_manager.c — Модуль автоматизации (авточтение, твинки, автоодобрение) (C ABI 3).
// Портирован из Python-модуля auto_manager.py (v1.1.2) в чистый C.
// Автор: Kote

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include "module_abi.h"

#define EMO_SUCCESS 5774022692642492953LL // ✅
#define EMO_ERROR   5985346521103604145LL // ❌
#define EMO_LIST    5956561916573782596LL // 📋

static void send_result(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) {
        api->c_edit(ctx, 0);
    } else {
        api->c_reply(ctx, 0);
    }
}

static const char* get_prefix(koto_ctx* ctx, const koto_api* api, char* buf, int n) {
    int pn = api->setting_get(ctx, "prefix", buf, n);
    return (pn > 0 && buf[0]) ? buf : ".";
}

// ── Хранение списков чатов в БД модуля (newline-delimited IDs) ────────────────

static int chat_set_has(koto_ctx* ctx, const koto_api* api, const char* key, long long target_id) {
    char buf[8192] = {0};
    int n = api->db_get(ctx, key, buf, sizeof(buf) - 1);
    if (n <= 0) return 0;
    buf[n < (int)sizeof(buf) ? n : (int)sizeof(buf)-1] = '\0';
    char target_str[64];
    snprintf(target_str, sizeof(target_str), "%lld", target_id);

    char* saveptr = NULL;
    char* line = strtok_r(buf, "\r\n", &saveptr);
    while (line) {
        while (*line == ' ') line++;
        if (strcmp(line, target_str) == 0) return 1;
        line = strtok_r(NULL, "\r\n", &saveptr);
    }
    return 0;
}

static int chat_set_toggle(koto_ctx* ctx, const koto_api* api, const char* key, long long target_id, int on) {
    char buf[8192] = {0};
    int n = api->db_get(ctx, key, buf, sizeof(buf) - 1);
    if (n < 0) n = 0;
    buf[n < (int)sizeof(buf) ? n : (int)sizeof(buf)-1] = '\0';

    long long ids[256];
    int count = 0;

    char* saveptr = NULL;
    char* line = strtok_r(buf, "\r\n", &saveptr);
    while (line && count < 256) {
        while (*line == ' ') line++;
        if (*line) {
            errno=0;char* number_end=NULL;
            long long id = strtoll(line, &number_end, 10);
            while(number_end && isspace((unsigned char)*number_end)) ++number_end;
            if(errno || number_end==line || (number_end && *number_end)) {line=strtok_r(NULL,"\r\n",&saveptr);continue;}
            int exists = 0;
            for (int i = 0; i < count; ++i) {
                if (ids[i] == id) { exists = 1; break; }
            }
            if (!exists) {
                ids[count++] = id;
            }
        }
        line = strtok_r(NULL, "\r\n", &saveptr);
    }

    if (on) {
        int found = 0;
        for (int i = 0; i < count; ++i) {
            if (ids[i] == target_id) { found = 1; break; }
        }
        if (!found && count >= 256) return 0;
        if (!found) ids[count++] = target_id;
    } else {
        int new_count = 0;
        for (int i = 0; i < count; ++i) {
            if (ids[i] != target_id) {
                ids[new_count++] = ids[i];
            }
        }
        count = new_count;
    }

    char out[8192] = {0};
    int off = 0;
    for (int i = 0; i < count; ++i) {
        off += snprintf(out + off, sizeof(out) - off, "%lld\n", ids[i]);
        if (off >= (int)sizeof(out) - 32) break;
    }
    api->db_set(ctx, key, out);
    return 1;
}

// ── Получение списка твинков (через twin_list или get_twins_json) ─────────────

typedef struct {
    long long uid;
    char name[64];
} TwinItem;

static int get_all_twins(koto_ctx* ctx, const koto_api* api, TwinItem* out_twins, int max_twins) {
    int count = 0;

    // 1. Попытка через twin_list ("uid\tname\tphone\n...")
    if (api->twin_list) {
        char tw_buf[4096] = {0};
        int tw_len = api->twin_list(ctx, tw_buf, sizeof(tw_buf) - 1);
        if (tw_len > 0) {
            tw_buf[tw_len < (int)sizeof(tw_buf) ? tw_len : (int)sizeof(tw_buf)-1] = '\0';
            char* saveptr = NULL;
            char* line = strtok_r(tw_buf, "\r\n", &saveptr);
            while (line && count < max_twins) {
                char* tab1 = strchr(line, '\t');
                if (tab1) {
                    *tab1 = '\0';
                    long long uid = strtoll(line, NULL, 10);
                    if (uid > 0) {
                        char* nm = tab1 + 1;
                        char* tab2 = strchr(nm, '\t');
                        if (tab2) *tab2 = '\0';
                        out_twins[count].uid = uid;
                        snprintf(out_twins[count].name, sizeof(out_twins[count].name), "%s", nm[0] ? nm : "Twin");
                        count++;
                    }
                }
                line = strtok_r(NULL, "\r\n", &saveptr);
            }
        }
    }

    // 2. Fallback через get_twins_json (если twin_list пуст)
    if (count == 0 && api->get_twins_json) {
        char json_buf[16384] = {0};
        int jlen = api->get_twins_json(ctx, json_buf, sizeof(json_buf) - 1);
        if (jlen > 0) {
            json_buf[jlen < (int)sizeof(json_buf) ? jlen : (int)sizeof(json_buf)-1] = '\0';
            const char* p = json_buf;
            while (*p && count < max_twins) {
                const char* obj_start = strchr(p, '{');
                if (!obj_start) break;
                const char* obj_end = strchr(obj_start, '}');
                if (!obj_end) break;

                const char* main_pos = strstr(obj_start, "\"is_main\":");
                int is_main = 0;
                if (main_pos && main_pos < obj_end) {
                    const char* val = main_pos + 10;
                    while (*val == ' ') val++;
                    if (strncmp(val, "true", 4) == 0) is_main = 1;
                }

                if (!is_main) {
                    long long uid = 0;
                    const char* uid_pos = strstr(obj_start, "\"uid\":");
                    if (uid_pos && uid_pos < obj_end) {
                        uid = strtoll(uid_pos + 6, NULL, 10);
                    }

                    char name[64] = "Twin";
                    const char* name_pos = strstr(obj_start, "\"name\":");
                    if (name_pos && name_pos < obj_end) {
                        const char* q1 = strchr(name_pos + 7, '"');
                        if (q1 && q1 < obj_end) {
                            const char* q2 = strchr(q1 + 1, '"');
                            if (q2 && q2 < obj_end) {
                                int nlen = (int)(q2 - (q1 + 1));
                                if (nlen >= (int)sizeof(name)) nlen = (int)sizeof(name) - 1;
                                memcpy(name, q1 + 1, nlen);
                                name[nlen] = '\0';
                            }
                        }
                    }

                    if (uid > 0) {
                        out_twins[count].uid = uid;
                        snprintf(out_twins[count].name, sizeof(out_twins[count].name), "%s", name);
                        count++;
                    }
                }

                p = obj_end + 1;
            }
        }
    }

    return count;
}

// ══════════════════════════════════════════════════════════════════════════════
// АВТОЧТЕНИЕ
// ══════════════════════════════════════════════════════════════════════════════

// Persist group and channel IDs in distinct Bot API domains. Older allow-list
// entries may contain a raw positive MTProto ID; they remain readable below.
static long long canonical_chat(koto_ctx* ctx, const koto_api* api) {
    long long id=api->chat_id(ctx);
    int type=api->chat_type ? api->chat_type(ctx) : KOTO_CHAT_UNKNOWN;
    if (!id || id==LLONG_MIN || type==KOTO_CHAT_UNKNOWN) return 0;
    if (type==KOTO_CHAT_CHANNEL) {
        long long raw=id<=-1000000000000LL ? -(id+1000000000000LL) : (id<0?-id:id);
        return raw>0 && raw<1000000000000LL ? -1000000000000LL-raw : 0;
    }
    if (type==KOTO_CHAT_GROUP) return id<0?id:-id;
    return id<0?-id:id;
}
static int parse_id(const char* text,long long* id) {
    if (!text || !*text) return 0;
    errno=0;char* end=NULL;long long value=strtoll(text,&end,10);
    if (errno || !end || *end || !value || value==LLONG_MIN) return 0;
    *id=value;return 1;
}
static void cmd_autread(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx,KOTO_LEVEL_TRUSTED)) return;
    const char* raw=api->cmd_args(ctx);
    char args[256]={0};
    if (raw && strlen(raw)>=sizeof(args)) goto usage;
    if (raw) snprintf(args,sizeof(args),"%s",raw);
    char* save=NULL;
    char* mode=strtok_r(args," \t\r\n",&save);
    char* scope=strtok_r(NULL," \t\r\n",&save);
    char* extra=strtok_r(NULL," \t\r\n",&save);
    if (!mode || extra || (strcasecmp(mode,"on") && strcasecmp(mode,"off"))) goto usage;
    int on=!strcasecmp(mode,"on");
    long long target=0;
    int local=scope!=NULL;
    if (local) {
        if (!strcasecmp(scope,"this")) target=canonical_chat(ctx,api);
        else if (!parse_id(scope,&target)) goto usage;
        if (!target) goto usage;
        if (target==api->chat_id(ctx)) target=canonical_chat(ctx,api);
        if (!target) goto usage;
    }
    int ok=1;
    if (!local) {
        if (on) ok=chat_set_toggle(ctx,api,"autoread_chats",0,1);
        else api->db_set(ctx,"autoread_chats","");
    } else if (on) {
        ok=chat_set_toggle(ctx,api,"autoread_chats",target,1);
        if (ok) chat_set_toggle(ctx,api,"autoread_excluded",target,0);
    } else {
        ok=chat_set_toggle(ctx,api,"autoread_excluded",target,1);
        if (ok) {
            chat_set_toggle(ctx,api,"autoread_chats",target,0);
            if (target==canonical_chat(ctx,api)) chat_set_toggle(ctx,api,"autoread_chats",api->chat_id(ctx),0);
        }
    }
    api->c_reset(ctx);
    if (!ok) api->c_text(ctx,"❌ Список чатов заполнен. Настройка не применена.");
    else if (!local) api->c_text(ctx,on ? "✅ Авточтение включено во всех чатах, кроме сохранённых исключений." : "✅ Авточтение основного аккаунта выключено во всех чатах.");
    else {
        char text[256];snprintf(text,sizeof(text),"✅ Авточтение %s в чате %lld.%s",on?"включено":"выключено",target,on?"":" Исключение действует и при включённом режиме для всех чатов.");
        api->c_text(ctx,text);
    }
    send_result(ctx,api);return;
usage:
    api->c_reset(ctx);api->c_text(ctx,"❌ Использование: .autread on|off [this|ID чата]. Неизвестные аргументы не меняют настройки.");
    send_result(ctx,api);
}
static void print_chat_set(koto_ctx* ctx,const koto_api* api,const char* key) {
    char buf[8192]={0};int n=api->db_get(ctx,key,buf,sizeof(buf)-1);
    if (n<0) n=0;
    buf[n<(int)sizeof(buf)?n:(int)sizeof(buf)-1]=0;
    char* save=NULL;char* line=strtok_r(buf,"\r\n",&save);int count=0;
    while (line) {
        long long id=0;
        if (parse_id(line,&id)) {
            if(count<25){char text[64];snprintf(text,sizeof(text),"• %lld\n",id);api->c_text(ctx,text);}
            ++count;
        }
        line=strtok_r(NULL,"\r\n",&save);
    }
    if (!count) api->c_text(ctx,"нет\n");
    if (count>25) {char text[64];snprintf(text,sizeof(text),"… ещё %d\n",count-25);api->c_text(ctx,text);}
}
static void cmd_autreadlist(koto_ctx* ctx,const koto_api* api) {
    if (!api->check_permission(ctx,KOTO_LEVEL_TRUSTED)) return;
    api->c_reset(ctx);
    api->c_text(ctx,chat_set_has(ctx,api,"autoread_chats",0)?"Авточтение: все чаты, кроме исключений.\n":"Авточтение по умолчанию выключено.\n");
    api->c_text(ctx,"\nИсключения (не читать):\n");print_chat_set(ctx,api,"autoread_excluded");
    api->c_text(ctx,"\nЯвно включённые чаты:\n");print_chat_set(ctx,api,"autoread_chats");
    send_result(ctx,api);
}

static void cmd_autreadnow(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    long long cid = api->chat_id(ctx);
    long long mid = api->msg_id ? api->msg_id(ctx) : 0;

    if (!api->read_history || !api->read_mentions || !cid) {
        api->c_reset(ctx);api->c_text(ctx,"❌ Ядро не предоставляет чтение истории и упоминаний.");send_result(ctx,api);return;
    }
    api->read_history(ctx,cid,mid);
    api->read_mentions(ctx,cid);
    api->c_reset(ctx);
    api->c_text(ctx,"✅ История и упоминания прочитаны.");
    send_result(ctx,api);
}

// ══════════════════════════════════════════════════════════════════════════════
// АВТООДОБРЕНИЕ
// ══════════════════════════════════════════════════════════════════════════════

static void cmd_autoapprove(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    char pbuf[32] = ".";
    const char* pfx = get_prefix(ctx, api, pbuf, sizeof(pbuf));

    const char* raw = api->cmd_args(ctx);
    char args[256] = {0};
    if (raw) snprintf(args, sizeof(args), "%s", raw);

    char* p = args;
    while (*p == ' ') p++;
    char* end = p + strlen(p);
    while (end > p && (*(end-1) == ' ' || *(end-1) == '\t' || *(end-1) == '\n' || *(end-1) == '\r')) {
        *(--end) = '\0';
    }

    int on = -1;
    int this_chat = 0;

    if (strncmp(p, "on", 2) == 0 && (p[2] == '\0' || p[2] == ' ')) {
        on = 1;
        p += 2;
    } else if (strncmp(p, "off", 3) == 0 && (p[3] == '\0' || p[3] == ' ')) {
        on = 0;
        p += 3;
    }

    while (*p == ' ') p++;
    if (strcmp(p, "this") == 0) {
        this_chat = 1;
    }

    if (on == -1) {
        api->c_reset(ctx);
        api->c_emoji(ctx, "❌", EMO_ERROR);
        api->c_fmt(ctx, " Использование: ", KOTO_ENT_BOLD);
        char u[128];
        snprintf(u, sizeof(u), "%sautoapprove on [this] или %sautoapprove off [this]", pfx, pfx);
        api->c_fmt(ctx, u, KOTO_ENT_CODE);
        send_result(ctx, api);
        return;
    }

    long long target_id = this_chat ? api->chat_id(ctx) : 0;
    const char* chat_desc = this_chat ? "текущем чате" : "всех чатах";

    chat_set_toggle(ctx, api, "autoapprove_chats", target_id, on);

    api->c_reset(ctx);
    api->c_emoji(ctx, "✅", EMO_SUCCESS);
    api->c_fmt(ctx, " Автоодобрение заявок ", KOTO_ENT_BOLD);
    api->c_fmt(ctx, on ? "включено" : "выключено", KOTO_ENT_CODE);
    char desc[128];
    snprintf(desc, sizeof(desc), " в %s.", chat_desc);
    api->c_fmt(ctx, desc, KOTO_ENT_BOLD);
    send_result(ctx, api);
}

static void cmd_autoapprovelist(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    char buf[8192] = {0};
    int n = api->db_get(ctx, "autoapprove_chats", buf, sizeof(buf) - 1);

    api->c_reset(ctx);
    api->c_emoji(ctx, "📋", EMO_LIST);
    api->c_fmt(ctx, " Чаты с автоодобрением заявок:\n\n", KOTO_ENT_BOLD);

    if (n <= 0) {
        api->c_text(ctx, "Список автоодобрения пуст.");
    } else {
        char* saveptr = NULL;
        char* line = strtok_r(buf, "\r\n", &saveptr);
        int printed = 0;
        while (line) {
            while (*line == ' ') line++;
            if (*line) {
                long long cid = strtoll(line, NULL, 10);
                api->c_text(ctx, "• ");
                if (cid == 0) {
                    api->c_fmt(ctx, "Все чаты (где есть права)\n", KOTO_ENT_BOLD);
                } else {
                    char id_str[64];
                    snprintf(id_str, sizeof(id_str), "%lld\n", cid);
                    api->c_fmt(ctx, id_str, KOTO_ENT_CODE);
                }
                printed++;
            }
            line = strtok_r(NULL, "\r\n", &saveptr);
        }
        if (!printed) {
            api->c_text(ctx, "Список автоодобрения пуст.");
        }
    }
    send_result(ctx, api);
}

// ══════════════════════════════════════════════════════════════════════════════
// УПРАВЛЕНИЕ ТВИНКАМИ (.amtwins)
// ══════════════════════════════════════════════════════════════════════════════

static void render_amtwins_panel(koto_ctx* ctx, const koto_api* api) {
    api->c_reset(ctx);
    api->c_fmt(ctx, "⚙️ AutoManager — Твинки (авточтение)\n\n", KOTO_ENT_BOLD);
    api->c_text(ctx, "• ❌ Выкл — твин не читает сообщения\n");
    api->c_text(ctx, "• ✅ Вкл — твин читает новые входящие сообщения\n");
    api->c_text(ctx, "Управление: .amtwins on ID или .amtwins off ID\nИсключения .autread off this действуют и для твинков.\n\n");

    TwinItem twins[32];
    int count = get_all_twins(ctx, api, twins, 32);

    if (count == 0) {
        api->c_text(ctx, "Нет добавленных твинков.");
        api->c_button(ctx, "❌ Закрыть", "am_close", 1);
        return;
    }

    for (int i = 0; i < count; ++i) {
        char mode_key[64];
        snprintf(mode_key, sizeof(mode_key), "twin_mode:%lld", twins[i].uid);
        char mode_val[32] = {0};
        api->db_get(ctx, mode_key, mode_val, sizeof(mode_val) - 1);
        int mode = (atoi(mode_val) == 1);

        char status[192];
        snprintf(status, sizeof(status), "%s %s — ID %lld\n", mode ? "✅" : "❌", twins[i].name, twins[i].uid);
        api->c_text(ctx, status);

        char btn_label[128];
        snprintf(btn_label, sizeof(btn_label), "👤 %s [%s]", twins[i].name, mode ? "✅ Вкл" : "❌ Выкл");
        api->c_button(ctx, btn_label, "am_noop", 1);

        char toggle_cb[64];
        snprintf(toggle_cb, sizeof(toggle_cb), "amtwins_toggle:%lld", twins[i].uid);
        api->c_button(ctx, mode ? "Отключить ❌" : "Включить ✅", toggle_cb, 1);
    }

    api->c_button(ctx, "❌ Закрыть", "am_close", 1);
}

static void cmd_amtwins(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;
    const char* args = api->cmd_args(ctx);
    while (args && isspace((unsigned char)*args)) ++args;
    if (args && *args && strcmp(args, "list") != 0) {
        char mode[8], id_text[32], extra[2];
        long long uid=0;
        if (sscanf(args, "%7s %31s %1s", mode, id_text, extra) != 2 ||
            (strcmp(mode,"on") && strcmp(mode,"off")) || !parse_id(id_text,&uid) || uid<=0) {
            api->c_reset(ctx);api->c_text(ctx,"❌ Использование: .amtwins [list|on ID|off ID]");send_result(ctx,api);return;
        }
        TwinItem twins[32];int count=get_all_twins(ctx,api,twins,32), found=0;
        for(int i=0;i<count;++i) if(twins[i].uid==uid) found=1;
        if(!found) {
            api->c_reset(ctx);api->c_text(ctx,"❌ Такой твин не добавлен. Список: .amtwins");send_result(ctx,api);return;
        }
        char key[64];snprintf(key,sizeof(key),"twin_mode:%lld",uid);
        api->db_set(ctx,key,strcmp(mode,"on")==0?"1":"0");
    }
    render_amtwins_panel(ctx, api);
    send_result(ctx, api);
}

// ── Watcher: авточтение сообщений ─────────────────────────────────────────────

static int on_watch(koto_ctx* ctx, const koto_api* api) {
    if (api->is_outgoing(ctx)) return 0;

    long long cid = api->chat_id(ctx);
    if (cid == 0) return 0;

    long long canonical=canonical_chat(ctx,api);
    if (!canonical || chat_set_has(ctx,api,"autoread_excluded",canonical)) return 0;
    long long my_id = api->get_me ? api->get_me(ctx) : 0;
    long long mid = api->msg_id ? api->msg_id(ctx) : 0;

    // Read the current registry for every event. Added/removed accounts and
    // isolated module processes must not share a stale process-global cache.
    TwinItem twins[32];
    int count = get_all_twins(ctx, api, twins, 32);
    int is_twin = 0;
    for (int i=0; i<count; ++i) if (my_id>0 && twins[i].uid==my_id) {is_twin=1;break;}

    if (is_twin) {
        char mode_key[64];
        snprintf(mode_key, sizeof(mode_key), "twin_mode:%lld", my_id);
        char mode_val[32] = {0};
        api->db_get(ctx, mode_key, mode_val, sizeof(mode_val) - 1);
        if (atoi(mode_val) == 1) {
            if (api->read_history) api->read_history(ctx, cid, mid);
            if (api->read_mentions) api->read_mentions(ctx, cid);
        }
        return 0;
    }

    // 2. Основной аккаунт: проверяем autoread_chats (0 = все чаты, или cid)
    if (chat_set_has(ctx, api, "autoread_chats", 0) ||
        chat_set_has(ctx, api, "autoread_chats", canonical) ||
        chat_set_has(ctx, api, "autoread_chats", cid)) {
        if (api->read_history) api->read_history(ctx, cid, mid);
        if (api->read_mentions) api->read_mentions(ctx, cid);
    }

    return 0;
}

// ── Коллбэки инлайн кнопок ───────────────────────────────────────────────────

static void on_callback(koto_ctx* ctx, const koto_api* api) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) {
        if (api->cb_answer) api->cb_answer(ctx, "❌ Нет доступа", 1);
        return;
    }

    const char* data = api->cb_data ? api->cb_data(ctx) : NULL;
    if (!data) return;

    if (strcmp(data, "am_noop") == 0) {
        if (api->cb_answer) api->cb_answer(ctx, "", 0);
        return;
    }

    if (strcmp(data, "am_close") == 0) {
        if (api->cb_answer) api->cb_answer(ctx, "Закрыто", 0);
        if (api->delete_msg) api->delete_msg(ctx);
        return;
    }

    if (strncmp(data, "amtwins_toggle:", 15) == 0) {
        long long tw_id = 0;
        TwinItem twins[32];int count=get_all_twins(ctx,api,twins,32), found=0;
        if (parse_id(data+15,&tw_id) && tw_id>0)
            for(int i=0;i<count;++i) if(twins[i].uid==tw_id) found=1;
        if (found) {
            char mode_key[64];
            snprintf(mode_key, sizeof(mode_key), "twin_mode:%lld", tw_id);
            char mode_val[32] = {0};
            api->db_get(ctx, mode_key, mode_val, sizeof(mode_val) - 1);
            int current = (atoi(mode_val) == 1);
            int next = current ? 0 : 1;
            char next_str[16];
            snprintf(next_str, sizeof(next_str), "%d", next);
            api->db_set(ctx, mode_key, next_str);

            if (api->cb_answer) {
                api->cb_answer(ctx, next ? "Твинк включен ✅" : "Твинк выключен ❌", 0);
            }
            render_amtwins_panel(ctx, api);
            api->c_edit(ctx, 0);
        }
        return;
    }
}

static const koto_command COMMANDS[] = {
    {
        "autread",
        &cmd_autread,
        KOTO_LEVEL_TRUSTED,
        "Управление авточтением чатов. Без 'this' — все чаты, с 'this' — текущий.",
        "<on|off> [this|ID]"
    },
    {
        "autreadlist",
        &cmd_autreadlist,
        KOTO_LEVEL_TRUSTED,
        "Список чатов с включённым авточтением.",
        ""
    },
    {
        "autreadnow",
        &cmd_autreadnow,
        KOTO_LEVEL_TRUSTED,
        "Прочитать историю и упоминания в текущем чате прямо сейчас.",
        ""
    },
    {
        "autoapprove",
        &cmd_autoapprove,
        KOTO_LEVEL_TRUSTED,
        "Управление автоодобрением заявок на вступление.",
        "<on|off> [this]"
    },
    {
        "autoapprovelist",
        &cmd_autoapprovelist,
        KOTO_LEVEL_TRUSTED,
        "Список чатов с автоодобрением заявок.",
        ""
    },
    {
        "amtwins",
        &cmd_amtwins,
        KOTO_LEVEL_TRUSTED,
        "Список и управление авточтением твинков.",
        "[list|on ID|off ID]"
    }
};

static const koto_module MODULE = {
    .abi_version      = KOTO_MODULE_ABI,
    .name             = "auto_manager",
    .description      = "Автоматизация: авточтение сообщений, твинки и управление заявками.",
    .version          = "1.2.2",
    .min_host_version = 62,
    .update_url       = "https://raw.githubusercontent.com/AresUser1/KoteModules/main/modules/auto_manager.so",
    .flags            = KOTO_MOD_TWINS,
    .commands         = COMMANDS,
    .command_count    = sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    .settings         = NULL,
    .settings_count   = 0,
    .on_watch         = &on_watch,
    .on_callback      = &on_callback,
    .watch_flags      = KOTO_WATCH_ALL,
};

const koto_module* koto_module_register(void) {
    return &MODULE;
}
