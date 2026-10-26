# Руководство: Поддержка 32-битной архитектуры (ARM32 / armeabi-v7a) в KoteLoader

Начиная с KoteLoader v0.4.0 (код 90), приложение и модули полностью поддерживают две целевые архитектуры Android:
* **`arm64-v8a`** — 64-битные современные процессоры (большинство смартфонов).
* **`armeabi-v7a`** — 32-битные процессоры (старые устройства, эмуляторы, планшеты).

Данное руководство объясняет отличия, типичные ловушки и пошаговый процесс портирования любого 64-битного модуля на 32-бит.

---

## 1. Ключевые различия архитектур 32-бит и 64-бит

| Параметр | ARM64 (`arm64-v8a`) | ARM32 (`armeabi-v7a`) | Влияние на код модуля |
|---|---|---|---|
| **Размер указателя** (`void*`) | 8 байт (64 бит) | 4 байта (32 бит) | Нельзя кастовать указатели в `long` / `int` без потери данных. Использовать `uintptr_t`. |
| **Размер `long`** | 8 байт | 4 байта | **Критично!** В C++ под Linux/Android `sizeof(long)` на 32-бит равен 4 байтам, а на 64-бит — 8 байтам. |
| **Размер `size_t`** | 8 байт | 4 байта | Ограничение на адресное пространство до 2–4 ГБ. |
| **Telegram ID (`user_id`, `chat_id`, `msg_id`)** | 8 байт (`int64_t`) | 8 байт (`int64_t`) | **Всегда** объявлять как `int64_t` или `long long`. Если объявить как `long` — на 32-бит ID обрежется и бот перестанет находить чаты и пользователей! |
| **Лимит стека потока** | ~2–8 МБ | ~512 КБ – 1 МБ | **Критично!** Выделение больших буферов на стеке (`char buf[200000]`) на 32-бит моментально вызывает `SIGSEGV` (Stack Overflow). |
| **Выравнивание памяти** | 8 байт | 4 / 8 байт | Использование 64-битных чисел в структурах требует аккуратного выравнивания. |

---

## 2. Главные ошибки при портировании с 64-бит на 32-бит

### Ошибка 1: Хранение ID через `long` или `int`
**Неправильно:**
```cpp
long user_id = api->sender_id(ctx); // На 32-бит это 32-битное число!
// ID в Telegram давно превышают 2^31, поэтому user_id станет отрицательным или усечётся!
```
**Правильно:**
```cpp
int64_t user_id = api->sender_id(ctx); // или long long
// Гарантированно 64 бита на любой архитектуре
```

### Ошибка 2: Переполнение стека (Stack Overflow)
На 32-битных Android-устройствах память стека потока сильно ограничена.
**Неправильно:**
```cpp
void on_watch(koto_ctx* ctx, const koto_api* api) {
    char big_buffer[256 * 1024]; // 256 КБ на стеке — крэш DeadObjectException / SIGSEGV на 32-бит!
    ...
}
```
**Правильно (выделение в куче):**
```cpp
void on_watch(koto_ctx* ctx, const koto_api* api) {
    std::vector<char> big_buffer(256 * 1024); // Выделяется в куче (heap)
    // или std::string / std::unique_ptr
    ...
}
```

### Ошибка 3: Спецификаторы формата `printf`
**Неправильно:**
```cpp
char buf[64];
snprintf(buf, sizeof(buf), "%ld", (long)chat_id); // На 32-бит chat_id (int64_t) обрежется до 32 бит!
```
**Правильно:**
```cpp
// Используйте std::to_string или PRId64:
std::string s = std::to_string(chat_id);
// или:
snprintf(buf, sizeof(buf), "%lld", (long long)chat_id);
```

### Ошибка 4: Каст указателей
**Неправильно:**
```cpp
uint64_t ptr_val = (uint64_t)ptr; // На 32-бит вызовет предупреждение компилятора
```
**Правильно:**
```cpp
uintptr_t ptr_val = reinterpret_cast<uintptr_t>(ptr);
```

---

## 3. Сборка модуля под обе архитектуры

В SDK обновлены скрипты сборки `build.bat` (Windows) и `build.sh` (Linux / macOS).

### Сборка под Windows:
```cmd
:: Собрать под обе архитектуры (arm64-v8a и armeabi-v7a):
build.bat --all

:: Собрать только под 32-бит:
build.bat --arm32

:: Собрать только под 64-бит:
build.bat --arm64

:: Собрать конкретный модуль:
build.bat src\my_module.cpp --all
```

Готовые бинарники `.so` будут помещены в:
* `out\android-arm64-v8a\<имя>.so`
* `out\android-armeabi-v7a\<имя>.so`

### Сборка под Linux / macOS / WSL:
```bash
# Собрать под обе архитектуры:
./build.sh --all

# Собрать под 32-бит:
./build.sh --arm32

# Собрать под 64-бит:
./build.sh --arm64
```

---

## 4. Требования к компиляторам NDK

* Для **`arm64-v8a`**:
  `$NDK/toolchains/llvm/prebuilt/.../bin/aarch64-linux-android26-clang++`
  Флаги: `-O2 -fPIC -shared -pthread -std=c++20 -Wl,-z,nodelete`

* Для **`armeabi-v7a` (32-bit)**:
  `$NDK/toolchains/llvm/prebuilt/.../bin/armv7a-linux-androideabi26-clang++`
  Флаги: `-O2 -fPIC -shared -pthread -std=c++20 -Wl,-z,nodelete`

---

## 5. Добавление модуля в репозиторий `index.json`

В `index.json` репозитория для модуля должны быть указаны обе ссылки:
```json
{
  "id": "my_module",
  "name": "my_module",
  "version": "1.0.0",
  "kind": "so",
  "abi": "arm64-v8a, armeabi-v7a",
  "min_host_version": 85,
  "url_arm64": "https://gitflic.ru/project/.../blob/raw?file=dist/android-arm64-v8a/my_module.so",
  "url_arm32": "https://gitflic.ru/project/.../blob/raw?file=dist/android-armeabi-v7a/my_module.so",
  "sha256_arm64": "<sha256_64>",
  "sha256_arm32": "<sha256_32>",
  "size_arm64": 123456,
  "size_arm32": 78910
}
```

KoteLoader автоматически определяет разрядность устройства и скачивает нужную версию модуля!
