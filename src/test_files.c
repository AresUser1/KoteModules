// test_files.c
// Тестовый модуль для проверки отправки файлов в Kotogram (C/C++ ABI 3).
// Команды:
// .tfinfo  - Проверка диагностики: chat_id, cache_dir, права записи
// .tfurl <url> - Отправка файла напрямую по ссылке
// .tfcache - Запись файла в cache_dir и отправка по абсолютному пути
// .tfrel   - Отправка файла по относительному пути (test_rel.txt)
// .tfmem   - Отправка сгенерированного файла из оперативной памяти (c_send_bytes)

#include "module_abi.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// 1. Диагностика путей и прав доступа
static void cmd_tf_info(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);
    long long me = api->get_me(ctx);

    char cache[512] = {0};
    int clen = api->cache_dir(ctx, cache, sizeof(cache));

    // Проверяем возможность записи в cache_dir
    char test_path[600];
    snprintf(test_path, sizeof(test_path), "%s/write_probe.tmp", cache);
    FILE* f = fopen(test_path, "w");
    int writable = 0;
    if (f) {
        writable = 1;
        fputs("kotogram write probe ok\n", f);
        fclose(f);
        remove(test_path);
    }

    api->c_reset(ctx);
    api->c_markdown(ctx, "🛠 **Диагностика отправки файлов (Kotogram ABI 3)**\n\n");
    
    char line[256];
    snprintf(line, sizeof(line), "👤 **Me ID:** `%lld`\n", me);
    api->c_markdown(ctx, line);

    snprintf(line, sizeof(line), "💬 **Chat ID:** `%lld`\n", cid);
    api->c_markdown(ctx, line);

    snprintf(line, sizeof(line), "📁 **Cache Dir:** `%s` (len=%d)\n", cache, clen);
    api->c_markdown(ctx, line);

    if (writable) {
        api->c_markdown(ctx, "✅ **Доступ на запись в кэш:** Доступен (OK)");
    } else {
        api->c_markdown(ctx, "❌ **Доступ на запись в кэш:** Ошибка записи!");
    }

    api->c_edit(ctx, 0);
}

// 2. Отправка файла по веб-ссылке (URL)
static void cmd_tf_url(koto_ctx* ctx, const koto_api* api) {
    const char* args = api->cmd_args(ctx);
    while (args && (*args == ' ' || *args == '\t')) args++;
    if (!args || !*args) {
        api->c_reset(ctx);
        api->c_markdown(ctx, "⚠️ **Использование:** `.tfurl <ссылка_на_файл>`");
        api->c_edit(ctx, 0);
        return;
    }

    char url[1024];
    if (sscanf(args, "%1023s", url) != 1) return;

    long long cid = api->chat_id(ctx);

    api->c_reset(ctx);
    api->c_markdown(ctx, "⬇️ **Скачивание и отправка файла по ссылке...**\n> ");
    api->c_text(ctx, url);
    api->c_edit(ctx, 0);

    // Подпись к самому файлу
    api->c_reset(ctx);
    api->c_markdown(ctx, "📦 **Файл успешно отправлен по ссылке:**\n> ");
    api->c_text(ctx, url);

    api->c_send_file(ctx, cid, url, 0);
}

// 3. Создание файла в кэше и отправка по абсолютному пути
static void cmd_tf_cache(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);

    char cache[512] = {0};
    api->cache_dir(ctx, cache, sizeof(cache));

    char path[600];
    snprintf(path, sizeof(path), "%s/test_from_cache.txt", cache);

    FILE* f = fopen(path, "w");
    if (!f) {
        api->c_reset(ctx);
        api->c_markdown(ctx, "❌ **Ошибка:** Не удалось создать файл в кэше!");
        api->c_edit(ctx, 0);
        return;
    }

    time_t now = time(NULL);
    fprintf(f, "Kotogram File Send Test\nTimestamp: %ld\nChat: %lld\nStatus: OK!\n", (long)now, cid);
    fclose(f);

    api->c_reset(ctx);
    api->c_markdown(ctx, "📁 **Локальный файл из кэша отправляется...**");
    api->c_edit(ctx, 0);

    api->c_reset(ctx);
    api->c_markdown(ctx, "✅ **Файл успешно отправлен из кэша устройства!**");
    api->c_send_file(ctx, cid, path, 0);
}

// 4. Отправка файла по относительному пути (проверка авто-резолвинга в движке)
static void cmd_tf_rel(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);

    char cache[512] = {0};
    api->cache_dir(ctx, cache, sizeof(cache));

    char path[600];
    snprintf(path, sizeof(path), "%s/test_rel.txt", cache);

    FILE* f = fopen(path, "w");
    if (f) {
        fputs("Тест относительного пути Kotogram C ABI 3\nРаботает корректно!\n", f);
        fclose(f);
    }

    api->c_reset(ctx);
    api->c_markdown(ctx, "📄 **Отправка файла по относительному пути `test_rel.txt`...**");
    api->c_edit(ctx, 0);

    api->c_reset(ctx);
    api->c_markdown(ctx, "✅ **Файл отправлен по относительному пути!**");
    api->c_send_file(ctx, cid, "test_rel.txt", 0);
}

// 5. Отправка файла напрямую из оперативной памяти
static void cmd_tf_mem(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);

    const char* sample_text = "Привет из Kotogram C/C++ ABI 3!\nЭто файл, сгенерированный в памяти (c_send_bytes).\nВсе системы работают отлично!";
    int len = (int)strlen(sample_text);

    api->c_reset(ctx);
    api->c_markdown(ctx, "💾 **Отправка файла напрямую из памяти...**");
    api->c_edit(ctx, 0);

    api->c_reset(ctx);
    api->c_markdown(ctx, "⚡️ **Файл отправлен из оперативной памяти!**");
    api->c_send_bytes(ctx, cid, "memory_test.txt", (const unsigned char*)sample_text, len, 0);
}

static const koto_command COMMANDS[] = {
    { "tfinfo",  &cmd_tf_info,  KOTO_LEVEL_ALL, "Диагностика чата, путей и кэша.", "" },
    { "tfurl",   &cmd_tf_url,   KOTO_LEVEL_ALL, "Отправить файл по прямой веб-ссылке.", "<url>" },
    { "tfcache", &cmd_tf_cache, KOTO_LEVEL_ALL, "Создать и отправить файл из кэша.", "" },
    { "tfrel",   &cmd_tf_rel,   KOTO_LEVEL_ALL, "Отправить файл по относительному пути.", "" },
    { "tfmem",   &cmd_tf_mem,   KOTO_LEVEL_ALL, "Отправить файл напрямую из памяти.", "" }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "test_files",
    "Тестирование отправки файлов и работы с кэшем (C++ ABI 3)",
    "1.0.0",
    12, // KoteLoader v0.2.1+
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
