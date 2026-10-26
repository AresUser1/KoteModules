# Kote Module SDK 0.4.0

Официальный набор инструментов для разработки нативных (C/C++) и Lua-модулей юзербота KoteLoader / Kotogram.

* **Архитектуры:** `arm64-v8a` (64-бит) и `armeabi-v7a` (32-бит)
* **Минимальный Android:** API 26+ (Android 8.0+)
* **Стандарты:** C11 / C++20, Module ABI 3
* **Руководство по 32-битным модулям:** [GUIDE_32BIT.md](GUIDE_32BIT.md)

---

## Структура SDK

- `include/module_abi.h` — Основной заголовочный файл ABI со всеми функциями хоста (`koto_api`, `koto_ctx`).
- `include/koto_utils.hpp`, `koto_file.hpp` — Удобные C++ обёртки для работы со строками, памятью и файлами.
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
