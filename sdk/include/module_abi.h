/* host/module_abi.h  — расширенный C-ABI (ABI 3) для .so-модулей юзербота.
 *
 * Автор модуля включает ТОЛЬКО этот заголовок. Заголовок рассчитан на перенос
 * 15 системных модулей Kote (см. docs/MIGRATION_INVENTORY.md). Хост грузит .so
 * из папки modules/ через dlopen и поддерживает ABI из диапазона [1..3].
 *
 * ─── МОДЕЛЬ ИСПОЛНЕНИЯ (решение юзера: ГИБРИД) ───────────────────────────────
 * • ДЕЙСТВИЯ — декларативные интенты: модуль КОПИТ намерения (составить текст,
 *   отредактировать своё сообщение, отправить файл, удалить, реакция, кнопки,
 *   рестарт…). Хост co_await-ит их ПОСЛЕ возврата из обработчика. Корутины не
 *   пересекают C-границу — как в ABI 2 (reply/log).
 * • ЧТЕНИЯ — синхронные вызовы, помеченные «MAY BLOCK»: там, где без
 *   результата по сети продолжать нельзя (ping/get_me/resolve/контент reply/
 *   скачивание/история). Хост обслуживает их синхронно. Автор модуля знает, что
 *   такой вызов может приостановить обработчик.
 *
 * ─── МОДЕЛЬ МОДУЛЯ ──────────────────────────────────────────────────────────
 * Вместо «один on_message, сам парсит текст» модуль ДЕКЛАРИРУЕТ команды
 * (koto_command[]) с их уровнем доступа/справкой; хост сам парсит префикс+имя и
 * зовёт нужный handler. Дополнительно: on_watch (видит ВСЕ сообщения — для
 * пошаговых диалогов twins, аварийного .resetprefix, ввода значения в cfg) и
 * on_callback (нажатие inline-кнопки). Реестр команд всех модулей строит хост —
 * из него работают help/aliases/minfo/modules/access.
 */
#ifndef KOTO_MODULE_ABI_H
#define KOTO_MODULE_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KOTO_MODULE_ABI 3

/* ─── Уровни доступа (как в Python-Kote: OWNER > TRUSTED(=sudo) > SUPPORT) ─── */
enum {
    KOTO_LEVEL_ALL     = 0,   /* кто угодно (вкл. входящие от чужих) */
    KOTO_LEVEL_SUPPORT = 1,
    KOTO_LEVEL_TRUSTED = 2,   /* он же «sudo» */
    KOTO_LEVEL_OWNER   = 3
};

/* ─── Флаги модуля ─── */
enum {
    KOTO_MOD_SYSTEM    = 1 << 0,  /* показывать в секции «Системные» (help/modules) */
    KOTO_MOD_PROTECTED = 1 << 1,  /* нельзя unload/delete/getm */
    KOTO_MOD_TWINS     = 1 << 2,  /* модуль явно поддерживает работу на твинках */
    KOTO_MOD_NO_TWIN   = 1 << 3   /* строго запрещено запускать на твинках */
};
/* ─── Типы entity для композера текста (UTF-16 офсеты считает ХОСТ) ─── */
enum {
    KOTO_ENT_NONE = 0,
    KOTO_ENT_BOLD, KOTO_ENT_ITALIC, KOTO_ENT_CODE, KOTO_ENT_PRE,
    KOTO_ENT_UNDERLINE, KOTO_ENT_STRIKE,
    KOTO_ENT_URL,            /* text_url — нужен url */
    KOTO_ENT_CUSTOM_EMOJI,   /* нужен document_id */
    KOTO_ENT_BLOCKQUOTE,
    KOTO_ENT_SPOILER, KOTO_ENT_MENTION
};

/* ─── Флаги отправки/редактирования ─── */
enum {
    KOTO_FMT_PLAIN       = 0,
    KOTO_NO_LINK_PREVIEW = 1 << 0, /* link_preview=False */
    KOTO_SEND_AS_FILE    = 1 << 1  /* c_send_file: отправить строго как документ-файл без авто-определения медиа */
};

/* ─── Типы настраиваемых полей (панель cfg) ─── */
enum {
    KOTO_FIELD_BOOL = 0, KOTO_FIELD_INT, KOTO_FIELD_FLOAT, KOTO_FIELD_STR,
    KOTO_FIELD_LIST, KOTO_FIELD_URL, KOTO_FIELD_CHOICE
};

