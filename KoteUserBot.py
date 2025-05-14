# *   /\_/\  
# *  ( o.o )   Mew!
# *   > ^ <
# *
# *╭╮╭━╮╱╱╭╮╱╱╱╭━╮╭━╮╱╱╱╱╭╮╱╱╭╮
# *┃┃┃╭╯╱╭╯╰╮╱╱┃┃╰╯┃┃╱╱╱╱┃┃╱╱┃┃
# *┃╰╯╯╭━┻╮╭╋━━┫╭╮╭╮┣━━┳━╯┣╮╭┫┃╭━━┳━━╮
# *┃╭╮┃┃╭╮┃┃┃┃━┫┃┃┃┃┃╭╮┃╭╮┃┃┃┃┃┃┃━┫━━┫
# *┃┃┃╰┫╰╯┃╰┫┃━┫┃┃┃┃┃╰╯┃╰╯┃╰╯┃╰┫┃━┋━━┃
# *╰╯╰━┻━━┻━┻━━┻╯╰╯╰┻━━┻━━┻━━┻━┻━━┻━━╯
# *
# *                        © Copyright 2025
# Name: KoteUserBot
# Authors: Kote
# Commands:
# .help | .info | .ping | .helps | .tag | .add | .remove | .dele | .сипался | .version | .stags | .stconfig | .stoptag | .name | .spam | .stopspam
# scope: Telegram_Only
# meta developer: @Aaaggrrr

import sys
import traceback
import time
import datetime
import os
from dotenv import load_dotenv
import platform
import subprocess
import json
import re
import unicodedata
import asyncio
import sqlite3
import aiohttp
from collections import defaultdict
from typing import List, Dict, Any

# Проверка зависимостей
try:
    from telethon import TelegramClient, events, types, functions
    from telethon.extensions import markdown
    from telethon.tl.functions.channels import LeaveChannelRequest
    from telethon.tl.functions.users import GetFullUserRequest
    from telethon.tl.functions.contacts import GetBlockedRequest
    from telethon.tl.types import PeerChannel
except ImportError as e:
    print(f"[Critical] Ошибка импорта зависимостей: {e}")
    print("[Critical] Убедитесь, что установлен Telethon: `pip install telethon`")
    sys.exit(1)

# Конфигурация API
load_dotenv()
api_id = int(os.getenv('API_ID'))
api_hash = os.getenv('API_HASH')
session = 'my_session'
start_time = time.time()
owner_id = None

# Конфигурация белых списков для .tag
WHITELISTS = defaultdict(list)
WHITELISTS_FILE = 'whitelists.json'
TAG_COOLDOWN = 10

# Конфигурация Silent Tags
SILENT_TAGS_ENABLED = False
SILENT_TAGS_CONFIG: Dict[str, Any] = {
    'silent': False,
    'ignore_bots': False,
    'ignore_blocked': False,
    'ignore_users': [],
    'ignore_chats': [],
    'use_whitelist': False,
    'use_chat_whitelist': False
}
BLOCKED_USERS = []  # Список заблокированных пользователей
FW_PROTECT = {}  # Защита от флуд-уведомлений
FW_PROTECT_LIMIT = 5  # Лимит уведомлений за 5 минут
SPAM_RUNNING = False  # Флаг для отслеживания выполнения .spam
SPAM_TASK = None  # Переменная для хранения задачи спама

# Добавляем состояние для отслеживания последней обработанной команды .stoptag
TAG_STATE = {
    'running': False,
    'last_message_id': None
    }

CONFIG = {
    'prefix': '.'  # Префикс команд
}

# База данных SQLite для Silent Tags и логов
DB_FILE = 'koteuserbot.db'

