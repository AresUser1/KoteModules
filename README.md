# KoteUserBot
Юзербот для Telegram с командами для тегирования, спама, Silent Tags и многого другого.

## Возможности
- `.ping` — Проверяет скорость отклика и время работы.
- `.tag <текст>` — Тегирует всех в группе (кроме ботов и белого списка).
- `.stags on/off` — Включает/выключает логи упоминаний.
- Полный список: `.help`.

## Установка
1. **Клонируйте репозиторий**:
   ```bash
   git clone https://github.com/AresUser1/KoteModules.git
   cd KoteModules
   ```

2. **Установите зависимости**:
   ```bash
   pip install -r requirements.txt
   ```

3. **Настройте API-ключи**:
   - Получите `API_ID` и `API_HASH` на [my.telegram.org](https://my.telegram.org).
   - Создайте файл `.env`:
     ```bash
     echo -e "API_ID=твой_api_id\nAPI_HASH=твой_api_hash" > .env
     ```

4. **Запустите бота**:
   ```bash
   python KoteUserBot.py
   ```

## Обновление
1. Подтяните изменения:
   ```bash
   git pull origin main
   ```
2. Обновите зависимости:
   ```bash
   pip install -r requirements.txt
   ```
3. Перезапустите:
   ```bash
   python KoteUserBot.py
   ```

## Альтернативная установка (без Git)
1. Скачайте [KoteUserBot.py](https://raw.githubusercontent.com/AresUser1/KoteModules/main/KoteUserBot.py).
2. Установите библиотеки:
   ```bash
   pip install telethon==1.36.0 aiohttp==3.10.10 python-dotenv==1.0.1
   ```
3. Создайте `.env` (см. выше).
4. Запустите: `python KoteUserBot.py`.

## Примечания
- Для работы медиа в `.version` нужен доступ к каналу `@KoteUserBotMedia` (сообщение ID=2). Если используете свой канал, обновите код.
- Используйте `tmux` для запуска в фоне:
  ```bash
  pkg install tmux
  tmux
  python KoteUserBot.py
  ```
  Выйти: `Ctrl+B`, `D`. Вернуться: `tmux attach`.

## Лицензия
MIT License

© 2025 Kote