/* ─── Режимы оверлей-холста (overlay_show) ─── */
enum {
    KOTO_OVERLAY_PASSTHROUGH = 0, /* Сквозной режим: клики проходят сквозь холст на рабочий стол */
    KOTO_OVERLAY_DRAW        = 1, /* Режим рисования: касания рисуют на холсте поверх экрана */
    KOTO_OVERLAY_HIDDEN      = 2  /* Холст скрыт */
};

/* ─── Покрас инлайн-кнопки (keyboardButtonStyle) для c_button_ex ─── */
enum {
    KOTO_BTN_DEFAULT = 0,   /* тема приложения */
    KOTO_BTN_PRIMARY,       /* основной (синий) */
    KOTO_BTN_DANGER,        /* деструктивный (красный) */
    KOTO_BTN_SUCCESS        /* позитивный (зелёный) */
};

/* ─── Баннер-картинка для via-bot сообщений (c_banner) ─── */
/* Превью по ссылке над/под текстом — как inline.form(banner=…) в Hikka. Это НЕ
   загруженное медиа, а link-preview: показывается/прячется флагом, кнопки при
   этом остаются. Работает на сообщениях, уходящих через бота-помощника. */
enum {
    KOTO_BANNER_DEFAULT = 0,
    KOTO_BANNER_ABOVE   = 1 << 0,  /* превью НАД текстом (invert_media) */
    KOTO_BANNER_LARGE   = 1 << 1   /* крупное превью (prefer_large_media) */
};

/* ─── Фильтр watcher-хука ─── */
enum {
    KOTO_WATCH_OUTGOING = 1 << 0,  /* наши исходящие (twins-логин, .resetprefix) */
    KOTO_WATCH_INCOMING = 1 << 1,
    KOTO_WATCH_ALL      = KOTO_WATCH_OUTGOING | KOTO_WATCH_INCOMING,
    /* Host >=64: read-only watcher, runs before consuming watchers and on
       periodic unread dialog snapshots. Only read_history/read_mentions intents
       execute in this mode; no commands, replies or other side effects. */
    KOTO_WATCH_UNREAD_SYNC = 1 << 2
};

/* Непрозрачный контекст одного вызова (сообщение + БД модуля + сервисы хоста). */
typedef struct koto_ctx koto_ctx;

/* Peer type comes from MTProto, never from the sign of a numeric chat ID. */
enum { KOTO_CHAT_UNKNOWN=0, KOTO_CHAT_PRIVATE=1, KOTO_CHAT_GROUP=2, KOTO_CHAT_CHANNEL=3 };
/* ban_user: report the target as spam before blocking/banning, host 60+. */
enum { KOTO_BAN_REPORT_SPAM=-1 };
enum {
    KOTO_ADMIN_PROMOTE=1, KOTO_ADMIN_DEMOTE=2, KOTO_ADMIN_UNRANK=3,
    KOTO_ADMIN_RANK=4, KOTO_ADMIN_CLEAR_TAGS=5
};

/* === СТРУКТУРЫ-ВЫХОДЫ для read-запросов =================================== */
typedef struct koto_mod_info {
    const char* name;
    const char* version;
    int loaded;        /* 1/0 */
    int protected_;    /* 1/0 */
    int system;        /* 1/0 */
    long long size;    /* байт файла .so */
    long long mtime;   /* unix-время модификации */
    int cmd_count;
    const char* update_url; /* URL для обновлений с Git/GitHub; NULL если не задан */
} koto_mod_info;

typedef struct koto_db_stats {
    int configs;
    int data_entries;
    long long last_activity;  /* unix */
} koto_db_stats;

/* Описание одного настраиваемого поля модуля (для панели cfg). */
typedef struct koto_setting_field {
    const char* key;
    const char* label;
    int         type;          /* KOTO_FIELD_* */
    const char* default_val;
    const char* choices;       /* для CHOICE: "a,b,c"; иначе NULL */
} koto_setting_field;

/* ========================================================================== *
 *  koto_api — таблица функций, которые ХОСТ предоставляет модулю.             *
 *  Разделена на секции. «MAY BLOCK» = синхронное чтение по сети.               *
 * ========================================================================== */
