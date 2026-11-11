# Kote Module SDK 0.4.5

Официальный набор инструментов для разработки нативных (C/C++) и Lua-модулей юзербота KoteLoader / Kotogram (совместимо с хостом v0.4.16 / код 109+).

* **Архитектуры:** `arm64-v8a` (64-бит) и `armeabi-v7a` (32-бит)
* **Минимальный Android:** API 26+ (Android 8.0+)
* **Стандарты:** C11 / C++20, Module ABI 3
* **Мультиязычность:** Полная поддержка русского и английского языков (RU / EN, `koto_utils::tr`, `koto_utils::is_en`)
* **Сетевой стек:** Системные `http_get` / `http_post` с автоматическим сохранением сессионных cookie (RFC 6265) и горячей заменой (Hot-Swap) модулей в фоне
* **Руководство по 32-битным модулям:** [GUIDE_32BIT.md](GUIDE_32BIT.md)

---

## Структура SDK

- `include/module_abi.h` — Основной заголовочный файл ABI со всеми функциями хоста (`koto_api`, `koto_ctx`), хелперами мультиязычности (`koto_is_lang_en`, `KOTO_TR`).
- `include/koto_utils.hpp`, `koto_file.hpp` — Удобные C++ обёртки для работы со строками, памятью, файлами и локализацией (`koto_utils::tr`, `is_en`).
- `include/nlohmann/json.hpp` — Заголовочная библиотека для работы с JSON.
- `include/koto_services.hpp` — Доступ к хост-сервисам; подробнее см. [HOST_SERVICES.md](HOST_SERVICES.md).
- `GUIDE_32BIT.md` — Подробное руководство по поддержке 32-битной архитектуры и переносу с 64-бит.
- `src/` — Каталог для исходников вашего нового модуля.
- `examples/native/` — Полный набор готовых примеров нативных модулей (включая poll, moomod, usermanage, moderation и др.).
- `examples/lua/` — Примеры скриптовых Lua-модулей; см. [LUA_GUIDE.md](LUA_GUIDE.md).
- `build.bat` — Скрипт сборки под Windows (NDK clang).
- `build.sh` — Скрипт сборки под Linux / macOS / WSL.
- `CMakeLists.txt` — Проект для сборки через CMake.

---

## 🌍 Мультиязычность и локализация модулей (RU / EN)

Начиная с версии хоста 0.4.11+, юзербот поддерживает выбор языка интерфейса (`RU` / `EN` / `System Default`). Модули могут легко адаптироваться под выбранный язык пользователя.

### 1. Описание модуля и команд (для .help и .minfo)
Используйте теги `[ru]` и `[en]` в строках описания модуля и документации команд:

```c
static const koto_command COMMANDS[] = {
    {
        "mycmd",
        &cmd_mycmd,
        KOTO_LEVEL_ALL,
        "[ru] Скачать видео или фото [en] Download video or photo",
        "[ru] <ссылка> [en] <url>"
    }
};

static const koto_module MODULE = {
    KOTO_MODULE_ABI,
    "my_module",
    "[ru] Полезный модуль для медиа [en] Useful media module",
    "1.1.0",
    // ...
};
```
Хост KoteLoader автоматически извлечёт нужную строку в зависимости от языка пользователя.

### 2. Определение языка в коде обработчика команды

#### В C (C11):
```c
#include "module_abi.h"

static void cmd_mycmd(koto_ctx* ctx, const koto_api* api) {
    int is_en = koto_is_lang_en(ctx, api);
    
    if (is_en) {
        api->c_text(ctx, "⏳ Processing request...");
    } else {
        api->c_text(ctx, "⏳ Обрабатываю запрос...");
    }
    api->c_edit(ctx, KOTO_NO_LINK_PREVIEW);
}
```
Или с помощью макроса `KOTO_TR`:
```c
api->c_text(ctx, KOTO_TR(ctx, api, "Готово!", "Done!"));
```

#### В C++ (C++20):
```cpp
#include "module_abi.h"
#include "koto_utils.hpp"

static void cmd_mycmd(koto_ctx* ctx, const koto_api* api) {
    using namespace koto_utils;
    
    api->c_text(ctx, tr(ctx, api, "Загрузка началась...", "Download started..."));
    api->c_edit(ctx, KOTO_NO_LINK_PREVIEW);
}
```

#### В Lua:
```lua
local lang = api.setting_get("app_language") or "ru"
local is_en = (lang == "en" or lang == "english")

local msg = is_en and "Done!" or "Готово!"
api.c_text(msg)
```

---

## Сборка модулей

### Под Windows:
```cmd
:: Собрать под обе архитектуры (arm64-v8a и armeabi-v7a):
build.bat --all

:: Собрать конкретный модуль:
build.bat src\my_module.cpp --all

:: Собрать только под 32-бит:
build.bat --arm32

:: Собрать только под 64-бит:
build.bat --arm64
```

### Под Linux / macOS:
```bash
./build.sh --all
```

Бинарники `.so` сохраняются в `out/android-arm64-v8a/` и `out/android-armeabi-v7a/`.