def init_db():
    print("[Debug] Инициализация базы данных")
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS silent_tags_config (
                param TEXT PRIMARY KEY,
                value TEXT
            )
        ''')
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS error_log_group (
                id INTEGER PRIMARY KEY,
                group_id INTEGER UNIQUE
            )
        ''')
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS silence_log_group (
                id INTEGER PRIMARY KEY,
                group_id INTEGER UNIQUE
            )
        ''')
        # Проверяем, нет ли повреждённых данных
        cursor.execute('SELECT group_id FROM error_log_group WHERE id = 1')
        result = cursor.fetchone()
        if result and result[0] is None:
            cursor.execute('DELETE FROM error_log_group WHERE id = 1')
        cursor.execute('SELECT group_id FROM silence_log_group WHERE id = 1')
        result = cursor.fetchone()
        if result and result[0] is None:
            cursor.execute('DELETE FROM silence_log_group WHERE id = 1')
        conn.commit()
    except Exception as e:
        print(f"[Error] Ошибка инициализации базы данных: {e}")
        raise
    finally:
        conn.close()

def load_silent_tags_config():
    global SILENT_TAGS_ENABLED, SILENT_TAGS_CONFIG
    print("[Debug] Загрузка конфигурации Silent Tags")
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()

        # Загрузка enabled
        cursor.execute('SELECT value FROM silent_tags_config WHERE param = ?', ('enabled',))
        result = cursor.fetchone()
        SILENT_TAGS_ENABLED = result[0] == 'true' if result else False

        # Загрузка остальных параметров
        for param in SILENT_TAGS_CONFIG:
            cursor.execute('SELECT value FROM silent_tags_config WHERE param = ?', (param,))
            result = cursor.fetchone()
            if result:
                if param in ['ignore_users', 'ignore_chats']:
                    SILENT_TAGS_CONFIG[param] = json.loads(result[0]) if result[0] else []
                elif param in ['silent', 'ignore_bots', 'ignore_blocked', 'use_whitelist', 'use_chat_whitelist']:
                    SILENT_TAGS_CONFIG[param] = result[0] == 'true'
    except Exception as e:
        print(f"[Error] Ошибка загрузки конфигурации Silent Tags: {e}")
        raise
    finally:
        conn.close()

def save_silent_tags_config():
    print("[Debug] Сохранение конфигурации Silent Tags")
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()

        # Сохранение enabled
        cursor.execute('INSERT OR REPLACE INTO silent_tags_config (param, value) VALUES (?, ?)',
                      ('enabled', 'true' if SILENT_TAGS_ENABLED else 'false'))

        # Сохранение остальных параметров
        for param, value in SILENT_TAGS_CONFIG.items():
            if isinstance(value, list):
                value = json.dumps(value)
            elif isinstance(value, bool):
                value = 'true' if value else 'false'
            cursor.execute('INSERT OR REPLACE INTO silent_tags_config (param, value) VALUES (?, ?)',
                          (param, value))

        conn.commit()
    except Exception as e:
        print(f"[Error] Ошибка сохранения конфигурации Silent Tags: {e}")
        raise
    finally:
        conn.close()

async def get_silence_log_group():
    print("[Debug] Получение ID группы Silent Tags")
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute('SELECT group_id FROM silence_log_group WHERE id = ?', (1,))
        result = cursor.fetchone()
        return result[0] if result else None
    except Exception as e:
        print(f"[Error] Ошибка получения группы Silent Tags: {e}")
        raise
    finally:
        conn.close()

async def save_silence_log_group(group_id):
    print(f"[Debug] Сохранение ID группы Silent Tags: {group_id}")
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute('INSERT OR REPLACE INTO silence_log_group (id, group_id) VALUES (?, ?)', (1, group_id))
        conn.commit()
    except Exception as e:
        print(f"[Error] Ошибка сохранения группы Silent Tags: {e}")
        raise
    finally:
        conn.close()

async def send_log(message, handler_name, event=None, is_test=False, is_tag_log=False):
    print(f"[Debug] Отправка {'тестового лога' if is_test else 'лога тега' if is_tag_log else 'ошибки'}: {message}")
    try:
        # Определяем, в какую группу отправлять
        if is_tag_log:
            group_id = await get_silence_log_group()
            if not group_id:
                group_id, group_link = await create_silence_log_group()
                if not group_id:
                    print("[Log] Не удалось создать группу для Silent Tags")
                    me = await client.get_me()
                    await client.send_message(me.id, f"<b>Ошибка:</b> Не удалось создать группу для Silent Tags\n<code>{message}</code>", parse_mode='HTML')
                    return
        else:
            group_id = await get_error_log_group()
            if not group_id:
                group_id, group_link = await create_error_log_group()
                if not group_id:
                    print("[Log] Не удалось создать группу для логов")
                    me = await client.get_me()
                    await client.send_message(me.id, f"<b>Ошибка:</b> Не удалось создать группу для логов\n<code>{message}</code>", parse_mode='HTML')
                    return

        # Формируем сообщение
        timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        chat_info = "Неизвестный чат"
        chat_id = None
        if event:
            try:
                chat = await event.get_chat()
                chat_info = getattr(chat, 'title', 'Private Chat') or f"ID {event.chat_id}"
                chat_id = event.chat_id
            except:
                pass

        silent_emoji = EMOJI_SET['regular']['silent']
        if is_test:
            log_text = (
                f"<b>{silent_emoji} Тестовый лог KoteUserBot</b>\n\n"
                f"<b>Время:</b> {timestamp}\n"
                f"<b>Сообщение:</b>\n<code>{message}</code>\n"
            )
        elif is_tag_log:
            log_text = message  # Для Silent Tags используем переданный текст
        else:
            log_text = (
                f"<b>{silent_emoji} Ошибка в KoteUserBot</b>\n\n"
                f"<b>Время:</b> {timestamp}\n"
                f"<b>Обработчик:</b> {handler_name}\n"
                f"<b>Чат:</b> {chat_info}\n"
                f"<b>Ошибка:</b>\n<code>{message}</code>\n"
            )
            if chat_id:
                log_text += f"<b>Chat ID:</b> {chat_id}\n"

        # Разрешаем сущность группы
        try:
            group_entity = await client.get_entity(group_id)
            print(f"[Debug] Группа найдена: {group_entity.title} (ID: {group_id})")
        except Exception as e:
            print(f"[Log] Не удалось разрешить сущность группы {group_id}: {str(e)}")
            group_entity = group_id

        # Отправляем сообщение
        try:
            await client.send_message(group_entity, log_text, parse_mode='HTML')
            print(f"[Log] {'Тестовый лог' if is_test else 'Лог тега' if is_tag_log else 'Ошибка'} отправлен в группу {group_id}")
        except Exception as e:
            print(f"[Log] Не удалось отправить в группу {group_id}: {str(e)}")
            me = await client.get_me()
            await client.send_message(me.id, f"<b>Ошибка отправки лога:</b> {str(e)}\n<code>{message}</code>", parse_mode='HTML')
            print(f"[Log] Лог отправлен в избранное")

    except Exception as e:
        error_msg = f"Критическая ошибка при отправке лога: {str(e)}"
        print(f"[Log] {error_msg}")
        try:
            me = await client.get_me()
            await client.send_message(me.id, error_msg, parse_mode='HTML')
        except Exception as e2:
            print(f"[Log] Не удалось отправить в избранное: {e2}")

async def create_silence_log_group():
    print("[Debug] Создание группы Silent Tags")
    try:
        me = await client.get_me()
        if not me:
            raise Exception("Не удалось получить данные аккаунта юзербота")

        group = await client(functions.channels.CreateChannelRequest(
            title='KoteUserBotSilence',
            about='Логи упоминаний Silent Tags KoteUserBot',
            megagroup=True
        ))

        print(f"[Debug] Ответ API: {group.__dict__}")

        if hasattr(group, 'chats') and group.chats:
            group_id = group.chats[0].id
        elif hasattr(group, 'updates') and group.updates:
            for update in group.updates:
                if hasattr(update, 'channel'):
                    group_id = update.channel.id
                    break
            else:
                raise Exception("Не удалось найти ID группы в ответе API")
        else:
            raise Exception("Неожиданный формат ответа API")

        # Установка аватарки из https://t.me/KoteUserBotMedia/5
        try:
            channel = await client.get_entity("@KoteUserBotMedia")
            msg = await client.get_messages(channel, ids=5)
            if not msg or not hasattr(msg, 'media') or not msg.media:
                raise ValueError("Изображение не найдено в указанном сообщении")
            if not isinstance(msg.media, types.MessageMediaPhoto):
                raise ValueError("Сообщение не содержит фотографию")
            photo = msg.media.photo
            await client(functions.channels.EditPhotoRequest(
                channel=group_id,
                photo=photo
            ))
            print(f"[Debug] Аватарка установлена для группы {group_id}")
        except Exception as e:
            print(f"[Log] Ошибка установки аватарки: {str(e)}")
            await send_log(str(e), "create_silence_log_group")

        # Получение приватной ссылки
        try:
            invite = await client(functions.messages.ExportChatInviteRequest(peer=group_id))
            group_link = invite.link
        except Exception as e:
            group_link = f"t.me/c/{group_id}"
            print(f"[Log] Не удалось получить ссылку на группу: {str(e)}")

        await save_silence_log_group(group_id)
        print(f"[Log] Создана новая группа для Silent Tags: {group_id}")
        print(f"[Log] Ссылка на группу: {group_link}")
        return group_id, group_link

    except Exception as e:
        error_msg = f"Ошибка при создании группы Silent Tags: {str(e)}"
        print(f"[Log] {error_msg}")
        try:
            me = await client.get_me()
            await client.send_message(me.id, error_msg, parse_mode='HTML')
        except Exception as e2:
            print(f"[Log] Не удалось отправить в избранное: {e2}")
        return None, None

# Функции для работы с группой логов
async def get_error_log_group():
    print("[Debug] Получение ID группы логов")
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute('SELECT group_id FROM error_log_group WHERE id = ?', (1,))
        result = cursor.fetchone()
        return result[0] if result else None
    except Exception as e:
        print(f"[Error] Ошибка получения группы логов: {e}")
        raise
    finally:
        conn.close()

async def save_error_log_group(group_id):
    print(f"[Debug] Сохранение ID группы логов: {group_id}")
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute('INSERT OR REPLACE INTO error_log_group (id, group_id) VALUES (?, ?)', (1, group_id))
        conn.commit()
    except Exception as e:
        print(f"[Error] Ошибка сохранения группы логов: {e}")
        raise
    finally:
        conn.close()

async def create_error_log_group():
    print("[Debug] Создание группы логов")
    try:
        me = await client.get_me()
        if not me:
            raise Exception("Не удалось получить данные аккаунта юзербота")

        group = await client(functions.channels.CreateChannelRequest(
            title='KoteUserBotDebug',  # Новое название
            about='Логи ошибок KoteUserBot',
            megagroup=True
        ))

        print(f"[Debug] Ответ API: {group.__dict__}")

        if hasattr(group, 'chats') and group.chats:
            group_id = group.chats[0].id
        elif hasattr(group, 'updates') and group.updates:
            for update in group.updates:
                if hasattr(update, 'channel'):
                    group_id = update.channel.id
                    break
            else:
                raise Exception("Не удалось найти ID группы в ответе API")
        else:
            raise Exception("Неожиданный формат ответа API")

        # Установка аватарки из сообщения
        try:
            channel = await client.get_entity("@KoteUserBotMedia")
            msg = await client.get_messages(channel, ids=3)  # ID сообщения с аватаркой
            if not msg or not hasattr(msg, 'media') or not msg.media:
                raise ValueError("Изображение не найдено в указанном сообщении")
            if not isinstance(msg.media, types.MessageMediaPhoto):
                raise ValueError("Сообщение не содержит фотографию")
            photo = msg.media.photo
            await client(functions.channels.EditPhotoRequest(
                channel=group_id,
                photo=photo
            ))
            print(f"[Debug] Аватарка установлена для группы {group_id}")
        except Exception as e:
            print(f"[ErrorLog] Ошибка установки аватарки: {str(e)}")
            await send_error_log(str(e), "create_error_log_group")

        # Получение приватной ссылки
        try:
            invite = await client(functions.messages.ExportChatInviteRequest(peer=group_id))
            group_link = invite.link
        except Exception as e:
            group_link = f"t.me/c/{group_id}"
            print(f"[ErrorLog] Не удалось получить ссылку на группу: {str(e)}")

        await save_error_log_group(group_id)
        print(f"[ErrorLog] Создана новая группа для логов: {group_id}")
        print(f"[ErrorLog] Ссылка на группу: {group_link}")
        return group_id, group_link

    except Exception as e:
        error_msg = f"Ошибка при создании группы: {str(e)}"
        print(f"[ErrorLog] {error_msg}")
        try:
            me = await client.get_me()
            await client.send_message(me.id, error_msg, parse_mode='HTML')
        except Exception as e2:
            print(f"[ErrorLog] Не удалось отправить в избранное: {e2}")
        return None, None

async def send_error_log(error_message, handler_name, event=None, is_test=False):
    print(f"[Debug] Отправка {'тестового лога' if is_test else 'ошибки'}: {error_message}")
    try:
        group_id = await get_error_log_group()
        if not group_id:
            group_id, group_link = await create_error_log_group()
            if not group_id:
                print("[ErrorLog] Не удалось создать группу для логов")
                # Логируем в избранное как запасной вариант
                me = await client.get_me()
                await client.send_message(me.id, f"<b>Ошибка:</b> Не удалось создать группу для логов\n<code>{error_message}</code>", parse_mode='HTML')
                return

        # Формируем красивое сообщение
        timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        chat_info = "Неизвестный чат"
        chat_id = None
        if event:
            try:
                chat = await event.get_chat()
                chat_info = getattr(chat, 'title', 'Private Chat') or f"ID {event.chat_id}"
                chat_id = event.chat_id
            except:
                pass

        silent_emoji = EMOJI_SET['regular']['silent']
        if is_test:
            error_text = (
                f"<b>{silent_emoji} Тестовый лог KoteUserBot</b>\n\n"
                f"<b>Время:</b> {timestamp}\n"
                f"<b>Сообщение:</b>\n<code>{error_message}</code>\n"
            )
        else:
            error_text = (
                f"<b>{silent_emoji} Ошибка в KoteUserBot</b>\n\n"
                f"<b>Время:</b> {timestamp}\n"
                f"<b>Обработчик:</b> {handler_name}\n"
                f"<b>Чат:</b> {chat_info}\n"
                f"<b>Ошибка:</b>\n<code>{error_message}</code>\n"
            )
            if chat_id:
                error_text += f"<b>Chat ID:</b> {chat_id}\n"

        # Разрешаем сущность группы
        try:
            group_entity = await client.get_entity(group_id)
            print(f"[Debug] Группа найдена: {group_entity.title} (ID: {group_id})")
        except Exception as e:
            print(f"[ErrorLog] Не удалось разрешить сущность группы {group_id}: {str(e)}")
            group_entity = group_id  # Пробуем отправить напрямую

        # Отправляем сообщение
        try:
            await client.send_message(group_entity, error_text, parse_mode='HTML')
            print(f"[ErrorLog] {'Тестовый лог' if is_test else 'Ошибка'} отправлен в группу {group_id}")
        except Exception as e:
            print(f"[ErrorLog] Не удалось отправить в группу {group_id}: {str(e)}")
            # Запасной вариант: отправка в избранное
            me = await client.get_me()
            await client.send_message(me.id, f"<b>Ошибка отправки лога:</b> {str(e)}\n<code>{error_message}</code>", parse_mode='HTML')
            print(f"[ErrorLog] Лог отправлен в избранное")

    except Exception as e:
        error_msg = f"Критическая ошибка при отправке лога: {str(e)}"
        print(f"[ErrorLog] {error_msg}")
        # Запасной вариант: отправка в избранное
        try:
            me = await client.get_me()
            await client.send_message(me.id, error_msg, parse_mode='HTML')
        except Exception as e2:
            print(f"[ErrorLog] Не удалось отправить в избранное: {e2}")

# Функции для работы с белыми списками (.tag)
def load_whitelists():
    print("[Debug] Загрузка белых списков")
    global WHITELISTS
    if os.path.exists(WHITELISTS_FILE):
        try:
            with open(WHITELISTS_FILE, 'r') as f:
                data = json.load(f)
                WHITELISTS = defaultdict(list, {int(k): v for k, v in data.items()})
        except Exception as e:
            print(f"[Error] Ошибка при загрузке белых списков: {e}")
    else:
        WHITELISTS = defaultdict(list)

def save_whitelists():
    print("[Debug] Сохранение белых списков")
    try:
        with open(WHITELISTS_FILE, 'w') as f:
            json.dump(dict(WHITELISTS), f)
    except Exception as e:
        print(f"[Error] Ошибка при сохранении белых списков: {e}")

# Общие утилиты
def detect_platform():
    if 'ANDROID_ROOT' in os.environ or os.path.exists('/data/data/com.termux'):
        return 'Termux'
    elif platform.system() == 'Linux':
        return 'VPS'
    else:
        return 'Unknown'

def get_git_branch():
    try:
        branch = subprocess.check_output(['git', 'rev-parse', '--abbrev-ref', 'HEAD']).decode('utf-8').strip()
        return branch
    except Exception:
        return 'main'

def get_uptime():
    uptime_seconds = time.time() - start_time
    uptime = str(datetime.timedelta(seconds=int(uptime_seconds)))
    return uptime

class InvalidFormatException(Exception):
    pass

class CustomParseMode:
    def __init__(self, parse_mode: str):
        self.parse_mode = parse_mode

    def parse(self, text):
        if self.parse_mode == 'markdown':
            text, entities = markdown.parse(text)
        else:
            raise InvalidFormatException("Only markdown supported.")

        for i, e in enumerate(entities):
            if isinstance(e, types.MessageEntityTextUrl):
                if e.url == 'spoiler':
                    entities[i] = types.MessageEntitySpoiler(e.offset, e.length)
                elif e.url.startswith('emoji/'):
                    try:
                        emoji_id = int(e.url.split('/')[1])
                        entities[i] = types.MessageEntityCustomEmoji(e.offset, e.length, emoji_id)
                    except Exception as e:
                        print(f"[Error] Ошибка с кастомным эмодзи: {e}")
                        entities[i] = types.MessageEntityTextUrl(e.offset, e.length, url='')
        return text, entities

# Инициализация клиента и парсера
print("[Debug] Инициализация Telegram клиента")
try:
    client = TelegramClient(session, api_id, api_hash)
    parser = CustomParseMode('markdown')
except Exception as e:
    print(f"[Critical] Ошибка инициализации клиента: {e}")
    print(traceback.format_exc())
    sys.exit(1)

# Набор эмодзи (с сохранением премиум-эмодзи)
EMOJI_SET = {
    'premium': {
        'ping': '[⚡️](emoji/5431449001532594346)',
        'rocket': '[🚀](emoji/5445284980978621387)',
        'help': '[📖](emoji/5373098009640836781)',
        'info': '[ℹ️](emoji/5228686859663585439)',
        'name': '[👤](emoji/5373012449597335010)',
        'username': '[📛](emoji/5431736674147114227)',
        'id': '[🆔](emoji/5974526806995242353)',
        'premium': '[⭐](emoji/5334523697174683404)',
        'leave': '[🥰](emoji/5420557514225770446)',
        'delete': '[🗑️](emoji/5445267414562389170)',
        'whitelist': '[📋](emoji/5334882760735598374)',
        'tag': '[🏷️]',
        'config': '[⚙️](emoji/5215327492738392838)',
        'silent': '[🤫](emoji/5370930189322688800)'
    },
    'regular': {
        'ping': '⚡️',
        'rocket': '🚀',
        'help': '📖',
        'info': 'ℹ️',
        'name': '👤',
        'username': '📛',
        'id': '🆔',
        'premium': '⭐',
        'leave': '🥰',
        'delete': '🗑️',
        'whitelist': '📋',
        'tag': '🏷️',
        'config': '⚙️',
        'silent': '🤫'
    }
}

# Утилиты для работы с пользователями и эмодзи
async def is_premium_user():
    print("[Debug] Проверка статуса Premium")
    try:
        user = await client.get_me()
        return user.premium
    except Exception as e:
        await send_error_log(str(e), "is_premium_user")
        return False

async def get_emoji(key):
    print(f"[Debug] Получение эмодзи: {key}")
    is_premium = await is_premium_user()
    emoji_type = 'premium' if is_premium else 'regular'
    emoji = EMOJI_SET[emoji_type][key]
    try:
        if emoji.startswith('[') and 'emoji/' in emoji:
            parsed_text, entities = parser.parse(emoji)
            if not any(isinstance(e, types.MessageEntityCustomEmoji) for e in entities):
                return EMOJI_SET['regular'][key]
        return emoji
    except Exception:
        return EMOJI_SET['regular'][key]

async def is_owner(event):
    return event.sender_id == owner_id

async def get_user_id(identifier):
    print(f"[Debug] Получение ID пользователя: {identifier}")
    try:
        if isinstance(identifier, int) or identifier.isdigit():
            return int(identifier)
        elif identifier.startswith('@'):
            user = await client(GetFullUserRequest(identifier))
            return user.users[0].id
        else:
            return None
    except Exception as e:
        await send_error_log(str(e), "get_user_id")
        return None

async def is_bot(user_id):
    print(f"[Debug] Проверка, является ли пользователь ботом: {user_id}")
    try:
        user = await client(GetFullUserRequest(user_id))
        return user.users[0].bot
    except Exception:
        return False

async def get_chat_title(chat_id):
    print(f"[Debug] Получение названия чата: {chat_id}")
    try:
        chat = await client.get_entity(chat_id)
        return chat.title if hasattr(chat, 'title') else "Личные сообщения"
    except Exception:
        return "Неизвестный чат"

# Функция для безопасного редактирования сообщения
async def safe_edit_message(event, text, entities):
    print(f"[Debug] Безопасное редактирование сообщения: message_id={event.message.id}, text={text}")
    try:
        if not event.message:
            print("[Debug] Сообщение отсутствует, отправка нового")
            await client.send_message(event.chat_id, text, formatting_entities=entities)
            return
        await event.message.edit(text, formatting_entities=entities)
    except Exception as e:
        error_msg = str(e)
        print(f"[Debug] Ошибка редактирования сообщения: {error_msg}")
        if "The document file was invalid" in error_msg:
            text_fallback = re.sub(r'\[([^\]]+)\]\(emoji/\d+\)', r'\1', text)
            parsed_text, entities = parser.parse(text_fallback)
            await event.message.delete()
            await client.send_message(event.chat_id, parsed_text, formatting_entities=entities)
        else:
            await send_error_log(error_msg, "safe_edit_message", event)
            await client.send_message(event.chat_id, f"Ошибка: {error_msg}")

# Декоратор для обработки ошибок
def error_handler(handler):
    async def wrapper(event):
        print(f"[Debug] Выполнение обработчика: {handler.__name__}")
        try:
            await handler(event)
        except Exception as e:
            error_msg = f"{str(e)}\n\nTraceback: {''.join(traceback.format_tb(e.__traceback__))}"
            await send_error_log(error_msg, handler.__name__, event)
            await safe_edit_message(event, f"**Ошибка:** {str(e)}", [])
    return wrapper

# === ОБРАБОТЧИК КОМАНДЫ .ping ===
@client.on(events.NewMessage(pattern=r'^\.ping$'))
@error_handler
async def ping_handler(event):
    if not await is_owner(event):
        return
    start = time.time()
    uptime = get_uptime()
    
    response_time = (time.time() - start) * 1000
    ping_emoji = await get_emoji('ping')
    rocket_emoji = await get_emoji('rocket')
    text = (
        f"**{ping_emoji} Скорость отклика Telegram:** {response_time:.3f} мс\n"
        f"**{rocket_emoji} Время работы:** {uptime}\n\n"
    )
    
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

# === ОБРАБОТЧИК КОМАНДЫ .help ===
@client.on(events.NewMessage(pattern=r'^\.help(?:\s+(.+))?$'))
@error_handler
async def help_handler(event):
    if not await is_owner(event):
        return
    args = event.pattern_match.group(1)
    help_emoji = await get_emoji('help')
    
    if args:
        args = args.lower().strip()
        commands_help = {
            'ping': f"**{help_emoji} .ping**\nПоказывает скорость отклика Telegram и время работы бота.",
            'help': f"**{help_emoji} .help**\nПоказывает список команд. Используйте `.help <команда>` для подробной информации.",
            'helps': f"**{help_emoji} .helps**\nПоказывает белый список пользователей для .tag в текущей группе.",
            'info': f"**{help_emoji} .info**\nПоказывает информацию об аккаунте (ник, username, ID, статус Premium).",
            'version': f"**{help_emoji} .version**\nПоказывает информацию о версии бота.",
            'сипался': f"**{help_emoji} .сипался**\nПокидает группу с прощальным сообщением (только в группах).",
            'dele': f"**{help_emoji} .dele <количество>**\nУдаляет указанное количество сообщений в группе (если есть права, до 100).",
            'add': f"**{help_emoji} .add @username/ID**\nДобавляет пользователя в белый список для .tag.",
            'remove': f"**{help_emoji} .remove @username/ID**\nУдаляет пользователя из белого списка для .tag.",
            'tag': f"**{help_emoji} .tag <текст>**\nТегирует всех пользователей группы, кроме ботов и белого списка.",
            'stoptag': f"**{help_emoji} .stoptag**\nОстанавливает выполнение команды .tag.",
            'name': f"**{help_emoji} .name <новый ник>**\nИзменяет имя аккаунта без входа в настройки.",
            'spam': f"**{help_emoji} .spam <количество> <текст>**\nОтправляет указанное количество сообщений с текстом (до 100, задержка 0.3с).",
            'stopspam': f"**{help_emoji} .stopspam**\nОстанавливает выполнение команды .spam.",
            'stags': f"**{help_emoji} .stags <on/off>**\nВключает или выключает Silent Tags (уведомления об упоминаниях в избранное).",
            'stconfig': (
                f"**{help_emoji} .stconfig**\nПоказывает текущие настройки Silent Tags.\n\n"
                f"**.stconfig <параметр> true/false**\nИзменяет булевый параметр (silent, ignore_bots, и т.д.).\n"
                f"**.stconfig <список> add/remove @username/ID/this**\nДобавляет/удаляет пользователя или чат в список (ignore_users, ignore_chats, и т.д.).\n"
                f"Пример: `.stconfig silent true`, `.stconfig ignore_chats add this`"
            )
        }
        text = commands_help.get(args, f"**Ошибка:** Команда `{args}` не найдена!")
    else:
        text = (
            f"**{help_emoji} Список команд:**\n\n"
            f"**Основные**\n"
            f"`.ping` — Пинг и время работы\n"
            f"`.info` — Инфо об аккаунте\n"
            f"`.version` — Версия бота\n"
            f"`.help [команда]` — Справка\n"
            f"`.name <ник>` — Сменить имя\n\n"
            f"**Группы**\n"
            f"`.tag [текст]` — Тег всех\n"
            f"`.stoptag` — Остановить тег\n"
            f"`.add @username` — Добавить в whitelist\n"
            f"`.remove @username` — Удалить из whitelist\n"
            f"`.helps` — Показать whitelist\n"
            f"`.dele <число>` — Удалить сообщения\n"
            f"`.сипался` — Покинуть группу\n"
            f"`.spam <число> <текст>` — Спам\n"
            f"`.stopspam` — Остановить спам\n\n"
            f"**Silent Tags**\n"
            f"`.stags [on/off]` — Вкл/выкл логи\n"
            f"`.stconfig` — Настройки\n\n"
            f"Подробно: `.help <команда>`"
        )
    
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

# === ОБРАБОТЧИК КОМАНДЫ .helps ===
@client.on(events.NewMessage(pattern=r'^\.helps$'))
@error_handler
async def helps_handler(event):
    if not await is_owner(event):
        return
    if not event.is_group:
        text = "**Ошибка:** Эта команда работает только в группах!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    chat = await event.get_chat()
    chat_id = chat.id
    whitelist = WHITELISTS[chat_id]
    
    if not whitelist:
        text = "**📋 Белый список пуст!**"
    else:
        users = []
        for user_id in whitelist:
            try:
                user = await client(GetFullUserRequest(user_id))
                username = f"@{user.users[0].username}" if user.users[0].username else f"ID {user_id}"
                users.append(username)
            except Exception:
                users.append(f"ID {user_id}")
        whitelist_emoji = await get_emoji('whitelist')
        text = f"**{whitelist_emoji} Белый список для этой группы:**\n\n" + "\n".join(users)
    
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

# === ОБРАБОТЧИК КОМАНДЫ .add ===
@client.on(events.NewMessage(pattern=r'^\.add\s+(.+)$'))
@error_handler
async def add_handler(event):
    if not await is_owner(event):
        return
    if not event.is_group:
        text = "**Ошибка:** Эта команда работает только в группах!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    identifier = event.pattern_match.group(1).strip()
    user_id = await get_user_id(identifier)
    if not user_id:
        text = "**Ошибка:** Неверный @username или ID!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    chat = await event.get_chat()
    chat_id = chat.id
    whitelist = WHITELISTS[chat_id]
    
    if user_id in whitelist:
        text = "**Ошибка:** Пользователь уже в белом списке!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    WHITELISTS[chat_id].append(user_id)
    save_whitelists()
    whitelist_emoji = await get_emoji('whitelist')
    text = f"**{whitelist_emoji} Пользователь {identifier} добавлен в белый список!**"
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

# === ОБРАБОТЧИК КОМАНДЫ .remove ===
@client.on(events.NewMessage(pattern=r'^\.remove\s+(.+)$'))
@error_handler
async def remove_handler(event):
    if not await is_owner(event):
        return
    if not event.is_group:
        text = "**Ошибка:** Эта команда работает только в группах!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    identifier = event.pattern_match.group(1).strip()
    user_id = await get_user_id(identifier)
    if not user_id:
        text = "**Ошибка:** Неверный @username или ID!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    chat = await event.get_chat()
    chat_id = chat.id
    whitelist = WHITELISTS[chat_id]
    
    if user_id not in whitelist:
        text = "**Ошибка:** Пользователь не в белом списке!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    WHITELISTS[chat_id].remove(user_id)
    save_whitelists()
    whitelist_emoji = await get_emoji('whitelist')
    text = f"**{whitelist_emoji} Пользователь {identifier} удалён из белого списка!**"
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

# Конфигурация для спама
SPAM_STATE = {
    'running': False,
    'task': None,
    'last_message_id': None  # Для отслеживания обработанного сообщения
}

@client.on(events.NewMessage(pattern=r'^\.spam\s+(\d+)\s+([\s\S]*)$'))
@error_handler
async def spam_handler(event):
    print(f"[Debug] Запуск spam_handler для сообщения: {event.raw_text}, message_id={event.message.id}")
    if not await is_owner(event):
        print("[Debug] Пользователь не владелец, пропуск")
        return

    if SPAM_STATE['last_message_id'] == event.message.id:
        print("[Debug] Сообщение уже обработано, пропуск")
        return
    SPAM_STATE['last_message_id'] = event.message.id

    if SPAM_STATE['running']:
        print("[Debug] Спам уже выполняется, пропуск")
        text = "**Ошибка:** Спам уже выполняется! Используйте `.stopspam` для остановки."
        parsed_text, entities = parser.parse(text)
        await event.message.edit(parsed_text, formatting_entities=entities)
        return

    try:
        count = int(event.pattern_match.group(1))
        message_text = event.pattern_match.group(2).strip()
        print(f"[Debug] Параметры спама: count={count}, message_text={message_text}")

        if count <= 0:
            text = "**Ошибка:** Укажите положительное количество сообщений!"
            parsed_text, entities = parser.parse(text)
            await event.message.edit(parsed_text, formatting_entities=entities)
            return
        if count > 100:
            text = "**Ошибка:** Нельзя отправить больше 100 сообщений за раз!"
            parsed_text, entities = parser.parse(text)
            await event.message.edit(parsed_text, formatting_entities=entities)
            return
        if not message_text:
            text = "**Ошибка:** Укажите текст для спама!"
            parsed_text, entities = parser.parse(text)
            await event.message.edit(parsed_text, formatting_entities=entities)
            return

        # Парсинг текста для спама
        parsed_text, parsed_entities = parser.parse(message_text)
        input_entities = event.message.entities or []
        adjusted_entities = []
        if input_entities:
            offset = len(f'.spam {count} ')
            for e in input_entities:
                if e.offset >= offset:
                    start = e.offset - offset
                    end = start + e.length
                    if end > len(message_text):
                        e.length = len(message_text) - start if start < len(message_text) else 0
                    entity_dict = e.__dict__.copy()
                    entity_dict['offset'] = start
                    for key in ['_client', '__weakref__']:
                        entity_dict.pop(key, None)
                    adjusted_entities.append(e.__class__(**entity_dict))
        
        entities = []
        seen = set()
        for e in parsed_entities + adjusted_entities:
            entity_key = (e.offset, e.length, type(e).__name__, str(e.__dict__))
            if entity_key not in seen:
                entities.append(e)
                seen.add(entity_key)

        SPAM_STATE['running'] = True
        chat = await event.get_chat()
        print(f"[Debug] Спам начат в чате {chat.id}, текст: {parsed_text}")

        async def spam_task():
            try:
                for i in range(count):
                    if not SPAM_STATE['running']:
                        print(f"[Debug] Спам остановлен на итерации {i}")
                        break
                    print(f"[Debug] Отправка сообщения {i+1}/{count}: {parsed_text}")
                    await client.send_message(chat, parsed_text, formatting_entities=entities)
                    await asyncio.sleep(0.5)
            except Exception as e:
                print(f"[Debug] Ошибка в spam_task: {str(e)}")
                await send_error_log(str(e), "spam_task", event)
            finally:
                SPAM_STATE['running'] = False
                SPAM_STATE['task'] = None
                print("[Debug] spam_task завершён")

        SPAM_STATE['task'] = client.loop.create_task(spam_task())
        start_text = f"🚀 Спам начат: {count} сообщений!"
        start_entities = [types.MessageEntityBold(offset=0, length=len(start_text))]
        await event.message.edit(start_text, formatting_entities=start_entities)
        print("[Debug] Сообщение о старте спама отправлено")
    except ValueError:
        text = "**Ошибка:** Количество должно быть числом!"
        parsed_text, entities = parser.parse(text)
        await event.message.edit(parsed_text, formatting_entities=entities)
    except Exception as e:
        print(f"[Debug] Ошибка в spam_handler: {str(e)}")
        await send_error_log(str(e), "spam_handler", event)
        text = f"Ошибка: Не удалось запустить спам: {str(e)}"
        entities = [types.MessageEntityBold(offset=0, length=len(text))]
        await event.message.edit(text, formatting_entities=entities)

@client.on(events.NewMessage(pattern=r'^\.stopspam$'))
@error_handler
async def stopspam_handler(event):
    print("[Debug] Запуск stopspam_handler")
    if not await is_owner(event):
        print("[Debug] Пользователь не владелец, пропуск")
        return
    if not SPAM_STATE['running']:
        print("[Debug] Спам не выполняется")
        text = "Ошибка: Спам не выполняется!"
        entities = [types.MessageEntityBold(offset=0, length=len(text))]
        await event.message.edit(text, formatting_entities=entities)
        return

    SPAM_STATE['running'] = False
    if SPAM_STATE['task']:
        SPAM_STATE['task'].cancel()
        SPAM_STATE['task'] = None
        print("[Debug] Задача спама отменена")
    text = "🛑 Спам остановлен!"
    entities = [types.MessageEntityBold(offset=0, length=len(text))]
    await event.message.edit(text, formatting_entities=entities)
    print("[Debug] Сообщение об остановке спама отправлено")

# === ОБРАБОТЧИК КОМАНДЫ .tag ===
@client.on(events.NewMessage(pattern=r'^\.tag\s*([\s\S]*)$'))
@error_handler
async def tag_handler(event):
    print(f"[Debug] Запуск tag_handler для сообщения: {event.raw_text}, message_id={event.message.id}")
    if not await is_owner(event):
        print("[Debug] Пользователь не владелец, пропуск")
        return
    if not event.is_group:
        text = "**Ошибка:** Эта команда работает только в группах!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    # Проверяем, не было ли сообщение уже обработано
    if TAG_STATE['last_message_id'] == event.message.id:
        print("[Debug] Сообщение уже обработано, пропуск")
        return
    TAG_STATE['last_message_id'] = event.message.id

    if TAG_STATE['running']:
        print("[Debug] Тегирование уже выполняется")
        text = "**Ошибка:** Тегирование уже выполняется! Используйте `.stoptag` для остановки."
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    chat = await event.get_chat()
    chat_id = chat.id
    whitelist = WHITELISTS[chat_id]
    
    users_to_tag = []
    async for user in client.iter_participants(chat):
        if user.bot or user.id in whitelist or user.id == owner_id or user.deleted:
            continue
        if not user.first_name and not user.last_name:
            continue
        users_to_tag.append(user)

    if not users_to_tag:
        text = "**Ошибка:** Нет подходящих пользователей для тегирования!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    TAG_STATE['running'] = True
    print(f"[Debug] Тегирование начато для {len(users_to_tag)} пользователей в чате {chat_id}")
    try:
        input_text = event.message.text[len('.tag '):].strip()
        input_entities = event.message.entities or []
        
        if input_text:
            parsed_text, parsed_entities = parser.parse(input_text)
        else:
            parsed_text, parsed_entities = input_text, []
        
        adjusted_entities = []
        if input_entities:
            offset = len('.tag ')
            for e in input_entities:
                if e.offset >= offset:
                    start = e.offset - offset
                    end = start + e.length
                    if end > len(input_text):
                        e.length = len(input_text) - start if start < len(input_text) else 0
                    entity_dict = e.__dict__.copy()
                    entity_dict['offset'] = start
                    for key in ['_client', '__weakref__']:
                        entity_dict.pop(key, None)
                    adjusted_entities.append(e.__class__(**entity_dict))
        
        entities = []
        seen = set()
        for e in parsed_entities + adjusted_entities:
            entity_key = (e.offset, e.length, type(e).__name__, str(e.__dict__))
            if entity_key not in seen:
                entities.append(e)
                seen.add(entity_key)

        start_text = f"🚀 Тегирование начато: {len(users_to_tag)} пользователей!"
        start_entities = [types.MessageEntityBold(offset=0, length=len(start_text))]
        await safe_edit_message(event, start_text, start_entities)

        for i in range(0, len(users_to_tag), 5):
            if not TAG_STATE['running']:
                print("[Debug] Тегирование остановлено по флагу")
                break  # Просто прерываем цикл, без отправки сообщения

            group = users_to_tag[i:i+5]
            
            if parsed_text:
                prefix = f"{parsed_text}\n\n"
                current_entities = entities[:]
            else:
                prefix = ""
                current_entities = []
            
            tags = []
            prefix_utf16 = prefix.encode('utf-16-le')
            current_offset = len(prefix_utf16) // 2
            
            for user in group:
                if user.username:
                    tag = f"@{user.username}"
                    tags.append(tag)
                else:
                    name_parts = []
                    if user.first_name:
                        name_parts.append(user.first_name.strip())
                    if user.last_name:
                        name_parts.append(user.last_name.strip())
                    name = ' '.join(name_parts) or 'User'
                    clean_name = ''.join(c for c in unicodedata.normalize('NFKC', name) if c.isalnum() or c.isspace())
                    clean_name = clean_name.strip() or 'User'
                    tag = clean_name
                    tags.append(tag)
                    tag_length = len(tag.encode('utf-16-le')) // 2
                    current_entities.append(
                        types.MessageEntityTextUrl(
                            offset=current_offset,
                            length=tag_length,
                            url=f"tg://user?id={user.id}"
                        )
                    )
                tag_utf16 = tag.encode('utf-16-le')
                current_offset += len(tag_utf16) // 2 + 1

            text = prefix + " ".join(tags)
            print(f"[Debug] Отправка тегов для группы {i//5 + 1}: {text}")
            await client.send_message(chat, text, formatting_entities=current_entities)
            
            if i + 5 < len(users_to_tag):
                print(f"[Debug] Ожидание {TAG_COOLDOWN} секунд перед следующей группой")
                await asyncio.sleep(TAG_COOLDOWN)
    except Exception as e:
        error_msg = f"Ошибка в tag_handler: {str(e)}"
        print(f"[Debug] {error_msg}")
        await send_error_log(error_msg, "tag_handler", event)
        text = f"**Ошибка:** Не удалось выполнить тегирование: {str(e)}"
        entities = [types.MessageEntityBold(offset=0, length=len(text))]
        await safe_edit_message(event, text, entities)
    finally:
        TAG_STATE['running'] = False
        print("[Debug] Тегирование завершено, флаг сброшен")
        # Удаляем начальное сообщение
        try:
            await event.message.delete()
            print("[Debug] Начальное сообщение удалено")
        except Exception as e:
            print(f"[Debug] Ошибка удаления начального сообщения: {str(e)}")

# === ОБРАБОТЧИК КОМАНДЫ .stoptag ===
@client.on(events.NewMessage(pattern=r'^\.stoptag$'))
@error_handler
async def stoptag_handler(event):
    print(f"[Debug] Запуск stoptag_handler для сообщения: {event.raw_text}, message_id={event.message.id}")
    if not await is_owner(event):
        print("[Debug] Пользователь не владелец, пропуск")
        return

    # Проверяем, не было ли сообщение уже обработано
    if TAG_STATE['last_message_id'] == event.message.id:
        print("[Debug] Сообщение уже обработано, пропуск")
        return
    TAG_STATE['last_message_id'] = event.message.id

    if not TAG_STATE['running']:
        print("[Debug] Тегирование не выполняется")
        text = "Ошибка: Тегирование не выполняется!"
        entities = [types.MessageEntityBold(offset=0, length=len(text))]
        await safe_edit_message(event, text, entities)
        return

    TAG_STATE['running'] = False
    text = "🛑 Тегирование остановлено!"
    entities = [types.MessageEntityBold(offset=0, length=len(text))]
    await safe_edit_message(event, text, entities)
    print("[Debug] Сообщение об остановке тегирования отправлено")

# === ОБРАБОТЧИК КОМАНДЫ .name ===
@client.on(events.NewMessage(pattern=r'^\.name\s+(.+)$'))
@error_handler
async def name_handler(event):
    if not await is_owner(event):
        return
    new_name = event.pattern_match.group(1).strip()
    if not new_name:
        text = "**Ошибка:** Укажите новое имя!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    try:
        await client(functions.account.UpdateProfileRequest(
            first_name=new_name
        ))
        name_emoji = await get_emoji('name')
        text = f"**{name_emoji} Имя изменено на `{new_name}`!**"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
    except Exception as e:
        text = f"**Ошибка:** Не удалось изменить имя: {str(e)}"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)

# === ОБРАБОТЧИК КОМАНДЫ .info ===
@client.on(events.NewMessage(pattern=r'^\.info$'))
@error_handler
async def info_handler(event):
    if not await is_owner(event):
        return
    user = await client.get_me()
    info_emoji = await get_emoji('info')
    name_emoji = await get_emoji('name')
    username_emoji = await get_emoji('username')
    id_emoji = await get_emoji('id')
    premium_emoji = await get_emoji('premium')
    
    username = f"@{user.username}" if user.username else "Нет"
    first_name = user.first_name or "Не указано"
    user_id = user.id
    premium_status = "Да" if user.premium else "Нет"
    
    text = (
        f"**{info_emoji} Информация об аккаунте:**\n\n"
        f"**{name_emoji} Ник:** {first_name}\n"
        f"**{username_emoji} Username:** {username}\n"
        f"**{id_emoji} ID:** {user_id}\n"
        f"**{premium_emoji} Premium:** {premium_status}\n"
    )
    
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

# === ОБРАБОТЧИК КОМАНДЫ .сипался ===
@client.on(events.NewMessage(pattern=r'^\.сипался$'))
@error_handler
async def leave_handler(event):
    if not await is_owner(event):
        return
    if not event.is_group:
        text = "**Ошибка:** Эта команда работает только в группах!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    leave_emoji = await get_emoji('leave')
    text = f"**{leave_emoji} Я ушёл, пока!**"
    parsed_text, entities = parser.parse(text)
    
    await safe_edit_message(event, parsed_text, entities)
    
    chat = await event.get_chat()
    await client(LeaveChannelRequest(chat))

# === ОБРАБОТЧИК КОМАНДЫ .dele ===
@client.on(events.NewMessage(pattern=r'^\.dele\s+(\d+)$'))
@error_handler
async def delete_handler(event):
    if not await is_owner(event):
        return
    if not event.is_group:
        text = "**Ошибка:** Эта команда работает только в группах!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    count = int(event.pattern_match.group(1))
    if count <= 0:
        text = "**Ошибка:** Укажите положительное число сообщений!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    if count > 100:
        text = "**Ошибка:** Нельзя удалить больше 100 сообщений за раз!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    chat = await event.get_chat()
    messages = []
    async for msg in client.iter_messages(chat, limit=count):
        if msg.id != event.message.id:
            messages.append(msg.id)

    if not messages:
        text = "**Ошибка:** Нет сообщений для удаления!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    await client.delete_messages(chat, messages)
    delete_emoji = await get_emoji('delete')
    text = f"**{delete_emoji} Удалено {len(messages)} сообщений!**"
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

# === ОБРАБОТЧИК КОМАНДЫ .version ===
@client.on(events.NewMessage(pattern=r'^\.version$'))
@error_handler
async def version_handler(event):
    if not await is_owner(event):
        return
    module_version = "1.0.1"  # Текущая версия
    uptime = get_uptime()
    user = await client.get_me()
    owner_username = f"@{user.username}" if user.username else "Не указан"
    branch = get_git_branch()
    prefix = CONFIG['prefix']
    platform = detect_platform()

    info_emoji = await get_emoji('info')
    premium_emoji = await get_emoji('premium')

    # Проверка новой версии
    latest_version = module_version
    update_text = "\n⚠️ Не удалось проверить обновления"
    try:
        async with aiohttp.ClientSession() as session:
            async with session.get('https://api.github.com/repos/AresUser1/KoteModules/releases/latest') as resp:
                status = resp.status
                print(f"[Debug] Статус GitHub API: {status}")
                if status == 200:
                    data = await resp.json()
                    print(f"[Debug] Ответ API: {data}")
                    latest_version = data['tag_name'].lstrip('v')
                    if latest_version != module_version:
                        update_text = (
                            f"\n⚠️ Доступна новая версия: {latest_version}\n"
                            f"Обнови: [GitHub](https://github.com/AresUser1/KoteModules/releases/latest)"
                        )
                    else:
                        update_text = ""  # Если версия та же, ничего не показываем
                elif status == 404:
                    print(f"[Debug] Релизы не найдены (404)")
                    update_text = "\n⚠️ Не удалось проверить обновления"
                else:
                    error_text = await resp.text()
                    print(f"[Debug] Ошибка GitHub API: Статус {status}, Текст: {error_text}")
                    update_text = "\n⚠️ Не удалось проверить обновления"
    except Exception as e:
        print(f"[Debug] Исключение GitHub API: {str(e)}")
        await send_error_log(f"Ошибка проверки обновлений: Исключение: {str(e)}", "version_handler", event)
        update_text = "\n⚠️ Не удалось проверить обновления"

    text = (
        f"{info_emoji} KoteUserBot\n"
        f"Owner: {owner_username}\n"
        f"\n"
        f"Version: {module_version}\n"
        f"Branch: {branch}\n"
        f"Uptime: {uptime}\n"
        f"Prefix: {prefix}\n"
        f"Platform: {platform}\n"
        f"{update_text}\n"
        f"\n"
        f"{premium_emoji} Developed with 💖 by Kote"
    )

    parsed_text, entities = parser.parse(text)

    try:
        channel = await client.get_entity("@KoteUserBotMedia")
        msg = await client.get_messages(channel, ids=2)
        if not msg or not hasattr(msg, 'media') or not msg.media:
            raise ValueError("Видео не найдено")
        video_file = msg.media.document

        await client.send_file(
            event.chat_id,
            file=video_file,
            caption=parsed_text,
            formatting_entities=entities,
            reply_to=event.message.id
        )
        await event.message.delete()
    except Exception as e:
        print(f"[Debug] Ошибка отправки медиа: {str(e)}")
        await send_error_log(f"Ошибка отправки медиа: {str(e)}", "version_handler", event)
        await safe_edit_message(event, parsed_text, entities)

# === ОБРАБОТЧИК КОМАНДЫ .stags ===
@client.on(events.NewMessage(pattern=r'^\.stags\s*(on|off)?$'))
async def stags_handler(event):
    if not await is_owner(event):
        return
    try:
        global SILENT_TAGS_ENABLED
        silent_emoji = await get_emoji('silent')
        args = event.pattern_match.group(1)
        if not args:
            status = "включены" if SILENT_TAGS_ENABLED else "выключены"
            text = f"**{silent_emoji} Silent Tags {status}**"
        else:
            SILENT_TAGS_ENABLED = args == "on"
            status = "включены" if SILENT_TAGS_ENABLED else "выключены"
            text = f"**{silent_emoji} Silent Tags теперь {status}**"
            FW_PROTECT.clear()
            save_silent_tags_config()

        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
    except Exception as e:
        print(f"Ошибка в stags_handler: {e}")
        await safe_edit_message(event, f"Ошибка: {str(e)}", [])

# === ОБРАБОТЧИК КОМАНДЫ .stconfig ===
@client.on(events.NewMessage(pattern=r'^\.stconfig(?:\s+(.+))?$'))
async def stconfig_handler(event):
    if not await is_owner(event):
        return
    try:
        silent_emoji = await get_emoji('silent')
        args = event.pattern_match.group(1)

        if not args:
            text = f"**{silent_emoji} Настройки Silent Tags:**\n\n"
            text += f"**Статус:** {'включены' if SILENT_TAGS_ENABLED else 'выключены'}\n"
            for param, value in SILENT_TAGS_CONFIG.items():
                if param in ['ignore_users', 'ignore_chats']:
                    if value:
                        items = []
                        for item in value:
                            if param.endswith('_users'):
                                try:
                                    user = await client.get_entity(item)
                                    items.append(f"@{user.username}" if user.username else f"ID {item}")
                                except Exception:
                                    items.append(f"ID {item}")
                            else:
                                items.append(f"{await get_chat_title(item)}")
                        text += f"**{param}:** {', '.join(items)}\n"
                elif value:
                    text += f"**{param}:** {value}\n"
        else:
            parts = args.strip().split()
            if len(parts) < 2:
                text = f"**{silent_emoji} Ошибка:** Неверный формат! Используйте `.help stconfig` для справки."
                parsed_text, entities = parser.parse(text)
                await safe_edit_message(event, parsed_text, entities)
                return

            param = parts[0].lower()
            valid_bool_params = ['silent', 'ignore_bots', 'ignore_blocked', 'use_whitelist', 'use_chat_whitelist']
            valid_list_params = ['ignore_users', 'ignore_chats']

            if param in valid_bool_params:
                if len(parts) != 2 or parts[1].lower() not in ['true', 'false']:
                    text = f"**{silent_emoji} Ошибка:** Для `{param}` укажите `true` или `false`! Пример: `.stconfig {param} true`"
                    parsed_text, entities = parser.parse(text)
                    await safe_edit_message(event, parsed_text, entities)
                    return
                value = parts[1].lower() == 'true'
                SILENT_TAGS_CONFIG[param] = value
                save_silent_tags_config()
                text = f"**{silent_emoji} Параметр `{param}` установлен в `{value}`!**"
            elif param in valid_list_params:
                if len(parts) != 3 or parts[1].lower() not in ['add', 'remove']:
                    text = f"**{silent_emoji} Ошибка:** Для `{param}` укажите `add` или `remove` и значение! Пример: `.stconfig {param} add @username`"
                    parsed_text, entities = parser.parse(text)
                    await safe_edit_message(event, parsed_text, entities)
                    return
                action = parts[1].lower()
                identifier = parts[2].strip()

                if param.endswith('_users'):
                    if identifier == 'this':
                        text = f"**{silent_emoji} Ошибка:** Для `{param}` укажите @username или ID, а не `this`!"
                        parsed_text, entities = parser.parse(text)
                        await safe_edit_message(event, parsed_text, entities)
                        return
                    entity_id = await get_user_id(identifier)
                    if not entity_id:
                        text = f"**{silent_emoji} Ошибка:** Неверный @username или ID!"
                        parsed_text, entities = parser.parse(text)
                        await safe_edit_message(event, parsed_text, entities)
                        return
                else:
                    if identifier == 'this':
                        if not event.is_group:
                            text = f"**{silent_emoji} Ошибка:** Команда работает только в группах!"
                            parsed_text, entities = parser.parse(text)
                            await safe_edit_message(event, parsed_text, entities)
                            return
                        chat = await event.get_chat()
                        entity_id = abs(chat.id)  # Нормализация chat_id
                    else:
                        try:
                            entity_id = abs(int(identifier))  # Нормализация для введённых ID
                        except ValueError:
                            text = f"**{silent_emoji} Ошибка:** Для `{param}` укажите ID чата или `this`!"
                            parsed_text, entities = parser.parse(text)
                            await safe_edit_message(event, parsed_text, entities)
                            return

                if action == 'add':
                    if entity_id not in SILENT_TAGS_CONFIG[param]:
                        SILENT_TAGS_CONFIG[param].append(entity_id)
                        save_silent_tags_config()
                        entity_name = await get_chat_title(entity_id) if param.endswith('_chats') else identifier
                        text = f"**{silent_emoji} {entity_name} добавлен в `{param}`!**"
                    else:
                        text = f"**{silent_emoji} Ошибка:** {identifier} уже в `{param}`!"
                else:
                    if entity_id in SILENT_TAGS_CONFIG[param]:
                        SILENT_TAGS_CONFIG[param].remove(entity_id)
                        save_silent_tags_config()
                        entity_name = await get_chat_title(entity_id) if param.endswith('_chats') else identifier
                        text = f"**{silent_emoji} {entity_name} удалён из `{param}`!**"
                    else:
                        text = f"**{silent_emoji} Ошибка:** {identifier} не в `{param}`!"

            else:
                text = f"**{silent_emoji} Ошибка:** Неверный параметр! Доступные: {', '.join(valid_bool_params + valid_list_params)}"

        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
    except Exception as e:
        print(f"Ошибка в stconfig_handler: {e}")
        await safe_edit_message(event, f"Ошибка: {str(e)}", [])

# === ОБРАБОТЧИК УПОМИНАНИЙ ДЛЯ SILENT TAGS ===
@client.on(events.NewMessage(incoming=True))
async def silent_tags_watcher(event):
    global FW_PROTECT
    if not event.mentioned or not SILENT_TAGS_ENABLED:
        return

    print(f"[SilentTags] Обработка упоминания: chat_id={event.chat_id}, sender_id={event.sender_id}")
    try:
        # Проверяем, инициализирован ли owner_id
        if owner_id is None:
            print("[SilentTags] owner_id не установлен, пропуск")
            return

        # Получаем отправителя
        try:
            sender = await event.get_sender()
            if sender is None:
                print(f"[SilentTags] Отправитель не определён, пропуск: chat_id={event.chat_id}")
                return
            sender_id = sender.id
            is_sender_bot = getattr(sender, 'bot', False)
        except Exception as e:
            print(f"[SilentTags] Ошибка получения отправителя: {str(e)}")
            return

        # Пропускаем сообщения от самого бота
        if sender_id == owner_id:
            print(f"[SilentTags] Пропуск упоминания от самого бота: sender_id={sender_id}")
            return

        # Получаем информацию о чате
        try:
            chat = await event.get_chat()
            chat_title = getattr(chat, 'title', 'Private Chat')
        except Exception as e:
            print(f"[SilentTags] Ошибка получения чата: {str(e)}")
            chat_title = "Неизвестный чат"

        # Нормализация chat_id
        normalized_chat_id = int(str(event.chat_id).replace('-100', '')) if str(event.chat_id).startswith('-100') else abs(event.chat_id)
        print(f"[SilentTags] normalized_chat_id={normalized_chat_id}, chat_title={chat_title}, is_bot={is_sender_bot}")
        print(f"[SilentTags] ignore_chats={SILENT_TAGS_CONFIG['ignore_chats']}, use_chat_whitelist={SILENT_TAGS_CONFIG['use_chat_whitelist']}, ignore_bots={SILENT_TAGS_CONFIG['ignore_bots']}")

        # Проверка: если ignore_bots включен и отправитель — бот
        if SILENT_TAGS_CONFIG['ignore_bots'] and is_sender_bot:
            await client.send_read_acknowledge(event.chat_id, clear_mentions=True)
            print(f"[SilentTags] Упоминание от бота помечено как прочитанное и пропущено (ignore_bots=true): chat_id={event.chat_id}, sender_id={sender_id}")
            return

        # Проверка фильтров для пропуска уведомлений
        if (
            (sender_id in SILENT_TAGS_CONFIG['ignore_users'] and SILENT_TAGS_CONFIG['use_whitelist']) or
            (SILENT_TAGS_CONFIG['ignore_blocked'] and sender_id in BLOCKED_USERS)
        ):
            print(f"[SilentTags] Упоминание пропущено: chat_id={event.chat_id}, normalized_chat_id={normalized_chat_id}, sender_id={sender_id}")
            return

        # Проверка чатов в игнор списке
        chat_ignored = (
            SILENT_TAGS_CONFIG['use_chat_whitelist'] and
            normalized_chat_id in SILENT_TAGS_CONFIG['ignore_chats']
        )

        cid = event.chat_id

        # Если чат игнорируется, выходим
        if chat_ignored:
            print(f"[SilentTags] Упоминание полностью игнорируется: chat_id={cid}")
            return

        # Антифлуд
        if cid in FW_PROTECT and len([t for t in FW_PROTECT[cid] if t > time.time()]) > FW_PROTECT_LIMIT:
            print(f"[SilentTags] Антифлуд сработал для chat_id={cid}")
            return

        # Инициализация _ratelimit
        if not hasattr(globals(), '_ratelimit'):
            globals()['_ratelimit'] = []

        # Отправка сообщения в чат
        if cid not in _ratelimit and not SILENT_TAGS_CONFIG['silent']:
            _ratelimit.append(cid)
            try:
                silent_emoji = await get_emoji('silent')
                text = f"**{silent_emoji} Silent Tags теперь включены**"
                parsed_text, entities = parser.parse(text)
                msg = await client.send_message(event.chat_id, parsed_text, formatting_entities=entities)
                await asyncio.sleep(3)
                await msg.delete()
            except Exception as e:
                print(f"[SilentTags] Ошибка отправки сообщения о Silent Tags: {str(e)}")
            finally:
                if cid in _ratelimit:
                    _ratelimit.remove(cid)

        # Помечаем сообщение как прочитанное
        try:
            await client.send_read_acknowledge(event.chat_id, clear_mentions=True)
            print(f"[SilentTags] Упоминание помечено как прочитанное: chat_id={cid}")
        except Exception as e:
            print(f"[SilentTags] Ошибка при отметке сообщения как прочитанного: {str(e)}")

        # Формирование уведомления в KoteUserBotSilence
        group_link = f"https://t.me/c/{str(normalized_chat_id)}" if not isinstance(chat, types.User) else ""
        user_name = getattr(sender, 'first_name', 'Unknown') or getattr(sender, 'title', 'Unknown')

        silent_emoji = EMOJI_SET['regular']['silent']
        message_text = (
            f"{silent_emoji} Вас упомянули в <a href=\"{group_link}\">{chat_title}</a> пользователем "
            f"<a href=\"tg://openmessage?user_id={sender_id}\">{user_name}</a>\n"
            f"<b>Сообщение:</b>\n<code>{event.raw_text}</code>\n"
            f"<b>Ссылка:</b> <a href=\"https://t.me/c/{str(normalized_chat_id)}/{event.id}\">перейти</a>"
        )

        # Отправка в KoteUserBotSilence
        try:
            await send_log(message_text, "silent_tags_watcher", event, is_tag_log=True)
            print(f"[SilentTags] Упоминание отправлено в KoteUserBotSilence: chat_id={cid}, sender_id={sender_id}")
        except Exception as e:
            print(f"[SilentTags] Ошибка отправки лога в KoteUserBotSilence: {str(e)}")

        # Обновление FW_PROTECT
        if cid not in FW_PROTECT:
            FW_PROTECT[cid] = []
        FW_PROTECT[cid].append(time.time() + 5 * 60)

    except Exception as e:
        print(f"[SilentTags] Ошибка в silent_tags_watcher: {str(e)}")
        try:
            await send_log(str(e), "silent_tags_watcher", event)
        except Exception as e2:
            print(f"[SilentTags] Ошибка при логировании: {str(e2)}")

# Функция для автоматического обновления BLOCKED_USERS
async def update_blocked_users():
    print("[Debug] Запуск задачи обновления заблокированных пользователей")
    global BLOCKED_USERS
    while True:
        try:
            if SILENT_TAGS_CONFIG['ignore_blocked']:
                blocked = await client(GetBlockedRequest(offset=0, limit=1000))
                new_blocked_users = [user.id for user in blocked.users]
                if new_blocked_users != BLOCKED_USERS:
                    BLOCKED_USERS = new_blocked_users
                    print(f"[SilentTags] Обновлено {len(BLOCKED_USERS)} заблокированных пользователей")
        except Exception as e:
            await send_error_log(str(e), "update_blocked_users")
        await asyncio.sleep(10)

def debug_db():
    print("[Debug] Отладка базы данных")
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute('SELECT param, value FROM silent_tags_config')
        results = cursor.fetchall()
        print("[SilentTags] Содержимое базы данных (silent_tags_config):")
        for param, value in results:
            print(f"  {param}: {value}")
        cursor.execute('SELECT id, group_id FROM error_log_group')
        results = cursor.fetchall()
        print("[ErrorLog] Содержимое базы данных (error_log_group):")
        for id, group_id in results:
            print(f"  id: {id}, group_id: {group_id}")
    except Exception as e:
        print(f"[Error] Ошибка отладки базы данных: {e}")
        raise
    finally:
        conn.close()

# Основная функция
async def main():
    global owner_id, BLOCKED_USERS
    print("[Debug] Запуск основной функции")
    try:
        print("[Debug] Загрузка белых списков")
        load_whitelists()
        print("[Debug] Инициализация базы данных")
        init_db()
        print("[Debug] Загрузка конфигурации Silent Tags")
        load_silent_tags_config()
        print("[Debug] Отладка базы данных")
        debug_db()
        
        print("[Debug] Получение информации о пользователе")
        user = await client.get_me()
        if not user:
            raise Exception("Не удалось получить данные пользователя")
        owner_id = user.id
        print(f"[Main] Userbot запущен! Владелец: ID {owner_id}, Username: @{user.username or 'None'}")

        # Проверяем или создаём группу логов ошибок
        print("[Debug] Проверка группы логов ошибок")
        group_id = await get_error_log_group()
        if group_id:
            print(f"[Main] Найдена группа логов ошибок: {group_id}")
            try:
                invite = await client(functions.messages.ExportChatInviteRequest(peer=group_id))
                group_link = invite.link
            except Exception as e:
                group_link = f"t.me/c/{group_id}"
                print(f"[Log] Не удалось получить ссылку на существующую группу ошибок: {str(e)}")
            print(f"[Main] Ссылка на группу логов ошибок: {group_link}")
        else:
            print("[Debug] Создание новой группы логов ошибок")
            group_id, group_link = await create_error_log_group()
            if group_id:
                print(f"[Main] Создана группа логов ошибок: {group_id}")
                print(f"[Main] Ссылка на группу логов ошибок: {group_link}")
            else:
                print("[Main] Не удалось создать группу логов ошибок")

        # Проверяем или создаём группу Silent Tags
        print("[Debug] Проверка группы Silent Tags")
        silence_group_id = await get_silence_log_group()
        if silence_group_id:
            print(f"[Main] Найдена группа Silent Tags: {silence_group_id}")
            try:
                invite = await client(functions.messages.ExportChatInviteRequest(peer=silence_group_id))
                group_link = invite.link
            except Exception as e:
                group_link = f"t.me/c/{silence_group_id}"
                print(f"[Log] Не удалось получить ссыл쿠 на существующую группу Silent Tags: {str(e)}")
            print(f"[Main] Ссылка на группу Silent Tags: {group_link}")
        else:
            print("[Debug] Создание новой группы Silent Tags")
            silence_group_id, group_link = await create_silence_log_group()
            if silence_group_id:
                print(f"[Main] Создана группа Silent Tags: {silence_group_id}")
                print(f"[Main] Ссылка на группу Silent Tags: {group_link}")
                # Отправляем тестовый лог
                print("[Debug] Отправка тестового лога в Silent Tags")
                await send_log("KoteUserBotSilence запущен успешно", "main", is_test=True, is_tag_log=True)
            else:
                print("[Main] Не удалось создать группу Silent Tags")

        print("[Debug] Загрузка заблокированных пользователей")
        if SILENT_TAGS_CONFIG['ignore_blocked']:
            blocked = await client(GetBlockedRequest(offset=0, limit=1000))
            BLOCKED_USERS = [user.id for user in blocked.users]
            print(f"[Main] Загружено {len(BLOCKED_USERS)} заблокированных пользователей")

        print("[Debug] Запуск задачи обновления заблокированных пользователей")
        client.loop.create_task(update_blocked_users())

        print("[Debug] Клиент готов, ожидание событий")
        await client.run_until_disconnected()
    except Exception as e:
        error_msg = f"Критическая ошибка при запуске: {str(e)}\n\nTraceback: {''.join(traceback.format_tb(e.__traceback__))}"
        print(f"[Main] {error_msg}")
        try:
            await send_log(error_msg, "main")
        except Exception as e2:
            print(f"[Error] Не удалось отправить ошибку в Telegram: {e2}")
        sys.exit(1)

# Асинхронная функция для запуска
async def start_bot():
    print("[Debug] Запуск программы")
    try:
        print("[Debug] Подключение клиента")
        await client.start()
        print("[Debug] Клиент подключен, запуск main")
        await main()
    except Exception as e:
        print(f"[Critical] Ошибка при запуске: {e}")
        print(traceback.format_exc())
        print("[Critical] Возможные причины:")
        print("- Недействительная сессия Telegram. Удалите my_session.session и запустите снова.")
        print("- Отсутствует Telethon. Установите: `pip install telethon`.")
        print("- Проблемы с сетью или доступом к файлам в Termux.")
        await client.disconnect()
        sys.exit(1)

if __name__ == '__main__':
    try:
        client.loop.run_until_complete(start_bot())
    except KeyboardInterrupt:
        print("\n[Main] Получен сигнал остановки (Ctrl+C), завершение работы...")
        try:
            # Отменяем все фоновые задачи
            tasks = [task for task in asyncio.all_tasks(client.loop) if task is not asyncio.current_task(client.loop)]
            for task in tasks:
                task.cancel()
                print(f"[Debug] Отмена задачи: {task.get_name()}")
            
            # Даём задачам время на завершение
            client.loop.run_until_complete(client.loop.shutdown_asyncgens())
            print("[Debug] Все асинхронные генераторы завершены")
            
            # Отключаем клиент
            print("[Debug] Отключение клиента Telegram")
            client.loop.run_until_complete(client.disconnect())
            print("[Main] Юзербот остановлен")
        except asyncio.CancelledError:
            print("[Debug] Получено CancelledError при завершении задач")
        except Exception as e:
            print(f"[Error] Ошибка при завершении: {e}")
        finally:
            # Закрываем цикл событий
            client.loop.close()
            print("[Debug] Цикл событий закрыт")
            sys.exit(0)
    except Exception as e:
        print(f"[Critical] Ошибка в цикле событий: {e}")
        print(traceback.format_exc())
        client.loop.run_until_complete(client.disconnect())
        client.loop.close()
        sys.exit(1)