typedef struct koto_api {

    /* ─── 1. БД модуля (свой неймспейс). data = внутреннее состояние. ─── */
    int  (*db_get)(koto_ctx*, const char* key, char* buf, int buflen); /* len или -1 */
    void (*db_set)(koto_ctx*, const char* key, const char* val);
    long long (*db_get_int)(koto_ctx*, const char* key, long long def);
    void (*db_set_int)(koto_ctx*, const char* key, long long val);
    void (*db_del)(koto_ctx*, const char* key);
    int  (*db_keys)(koto_ctx*, char* buf, int buflen);  /* ключи через '\n' */

    /* config = пользовательские настройки (видны в cfg, считаются отдельно). */
    int  (*cfg_get)(koto_ctx*, const char* key, char* buf, int buflen);
    void (*cfg_set)(koto_ctx*, const char* key, const char* val);
    int  (*cfg_keys)(koto_ctx*, char* buf, int buflen);

    /* ─── 2. Глобальные настройки (общий стор NS_SETTINGS: prefix,
     *        userbot_enabled, repo_url, github_token, profile_media, …). ─── */
    int  (*setting_get)(koto_ctx*, const char* key, char* buf, int buflen);
    void (*setting_set)(koto_ctx*, const char* key, const char* val);
    long long (*setting_get_int)(koto_ctx*, const char* key, long long def);
    void (*setting_set_int)(koto_ctx*, const char* key, long long val);

    /* ─── 3. Текущее сообщение ─── */
    const char* (*msg_text)(koto_ctx*);
    long long   (*chat_id)(koto_ctx*);
    long long   (*sender_id)(koto_ctx*);
    long long   (*msg_id)(koto_ctx*);
    int         (*is_outgoing)(koto_ctx*);
    /* Разобранная команда (хост-роутер): имя без префикса и аргументы после. */
    const char* (*cmd_name)(koto_ctx*);
    const char* (*cmd_args)(koto_ctx*);
    /* Entity входящего текста (profile setbio/setinfo/addfield — round-trip). */
    int (*msg_entity_count)(koto_ctx*);
    int (*msg_entity)(koto_ctx*, int i, int* type, int* off, int* len,
                      long long* document_id, char* url, int url_len);

    /* ─── 4. Инфо о юзере/хосте (часть — MAY BLOCK) ─── */
    long long (*get_me)(koto_ctx*);                                   /* свой id (дёшево) */
    int       (*get_me_name)(koto_ctx*, char* buf, int buflen);
    long long (*ping_ms)(koto_ctx*);                                  /* MAY BLOCK: RTT GetUsers */
    long long (*uptime_sec)(koto_ctx*);
    long long (*resolve)(koto_ctx*, const char* username);            /* @username -> id. 0, если не разрешён хостом */
    int       (*user_name)(koto_ctx*, long long uid, char* buf, int buflen); /* имя отправителя/автора reply; "" для прочих uid */
    int       (*my_bio)(koto_ctx*, char* buf, int buflen);            /* MAY BLOCK: about из UserFull */

    /* ─── 5. Права доступа ─── */
    int  (*user_level)(koto_ctx*);                       /* уровень отправителя */
    int  (*check_permission)(koto_ctx*, int min_level);  /* 1/0 (отправитель >= min) */
    /* trust-листы (settrust/trust/untrust/gettrust/listtrust, access add/del). */
    void (*trust_set)(koto_ctx*, long long uid, int level);
    void (*trust_del)(koto_ctx*, long long uid);
    int  (*trust_get)(koto_ctx*, long long uid);         /* уровень или KOTO_LEVEL_ALL */
    int  (*trust_list)(koto_ctx*, int level, char* buf, int buflen); /* uid'ы через '\n' */
    /* allowed_mods для уровня/юзера (access mods). */
    int  (*allowed_mods_get)(koto_ctx*, long long uid, char* buf, int buflen);
    void (*allowed_mods_set)(koto_ctx*, long long uid, const char* csv);
    /* security-маски команд (access cmd): min-level для конкретной команды. */
    int  (*command_level_get)(koto_ctx*, const char* cmd);
    void (*command_level_set)(koto_ctx*, const char* cmd, int level); /* -1 = сброс в дефолт */

    /* ─── 6. Реестр команд (кросс-модульный; help/aliases/minfo/modules) ─── */
    int (*registry_count)(koto_ctx*);
    int (*registry_get)(koto_ctx*, int i, char* name, int name_len,
                        char* module, int module_len, char* doc, int doc_len,
                        char* usage, int usage_len, int* min_level, int* flags);
    /* сколько модулей объявили команду name (коллизии для aliases). */
    int (*registry_find)(koto_ctx*, const char* name, char* modules_nl, int buflen);
    /* динамические алиасы (aliases): имя -> реальная команда. */
    void (*alias_add)(koto_ctx*, const char* alias, const char* real_cmd, const char* module);
    void (*alias_del)(koto_ctx*, const char* alias);
    int  (*alias_list)(koto_ctx*, char* buf, int buflen); /* "alias\treal\tmodule\n" … */

    /* ─── 7. Композер сообщения (авто UTF-16 офсеты; как Python parts) ─── */
    void (*c_reset)(koto_ctx*);
    void (*c_text)(koto_ctx*, const char* utf8);                      /* обычный run */
    void (*c_fmt)(koto_ctx*, const char* utf8, int entity_type);      /* bold/italic/code/pre/… */
    void (*c_url)(koto_ctx*, const char* utf8, const char* url);
    void (*c_emoji)(koto_ctx*, const char* fallback_utf8, long long document_id);
    void (*c_markdown)(koto_ctx*, const char* md);                    /* хост парсит md (вкл. tg://emoji) */
    /* сырая entity на абсолютный диапазон (profile восстановление сохранённых). */
    void (*c_entity)(koto_ctx*, int type, int off, int len, long long document_id, const char* url);
    int  (*c_mark)(koto_ctx*);                                        /* текущий UTF-16 офсет */
    void (*c_wrap)(koto_ctx*, int from_mark, int entity_type, int collapsed); /* обернуть [from..now) */
    void (*c_button)(koto_ctx*, const char* text, const char* callback_data, int new_row);

    /* ─── 8. Действия над составленным (интенты; выполняются после возврата) ─── */
    void (*c_edit)(koto_ctx*, int flags);                             /* edit своего сообщения-команды */
    void (*c_reply)(koto_ctx*, int flags);
    void (*c_send)(koto_ctx*, long long chat_id, int flags);
    void (*c_edit_message)(koto_ctx*, long long chat_id, long long msg_id, int flags);
    void (*c_send_file)(koto_ctx*, long long chat_id, const char* path, int flags);     /* caption=составленное */
    void (*c_send_bytes)(koto_ctx*, long long chat_id, const char* name,
                         const unsigned char* data, int len, int flags);

    /* ─── 9. Управление модулями (modules: load/unload/reload/minfo/getm/delm) ─── */
    int  (*mod_count)(koto_ctx*);
    int  (*mod_info)(koto_ctx*, int i, koto_mod_info* out);           /* по индексу; 1/0 */
    int  (*mod_find)(koto_ctx*, const char* name, koto_mod_info* out);/* по имени; 1/0 */
    int  (*mod_is_protected)(koto_ctx*, const char* name);
    int  (*mod_file_path)(koto_ctx*, const char* name, char* buf, int buflen);
    /* действия-интенты над модулями (выполняются после возврата). */
    void (*mod_load)(koto_ctx*, const char* name);
    void (*mod_unload)(koto_ctx*, const char* name);
    void (*mod_reload)(koto_ctx*, const char* name);
    void (*mod_delete)(koto_ctx*, const char* name);                 /* delm: снести .so */
    /* установка нового .so (install/upload/forceinstall): положить файл и загрузить. */
    void (*mod_install_file)(koto_ctx*, const char* name,
                             const unsigned char* data, int len);
    /* кастом-эмодзи модулей (setmodemoji/delmodemoji/modemojis). */
    int  (*mod_emoji_get)(koto_ctx*, const char* name, long long* document_id);
    void (*mod_emoji_set)(koto_ctx*, const char* name, long long document_id);
    void (*mod_emoji_del)(koto_ctx*, const char* name);

    /* ─── 10. Сеть/файлы для install/updater (MAY BLOCK) ─── */
    long long (*http_get)(koto_ctx*, const char* url,
                          unsigned char* buf, int buflen);           /* MAY BLOCK: тело; длина или -1 */
    int  (*sha256_hex)(koto_ctx*, const unsigned char* data, int len, char* out65);
    /* контент reply-файла (getm/forceupload по reply на документ). MAY BLOCK. */
    long long (*reply_file)(koto_ctx*, unsigned char* buf, int buflen); /* MAY BLOCK: байты вложения reply */
    int  (*reply_file_name)(koto_ctx*, char* buf, int buflen);

    /* ─── 11. Рестарт/обновление (интенты: restart, update, updatecore, off/on) ─── */
    void (*request_restart)(koto_ctx*);                              /* restart */
    void (*request_update)(koto_ctx*);                               /* update: pull dist-манифеста, обновить .so */
    void (*request_apk_update)(koto_ctx*, const char* apk_url);      /* updatecore: обновить APK */
    void (*request_shutdown)(koto_ctx*);                             /* off (userbot_enabled=0) */
    void (*request_startup)(koto_ctx*);                              /* on  (userbot_enabled=1) */
    int  (*host_version)(koto_ctx*);                                 /* versionCode приложения (gating) */
    int  (*host_version_name)(koto_ctx*, char* buf, int buflen);     /* "2.0.0" и т.п. */

    /* ─── 12. Привилегированный доступ ко ВСЕМ неймспейсам БД (admin db_*) ─── */
    int  (*adb_namespaces)(koto_ctx*, char* buf, int buflen);        /* NS через '\n' (db_list) */
    int  (*adb_keys)(koto_ctx*, const char* ns, char* buf, int buflen);
    int  (*adb_get)(koto_ctx*, const char* ns, const char* key, char* buf, int buflen);
    void (*adb_set)(koto_ctx*, const char* ns, const char* key, const char* val);
    void (*adb_del)(koto_ctx*, const char* ns, const char* key);
    void (*adb_clear)(koto_ctx*, const char* ns);                    /* db_clear: весь NS ("" = вся БД) */
    int  (*adb_stats)(koto_ctx*, koto_db_stats* out);                /* db_stats */
    /* бэкап/восстановление БД (db_backup/restore_db — интенты через файл). */
    void (*adb_backup)(koto_ctx*, long long chat_id);                /* выгрузить дамп как файл */
    void (*adb_restore)(koto_ctx*, const unsigned char* data, int len); /* залить дамп */

    /* ─── 13. Twins — мультиаккаунт-логин (пошаговый диалог через on_watch) ─── */
    void (*twin_add_begin)(koto_ctx*, const char* phone);            /* addtwin: старт логина */
    void (*twin_send_code)(koto_ctx*, const char* code);
    void (*twin_send_password)(koto_ctx*, const char* password);     /* 2FA */
    void (*twin_cancel)(koto_ctx*);                                  /* cancel */
    int  (*twin_list)(koto_ctx*, char* buf, int buflen);             /* "id\tname\tphone\n" … (twins) */
    void (*twin_remove)(koto_ctx*, long long uid);                   /* deltwin */
    int  (*twin_pending)(koto_ctx*, char* stage, int stage_len);     /* шаг логина: ""/phone/code/password; 1/0 */

    /* ─── 14. Схема настроек другого модуля (cfg-панель чужого модуля) ─── */
    int  (*mod_settings_count)(koto_ctx*, const char* module);
    int  (*mod_setting_schema)(koto_ctx*, const char* module, int i, koto_setting_field* out);
    int  (*mod_config_get)(koto_ctx*, const char* module, const char* key, char* buf, int buflen);
    void (*mod_config_set)(koto_ctx*, const char* module, const char* key, const char* val);

    /* ─── 15. Callback inline-кнопки (on_callback) ─── */
    const char* (*cb_data)(koto_ctx*);                               /* payload нажатой кнопки */
    long long   (*cb_message_id)(koto_ctx*);                         /* id сообщения с кнопкой */
    void        (*cb_answer)(koto_ctx*, const char* text, int alert);/* всплывашка (alert=1) / тост */

    /* ─── 16. Прочие действия-интенты над сообщениями ─── */
    void (*delete_msg)(koto_ctx*);                                   /* удалить своё сообщение-команду */
    void (*delete_message)(koto_ctx*, long long chat_id, long long msg_id);
    void (*react)(koto_ctx*, const char* emoji);                     /* send_reaction */
    void (*react_custom)(koto_ctx*, long long document_id);          /* кастом-реакция премиум-эмодзи */

    /* ─── 17. Бэкап и восстановление модулей (ZIP) ─── */
    void (*backup_modules)(koto_ctx*, long long chat_id);
    void (*restore_modules)(koto_ctx*, const unsigned char* data, int len);

    /* ─── 18. Расширенное управление доступом (grant/revoke/access) ─── */
    long long (*reply_sender_id)(koto_ctx*);
    int  (*access_set)(koto_ctx*, long long uid, const char* name, const char* modules, const char* commands, const char* chats);
    int  (*access_del)(koto_ctx*, long long uid);
    int  (*access_get)(koto_ctx*, long long uid, char* name_buf, int name_len,
                       char* mod_buf, int mod_len, char* cmd_buf, int cmd_len, char* chat_buf, int chat_len);
    int  (*access_list)(koto_ctx*, char* buf, int buflen);

    /* ─── 19. Дополнительные данные реплая (добавлены в конец для сохранения ABI) ─── */
    const char* (*reply_text)(koto_ctx*);
    int (*reply_entity_count)(koto_ctx*);
    int (*reply_entity)(koto_ctx*, int i, int* type, int* off, int* len,
                        long long* document_id, char* url, int url_len);

    /* ─── 20. Скачивание файлов по ссылке и директория кэша (MAY BLOCK) ─── */
    int (*download_file)(koto_ctx*, const char* url, const char* target_path);
    int (*cache_dir)(koto_ctx*, char* buf, int buflen);

    /* ─── 21. Inline-боты: отправка контента через стороннего бота (@lybot, @vkmusic_bot и т.д.) ─── */
    void (*c_send_inline_bot)(koto_ctx*, const char* bot_username, const char* query, long long chat_id, long long reply_to_msg_id);

    /* ─── 22. Инлайн-кнопка с покрасом (keyboardButtonStyle; добавлено в конец ради ABI) ─── */
    /* color = KOTO_BTN_* (фон кнопки); icon = id кастом-эмодзи перед текстом (0 — нет). */
    void (*c_button_ex)(koto_ctx*, const char* text, const char* callback_data,
                        int new_row, int color, long long icon);

    /* ─── 23. Баннер-картинка (link preview) для via-bot сообщения (добавлено в конец ради ABI) ─── */
    /* url — ссылка на картинку/страницу для превью; показывается на сообщении,
       уходящем через бота-помощника (с кнопками). flags = KOTO_BANNER_*.
       Чтобы убрать баннер (оставив кнопки) — просто не вызывать c_banner при
       следующем edit/send. Проверяйте api->c_banner на nullptr (старый хост). */
    void (*c_banner)(koto_ctx*, const char* url, int flags);

    /* ─── 24. Альбом фото одним сообщением (media group) (добавлено в конец ради ABI) ─── */
    /* Отправить сразу несколько фото ОДНИМ альбомом. urls — массив из count
       прямых ссылок (http/https) или локальных путей; хост скачивает каждую и
       шлёт пачкой (чанки по 10 — лимит Telegram). Подпись = составленный
       композер (как в c_send_file). flags зарезервированы (0).
       Хост 0.3.4+ (versionCode 24) — проверяйте api->c_send_album на nullptr. */
    void (*c_send_album)(koto_ctx*, long long chat_id,
                         const char* const* urls, int count, int flags);

    /* ─── 25. Оверлей-холст (рисование и отображение поверх экрана / SYSTEM_ALERT_WINDOW) ─── */
    /* mode = KOTO_OVERLAY_PASSTHROUGH (0), KOTO_OVERLAY_DRAW (1), KOTO_OVERLAY_HIDDEN (2).
       options_json: конфигурационный JSON ("{\"mode\":1,\"pen_color\":\"#FF2D55\",\"pen_width\":12.0}") или NULL.
       Хост 0.3.4+ (versionCode 24) — проверяйте api->overlay_show на nullptr. */
    int  (*overlay_show)(koto_ctx*, int mode, const char* options_json);
    void (*overlay_hide)(koto_ctx*);
    int  (*overlay_set_image)(koto_ctx*, const char* path_or_url);
    int  (*overlay_set_bytes)(koto_ctx*, const unsigned char* data, int len);
    void (*overlay_clear)(koto_ctx*);
    int  (*overlay_save)(koto_ctx*, char* out_path, int max_len);
    int  (*overlay_get_state)(koto_ctx*, char* out_json, int max_len);

    /* ─── 26. Управление профилем/аккаунтом пользователя (v0.3.6 / versionCode 26+) ─── */
    void (*set_name)(koto_ctx*, const char* first_name, const char* last_name);
    void (*set_bio)(koto_ctx*, const char* about);
    void (*set_username)(koto_ctx*, const char* username);
    void (*set_emoji_status)(koto_ctx*, long long document_id, int until_sec);
    void (*set_profile_photo)(koto_ctx*, const char* path);
    void (*delete_profile_photo)(koto_ctx*);
    void (*set_online)(koto_ctx*, int online);

    /* ─── 27. Расширенные данные пользователей и чатов (v0.3.6 Hotfix 4 / versionCode 26+) ─── */
    int  (*user_first_name)(koto_ctx*, long long uid, char* buf, int buflen);
    int  (*user_last_name)(koto_ctx*, long long uid, char* buf, int buflen);
    int  (*user_username)(koto_ctx*, long long uid, char* buf, int buflen);
    int  (*user_full_json)(koto_ctx*, long long uid, char* buf, int buflen);
    int  (*chat_full_json)(koto_ctx*, long long cid, char* buf, int buflen);

    /* ─── 28. Модерация и история сообщений (v0.3.7 / versionCode 27+) ─── */
    void (*ban_user)(koto_ctx*, long long chat_id, long long user_id, int until_sec);
    void (*unban_user)(koto_ctx*, long long chat_id, long long user_id);
    void (*kick_user)(koto_ctx*, long long chat_id, long long user_id);
    void (*mute_user)(koto_ctx*, long long chat_id, long long user_id, int until_sec);
    void (*unmute_user)(koto_ctx*, long long chat_id, long long user_id);
    int  (*get_history_json)(koto_ctx*, long long chat_id, int offset_id, int limit, int add_offset, char* buf, int buflen);
    long long (*reply_msg_id)(koto_ctx*);

    /* ─── 29. Интерактивные кнопки, мульти-аккаунты (Twins) и пересылка (v0.3.8 / versionCode 28+) ─── */
    void (*click_button)(koto_ctx*, long long chat_id, long long msg_id, const char* data);
    void (*forward_messages)(koto_ctx*, long long from_chat_id, const int* msg_ids, int count, long long to_chat_id);
    void (*twin_send)(koto_ctx*, long long twin_uid, long long chat_id, const char* text);
    void (*twin_click_button)(koto_ctx*, long long twin_uid, long long chat_id, long long msg_id, const char* data);
    int  (*twin_get_history_json)(koto_ctx*, long long twin_uid, long long chat_id, int offset_id, int limit, int add_offset, char* buf, int buflen);
    void (*twin_forward_messages)(koto_ctx*, long long twin_uid, long long from_chat_id, const int* msg_ids, int count, long long to_chat_id);
    int  (*get_twins_json)(koto_ctx*, char* buf, int buflen);
    int  (*msg_buttons_json)(koto_ctx*, char* buf, int buflen);

    /* ─── 30. Прочтение истории и очистка упоминаний (v0.3.20 / versionCode 40+) ─── */
    void (*read_history)(koto_ctx*, long long chat_id, long long max_id);
    void (*read_mentions)(koto_ctx*, long long chat_id);

    /* ─── 31. Флаги сообщения (упоминания, закрепы) (v0.3.22 / versionCode 42+) ─── */
    int  (*msg_is_mentioned)(koto_ctx*);
    int  (*msg_is_pinned)(koto_ctx*);

    /* 32. Administration, versionCode 60+. Append-only; older hosts must be
       excluded with min_host_version. admin_action queues a request, not a
       success reply. The host reports the actual Telegram result. rights_hash
       uses the 13 administration rights in their documented order. */
    int  (*chat_type)(koto_ctx*);
    void (*admin_action)(koto_ctx*, int operation, long long chat_id,
                         long long user_id, const char* rights_hash, const char* rank);
    /* 33. Host process metrics, versionCode 63+. Append-only, read-only.
       Returns JSON schema=1; missing/null counters are unavailable, never zero.
       Android app_rx/tx_bytes are TrafficStats counters for the app UID since
       device boot; process_* refers to the host, including in the sandbox.
       Returns full JSON byte length (may exceed buflen), -1 if unavailable;
       output is NUL-terminated when buflen>0. Requires min_host_version=63. */
    int (*system_metrics_json)(koto_ctx*, char* buf, int buflen);
} koto_api;
/* ========================================================================== *
 *  Хэндлеры модуля. Все СИНХРОННЫЕ (как on_message в ABI 2): api передаётся    *
 *  аргументом, глобального состояния не требуется. ctx валиден только на       *
 *  время вызова. Действия копятся в ctx и выполняются хостом после возврата;   *
 *  read-вызовы «MAY BLOCK» хост обслуживает синхронно.                         *
 * ========================================================================== */
