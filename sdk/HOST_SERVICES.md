# Host Services — Kotogram / KoteLoader (Host versionCode 111)

В KoteLoader и Kotogram архитектура C-ABI 3 расширяется версионируемыми системными сервисами хоста через функции `host_call_json` и `host_action_json` в структуре `koto_api`. Это позволяет модулям вызывать высокоуровневые операции MTProto (управление каналами, чтение розыгрышей, парсинг опросов, управление контактами, уникальные подарки и поиск контактов) без изменения бинарного ABI.

* **Текущая версия хоста:** `versionCode 111` (Kotogram v0.4.18+)
* **Обратная совместимость:** ABI 3 полностью сохранён; старые модули работают без изменений.
* **Gating совместимости:** Модули, использующие базовые сервисы, указывают `min_host_version=74`. Модули, которым требуются сервисы каналов и розыгрышей, указывают `min_host_version=110`. Модули уникальных подарков указывают `min_host_version=111`.
* **Динамическая проверка:** Перед вызовом новых функций проверяйте `api->host_version(ctx) >= 111`.

---

## 🛠 Вызов сервисов в C++ и Lua

### В C++ (C++20):
Используйте удобный заголовочный файл `#include "koto_services.hpp"`:

```cpp
#include "module_abi.h"
#include "koto_services.hpp"

// Проверка доступности хост-сервисов
if (koto::services::available(ctx, api, 110)) {
    // Вступление в канал с автоматическим глушением и архивацией
    std::string res = koto::services::channel_join(ctx, api, "https://t.me/durov", true, true);
    
    // Получение информации о розыгрыше Telegram
    std::string gw = koto::services::giveaway_info(ctx, api, chat_id, msg_id);
}
```

### В Lua:
```lua
-- Синхронный запрос данных
local res_json = koto.host_call_json("telegram.channel.join.v1", '{"link":"https://t.me/durov","mute":true}')

-- Асинхронное действие-интент
koto.host_action_json("telegram.channel.leave.v1", '{"channel_id":123456789}')
```

---

## 📖 Формат запросов и ответов

Все методы обмениваются данными в формате JSON.
- **Успех:** `{"schema": 1, "ok": true, ...}`
- **Ошибка:** `{"schema": 1, "ok": false, "error": "код_ошибки"}`
- Ограничение размера запроса: до 16 КиБ.
- Ограничение размера ответа: до 256 КиБ.

---

## 🔍 Читающие методы (`host_call_json`)

Читающие вызовы выполняются синхронно («MAY BLOCK») в отдельном пуле воркеров ядра:

| Метод | Минимальный хост | Параметры запроса | Возвращаемые данные |
| :--- | :---: | :--- | :--- |
| `capabilities` | 74+ | `{}` | Поддерживаемые методы `read`, `actions`, `host_version` |
| `telegram.peer.v1` | 74+ | `{"peer":{"type":"user","id":123}}` | Информация о пользователе (`id`, `first_name`, `username`, `is_bot`, `is_contact`) |
| `telegram.history.v1` | 74+ | `peer`, `limit` (1..200), `offset_id`, `add_offset`, `oldest`, `include_text` | Массив сообщений `messages` |
| `telegram.dialogs.v1` | 74+ | `limit` (1..200), `folder_id` (0/1), `exclude_pinned`, `offset` | Массив `dialogs`, курсор пагинации `next`, флаг `complete` |
| `telegram.reply_file.v1` | 74+ | `{}` | Путь к скачанному вложению reply: `path`, `file_name`, `mime_type`, `size` |
| `telegram.poll.scan.v1` | 87+ | `peer` / `chat_id`, `id` (msg_id) | Опрос: `question`, `options`, `closed`, `public_voters`, `non_voters` |
| `telegram.delme.v1` | 87+ | `peer` / `chat_id` | Количество собственных сообщений пользователя в чате: `count` |
| `telegram.invite.export.v1` | 87+ | `peer` / `chat_id`, `title`, `expire_date`, `usage_limit` | Экспортированная invite-ссылка: `link` |
| **`telegram.channel.join.v1`** | **110+** | `link` (URL/invite), `username` (@channel), или `channel_id` / `chat_id`, `mute` (bool), `archive` (bool) | Вступление в публичный или приватный канал/супергруппу |
| **`telegram.channel.leave.v1`** | **110+** | `channel_id` / `chat_id` / `id` | Выход из канала/супергруппы |
| **`telegram.giveaway.info.v1`** | **110+** | `peer` / `chat_id`, `msg_id` | Информация о розыгрыше Telegram Premium / Stars (`participating`, `status`, `winner`, `stars_prize`) |
| **`telegram.message.giveaway.v1`**| **110+** | `peer` / `chat_id`, `id` (msg_id) | Инспекция розыгрыша из медиа сообщения (`countries_iso2`, `months`, `stars`, `channels`) |
| **`telegram.stargift.info.v1`** | **111+** | `slug` (например `"SnakeBox-190727"`) | Информация об уникальном подарке Telegram (NFT): владелец, адрес TON, атрибуты |
| **`telegram.contacts.search.v1`** | **111+** | `q` (поисковая строка), `limit` (1..50) | Поиск пользователей и контактов в Telegram по имени / юзернейму |

---

## ⚡ Действия-интенты (`host_action_json`)

Действия ставятся в очередь намерений текущего контекста и исполняются ядром MTProto сразу после возврата из обработчика:

