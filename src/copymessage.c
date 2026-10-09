// copymessage.c
// Модуль для сохранения сообщений (в т.ч. из каналов/чатов с запретом пересылки) в Избранное.
// Перенос с Python (KoteLoader) на нативный C (ABI 3, .so).
// Команды:
//   .copy   — скопировать сообщение (в ответ на него) в Избранное с отчетом в чате
//   .hcopy  — тихое копирование (команда удаляется, лог не выводится)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "module_abi.h"

#define MAX_FILE_BUF (16 * 1024 * 1024) // До 16 МБ для медиа в памяти

static void do_copy_logic(koto_ctx* ctx, const koto_api* api, int silent) {
    if (!api->check_permission(ctx, KOTO_LEVEL_TRUSTED)) return;

    const char* text = api->reply_text(ctx);
    int ent_count = api->reply_entity_count(ctx);
    long long me_id = api->get_me(ctx);

    // Проверяем наличие текста или файла в reply
    char file_name[256] = {0};
    int has_file_name = api->reply_file_name(ctx, file_name, sizeof(file_name));

    if ((!text || !*text) && (!has_file_name || !file_name[0])) {
        if (!silent) {
            api->c_reset(ctx);
            api->c_fmt(ctx, "❌ Ответьте на сообщение, которое нужно сохранить.", KOTO_ENT_BOLD);
            if (api->is_outgoing(ctx)) api->c_edit(ctx, KOTO_FMT_PLAIN);
            else api->c_reply(ctx, KOTO_FMT_PLAIN);
        } else {
            if (api->is_outgoing(ctx)) api->delete_msg(ctx);
        }
        return;
    }

    if (silent) {
        if (api->is_outgoing(ctx)) api->delete_msg(ctx);
    } else {
        api->c_reset(ctx);
        api->c_text(ctx, "⌛️ Копирую сообщение в Избранное...");
        if (api->is_outgoing(ctx)) api->c_edit(ctx, KOTO_FMT_PLAIN);
    }

    // Если есть прикрепленный файл/медиа
    if (has_file_name && file_name[0]) {
        unsigned char* buf = (unsigned char*)malloc(MAX_FILE_BUF);
        if (buf) {
            long long bytes_read = api->reply_file(ctx, buf, MAX_FILE_BUF);
            if (bytes_read > 0) {
                api->c_reset(ctx);
                if (text && *text) {
                    api->c_text(ctx, text);
                    for (int i = 0; i < ent_count; ++i) {
                        int type = 0, off = 0, len = 0;
                        long long doc_id = 0;
                        char url[256] = {0};
                        if (api->reply_entity(ctx, i, &type, &off, &len, &doc_id, url, sizeof(url))) {
                            api->c_entity(ctx, type, off, len, doc_id, url);
                        }
                    }
                }
                api->c_send_bytes(ctx, me_id, file_name, buf, (int)bytes_read, KOTO_FMT_PLAIN);
                free(buf);

                if (!silent) {
                    api->c_reset(ctx);
                    api->c_fmt(ctx, "✅ Медиафайл успешно скопирован в Избранное!", KOTO_ENT_BOLD);
                    if (api->is_outgoing(ctx)) api->c_edit(ctx, KOTO_FMT_PLAIN);
                    else api->c_reply(ctx, KOTO_FMT_PLAIN);
                }
                return;
            }
            free(buf);
        }
    }

    // Текстовое сообщение
    if (text && *text) {
        api->c_reset(ctx);
        api->c_text(ctx, text);
        for (int i = 0; i < ent_count; ++i) {
            int type = 0, off = 0, len = 0;
            long long doc_id = 0;
            char url[256] = {0};
            if (api->reply_entity(ctx, i, &type, &off, &len, &doc_id, url, sizeof(url))) {
                api->c_entity(ctx, type, off, len, doc_id, url);
            }
        }
        api->c_send(ctx, me_id, KOTO_FMT_PLAIN);

        if (!silent) {
            api->c_reset(ctx);
            api->c_fmt(ctx, "✅ Текст успешно скопирован в Избранное!", KOTO_ENT_BOLD);
            if (api->is_outgoing(ctx)) api->c_edit(ctx, KOTO_FMT_PLAIN);
            else api->c_reply(ctx, KOTO_FMT_PLAIN);
        }
    }
}

static void cmd_copy(koto_ctx* ctx, const koto_api* api) {
    do_copy_logic(ctx, api, 0);
}

static void cmd_hcopy(koto_ctx* ctx, const koto_api* api) {
    do_copy_logic(ctx, api, 1);
}

static const koto_command COMMANDS[] = {
    {
        "copy",
        &cmd_copy,
        KOTO_LEVEL_TRUSTED,
        "Копирование сообщения (в т.ч. защищенного) в Избранное с отчетом",
        "[в ответ на сообщение]"
    },
    {
        "hcopy",
        &cmd_hcopy,
        KOTO_LEVEL_TRUSTED,
        "Тихое копирование сообщения в Избранное (без лишних сообщений)",
        "[в ответ на сообщение]"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "copymessage",
    "Копирование сообщений и медиа из защищенных чатов в Избранное",
    "1.3.0",
    0,
    0,
    COMMANDS,
    sizeof(COMMANDS) / sizeof(COMMANDS[0]),
    NULL, 0,
    NULL,
    NULL, 0,
    NULL
};

const koto_module* koto_module_register(void) {
    return &MODULE;
}
