# Kote Module SDK 0.4.9 (Host versionCode 113)

Официальный набор инструментов для разработки нативных (C/C++) и Lua-модулей юзербота KoteLoader / Kotogram.

* **Целевая версия хоста:** `versionCode 113` (Kotogram / KoteLoader v0.4.19+)
* **Архитектуры:** `arm64-v8a` (64-бит) и `armeabi-v7a` (32-бит)
* **Минимальный Android:** API 26+ (Android 8.0+)
* **Стандарты:** C11 / C++20, Module ABI 3
* **Мультиязычность:** Полная поддержка русского и английского языков (RU / EN)
* **Руководство по 32-битным модулям:** [GUIDE_32BIT.md](GUIDE_32BIT.md)
* **Руководство по сервисам хоста (112+):** [HOST_SERVICES.md](HOST_SERVICES.md)

---

## 🚀 Что нового в SDK 0.4.9 (Host 113)

1. ⚡ **Универсальный мгновенный сброс действий (Live Action Flush):**
   - Добавлена поддержка немедленного применения и отправки действий (`api->c_edit`, `api->c_reply`, `api->c_send`, `api->delete_msg`, `api->react`) прямо во время выполнения команды модуля, до перехода к долгим синхронным операциям (загрузка медиафайлов, HTTP-запросы к внешним API, обработка данных).
   - Поддерживается как в нативных C/C++ модулях ABI 3 (`native_module.cpp`), так и в скриптовом рантайме Lua 5.4 (`lua_module.cpp`).
   - Позволяет модулям моментально менять текст сообщения на статус выполнения (например, *"⏳ Обработка..."*), гарантируя, что пользователь увидит актуальное состояние без задержек.

2. ✂️ **Автоматическая нормализация и защита от лимита подписей MTProto (Safe Caption Truncation):**
   - В ядро и JNI-мост внедрена защита от RPC-ошибки `400: MEDIA_CAPTION_TOO_LONG` при отправке фото, видео, файлов и альбомов с длинными описаниями из внешних источников.
   - Движок производит строгий подсчёт UTF-16 code units (лимит Telegram 1024 / 1020 символов с многоточием `...`) и автоматически валидирует и корректирует границы сущностей форматирования (`sanitize_message_entities`), полностью исключая сбои отправки.

3. 🔒 **Повышение стабильности многопоточного моста:**
   - Оптимизирована синхронизация между рабочими потоками JNI, хост-сервисом и очередью команд ядра, исключающая взаимные блокировки при старте и перезапуске приложения.

---

## 🚀 Что нового в SDK 0.4.8 (Host 112)

1. 🎁 **Нативные MTProto-сервисы подарков и Telegram Stars:**
   - `telegram.stargifts.saved.v1` (`stargifts_saved`) — список сохранённых подарков пользователя с пагинацией (`payments.getSavedStarGifts`).
   - `telegram.stargift.value.v1` (`stargift_value`) — оценка стоимости коллекционного NFT-подарка (`payments.getUniqueStarGiftValueInfo`).
   - `telegram.stars.summary.v1` (`stars_summary`) — баланс и сводка транзакций Telegram Stars (`payments.getStarsStatus`).
2. 🛡️ **Нативные MTProto-сервисы управления чатом и заявками:**
   - `telegram.chat.default_rights.v1` (`chat_default_rights`) — изменение прав участников по умолчанию (`messages.editChatDefaultBannedRights`).
   - `telegram.chat.slowmode.v1` (`chat_slowmode`) — включение и настройка медленного режима (`channels.toggleSlowMode`).
   - `telegram.join_requests.list.v1` (`join_requests_list`) и `telegram.join_requests.resolve_all.v1` (`join_requests_resolve_all`) — просмотр и массовое одобрение/отклонение заявок на вступление.
   - `telegram.chat.participants.v1` (`chat_participants`) — получение списков участников, ботов и удалённых аккаунтов (`channels.getParticipants` / `messages.getFullChat`).
3. 🔧 **Улучшения `koto_services.hpp` и `HOST_SERVICES.md`:**
   - Добавлены C++20 обёртки для всех 8 новых сервисов Host 112 и обновлена документация.

---

## Структура SDK

- `include/module_abi.h` — Основной заголовочный файл ABI со всеми функциями хоста (`koto_api`, `koto_ctx`), хелперами мультиязычности (`koto_is_lang_en`, `KOTO_TR`).
- `include/koto_utils.hpp`, `koto_file.hpp` — Удобные C++ обёртки для работы со строками, памятью, файлами и локализацией (`koto_utils::tr`, `is_en`).
- `include/nlohmann/json.hpp` — Заголовочная библиотека для работы с JSON.
- `include/koto_services.hpp` — Доступ к хост-сервисам MTProto (Host 110+); подробнее см. [HOST_SERVICES.md](HOST_SERVICES.md).
- `GUIDE_32BIT.md` — Подробное руководство по поддержке 32-битной архитектуры и переносу с 64-бит.
- `src/` — Каталог для исходников вашего нового модуля.
- `examples/native/` — Полный набор готовых примеров нативных модулей (match3, poll, moomod, usermanage, moderation, administration и др.).
- `examples/lua/` — Примеры скриптовых Lua-модулей; см. [LUA_GUIDE.md](LUA_GUIDE.md).
- `build.bat` — Скрипт сборки под Windows (NDK clang).
- `build.sh` — Скрипт сборки под Linux / macOS / WSL.
- `CMakeLists.txt` — Проект для сборки через CMake.

---

## 🌍 Мультиязычность и локализация модулей (RU / EN)

Начиная с версии хоста 0.4.11+, юзербот поддерживает выбор языка интерфейса (`RU` / `EN` / `System Default`). Модули могут легко адаптироваться под выбранный язык пользователя.

### 1. Локализация описаний команд и модуля
В полях `description`, `usage` и описаниях параметров настроек можно указывать локализованные строки с префиксами `[ru]` и `[en]`:
```c
static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "sample",
    "[ru] Пример модуля на C++ ABI 3. [en] Sample module on C++ ABI 3.",
    "1.0.0",
    24,
    0,
    COMMANDS, sizeof(COMMANDS)/sizeof(COMMANDS[0]),
    ...
};
```
Хост автоматически извлечёт нужную часть строки в зависимости от языка, выбранного в настройках приложения.

### 2. Локализация вывода в чат
Используйте хелперы из `koto_utils.hpp`:
```cpp
#include "koto_utils.hpp"

// Проверка текущего языка:
if (koto_utils::is_en(ctx, api)) {
    api->c_add_text(ctx, "Done successfully!", 0);
} else {
    api->c_add_text(ctx, "Успешно выполнено!", 0);
}

// Или макрос KOTO_TR:
api->c_add_text(ctx, KOTO_TR(ctx, api, "Успешно!", "Success!"), 0);
```

---

## 📦 Быстрый старт сборки собственного модуля

1. Откройте файл `src/sample.cpp` (или скопируйте любой из `examples/native/`).
2. Настройте NDK путь в `build.bat` (Windows) или `build.sh` (Linux/macOS).
3. Запустите сборку:
   ```bash
   ./build.sh
   ```
4. Скомпилированные библиотеки появятся в папке `out/`:
   - `out/android-arm64-v8a/` (64-бит)
   - `out/android-armeabi-v7a/` (32-бит)
5. Отправьте файл `.so` в Избранное Telegram и ответьте на него командой `.install`.