/* Обработчик команды. Хост уже распарсил префикс+имя (cmd_name/cmd_args) и
 * проверил уровень доступа (min_level команды, с учётом access cmd). */
typedef void (*koto_cmd_fn)(koto_ctx* ctx, const koto_api* api);
/* Watcher: видит сообщения по watch_flags (для twins-логина, аварийного
 * .resetprefix, ввода значения в cfg-панель). Возврат 1 = «обработал, дальше
 * команды не роутить»; 0 = пропустить к обычному роутингу. */
typedef int  (*koto_watch_fn)(koto_ctx* ctx, const koto_api* api);
/* Нажатие inline-кнопки (cb_data/cb_message_id/cb_answer + композер для edit). */
typedef void (*koto_callback_fn)(koto_ctx* ctx, const koto_api* api);

/* Одна декларативная команда модуля. Из команд всех модулей хост строит общий
 * реестр (help/aliases/minfo/modules/access). */
typedef struct koto_command {
    const char*  name;        /* без префикса: "help", "ping", "setbio"… */
    koto_cmd_fn  handler;
    int          min_level;   /* KOTO_LEVEL_* по умолчанию (переопределяется access cmd) */
    const char*  doc;         /* короткая справка (help) */
    const char*  usage;       /* "<username>" / "[on|off]" / NULL */
} koto_command;