| Метод | Минимальный хост | Параметры запроса | Описание действия |
| :--- | :---: | :--- | :--- |
| **`telegram.channel.join.v1`** | **110+** | `link`, `username`, `channel_id`, `mute`, `archive` | Асинхронное вступление в канал |
| **`telegram.channel.leave.v1`** | **110+** | `channel_id` / `chat_id` | Асинхронный выход из канала |
| `telegram.block.v1` | 74+ | `{"peer":{"type":"user","id":123}}` | Блокировка пользователя в Telegram |
| `telegram.unblock.v1` | 74+ | `{"peer":{"type":"user","id":123}}` | Разблокировка пользователя |
| `telegram.report_spam.v1` | 74+ | `{"peer":{"type":"user","id":123}}` | Жалоба на спам |
| `telegram.clear_history.v1` | 74+ | `peer` / `chat_id`, `revoke` (bool) | Очистка истории переписки |
| `telegram.contact.add.v1` | 87+ | `user_id`, `first_name`, `last_name`, `phone` | Добавление в список контактов Telegram |
| `telegram.contact.delete.v1`| 87+ | `user_id` | Удаление пользователя из контактов |
| `telegram.poll.send.v1` | 87+ | `peer` / `chat_id`, `question`, `answers`, `quiz`, `correct_id` | Создание и отправка опроса / викторины |
| `telegram.delme.v1` | 87+ | `peer` / `chat_id`, `limit` | Массовое удаление своих сообщений |
| `telegram.invite.edit.v1` | 87+ | `chat_id`, `link`, `revoked: true` | Отзыв invite-ссылки чата |

---

## 💡 Примеры использования

### 1. Автоматическое вступление в канал и архивация (Host 110)
```cpp
#include "module_abi.h"
#include "koto_services.hpp"

static void cmd_join(koto_ctx* ctx, const koto_api* api) {
    const char* args = api->cmd_args(ctx);
    if (!args || !*args) {
        api->c_text(ctx, "Использование: .join <@channel | invite_url>");
        api->c_edit(ctx, KOTO_NO_LINK_PREVIEW);
        return;
    }

    std::string resp = koto::services::channel_join(ctx, api, args, /*mute=*/true, /*archive=*/true);
    // resp: {"schema":1,"ok":true}
    api->c_text(ctx, "✅ Успешно вступили в канал (заглушен и перемещён в архив)");
    api->c_edit(ctx, KOTO_NO_LINK_PREVIEW);
}
```

### 3. Получение информации об уникальном подарке (Host 111)
```cpp
#include "module_abi.h"
#include "koto_services.hpp"
#include "nlohmann/json.hpp"

static void cmd_nft(koto_ctx* ctx, const koto_api* api) {
    std::string slug = "SnakeBox-190727";
    std::string res = koto::services::stargift_info(ctx, api, slug);
    auto j = nlohmann::json::parse(res, nullptr, false);
    if (j.is_object() && j.value("ok", false)) {
        std::string title = j.value("title", "");
        int num = j.value("num", 0);
        std::string owner = j.value("owner_name", "Скрыт");
        api->c_text(ctx, ("🎁 Подарок: " + title + " #" + std::to_string(num) + "\nВладелец: " + owner).c_str());
    } else {
        api->c_text(ctx, "Подарок не найден или скрыт.");
    }
    api->c_edit(ctx, KOTO_NO_LINK_PREVIEW);
}
```

### 2. Проверка результатов розыгрыша (Host 110)
```cpp
#include "module_abi.h"
#include "koto_services.hpp"
#include "nlohmann/json.hpp"

static void cmd_check_gw(koto_ctx* ctx, const koto_api* api) {
    long long cid = api->chat_id(ctx);
    long long rid = api->reply_msg_id(ctx);
    if (!rid) {
        api->c_text(ctx, "Ответьте на сообщение с розыгрышем!");
        api->c_edit(ctx, KOTO_NO_LINK_PREVIEW);
        return;
    }

    std::string res = koto::services::giveaway_info(ctx, api, cid, (int)rid);
    auto j = nlohmann::json::parse(res, nullptr, false);
    if (j.is_object() && j.value("ok", false)) {
        std::string status = j.value("status", "unknown");
        bool winner = j.value("winner", false);
        api->c_text(ctx, winner ? "🎉 Вы выиграли в этом розыгрыше!" : "Увы, выигрыш не выпал.");
    } else {
        api->c_text(ctx, "Не удалось получить информацию о розыгрыше.");
    }
    api->c_edit(ctx, KOTO_NO_LINK_PREVIEW);
}
```

### 3. Получение информации об уникальном подарке (Host 111)
```cpp
#include "module_abi.h"
#include "koto_services.hpp"
#include "nlohmann/json.hpp"

static void cmd_nft(koto_ctx* ctx, const koto_api* api) {
    std::string slug = "SnakeBox-190727";
    std::string res = koto::services::stargift_info(ctx, api, slug);
    auto j = nlohmann::json::parse(res, nullptr, false);
    if (j.is_object() && j.value("ok", false)) {
        std::string title = j.value("title", "");
        int num = j.value("num", 0);
        std::string owner = j.value("owner_name", "Скрыт");
        api->c_text(ctx, ("🎁 Подарок: " + title + " #" + std::to_string(num) + "\nВладелец: " + owner).c_str());
    } else {
        api->c_text(ctx, "Подарок не найден или скрыт.");
    }
    api->c_edit(ctx, KOTO_NO_LINK_PREVIEW);
}
```