/* ========================================================================== *
 *  koto_module — то, что модуль ЭКСПОРТИРУЕТ хосту (указатель на статику).     *
 *  Поля идут слоями по ABI: хост читает поле ТОЛЬКО если abi_version достаточен.*
 * ========================================================================== */
typedef struct koto_module {
    int          abi_version;      /* обязательно = KOTO_MODULE_ABI (3) */
    const char*  name;             /* короткое имя (== неймспейс БД): "profile"… */
    const char*  description;      /* человекочитаемое описание для UI */
    const char*  version;          /* версия самого модуля ("1.2.0"); может быть NULL */
    int          min_host_version; /* gating: нужен versionCode приложения ≥; 0 — без ограничений */
    int          flags;            /* KOTO_MOD_SYSTEM | KOTO_MOD_PROTECTED */

    /* Декларация команд: хост сам парсит префикс+имя и зовёт нужный handler. */
    const koto_command* commands;
    int                 command_count;

    /* Watcher (опц.): NULL — не нужен. Иначе watch_flags = KOTO_WATCH_*. */
    koto_watch_fn       on_watch;
    int                 watch_flags;

    /* Callback inline-кнопок (опц.): NULL — модуль не рисует кнопки. */
    koto_callback_fn    on_callback;

    /* Схема настраиваемых полей для cfg-панели (опц.): NULL / 0. */
    const koto_setting_field* settings;
    int                       settings_count;

    /* URL для проверки и скачивания обновлений с Git/GitHub (опц.): NULL */
    const char*               update_url;
} koto_module;

/* ЕДИНСТВЕННЫЙ обязательный экспорт .so. Хост зовёт его один раз при dlopen и
 * получает указатель на статический koto_module. */
const koto_module* koto_module_register(void);

#ifdef __cplusplus
}
#endif
#endif /* KOTO_MODULE_ABI_H */
