# * /_/\
# * ( o.o )   Mew!
# * > ^ <
# *
# *╭╮╭━╮╱╱╭╮╱╱╱╭━╮╭━╮╱╱╱╱╭╮╱╱╭╮
# *┃┃┃╭╯╱╭╯╰╮╱╱┃┃╰╯┃┃╱╱╱╱┃┃╱╱┃┃
# *┃╰╯╯╭━┻╮╭╋━━┫╭╮╭╮┣━━┳━╯┣╮╭┫┃╭━━┳━━╮
# *┃╭╮┃┃╭╮┃┃┃┃━┫┃┃┃┃┃╭╮┃╭╮┃┃┃┃┃┃┃━┫━━┫
# *┃┃┃╰┫╰╯┃╰┫┃━┫┃┃┃┃┃╰╯┃╰╯┃╰╯┃╰┫┃━⋋━━┃
# *╰╯╰━┻━━┻━┻━━┻╯╰╯╰┻━━┻━━┻━━┻━┻━━┻━━╯
# *
# * © Copyright 2025
# Name: KoteUserBot
# Authors: Kote
# Commands:
# .help | .helps | .ping | .info | .version | .сипался | .dele | .add | .remove | .tag | .stoptag | .name | .autoupdate | .restart | .spam | .stopspam | .stags | .stconfig | .on | .off | .setprefix | .status | .profile | .backup | .mus | .dice | .typing | .stoptyping | .weather | .admin | .prefix | .unadmin | .unprefix | .adminhelp | .adminsettings | .admins | .addrp | .delrp | .rplist | .rp | .addrpcreator | .delrpcreator | .listrpcreators | .setrpnick | .delrpnick | .rpnick
# scope: Telegram_Only
# meta developer: @Aaaggrrr

import shutil
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
import uuid
import unicodedata
import asyncio
import sqlite3
import aiohttp
import random
from collections import defaultdict
from typing import List, Dict, Any
import zipfile

try:
    from telethon import TelegramClient, events, types, functions, errors
    from telethon.extensions import markdown, html
    from telethon.tl.functions.channels import LeaveChannelRequest, EditAdminRequest, GetParticipantRequest
    from telethon.tl.functions.users import GetFullUserRequest
    from telethon.tl.functions.contacts import GetBlockedRequest, UnblockRequest, BlockRequest
    from telethon.tl.functions.account import UpdateProfileRequest
    from telethon.tl.types import PeerChannel, ChatAdminRights, ChannelParticipantAdmin
    from telethon.utils import get_display_name
    from telethon.errors.rpcerrorlist import ChatAdminRequiredError, UserNotParticipantError, RightForbiddenError, UserAdminInvalidError
except ImportError as e:
    print(f"[Critical] Ошибка импорта зависимостей: {e}")
    print(f"[Critical] Установите Telethon: `pip install telethon`")
    sys.exit(1)

def create_env_file():
    print("\n[Setup] Файл .env не найден. Создаём новый.")
    print("1. Перейдите на https://my.telegram.org")
    print("2. Войдите и выберите 'API development tools'")
    print("3. Создайте приложение, получите API_ID и API_HASH")
    api_id = input("\nВведите API_ID: ").strip()
    api_hash = input("Введите API_HASH: ").strip()

    if not api_id.isdigit() or not api_hash:
        print("[Error] API_ID — число, API_HASH — не пустой!")
        sys.exit(1)

    try:
        with open('.env', 'w') as f:
            f.write(f"API_ID={api_id}\n")
            f.write(f"API_HASH={api_hash}\n")
        os.chmod('.env', 0o600)
    except Exception as e:
        print(f"[Error] Ошибка создания .env: {e}")
        sys.exit(1)

if not os.path.exists('.env'):
    create_env_file()

load_dotenv()
api_id = int(os.getenv('API_ID'))
api_hash = os.getenv('API_HASH')
session = 'my_session'
start_time = time.time()
owner_id = None
RESTART_FLAG = False

CONFIG = {
    'prefix': '.',
}

ADMIN_RIGHTS_MAP = {
    "info": "change_info",
    "post": "post_messages",
    "edit": "edit_messages",
    "delete": "delete_messages",
    "ban": "ban_users",
    "invite": "invite_users",
    "pin": "pin_messages",
    "add_admins": "add_admins",
    "anon": "anonymous",
    "call": "manage_call",
    "poststory": "post_stories",
    "editstory": "edit_stories",
    "delstory": "delete_stories",
}

ADMIN_RIGHTS_HELP = {
    "info": "Изменение информации о группе",
    "post": "Публикация сообщений (для каналов)",
    "edit": "Редактирование чужих сообщений (для каналов)",
    "delete": "Удаление чужих сообщений",
    "ban": "Блокировка пользователей",
    "invite": "Приглашение пользователей",
    "pin": "Закрепление сообщений",
    "add_admins": "Добавление новых администраторов",
    "anon": "Анонимная отправка (в группах)",
    "call": "Управление видеочатами",
    "poststory": "Публикация историй",
    "editstory": "Редактирование историй",
    "delstory": "Удаление историй",
}

ADMIN_RIGHTS_CONFIG = {}
TYPING_STATE = {'running': False, 'task': None, 'chat_id': None}
WHITELISTS = defaultdict(list)
WHITELISTS_FILE = 'whitelists.json'
BOT_ENABLED = True
SILENT_TAGS_ENABLED = False
SILENT_TAGS_CONFIG: Dict[str, Any] = {
    'silent': False, 'ignore_bots': False, 'ignore_blocked': False,
    'ignore_users': [], 'ignore_chats': [], 'use_whitelist': False, 'use_chat_whitelist': False
}

TAG_CONFIG = {
    'delay': 3,
    'priority': 'id',
    'position': 'before'
}

BOT_BLOCKED_USERS = set()
FW_PROTECT = {}
FW_PROTECT_LIMIT = 5
SPAM_RUNNING = False
SPAM_TASK = None
TAG_STATE = {'running': False, 'last_message_id': None}
RP_COMMANDS = {}
RP_ENABLED_CHATS = set()
RP_ACCESS_LIST = defaultdict(set)
RP_PUBLIC_CHATS = set()
RP_CREATORS = set()
GLOBAL_NICKS = defaultdict(dict)
RP_NICKS = defaultdict(dict)
ADMIN_CONFIGS = {}
DB_FILE = 'koteuserbot.db'

def init_db():
    print("[Debug] Инициализация базы данных")
    conn = None
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()

        # --- НАЧАЛО ИСПРАВЛЕНИЯ ---

        # Миграция для rp_commands (уже была, оставляем)
        cursor.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='rp_commands'")
        if cursor.fetchone():
            cursor.execute("PRAGMA table_info(rp_commands)")
            columns = [column[1] for column in cursor.fetchall()]
            if 'premium_emoji_id' in columns and 'premium_emoji_ids' not in columns:
                print("[Debug] Обнаружена старая структура 'rp_commands'. Миграция...")
                cursor.execute('ALTER TABLE rp_commands RENAME TO rp_commands_old')
                cursor.execute('''CREATE TABLE rp_commands (
                                    command TEXT PRIMARY KEY,
                                    action TEXT NOT NULL,
                                    premium_emoji_ids TEXT,
                                    standard_emoji TEXT)
                               ''')
                cursor.execute('''INSERT INTO rp_commands (command, action, premium_emoji_ids, standard_emoji)
                                  SELECT command, action,
                                         CASE WHEN premium_emoji_id IS NOT NULL THEN json_array(premium_emoji_id) ELSE NULL END,
                                         standard_emoji
                                  FROM rp_commands_old
                               ''')
                cursor.execute('DROP TABLE rp_commands_old')
                conn.commit()
                print("[Debug] Миграция 'rp_commands' завершена.")

        # НОВАЯ МИГРАЦИЯ для rp_nicknames
        cursor.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='rp_nicknames'")
        if cursor.fetchone():
            cursor.execute("PRAGMA table_info(rp_nicknames)")
            columns = [column[1] for column in cursor.fetchall()]
            if 'chat_id' not in columns:
                print("[Debug] Обнаружена старая структура 'rp_nicknames'. Миграция...")
                cursor.execute('ALTER TABLE rp_nicknames RENAME TO rp_nicknames_old')
                cursor.execute('''
                    CREATE TABLE rp_nicknames (
                        user_id INTEGER NOT NULL,
                        chat_id INTEGER NOT NULL,
                        nickname TEXT NOT NULL,
                        PRIMARY KEY (user_id, chat_id)
                    )
                ''')
                # Переносим данные, предполагая что старые ники были глобальными (chat_id = 0)
                cursor.execute('''
                    INSERT INTO rp_nicknames (user_id, chat_id, nickname)
                    SELECT user_id, 0, nickname FROM rp_nicknames_old
                ''')
                cursor.execute('DROP TABLE rp_nicknames_old')
                conn.commit()
                print("[Debug] Миграция 'rp_nicknames' завершена.")

        # НОВАЯ МИГРАЦИЯ для global_nicknames (на всякий случай)
        cursor.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='global_nicknames'")
        if cursor.fetchone():
            cursor.execute("PRAGMA table_info(global_nicknames)")
            columns = [column[1] for column in cursor.fetchall()]
            if 'chat_id' not in columns:
                print("[Debug] Обнаружена старая структура 'global_nicknames'. Миграция...")
                cursor.execute('ALTER TABLE global_nicknames RENAME TO global_nicknames_old')
                cursor.execute('''
                    CREATE TABLE global_nicknames (
                        user_id INTEGER NOT NULL,
                        chat_id INTEGER NOT NULL,
                        nickname TEXT NOT NULL,
                        PRIMARY KEY (user_id, chat_id)
                    )
                ''')
                cursor.execute('''
                    INSERT INTO global_nicknames (user_id, chat_id, nickname)
                    SELECT user_id, 0, nickname FROM global_nicknames_old
                ''')
                cursor.execute('DROP TABLE global_nicknames_old')
                conn.commit()
                print("[Debug] Миграция 'global_nicknames' завершена.")

        # --- КОНЕЦ ИСПРАВЛЕНИЯ ---

        # Создание таблиц (если их нет)
        cursor.execute('''CREATE TABLE IF NOT EXISTS rp_commands (
                            command TEXT PRIMARY KEY,
                            action TEXT NOT NULL,
                            premium_emoji_ids TEXT,
                            standard_emoji TEXT)
                       ''')
        cursor.execute('CREATE TABLE IF NOT EXISTS silent_tags_config (param TEXT PRIMARY KEY, value TEXT)')
        cursor.execute('CREATE TABLE IF NOT EXISTS error_log_group (id INTEGER PRIMARY KEY, group_id INTEGER UNIQUE)')
        cursor.execute('CREATE TABLE IF NOT EXISTS silence_log_group (id INTEGER PRIMARY KEY, group_id INTEGER UNIQUE)')
        cursor.execute('CREATE TABLE IF NOT EXISTS bot_config (param TEXT PRIMARY KEY, value TEXT)')
        cursor.execute('CREATE TABLE IF NOT EXISTS rp_enabled_chats (chat_id INTEGER PRIMARY KEY)')
        cursor.execute('CREATE TABLE IF NOT EXISTS rp_access_list (chat_id INTEGER NOT NULL, user_id INTEGER NOT NULL, PRIMARY KEY (chat_id, user_id))')
        cursor.execute('CREATE TABLE IF NOT EXISTS rp_public_chats (chat_id INTEGER PRIMARY KEY)')
        cursor.execute('CREATE TABLE IF NOT EXISTS rp_creators (user_id INTEGER PRIMARY KEY)')
        cursor.execute('CREATE TABLE IF NOT EXISTS admin_rights_config (right_name TEXT PRIMARY KEY, is_enabled INTEGER)')
        cursor.execute('CREATE TABLE IF NOT EXISTS admin_configs (name TEXT PRIMARY KEY, rights TEXT)')
        cursor.execute('CREATE TABLE IF NOT EXISTS tag_config (param TEXT PRIMARY KEY, value TEXT)')
        cursor.execute('CREATE TABLE IF NOT EXISTS bot_blocklist (user_id INTEGER PRIMARY KEY)')

        cursor.execute('''
            CREATE TABLE IF NOT EXISTS rp_nicknames (
                user_id INTEGER NOT NULL,
                chat_id INTEGER NOT NULL,
                nickname TEXT NOT NULL,
                PRIMARY KEY (user_id, chat_id)
            )
        ''')
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS global_nicknames (
                user_id INTEGER NOT NULL,
                chat_id INTEGER NOT NULL,
                nickname TEXT NOT NULL,
                PRIMARY KEY (user_id, chat_id)
            )
        ''')

        cursor.execute('SELECT COUNT(*) FROM admin_rights_config')
        if cursor.fetchone()[0] == 0:
            default_rights = {
                'change_info': 0, 'post_messages': 0, 'edit_messages': 0,
                'delete_messages': 1, 'ban_users': 1, 'invite_users': 1,
                'pin_messages': 1, 'add_admins': 0, 'anonymous': 0, 'manage_call': 1,
                'post_stories': 0, 'edit_stories': 0, 'delete_stories': 0
            }
            cursor.executemany('INSERT OR REPLACE INTO admin_rights_config (right_name, is_enabled) VALUES (?, ?)', list(default_rights.items()))

        conn.commit()
    except Exception as e:
        print(f"[Error] Ошибка инициализации базы данных: {e}")
        raise
    finally:
        if conn:
            conn.close()

def load_config():
    global CONFIG
    print("[Debug] Загрузка конфигурации")
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute("SELECT param, value FROM bot_config")
        db_config = {row[0]: row[1] for row in cursor.fetchall()}
        CONFIG['prefix'] = db_config.get('prefix', '.')
    except Exception as e:
        print(f"[Error] Ошибка загрузки конфигурации: {e}")
        CONFIG['prefix'] = '.'
    finally:
        conn.close()

def save_config():
    print("[Debug] Сохранение конфигурации")
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute('INSERT OR REPLACE INTO bot_config (param, value) VALUES (?, ?)', ('prefix', CONFIG['prefix']))
        conn.commit()
    except Exception as e:
        print(f"[Error] Ошибка сохранения конфигурации: {e}")
    finally:
        conn.close()

def load_admin_rights_config():
    global ADMIN_RIGHTS_CONFIG
    print("[Debug] Загрузка конфигурации прав администратора")
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute("SELECT right_name, is_enabled FROM admin_rights_config")
        ADMIN_RIGHTS_CONFIG = {row[0]: bool(row[1]) for row in cursor.fetchall()}
    except Exception as e:
        print(f"[Error] Ошибка загрузки конфигурации прав администратора: {e}")
    finally:
        conn.close()

def save_admin_right(right_name, is_enabled):
    print(f"[Debug] Сохранение права администратора: {right_name} -> {is_enabled}")
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute('INSERT OR REPLACE INTO admin_rights_config (right_name, is_enabled) VALUES (?, ?)', (right_name, 1 if is_enabled else 0))
        conn.commit()
        ADMIN_RIGHTS_CONFIG[right_name] = is_enabled
    except Exception as e:
        print(f"[Error] Ошибка сохранения права администратора: {e}")
    finally:
        conn.close()

def get_rp_nick(user_id, chat_id):
    # Эта функция теперь просто достает значение, логика в get_rp_display_name
    if chat_id in RP_NICKS and user_id in RP_NICKS[chat_id]:
        return RP_NICKS[chat_id][user_id]
    # Для глобального вызова передаем 0
    if chat_id != 0:
        return RP_NICKS[0].get(user_id)
    return None

def set_rp_nick(user_id, chat_id, nickname):
    conn = None
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute('INSERT OR REPLACE INTO rp_nicknames (user_id, chat_id, nickname) VALUES (?, ?, ?)', (user_id, chat_id, nickname))
        conn.commit()
        RP_NICKS[chat_id][user_id] = nickname
    finally:
        if conn: conn.close()

def delete_rp_nick(user_id, chat_id):
    conn = None
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute('DELETE FROM rp_nicknames WHERE user_id = ? AND chat_id = ?', (user_id, chat_id))
        conn.commit()
        if chat_id in RP_NICKS and user_id in RP_NICKS[chat_id]:
            del RP_NICKS[chat_id][user_id]
    finally:
        if conn: conn.close()

def load_silent_tags_config():
    global SILENT_TAGS_ENABLED, SILENT_TAGS_CONFIG
    print("[Debug] Загрузка конфигурации Silent Tags")
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute('SELECT value FROM silent_tags_config WHERE param = ?', ('enabled',))
        result = cursor.fetchone()
        SILENT_TAGS_ENABLED = result[0] == 'true' if result else False
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
        cursor.execute('INSERT OR REPLACE INTO silent_tags_config (param, value) VALUES (?, ?)', ('enabled', 'true' if SILENT_TAGS_ENABLED else 'false'))
        for param, value in SILENT_TAGS_CONFIG.items():
            if isinstance(value, list):
                value = json.dumps(value)
            elif isinstance(value, bool):
                value = 'true' if value else 'false'
            cursor.execute('INSERT OR REPLACE INTO silent_tags_config (param, value) VALUES (?, ?)', (param, value))
        conn.commit()
    except Exception as e:
        print(f"[Error] Ошибка сохранения конфигурации Silent Tags: {e}")
        raise
    finally:
        conn.close()

def load_rp_creators():
    global RP_CREATORS
    print("[Debug] Загрузка создателей RP")
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute("SELECT user_id FROM rp_creators")
        RP_CREATORS = {row[0] for row in cursor.fetchall()}
    except Exception as e:
        print(f"[Error] Ошибка загрузки создателей RP: {e}")
    finally:
        conn.close()

def add_rp_creator(user_id):
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute('INSERT OR IGNORE INTO rp_creators (user_id) VALUES (?)', (user_id,))
        conn.commit()
    finally:
        conn.close()

def remove_rp_creator(user_id):
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute('DELETE FROM rp_creators WHERE user_id = ?', (user_id,))
        conn.commit()
    finally:
        conn.close()

def load_rp_config():
    global RP_COMMANDS, RP_ENABLED_CHATS, RP_ACCESS_LIST, RP_PUBLIC_CHATS
    print("[Debug] Загрузка конфигурации RP")
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        # Читаем новую колонку premium_emoji_ids
        cursor.execute("SELECT command, action, premium_emoji_ids, standard_emoji FROM rp_commands")
        for row in cursor.fetchall():
            command, action, prem_ids_json, standard_emoji = row
            # Преобразуем JSON-строку обратно в список, если она есть
            prem_ids = json.loads(prem_ids_json) if prem_ids_json else []
            RP_COMMANDS[command] = {'action': action, 'premium_emoji_ids': prem_ids, 'standard_emoji': standard_emoji}

        cursor.execute("SELECT chat_id FROM rp_enabled_chats")
        RP_ENABLED_CHATS = {row[0] for row in cursor.fetchall()}
        RP_ACCESS_LIST.clear()
        cursor.execute("SELECT chat_id, user_id FROM rp_access_list")
        for row in cursor.fetchall():
            chat_id, user_id = row
            RP_ACCESS_LIST[chat_id].add(user_id)
        cursor.execute("SELECT chat_id FROM rp_public_chats")
        RP_PUBLIC_CHATS = {row[0] for row in cursor.fetchall()}
    except Exception as e:
        print(f"[Error] Ошибка загрузки конфигурации RP: {e}")
    finally:
        conn.close()

def save_rp_command(command, action, prem_ids, std_emoji):
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        # Преобразуем список ID в JSON-строку для сохранения
        prem_ids_json = json.dumps(prem_ids) if prem_ids else None
        cursor.execute('INSERT OR REPLACE INTO rp_commands (command, action, premium_emoji_ids, standard_emoji) VALUES (?, ?, ?, ?)', (command, action, prem_ids_json, std_emoji))
        conn.commit()
    finally:
        conn.close()

def delete_rp_command(command):
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute('DELETE FROM rp_commands WHERE command = ?', (command,))
        conn.commit()
    finally:
        conn.close()

def load_rp_nicks():
    global RP_NICKS
    RP_NICKS = defaultdict(dict)
    conn = None
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute("SELECT user_id, chat_id, nickname FROM rp_nicknames")
        for user_id, chat_id, nickname in cursor.fetchall():
            RP_NICKS[chat_id][user_id] = nickname
    finally:
        if conn: conn.close()

def get_global_nick(user_id, chat_id):
    # Эта функция теперь просто достает значение, логика в get_universal_display_name
    if chat_id in GLOBAL_NICKS and user_id in GLOBAL_NICKS[chat_id]:
        return GLOBAL_NICKS[chat_id][user_id]
    # Для глобального вызова передаем 0
    if chat_id != 0:
        return GLOBAL_NICKS[0].get(user_id)
    return None

def set_global_nick(user_id, chat_id, nickname):
    conn = None
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute('INSERT OR REPLACE INTO global_nicknames (user_id, chat_id, nickname) VALUES (?, ?, ?)', (user_id, chat_id, nickname))
        conn.commit()
        GLOBAL_NICKS[chat_id][user_id] = nickname
    finally:
        if conn: conn.close()

def delete_global_nick(user_id, chat_id):
    conn = None
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute('DELETE FROM global_nicknames WHERE user_id = ? AND chat_id = ?', (user_id, chat_id))
        conn.commit()
        if chat_id in GLOBAL_NICKS and user_id in GLOBAL_NICKS[chat_id]:
            del GLOBAL_NICKS[chat_id][user_id]
    finally:
        if conn: conn.close()

def load_global_nicks():
    global GLOBAL_NICKS
    GLOBAL_NICKS = defaultdict(dict)
    conn = None
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute("SELECT user_id, chat_id, nickname FROM global_nicknames")
        for user_id, chat_id, nickname in cursor.fetchall():
            GLOBAL_NICKS[chat_id][user_id] = nickname
    finally:
        if conn: conn.close()

def save_admin_config(name, rights_dict):
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        rights_json = json.dumps(rights_dict)
        cursor.execute('INSERT OR REPLACE INTO admin_configs (name, rights) VALUES (?, ?)', (name, rights_json))
        conn.commit()
        ADMIN_CONFIGS[name] = rights_dict
    except Exception as e:
        print(f"[Error] Ошибка сохранения конфига админки: {e}")
    finally:
        conn.close()

def load_admin_configs():
    global ADMIN_CONFIGS
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute("SELECT name, rights FROM admin_configs")
        ADMIN_CONFIGS = {name: json.loads(rights_json) for name, rights_json in cursor.fetchall()}
    except Exception as e:
        print(f"[Error] Ошибка загрузки конфигов админки: {e}")
    finally:
        conn.close()

def toggle_rp_chat(chat_id, enabled):
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        if enabled:
            cursor.execute('INSERT OR IGNORE INTO rp_enabled_chats (chat_id) VALUES (?)', (chat_id,))
        else:
            cursor.execute('DELETE FROM rp_enabled_chats WHERE chat_id = ?', (chat_id,))
        conn.commit()
    finally:
        conn.close()

def toggle_rp_access(chat_id, user_id, has_access):
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        if has_access:
            cursor.execute('INSERT OR IGNORE INTO rp_access_list (chat_id, user_id) VALUES (?, ?)', (chat_id, user_id,))
        else:
            cursor.execute('DELETE FROM rp_access_list WHERE chat_id = ? AND user_id = ?', (chat_id, user_id,))
        conn.commit()
    finally:
        conn.close()

def toggle_rp_public_access(chat_id, is_public):
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        if is_public:
            cursor.execute('INSERT OR IGNORE INTO rp_public_chats (chat_id) VALUES (?)', (chat_id,))
        else:
            cursor.execute('DELETE FROM rp_public_chats WHERE chat_id = ?', (chat_id,))
        conn.commit()
    finally:
        conn.close()

def format_last_seen(status):
    if isinstance(status, types.UserStatusOnline):
        return "В сети"
    if isinstance(status, types.UserStatusOffline):
        # Показываем точную дату и время, если они доступны
        return f"был(а) в сети {status.was_online.strftime('%Y-%m-%d %H:%M')}"
    if isinstance(status, types.UserStatusRecently):
        return "Недавно"
    if isinstance(status, types.UserStatusLastWeek):
        return "На этой неделе"
    if isinstance(status, types.UserStatusLastMonth):
        return "В этом месяце"
    if isinstance(status, types.UserStatusEmpty):
        return "Давно"
    return "Неизвестно" # Резервный вариант

def save_tag_config():
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        for param, value in TAG_CONFIG.items():
            cursor.execute('INSERT OR REPLACE INTO tag_config (param, value) VALUES (?, ?)', (param, str(value)))
        conn.commit()
    except Exception as e:
        print(f"[Error] Ошибка сохранения конфига тегов: {e}")
    finally:
        if conn: conn.close()

def load_tag_config():
    global TAG_CONFIG
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute("SELECT param, value FROM tag_config")
        db_config = {row[0]: row[1] for row in cursor.fetchall()}
        TAG_CONFIG['delay'] = int(db_config.get('delay', 3))
        TAG_CONFIG['priority'] = db_config.get('priority', 'id')
        TAG_CONFIG['position'] = db_config.get('position', 'before')
    except Exception as e:
        print(f"[Error] Ошибка загрузки конфига тегов: {e}")
    finally:
        if conn: conn.close()

def load_bot_blocklist():
    global BOT_BLOCKED_USERS
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute("SELECT user_id FROM bot_blocklist")
        BOT_BLOCKED_USERS = {row[0] for row in cursor.fetchall()}
    except Exception as e:
        print(f"[Error] Ошибка загрузки списка заблокированных: {e}")
    finally:
        if conn: conn.close()

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
            log_text = (f"<b>{silent_emoji} Тестовый лог KoteUserBot</b>\n\n<b>Время:</b> {timestamp}\n<b>Сообщение:</b>\n<code>{message}</code>\n")
        elif is_tag_log:
            log_text = message
        else:
            log_text = (f"<b>{silent_emoji} Ошибка в KoteUserBot</b>\n\n<b>Время:</b> {timestamp}\n<b>Обработчик:</b> {handler_name}\n<b>Чат:</b> {chat_info}\n<b>Ошибка:</b>\n<code>{message}</code>\n")
            if chat_id: log_text += f"<b>Chat ID:</b> {chat_id}\n"
        try:
            group_entity = await client.get_entity(group_id)
            print(f"[Debug] Группа найдена: {group_entity.title} (ID: {group_id})")
        except Exception as e:
            print(f"[Log] Не удалось разрешить сущность группы {group_id}: {str(e)}")
            group_entity = group_id
        try:
            await client.send_message(group_entity, log_text, parse_mode='HTML')
            print(f"[Log] {'Тестовый лога' if is_test else 'лога тега' if is_tag_log else 'Ошибка'} отправлен в группу {group_id}")
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
        if not me: raise Exception("Не удалось получить данные аккаунта юзербота")
        group = await client(functions.channels.CreateChannelRequest(title='KoteUserBotSilence', about='Логи упоминаний Silent Tags KoteUserBot', megagroup=True))
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
        try:
            channel = await client.get_entity("@KoteUserBotMedia")
            msg = await client.get_messages(channel, ids=5)
            if not msg or not hasattr(msg, 'media') or not msg.media: raise ValueError("Изображение не найдено в указанном сообщении")
            if not isinstance(msg.media, types.MessageMediaPhoto): raise ValueError("Сообщение не содержит фотографию")
            photo = msg.media.photo
            await client(functions.channels.EditPhotoRequest(channel=group_id, photo=photo))
            print(f"[Debug] Аватарка установлена для группы {group_id}")
        except Exception as e:
            print(f"[Log] Ошибка установки аватарки: {str(e)}")
            await send_log(str(e), "create_silence_log_group")
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
        if not me: raise Exception("Не удалось получить данные аккаунта юзербота")
        group = await client(functions.channels.CreateChannelRequest(title='KoteUserBotDebug', about='Логи ошибок KoteUserBot', megagroup=True))
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
        try:
            channel = await client.get_entity("@KoteUserBotMedia")
            msg = await client.get_messages(channel, ids=3)
            if not msg or not hasattr(msg, 'media') or not msg.media: raise ValueError("Изображение не найдено в указанном сообщении")
            if not isinstance(msg.media, types.MessageMediaPhoto): raise ValueError("Сообщение не содержит фотографию")
            photo = msg.media.photo
            await client(functions.channels.EditPhotoRequest(channel=group_id, photo=photo))
            print(f"[Debug] Аватарка установлена для группы {group_id}")
        except Exception as e:
            print(f"[ErrorLog] Ошибка установки аватарки: {str(e)}")
            await send_error_log(str(e), "create_error_log_group")
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
                me = await client.get_me()
                await client.send_message(me.id, f"<b>Ошибка:</b> Не удалось создать группу для логов\n<code>{error_message}</code>", parse_mode='HTML')
                return
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
            error_text = (f"<b>{silent_emoji} Тестовый лог KoteUserBot</b>\n\n<b>Время:</b> {timestamp}\n<b>Сообщение:</b>\n<code>{error_message}</code>\n")
        else:
            error_text = (f"<b>{silent_emoji} Ошибка в KoteUserBot</b>\n\n<b>Время:</b> {timestamp}\n<b>Обработчик:</b> {handler_name}\n<b>Чат:</b> {chat_info}\n<b>Ошибка:</b>\n<code>{error_message}</code>\n")
            if chat_id: error_text += f"<b>Chat ID:</b> {chat_id}\n"
        try:
            group_entity = await client.get_entity(group_id)
            print(f"[Debug] Группа найдена: {group_entity.title} (ID: {group_id})")
        except Exception as e:
            print(f"[ErrorLog] Не удалось разрешить сущность группы {group_id}: {str(e)}")
            group_entity = group_id
        try:
            await client.send_message(group_entity, error_text, parse_mode='HTML')
            print(f"[ErrorLog] {'Тестовый лог' if is_test else 'Ошибка'} отправлен в группу {group_id}")
        except Exception as e:
            print(f"[ErrorLog] Не удалось отправить в группу {group_id}: {str(e)}")
            me = await client.get_me()
            await client.send_message(me.id, f"<b>Ошибка отправки лога:</b> {str(e)}\n<code>{error_message}</code>", parse_mode='HTML')
            print(f"[ErrorLog] Лог отправлен в избранное")
    except Exception as e:
        error_msg = f"Критическая ошибка при отправке лога: {str(e)}"
        print(f"[ErrorLog] {error_msg}")
        try:
            me = await client.get_me()
            await client.send_message(me.id, error_msg, parse_mode='HTML')
        except Exception as e2:
            print(f"[ErrorLog] Не удалось отправить в избранное: {e2}")

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

class InvalidFormatException(Exception): pass

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

print("[Debug] Инициализация Telegram клиента")
client = TelegramClient(session, api_id, api_hash)
parser = CustomParseMode('markdown')

EMOJI_SET = {
    'premium': {
        'ping': '[⚡️](emoji/5431449001532594346)', 'rocket': '[🚀](emoji/5445284980978621387)', 'help': '[📖](emoji/5373098009640836781)',
        'info': '[ℹ️](emoji/5228686859663585439)', 'name': '[👤](emoji/5373012449597335010)', 'username': '[🪪](emoji/5422683699130933153)',
        'id': '[🆔](emoji/5974526806995242353)', 'premium': '[⭐](emoji/5334523697174683404)', 'leave': '[🥰](emoji/5420557514225770446)',
        'delete': '[🗑️](emoji/5445267414562389170)', 'whitelist': '[📋](emoji/5334882760735598374)', 'tag': '[🏷️]',
        'config': '[⚙️](emoji/5215327492738392838)', 'silent': '[🤫](emoji/5370930189322688800)', 'music': '[🎶](emoji/5188705588925702510)',
        'search': '[🔥](emoji/5420315771991497307)', 'typing': '[⌨️](emoji/5472111548572900003)', 'weather': '[🌦️](emoji/5283097055852503586)',
        'dice': '🎲', 'comment': '[💬](emoji/5465300082628763143)', 'admin': '[🛡️](emoji/5818967120213445821)', 'prefix': '[🆎](emoji/5818740513443942870)',
        'rp_nick': '[📛](emoji/5819016409258135133)', 'success': '[✅](emoji/5980930633298350051)'
    },
    'regular': {
        'ping': '⚡️', 'rocket': '🚀', 'help': '📖', 'info': 'ℹ️', 'name': '👤', 'username': '🪪',
        'id': '🆔', 'premium': '⭐', 'leave': '🥰', 'delete': '🗑️', 'whitelist': '📋', 'tag': '🏷️',
        'config': '⚙️', 'silent': '🤫', 'music': '🎶', 'search': '🔥', 'dice': '🎲', 'typing': '⌨️',
        'weather': '🌦️', 'comment': '💬', 'admin': '🛡️', 'prefix': '🆎', 'rp_nick': '📛', 'success': '✅'
    }
}

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
    emoji = EMOJI_SET[emoji_type].get(key, '❔')
    try:
        if isinstance(emoji, str) and emoji.startswith('[') and 'emoji/' in emoji:
            parsed_text, entities = parser.parse(emoji)
            if not any(isinstance(e, types.MessageEntityCustomEmoji) for e in entities):
                return EMOJI_SET['regular'].get(key, '❔')
        return emoji
    except Exception:
        return EMOJI_SET['regular'].get(key, '❔')

async def is_owner(event):
    return event.sender_id == owner_id

async def get_user_id(identifier):
    print(f"[Debug] Получение ID пользователя: {identifier}")
    try:
        if isinstance(identifier, int) or identifier.isdigit(): return int(identifier)
        reply = await identifier.get_reply_message()
        if reply: return reply.sender_id
        if identifier.message.entities:
            for entity in identifier.message.entities:
                if isinstance(entity, types.MessageEntityMentionName): return entity.user_id
        if identifier.pattern_match.group(1).startswith('@'):
            user = await client.get_entity(identifier.pattern_match.group(1))
            return user.id
    except Exception:
        try:
            return int(identifier.pattern_match.group(1))
        except Exception as e:
            await send_error_log(str(e), "get_user_id")
            return None

async def get_target_user(event):
    identifier = None
    if event.pattern_match:
        for i in range(1, event.pattern_match.re.groups + 1):
            try:
                group_content = event.pattern_match.group(i)
                if group_content and (group_content.startswith('@') or group_content.strip().isdigit()):
                    identifier = group_content.strip()
                    break
            except IndexError:
                continue

    if identifier:
        try:
            clean_id = identifier.strip('@')
            if clean_id.isdigit():
                return await client.get_entity(int(clean_id))
            return await client.get_entity(identifier)
        except (ValueError, TypeError, AttributeError, Exception):
            pass

    reply = await event.get_reply_message()
    if reply:
        return await reply.get_sender()

    if event.is_private:
        return await event.get_chat()

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

async def get_rp_display_name(user_entity, chat_id):
    if not user_entity: return "Неизвестный"
    
    # 1. Проверяем RP-ник для этого чата
    chat_rp_nick = RP_NICKS.get(chat_id, {}).get(user_entity.id)
    if chat_rp_nick:
        # Если ник 'none', используем настоящее имя. Иначе - сам ник.
        return get_display_name(user_entity) if chat_rp_nick.lower() == 'none' else chat_rp_nick

    # 2. Если нет, проверяем глобальный RP-ник
    global_rp_nick = RP_NICKS.get(0, {}).get(user_entity.id)
    if global_rp_nick:
        return global_rp_nick
        
    # 3. Если и его нет, используем универсальный ник (из .nonick)
    universal_display_name = get_universal_display_name(user_entity, chat_id)
    
    # --- ИСПРАВЛЕНИЕ ЗДЕСЬ ---
    # Убираем @username, если он случайно попался
    if universal_display_name.startswith('@') and user_entity.username and universal_display_name[1:] == user_entity.username:
         return get_display_name(user_entity) or f"User {user_entity.id}"

    return universal_display_name

def get_universal_display_name(user_entity, chat_id):
    if not user_entity: return "Неизвестный"
    
    chat_global_nick = GLOBAL_NICKS.get(chat_id, {}).get(user_entity.id)
    if chat_global_nick:
        return get_display_name(user_entity) if chat_global_nick.lower() == 'none' else chat_global_nick
        
    global_nick = get_global_nick(user_entity.id, 0)
    if global_nick:
        return global_nick
        
    display_name = get_display_name(user_entity)
    
    if not display_name or not display_name.strip():
        return f"User {user_entity.id}"
    
    return display_name

def get_tag_display_name(user_entity, chat_id):
    if TAG_CONFIG.get('priority') == 'username' and user_entity.username:
        return f"@{user_entity.username}"
    return get_universal_display_name(user_entity, chat_id)

async def safe_edit_message(event, text, entities):
    print(f"[Debug] Безопасное редактирование сообщения: message_id={event.message.id}, text={text}")
    try:
        if not event.message:
            print("[Debug] Сообщение отсутствует, отправка нового")
            await client.send_message(event.chat_id, text, formatting_entities=entities)
            return
        # Проверяем, содержит ли текст премиум-эмодзи
        parsed_text, parsed_entities = parser.parse(text)
        final_entities = entities if entities else parsed_entities
        await event.message.edit(parsed_text, formatting_entities=final_entities)
    except Exception as e:
        error_msg = str(e)
        print(f"[Debug] Ошибка редактирования сообщения: {error_msg}")
        if "The document file was invalid" in error_msg or "Invalid constructor" in error_msg:
            # Удаляем премиум-эмодзи и пробуем снова
            text_fallback = re.sub(r'\[([^\]]+)\]\(emoji/\d+\)', r'\1', text)
            parsed_text, parsed_entities = parser.parse(text_fallback)
            await event.message.delete()
            await client.send_message(event.chat_id, parsed_text, formatting_entities=parsed_entities)
        else:
            await send_error_log(error_msg, "safe_edit_message", event)
            await client.send_message(event.chat_id, f"Ошибка: {error_msg}")

def error_handler(handler):
    async def wrapper(event):
        global BOT_ENABLED
        if handler.__name__ != "generic_rp_handler":
            print(f"[Debug] Выполнение обработчика: {handler.__name__}")
            if not BOT_ENABLED and handler.__name__ != "on_handler":
                print("[Debug] Бот выключен, команда игнорируется")
                return
            is_owner_user = await is_owner(event)
            has_permission = is_owner_user
            rp_creator_allowed_handlers = ["addrp_handler", "delrp_handler", "rplist_handler"]
            if handler.__name__ in rp_creator_allowed_handlers:
                if event.sender_id in RP_CREATORS: has_permission = True
            if not has_permission:
                print(f"[Debug] У пользователя {event.sender_id} нет доступа к команде {handler.__name__}")
                return
        try:
            await handler(event)
        except Exception as e:
            error_msg = f"{str(e)}\n\nTraceback: {''.join(traceback.format_tb(e.__traceback__))}"
            await send_error_log(error_msg, handler.__name__, event)
            try:
                await client.send_message(event.chat_id, f"**Ошибка:** {str(e)}")
            except:
                pass
    return wrapper

async def update_files_from_git():
    print("[Debug] Запуск обновления файлов из Git")
    try:
        branch = get_git_branch()
        repo_url = "https://github.com/AresUser1/KoteModules.git"
        temp_dir = "temp_git_update"
        if os.path.exists(temp_dir): shutil.rmtree(temp_dir)
        os.makedirs(temp_dir)
        subprocess.run(["git", "clone", "--branch", branch, repo_url, temp_dir], check=True)
        print(f"[Debug] Репозиторий клонирован в {temp_dir}")
        for item in os.listdir(temp_dir):
            item_path = os.path.join(temp_dir, item)
            if os.path.isfile(item_path):
                shutil.copy2(item_path, os.getcwd())
                print(f"[Debug] Скопирован файл: {item}")
        shutil.rmtree(temp_dir)
        print("[Debug] Временная папка удалена")
        return True, "Файлы успешно обновлены!"
    except Exception as e:
        error_msg = f"Ошибка обновления: {str(e)}"
        print(f"[Error] {error_msg}")
        return False, error_msg

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*help(?:\s+(.+))?$', x)))
@error_handler
async def help_handler(event):
    if not await is_owner(event): return
    args = event.pattern_match.group(1).lower().strip() if event.pattern_match and event.pattern_match.group(1) else None
    help_emoji = await get_emoji('help')
    prefix = CONFIG['prefix']
    commands_help = {
        'ping': f"**{prefix}ping**\nПоказывает скорость отклика Telegram и время работы бота.",
        'help': f"**{prefix}help [команда]**\nПоказывает список команд или справку по команде.",
        'helps': f"**{prefix}helps**\nПоказывает белый список для `{prefix}tag` в текущей группе.",
        'info': f"**{prefix}info**\nПоказывает данные аккаунта (ник, username, ID, Premium).",
        'version': f"**{prefix}version**\nПоказывает версию бота, ветку, платформу и проверяет обновления.",
        'сипался': f"**{prefix}сипался**\nПокидает группу с прощальным сообщением.",
        'dele': f"**{prefix}dele <число>**\nУдаляет до 100 сообщений в группе (нужны права).",
        'add': f"**{prefix}add [@username/ID]**\nДобавляет пользователя в белый список для `{prefix}tag` (можно по ответу).",
        'remove': f"**{prefix}remove [@username/ID]**\nУдаляет пользователя из белого списка для `{prefix}tag` (можно по ответу).",
        'tag': f"**{prefix}tag [текст]** или **{prefix}tag [группа] | [шаблон] [-r]**\nТегирует участников чата.\n\n**Классический режим (без `|`):**\n`{prefix}tag текст с форматом`\nТеги будут сверху (или снизу, см. `.tagsettings`), под ними ваш текст.\n\n**Режим с шаблоном (требует `|`):**\n`{prefix}tag [группа] | шаблон [-r]`\n• **группа**: `all`, `admins`, `random N`\n• **шаблон**: Текст, где `{{name}}` или `@Admin` заменяется на тег.\n• **-r**: Добавить случайную реакцию.\n\n**Примеры:**\n`{prefix}tag ✅ Сбор!`\n`{prefix}tag admins | {{name}}, зайди в игру!`\n`{prefix}tag random 5 | 🔥 {{name}} 🔥 -r`",
        'stoptag': f"**{prefix}stoptag**\nОстанавливает выполнение `{prefix}tag`.",
        'tagsettings': f"**{prefix}tagsettings [параметр] [значение]**\nНастраивает команду .tag.\n\n`delay <сек>` - задержка между тегами.\n`priority <id/username>` - приоритет отображения ника.\n`position <before/after>` - позиция тегов.",
        'name': f"**{prefix}name <ник>**\nМеняет имя аккаунта.",
        'autoupdate': f"**{prefix}autoupdate**\nОбновляет файлы бота из Git-репозитория и автоматически перезапускает его.",
        'restart': f"**{prefix}restart**\nПерезапускает бота для применения обновлений или в случае ошибок.",
        'spam': f"**{prefix}spam <число> <текст>**\nОтправляет до 100 сообщений с задержкой 0.5с.",
        'stopspam': f"**{prefix}stopspam**\nОстанавливает выполнение `{prefix}spam`.",
        'stags': f"**{prefix}stags [on/off]**\nВключает или выключает Silent Tags (логи упоминаний).",
        'stconfig': f"**{prefix}stconfig [параметр] [значение]**\nПоказывает или меняет настройки Silent Tags.\nПримеры:\n`{prefix}stconfig` - показать настройки\n`{prefix}stconfig silent true` - включить тихий режим\n`{prefix}stconfig ignore_users add @username` - добавить в игнор",
        'mus': f"**{prefix}mus <запрос>**\nИщет музыку через @lybot и отправляет трек.",
        'on': f"**{prefix}on**\nВключает бота для обработки команд.",
        'off': f"**{prefix}off**\nВыключает бота (кроме `{prefix}on`).",
        'setprefix': f"**{prefix}setprefix <префикс>**\nМеняет префикс команд (до 5 символов).",
        'status': f"**{prefix}status**\nПоказывает статус бота, префикс, Silent Tags и время работы.",
        'profile': f"**{prefix}profile [@username/ID] [groups]**\nПоказывает профиль пользователя (можно по ответу на сообщение).\nОтображает глобальный и RP-ник.\nДобавьте `groups` для отображения общих групп.",
        'backup': f"**{prefix}backup**\nСоздаёт архив (.env, БД, whitelist) и отправляет в избранное.",
        'dice': f"**{prefix}dice**\nБросает анимированный кубик 🎲.",
        'typing': f"**{prefix}typing <время>**\nИмитирует набор текста (s, m, h, до 1 часа).",
        'stoptyping': f"**{prefix}stoptyping**\nОстанавливает имитацию набора текста.",
        'weather': f"**{prefix}weather <город>**\nПоказывает погоду для указанного города.",
        'addrp': f"**{prefix}addrp <команда/алиас>|<действие>|<эмодзи>**\nДобавляет RP-команду. Доступно владельцу и создателям RP.\n**Пример:** `{prefix}addrp обнять/обнял|крепко обнял(а)|🤗`",
        'delrp': f"**{prefix}delrp <команда>**\nУдаляет RP-команду. Доступно владельцу и создателям RP.",
        'rplist': f"**{prefix}rplist**\nПоказывает список всех RP-команд. Доступно владельцу и создателям RP.",
        'rp': f"**{prefix}rp <on/off|access add/remove/list @user/all>**\nУправление RP-командами в чате.",
        'addrpcreator': f"**{prefix}addrpcreator @user**\nДает пользователю право создавать RP-команды.",
        'delrpcreator': f"**{prefix}delrpcreator @user**\nЗабирает у пользователя право создавать RP-команды.",
        'listrpcreators': f"**{prefix}listrpcreators**\nПоказывает список создателей RP-команд.",
        'setrpnick': f"**{prefix}setrpnick [-g] [@user] <ник>**\nУстанавливает RP-ник.\nПо умолчанию для текущего чата. Флаг `-g` устанавливает глобальный ник. Ник `none` отключает ник в чате.",
        'delrpnick': f"**{prefix}delrpnick [-g] [@user]**\nОтключает ник для чата или удаляет глобальный (с флагом `-g`).",
        'rpnick': f"**{prefix}rpnick [@user]**\nПоказывает RP-ники пользователя (для чата и глобальный).",
        'adminhelp': f"**{prefix}adminhelp**\nПоказывает список всех доступных прав для настройки администрирования.",
        'adminsettings': f"**{prefix}adminsettings**\nПоказывает текущие настройки прав для команды `{prefix}admin`.",
        'admins': f"**{prefix}admins <право> <on/off>**\nВключает или выключает право в настройках для команды `{prefix}admin`.",
        'prefix': f"**{prefix}prefix @user <звание>**\nУстанавливает пользователю только звание (префикс) без реальных прав.",
        'admin': f"**{prefix}admin [<конфиг>] @user <звание>**\nНазначает админа. Можно указать имя сохраненного конфига прав.",
        'unprefix': f"**{prefix}unprefix @user**\nСнимает с пользователя только звание (префикс), не трогая права.",
        'unadmin': f"**{prefix}unadmin @user**\nСнимает с пользователя все права администратора и звание.",
        'nonick': f"**{prefix}nonick <add|del|list> [-g] ...**\nУправляет никами.\n`add [-g] <ник>` - добавить\n`del [-g]` - отключить/удалить ник.\n`list [-g]` - список ников.",
        'block': f"**{prefix}block @user**\nБлокирует пользователя (добавляет в ЧС Telegram).",
        'unblock': f"**{prefix}unblock @user**\nРазблокирует пользователя.",
        'blocklist': f"**{prefix}blocklist**\nПоказывает ваш черный список Telegram.",
        'adminsave': f"**{prefix}adminsave <имя>**\nСохраняет текущие настройки прав админа как именованный конфиг.",
        'adminload': f"**{prefix}adminload <имя>**\nЗагружает сохраненный конфиг прав в текущие настройки.",
        'admincfgs': f"**{prefix}admincfgs**\nПоказывает список всех сохраненных конфигов прав.",
    }
    if args:
        args = 'сипался' if args == 'сипался' else args
        text = commands_help.get(args, f"**Ошибка:** Команда `{args}` не найдена! Используйте `{prefix}help`.")
        text = f"{help_emoji} **Справка по команде `{args}`:**\n\n{text}"
    else:
        text = (
            f"**{help_emoji} Команды KoteUserBot:**\n\n"
            "**Основные**\n"
            f"`{prefix}ping`, `{prefix}info`, `{prefix}version`, `{prefix}help`, `{prefix}on`, `{prefix}off`, `{prefix}setprefix`, `{prefix}status`, `{prefix}backup`, `{prefix}autoupdate`, `{prefix}restart`\n\n"
            "**Управление пользователями**\n"
            f"`{prefix}profile`, `{prefix}name`, `{prefix}nonick`, `{prefix}block`, `{prefix}unblock`, `{prefix}blocklist`\n\n"
            "**Управление группой**\n"
            f"`{prefix}tag`, `{prefix}stoptag`, `{prefix}tagsettings`, `{prefix}add`, `{prefix}remove`, `{prefix}helps`, `{prefix}dele`, `{prefix}сипался`, `{prefix}spam`, `{prefix}stopspam`, `{prefix}mus`, `{prefix}dice`, `{prefix}weather`, `{prefix}typing`, `{prefix}stoptyping`\n\n"
            "**Администрирование**\n"
            f"`{prefix}admin`, `{prefix}unadmin`, `{prefix}prefix`, `{prefix}unprefix`, `{prefix}admins`, `{prefix}adminsettings`, `{prefix}adminhelp`\n\n"
            "**Конфиги админ. прав**\n"
            f"`{prefix}adminsave`, `{prefix}adminload`, `{prefix}admincfgs`\n\n"
            "**Silent Tags**\n"
            f"`{prefix}stags`, `{prefix}stconfig`\n\n"
            "**RP-Команды и Ники**\n"
            f"`{prefix}rp`, `{prefix}addrp`, `{prefix}delrp`, `{prefix}rplist`, `{prefix}setrpnick`, `{prefix}delrpnick`, `{prefix}rpnick`, `{prefix}addrpcreator`, `{prefix}delrpcreator`\n\n"
            f"Подробности: `{prefix}help <команда>`"
        )
    parsed_text, entities = parser.parse(text)
    await client.send_message(event.chat_id, parsed_text, formatting_entities=entities, reply_to=event.message.id)
    await event.message.delete()

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*mus\s*(.*)$', x)))
@error_handler
async def mus_handler(event):
    if not await is_owner(event): return
    args = event.pattern_match.group(1).strip() if event.pattern_match else ""
    reply = await event.get_reply_message()
    music_emoji, search_emoji = await get_emoji('music'), await get_emoji('search')
    if not args:
        text = f"**{music_emoji} Не указан запрос!**"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    try:
        text = f"**{search_emoji} Поиск...**"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        music = await client.inline_query("lybot", args)
        if not music: raise Exception("Трек не найден")
        await event.message.delete()
        await client.send_file(event.chat_id, music[0].result.document, reply_to=reply.id if reply else None)
        print(f"[Debug] Трек отправлен: запрос={args}, chat_id={event.chat_id}")
    except Exception as e:
        error_msg = f"Ошибка поиска трека '{args}': {str(e)}"
        await send_error_log(error_msg, "mus_handler", event)
        text = f"**{music_emoji} Трек: `{args}` не найден.**"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*helps$', x)))
@error_handler
async def helps_handler(event):
    if not await is_owner(event): return
    if not event.is_group:
        text = "**Ошибка:** Эта команда работает только в группах!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    chat_id = (await event.get_chat()).id
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

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*add(?:\s+(.+))?$', x)))
@error_handler
async def add_handler(event):
    if not await is_owner(event): return
    if not event.is_group:
        text = "**Ошибка:** Эта команда работает только в группах!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    user = await get_target_user(event)

    if not user:
        text = "**Ошибка:** Не удалось найти пользователя. Укажите @username/ID или ответьте на сообщение."
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    chat_id = event.chat_id
    if user.id in WHITELISTS[chat_id]:
        text = f"**Ошибка:** Пользователь `{get_universal_display_name(user, chat_id)}` уже в белом списке!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    WHITELISTS[chat_id].append(user.id)
    save_whitelists()
    whitelist_emoji = await get_emoji('whitelist')
    text = f"**{whitelist_emoji} Пользователь `{get_universal_display_name(user, chat_id)}` добавлен в белый список!**"
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*remove(?:\s+(.+))?$', x)))
@error_handler
async def remove_handler(event):
    if not await is_owner(event): return
    if not event.is_group:
        text = "**Ошибка:** Эта команда работает только в группах!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
        
    user = await get_target_user(event)

    if not user:
        text = "**Ошибка:** Не удалось найти пользователя. Укажите @username/ID или ответьте на сообщение."
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    chat_id = event.chat_id
    if user.id not in WHITELISTS[chat_id]:
        text = f"**Ошибка:** Пользователя `{get_universal_display_name(user, chat_id)}` нет в белом списке!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
        
    WHITELISTS[chat_id].remove(user.id)
    save_whitelists()
    whitelist_emoji = await get_emoji('whitelist')
    text = f"**{whitelist_emoji} Пользователь `{get_universal_display_name(user, chat_id)}` удалён из белого списка!**"
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*prefix(?:\s+(.*))?$', x)))
@error_handler
async def prefix_handler(event):
    if not event.is_group:
        await event.edit("Эта команда работает только в группах.")
        return
    try:
        args_str = (event.pattern_match.group(1) or "").strip()
        
        user_to_promote = await get_target_user(event)
        if not user_to_promote:
            await event.edit("Не удалось найти пользователя.")
            return

        rank = ""
        reply = await event.get_reply_message()
        # Если это ответ на сообщение, то все аргументы - это звание
        if reply:
            rank = args_str
        else:
            # Если не ответ, то звание - это всё после юзернейма/ID
            parts = args_str.split()
            if parts and (parts[0].startswith('@') or parts[0].isdigit()):
                rank = " ".join(parts[1:])
            else:
                rank = args_str

        if len(rank) > 16:
            await event.edit(f"Звание не может быть длиннее 16 символов. Вы указали {len(rank)}.")
            return

        # Получаем текущие права, чтобы не разжаловать админа, или даем минимальные права для звания
        try:
            participant = await client(GetParticipantRequest(channel=event.chat_id, participant=user_to_promote.id))
            if isinstance(participant.participant, ChannelParticipantAdmin):
                current_rights = participant.participant.admin_rights
            else:
                # Если пользователь не админ, даем ему минимальное право, чтобы он мог "держать" звание
                current_rights = ChatAdminRights(change_info=True)
        except UserNotParticipantError:
            await event.edit("Пользователь не является участником этого чата.")
            return
        except Exception:
             # На случай других ошибок или если пользователь не админ
             current_rights = ChatAdminRights(change_info=True)

        await client(EditAdminRequest(channel=event.chat_id, user_id=user_to_promote.id, admin_rights=current_rights, rank=rank))
        prefix_emoji = await get_emoji('prefix')
        text = f"**{prefix_emoji} Звание для {get_universal_display_name(user_to_promote, event.chat_id)} успешно изменено на «{rank}».**"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
    except (ChatAdminRequiredError, RightForbiddenError, UserAdminInvalidError):
        await event.edit("У меня нет прав на назначение администраторов в этом чате, либо я пытаюсь управлять тем, кто имеет больше прав.")
    except Exception as e:
        await event.edit(f"Произошла ошибка: {str(e)}")
        await send_error_log(str(e), "prefix_handler", event)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*admin(?:\s+(.*))?$', x)))
@error_handler
async def admin_handler(event):
    if not event.is_group:
        await event.edit("Эта команда работает только в группах.")
        return
    try:
        args_str = (event.pattern_match.group(1) or "").strip()
        args = args_str.split()
        
        user_to_promote = None
        rank = ""
        rights_to_set = ADMIN_RIGHTS_CONFIG.copy()
        config_name_used = None

        # Сначала ищем пользователя в реплае
        reply = await event.get_reply_message()
        if reply:
            user_to_promote = await reply.get_sender()

        # Теперь разбираем аргументы
        if args:
            # Проверяем, является ли первый аргумент существующим конфигом
            if args[0] in ADMIN_CONFIGS:
                config_name_used = args[0]
                rights_to_set = ADMIN_CONFIGS[config_name_used].copy()
                args.pop(0) # Убираем имя конфига из дальнейшего разбора

            # Если пользователь ещё не определен (не было реплая), ищем его в оставшихся аргументах
            if not user_to_promote and args:
                try:
                    # Пробуем сделать первый оставшийся аргумент пользователем
                    user_to_promote = await client.get_entity(args[0])
                    args.pop(0) # Если успешно, убираем его
                except Exception:
                    # Если не получилось, значит юзера в аргументах нет,
                    # и все аргументы - это звание. Юзера возьмем из реплая (если он был)
                    pass

            # Все, что осталось в args - это звание
            rank = " ".join(args)

        if not user_to_promote:
            text = "**❌ Ошибка:** Не удалось найти пользователя. Укажите его через @/ID или ответьте на сообщение."
            parsed_text, entities = parser.parse(text)
            await safe_edit_message(event, parsed_text, entities)
            return
        
        if len(rank) > 16:
            await event.edit(f"Звание не может быть длиннее 16 символов. Вы указали {len(rank)}.")
            return

        final_rights = ChatAdminRights(**{k: v for k, v in rights_to_set.items() if isinstance(v, bool)})
        await client(EditAdminRequest(channel=event.chat_id, user_id=user_to_promote.id, admin_rights=final_rights, rank=rank))
        admin_emoji = await get_emoji('admin')
        text = f"**{admin_emoji} {get_universal_display_name(user_to_promote, event.chat_id)} назначен(а) администратором с званием «{rank}».**"
        if config_name_used:
            text += f"\n*(Применён конфиг прав '{config_name_used}')*"
            
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
    except (ChatAdminRequiredError, RightForbiddenError, UserAdminInvalidError):
        await event.edit("У меня нет прав на назначение администраторов, либо я пытаюсь управлять тем, кто имеет больше прав.")
    except Exception as e:
        await event.edit(f"Произошла ошибка: {str(e)}")
        await send_error_log(str(e), "admin_handler", event)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*unprefix(?:\s+(@?\S+))?$', x)))
@error_handler
async def unprefix_handler(event):
    if not event.is_group:
        await event.edit("Эта команда работает только в группах.")
        return
    try:
        user_to_demote = await get_target_user(event)
        if not user_to_demote:
            await event.edit("Не удалось найти пользователя.")
            return
        participant = await client(GetParticipantRequest(channel=event.chat_id, participant=user_to_demote.id))
        if not isinstance(participant.participant, ChannelParticipantAdmin):
            await event.edit("Пользователь не является администратором.")
            return
        current_rights = participant.participant.admin_rights
        await client(EditAdminRequest(channel=event.chat_id, user_id=user_to_demote.id, admin_rights=current_rights, rank=""))
        success_emoji = await get_emoji('success')
        text = f"**{success_emoji} Звание с пользователя {get_universal_display_name(user_to_demote, event.chat_id)} успешно снято. Права сохранены.**"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
    except (ChatAdminRequiredError, RightForbiddenError, UserAdminInvalidError):
        await event.edit("У меня нет прав на управление администраторами, либо я пытаюсь управлять тем, кто имеет больше прав.")
    except UserNotParticipantError:
        await event.edit("Пользователь не является участником этого чата.")
    except Exception as e:
        await event.edit(f"Произошла ошибка: {str(e)}")
        await send_error_log(str(e), "unprefix_handler", event)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*unadmin(?:\s+(@?\S+))?$', x)))
@error_handler
async def unadmin_handler(event):
    if not await is_owner(event): return
    if not event.is_group:
        await event.edit("Эта команда работает только в группах.")
        return
    try:
        user_to_demote = await get_target_user(event)
        if not user_to_demote:
            await event.edit("Не удалось найти пользователя.")
            return
        rights = ChatAdminRights()
        await client(EditAdminRequest(channel=event.chat_id, user_id=user_to_demote.id, admin_rights=rights, rank=""))
        success_emoji = await get_emoji('success')
        text = f"**{success_emoji} Все права и звание с пользователя {get_universal_display_name(user_to_demote, event.chat_id)} успешно сняты.**"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
    except (ChatAdminRequiredError, RightForbiddenError, UserAdminInvalidError):
        await event.edit("У меня нет прав на управление администраторами, либо я пытаюсь управлять тем, кто имеет больше прав.")
    except Exception as e:
        await event.edit(f"Произошла ошибка: {str(e)}")
        await send_error_log(str(e), "unadmin_handler", event)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*adminhelp$', x)))
@error_handler
async def adminhelp_handler(event):
    if not await is_owner(event): return
    admin_emoji = await get_emoji('admin')
    text = f"**{admin_emoji} Справка по правам администратора**\n\nИспользуйте эти ключевые слова с командой `{CONFIG['prefix']}admins`:\n\n"
    for key, desc in ADMIN_RIGHTS_HELP.items():
        text += f"🔹 `{key}` - {desc}\n"
    text += f"\n**Пример:** `{CONFIG['prefix']}admins pin on`"
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*adminsettings$', x)))
@error_handler
async def adminsettings_handler(event):
    if not await is_owner(event): return
    config_emoji = await get_emoji('config')
    text = f"**{config_emoji} Текущие права для команды `{CONFIG['prefix']}admin`:**\n\n"
    for key, telethon_right in ADMIN_RIGHTS_MAP.items():
        status = "✅" if ADMIN_RIGHTS_CONFIG.get(telethon_right) else "❌"
        text += f"{status} `{key}` ({ADMIN_RIGHTS_HELP.get(key, 'N/A')})\n"
    text += f"\nИзменить: `{CONFIG['prefix']}admins <право> <on/off>`"
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*admins\s+(\w+)\s+(on|off)$', x, re.IGNORECASE)))
@error_handler
async def admins_handler(event):
    if not await is_owner(event): return
    right_key = event.pattern_match.group(1).lower()
    new_state_str = event.pattern_match.group(2).lower()
    new_state = new_state_str == 'on'
    if right_key not in ADMIN_RIGHTS_MAP:
        text = f"❌ **Ошибка:** Неверное право `{right_key}`. Используйте `{CONFIG['prefix']}adminhelp` для списка."
    else:
        telethon_right = ADMIN_RIGHTS_MAP[right_key]
        save_admin_right(telethon_right, new_state)
        status = "ВКЛЮЧЕНО" if new_state else "ВЫКЛЮЧЕНО"
        success_emoji = await get_emoji('success')
        text = f"**{success_emoji} Право `{right_key}` теперь {status}** для команды `{CONFIG['prefix']}admin`."
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

SPAM_STATE = {'running': False, 'task': None, 'last_message_id': None}

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*spam\s+(\d+)\s+([\s\S]*)$', x)))
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
        text = f"**Ошибка:** Спам уже выполняется! Используйте `{CONFIG['prefix']}stopspam` для остановки."
        parsed_text, entities = parser.parse(text)
        await event.message.edit(parsed_text, formatting_entities=entities)
        return
    try:
        if not event.pattern_match:
            text = f"**Ошибка:** Неверный формат команды! Пример: `{CONFIG['prefix']}spam 5 текст`"
            parsed_text, entities = parser.parse(text)
            await event.message.edit(parsed_text, formatting_entities=entities)
            return
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
        parsed_text, parsed_entities = parser.parse(message_text)
        input_entities = event.message.entities or []
        adjusted_entities = []
        if input_entities:
            offset = len(f'{CONFIG["prefix"]}spam {count} ')
            for e in input_entities:
                if e.offset >= offset:
                    start = e.offset - offset
                    end = start + e.length
                    if end > len(message_text): e.length = len(message_text) - start if start < len(message_text) else 0
                    entity_dict = e.__dict__.copy()
                    entity_dict['offset'] = start
                    for key in ['_client', '__weakref__']: entity_dict.pop(key, None)
                    adjusted_entities.append(e.__class__(**entity_dict))
        entities, seen = [], set()
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

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*stopspam$', x)))
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

def convert_to_html(text: str, entities: list) -> str:
    if not text: return ""
    if not entities: return html.escape(text)
    
    text_bytes = text.encode('utf-16-le')
    boundaries = []
    for entity in entities:
        start_tag, end_tag = '', ''
        offset, length = entity.offset, entity.length

        try:
            start_index = len(text_bytes[:offset * 2].decode('utf-16-le'))
            entity_text_slice = text_bytes[offset * 2 : (offset + length) * 2].decode('utf-16-le')
            end_index = start_index + len(entity_text_slice)
        except Exception:
            continue
            
        if isinstance(entity, types.MessageEntityBold): start_tag, end_tag = '<b>', '</b>'
        elif isinstance(entity, types.MessageEntityItalic): start_tag, end_tag = '<i>', '</i>'
        elif isinstance(entity, types.MessageEntityUnderline): start_tag, end_tag = '<u>', '</u>'
        elif isinstance(entity, types.MessageEntityStrike): start_tag, end_tag = '<s>', '</s>'
        elif isinstance(entity, types.MessageEntityCode): start_tag, end_tag = '<code>', '</code>'
        elif isinstance(entity, types.MessageEntityPre):
            lang = getattr(entity, 'language', '') or ''
            start_tag = f'<pre><code class="language-{lang}">' if lang else '<pre>'
            end_tag = '</code></pre>' if lang else '</pre>'
        elif isinstance(entity, types.MessageEntityBlockquote): start_tag, end_tag = '<blockquote>', '</blockquote>'
        elif isinstance(entity, types.MessageEntitySpoiler): start_tag, end_tag = '<tg-spoiler>', '</tg-spoiler>'
        elif isinstance(entity, types.MessageEntityTextUrl): start_tag, end_tag = f'<a href="{html.escape(entity.url)}">', '</a>'
        elif isinstance(entity, types.MessageEntityCustomEmoji):
            start_tag = f'<tg-emoji emoji-id="{entity.document_id}">'
            end_tag = '</tg-emoji>'

        if start_tag:
            boundaries.append((start_index, False, start_tag))
            boundaries.append((end_index, True, end_tag))

    # Эта сортировка гарантирует, что теги будут закрываться в правильном порядке
    boundaries.sort(key=lambda b: (b[0], 1 if not b[1] else 0))
    
    result, last_offset = [], 0
    for offset, is_closing, tag in boundaries:
        text_slice = text[last_offset:offset]
        if text_slice:
            result.append(html.escape(text_slice))
        result.append(tag)
        last_offset = offset

    if last_offset < len(text):
        result.append(html.escape(text[last_offset:]))
        
    return "".join(result)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}tag(\s+[\s\S]*)?$', x, re.DOTALL)))
@error_handler
async def tag_handler(event):
    if not await is_owner(event): return
    if not event.is_group:
        await safe_edit_message(event, "**❌ Ошибка:** Команда работает только в группах!", [])
        return
    if TAG_STATE.get('running'):
        await safe_edit_message(event, f"**❌ Ошибка:** Тегирование уже выполняется! Используйте `{CONFIG['prefix']}stoptag`", [])
        return

    load_tag_config()
    await safe_edit_message(event, "🚀 **Начинаю тегирование...**", [])
    
    raw_content_after_command = (event.pattern_match.group(1) or "").strip()
    
    use_template_style = '|' in raw_content_after_command
    group_type = "all"
    text_with_formatting = ""
    template_str = "{name}"
    
    content_words = raw_content_after_command.split()
    add_reaction = "-r" in content_words
    
    if use_template_style:
        parts = raw_content_after_command.split('|', 1)
        command_part = parts[0].strip()
        template_str = parts[1].strip() if len(parts) > 1 else "{name}"
        
        command_part_words = command_part.split()
        if command_part_words:
            if "-r" in command_part_words: command_part_words.remove("-r")
            potential_group = " ".join(command_part_words)
            if potential_group and (potential_group in ["all", "admins"] or potential_group.startswith("random")):
                group_type = potential_group
        text_with_formatting = template_str
    else:
        text_with_formatting = raw_content_after_command

    base_entities = []
    base_text_clean = text_with_formatting.replace("-r", "").strip()
    
    if base_text_clean and event.message.entities:
        text_start_in_raw = event.raw_text.find(base_text_clean)
        if text_start_in_raw != -1:
            offset_to_subtract_utf16 = len(event.raw_text[:text_start_in_raw].encode('utf-16-le')) // 2
            for entity in event.message.entities:
                entity_text_slice = event.raw_text.encode('utf-16-le')[entity.offset*2:(entity.offset + entity.length)*2].decode('utf-16-le')
                if entity.offset >= offset_to_subtract_utf16 and entity_text_slice.strip() != '-r':
                    new_entity_dict = entity.to_dict()
                    new_entity_dict.pop('_', None)
                    new_entity_dict['offset'] = entity.offset - offset_to_subtract_utf16
                    base_entities.append(type(entity)(**new_entity_dict))
    
    chat = await event.get_chat()
    all_participants = []
    async for user in client.iter_participants(chat):
         if not (user.bot or user.id in WHITELISTS.get(chat.id, []) or user.id == owner_id or user.deleted):
            all_participants.append(user)
    
    users_to_tag = []
    if group_type == "all": users_to_tag = all_participants
    elif group_type == "admins":
        users_to_tag = [
            user for user in await client.get_participants(chat, filter=types.ChannelParticipantsAdmins)
            if not (user.bot or user.id in WHITELISTS.get(chat.id, []) or user.id == owner_id or user.deleted)
        ]
    elif group_type.startswith("random"):
        try:
            n = int(group_type.split()[1])
            users_to_tag = random.sample(all_participants, min(n, len(all_participants)))
        except (IndexError, ValueError):
            await client.send_message(event.chat_id, "**❌ Ошибка:** Укажите число для `random N`.")
            return

    if not users_to_tag:
        await client.send_message(event.chat_id, "**❌ Ошибка:** Нет подходящих пользователей для тегирования!")
        try: await event.delete()
        except: pass
        return

    TAG_STATE['running'] = True
    try:
        await event.delete()
        reactions = ['👍', '❤️', '🔥', '🥰', '😁', '🎉', '🤩', '👌', '👏', '✨', '😻', '💯', '😇', '🤗'] if add_reaction else []

        for i in range(0, len(users_to_tag), 5):
            if not TAG_STATE['running']:
                break
            
            group = users_to_tag[i:i+5]

            final_html = ""
            if use_template_style:
                template_clean = template_str.replace("-r", "").strip()
                template_html_base = convert_to_html(template_clean, base_entities)
                full_text_parts = []
                for user in group:
                    display_name = get_tag_display_name(user, event.chat_id)
                    mention = f'<a href="tg://user?id={user.id}">{html.escape(display_name)}</a>'
                    user_html = template_html_base.replace("{name}", mention).replace("@Admin", mention)
                    full_text_parts.append(user_html)
                final_html = "\n\n".join(full_text_parts)
            else:
                # ИСПРАВЛЕНО: base_text_str -> base_text_clean
                base_text_clean_for_html = base_text_clean.replace("-r", "").strip()
                tags_html_parts = []
                for user in group:
                    display_name = get_tag_display_name(user, event.chat_id)
                    safe_name = html.escape(display_name)
                    tags_html_parts.append(f'<a href="tg://user?id={user.id}">{safe_name}</a>')
                tags_html_string = " ".join(tags_html_parts)
                base_text_html = convert_to_html(base_text_clean_for_html, base_entities)
                
                if TAG_CONFIG['position'] == 'before':
                    final_html = tags_html_string
                    if base_text_html:
                        final_html += f"\n\n{base_text_html}"
                else:
                    final_html = base_text_html
                    if tags_html_string:
                        final_html += f"\n\n{tags_html_string}" if base_text_html else tags_html_string

            if final_html:
                try:
                    msg = await client.send_message(event.chat_id, final_html, parse_mode='html', reply_to=event.reply_to_msg_id)
                    if add_reaction and reactions:
                        try:
                            await client(functions.messages.SendReactionRequest(
                                peer=event.chat_id, msg_id=msg.id,
                                reaction=[types.ReactionEmoji(emoticon=random.choice(reactions))]
                            ))
                        except Exception:
                            pass
                
                # ИСПРАВЛЕНО: Добавлена обработка FloodWaitError
                except errors.FloodWaitError as e:
                    print(f"[Warning] Сработал флуд-контроль в tag_handler. Жду {e.seconds} секунд.")
                    await send_error_log(f"Flood Wait: Пауза на {e.seconds} секунд.", "tag_handler", event, is_test=True)
                    await asyncio.sleep(e.seconds + 1)
                    # Повторная отправка сообщения после ожидания
                    msg = await client.send_message(event.chat_id, final_html, parse_mode='html', reply_to=event.reply_to_msg_id)

            if i + 5 < len(users_to_tag) and TAG_STATE['running']:
                await asyncio.sleep(TAG_CONFIG['delay'])

    except Exception as e:
        error_msg = f"Ошибка в tag_handler: {str(e)}\n{traceback.format_exc()}"
        await send_error_log(error_msg, "tag_handler", event)
    finally:
        TAG_STATE['running'] = False

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*tagsettings(?:\s+(.*))?$', x)))
@error_handler
async def tagsettings_handler(event):
    if not await is_owner(event): return

    args_str = (event.pattern_match.group(1) or "").strip()
    
    if not args_str:
        # Показываем текущие настройки
        text = (f"**⚙️ Настройки тегов:**\n\n"
                f" Delay: `{TAG_CONFIG['delay']}` сек.\n"
                f" Priority: `{TAG_CONFIG['priority']}` (`id` или `username`)\n"
                f" Position: `{TAG_CONFIG['position']}` (`before` или `after`)\n\n"
                f"**Пример:** `{CONFIG['prefix']}tagsettings delay 5`")
    else:
        args = args_str.split(" ", 1)
        key = args[0].lower()
        if len(args) < 2:
            text = "❌ **Ошибка:** Укажите значение для параметра."
        else:
            value = args[1].lower()
            if key == 'delay':
                try:
                    delay = int(value)
                    if 0 <= delay <= 60:
                        TAG_CONFIG['delay'] = delay
                        save_tag_config()
                        text = f"✅ **Задержка между тегами установлена на `{delay}` секунд.**"
                    else:
                        text = "❌ **Ошибка:** Задержка должна быть между 0 и 60 секундами."
                except ValueError:
                    text = "❌ **Ошибка:** Задержка должна быть числом."
            elif key == 'priority':
                if value in ['id', 'username']:
                    TAG_CONFIG['priority'] = value
                    save_tag_config()
                    text = f"✅ **Приоритет тега установлен на `{value}`.**"
                else:
                    text = "❌ **Ошибка:** Приоритет может быть `id` или `username`."
            elif key == 'position':
                if value in ['before', 'after']:
                    TAG_CONFIG['position'] = value
                    save_tag_config()
                    text = f"✅ **Позиция тегов установлена на `{value}` текста.**"
                else:
                    text = "❌ **Ошибка:** Позиция может быть `before` или `after`."
            else:
                text = "❌ **Ошибка:** Неизвестный параметр. Доступно: `delay`, `priority`, `position`."

    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*stoptag$', x)))
@error_handler
async def stoptag_handler(event):
    print(f"[Debug] Запуск stoptag_handler для сообщения: {event.raw_text}, message_id={event.message.id}")
    if not await is_owner(event):
        print("[Debug] Пользователь не владелец, пропуск")
        return
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

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*name\s+(.+)$', x)))
@error_handler
async def name_handler(event):
    if not await is_owner(event): return
    new_name = event.pattern_match.group(1).strip() if event.pattern_match else None
    if not new_name:
        text = "**Ошибка:** Укажите новое имя!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    try:
        await client(functions.account.UpdateProfileRequest(first_name=new_name))
        name_emoji = await get_emoji('name')
        text = f"**{name_emoji} Имя изменено на `{new_name}`!**"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
    except Exception as e:
        text = f"**Ошибка:** Не удалось изменить имя: {str(e)}"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*restart$', x)))
@error_handler
async def restart_handler(event):
    if not await is_owner(event): return
    global RESTART_FLAG
    RESTART_FLAG = True
    await safe_edit_message(event, "🔄 **Перезапуск...**", [])
    await client.disconnect()

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*autoupdate$', x)))
@error_handler
async def autoupdate_handler(event):
    if not await is_owner(event): return
    await safe_edit_message(event, "⏳ **Запускаю обновление файлов из Git...**", [])
    success, message = await update_files_from_git()
    text = f"**Обновление:** {message}"
    if success:
        text += "\n\n✅ **Файлы успешно обновлены! Перезапускаю бота для применения изменений...**"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        
        global RESTART_FLAG
        RESTART_FLAG = True
        await client.disconnect()
    else:
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*on$', x)))
@error_handler
async def on_handler(event):
    global BOT_ENABLED
    if not await is_owner(event): return
    if BOT_ENABLED:
        text = "**Бот уже включен!**"
    else:
        BOT_ENABLED = True
        text = "**Бот включен!**"
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*off$', x)))
@error_handler
async def off_handler(event):
    global BOT_ENABLED
    if not await is_owner(event): return
    if not BOT_ENABLED:
        text = "**Бот уже выключен!**"
    else:
        BOT_ENABLED = False
        text = "**Бот выключен!**"
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*ping$', x)))
@error_handler
async def ping_handler(event):
    if not await is_owner(event): return
    start = time.time()
    await client(functions.users.GetUsersRequest(id=[await client.get_me()]))
    telegram_ping = (time.time() - start) * 1000
    uptime = get_uptime()
    ping_emoji, rocket_emoji = await get_emoji('ping'), await get_emoji('rocket')
    text = f"**{ping_emoji} Скорость отклика Telegram:** {telegram_ping:.3f} мс\n**{rocket_emoji} Время работы:** {uptime}\n\n"
    parsed_text, entities = parser.parse(text)
    await client.send_message(event.chat_id, parsed_text, formatting_entities=entities, reply_to=event.message.id)
    await event.message.delete()

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*info$', x)))
@error_handler
async def info_handler(event):
    if not await is_owner(event): return
    user = await client.get_me()
    info_emoji, name_emoji = await get_emoji('info'), await get_emoji('name')
    username_emoji, id_emoji, premium_emoji = await get_emoji('username'), await get_emoji('id'), await get_emoji('premium')
    username = f"@{user.username}" if user.username else "Нет"
    first_name = user.first_name or "Не указано"
    premium_status = "Да" if user.premium else "Нет"
    text = f"**{info_emoji} Информация об аккаунте:**\n\n**{name_emoji} Ник:** {first_name}\n**{username_emoji} Username:** {username}\n**{id_emoji} ID:** {user.id}\n**{premium_emoji} Premium:** {premium_status}\n"
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*сипался$', x)))
@error_handler
async def leave_handler(event):
    if not await is_owner(event): return
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
    chat_id = chat.id
    print(f"[Debug] Попытка покинуть чат: chat_id={chat_id}, type={type(chat)}")
    try:
        if isinstance(chat, types.Channel):
            print(f"[Debug] Чат является супергруппой/каналом, использование LeaveChannelRequest")
            await client(LeaveChannelRequest(chat))
        elif isinstance(chat, types.Chat):
            print(f"[Debug] Чат является обычной группой, использование DeleteChatUserRequest")
            me = await client.get_me()
            await client(functions.messages.DeleteChatUserRequest(chat_id=chat.id, user_id=me.id))
        else:
            error_msg = f"Неизвестный тип чата: {type(chat)}"
            print(f"[Error] {error_msg}")
            await send_error_log(error_msg, "leave_handler", event)
            await client.send_message(event.chat_id, f"**Ошибка:** {error_msg}")
            return
        print(f"[Debug] Успешно покинул чат: chat_id={chat_id}")
    except Exception as e:
        error_msg = f"Не удалось покинуть чат: {str(e)}"
        print(f"[Error] {error_msg}")
        await send_error_log(error_msg, "leave_handler", event)
        await client.send_message(event.chat_id, f"**Ошибка:** {error_msg}")

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*dele\s+(\d+)$', x)))
@error_handler
async def delete_handler(event):
    if not await is_owner(event): return
    if not event.is_group:
        text = "**Ошибка:** Эта команда работает только в группах!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    count = int(event.pattern_match.group(1)) if event.pattern_match else 0
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
        if msg.id != event.message.id: messages.append(msg.id)
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

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*version$', x)))
@error_handler
async def version_handler(event):
    if not await is_owner(event): return
    module_version = "1.0.8"
    uptime, user = get_uptime(), await client.get_me()
    owner_username = f"@{user.username}" if user.username else "Не указан"
    branch, prefix, platform = get_git_branch(), CONFIG['prefix'], detect_platform()
    info_emoji, premium_emoji = await get_emoji('info'), await get_emoji('premium')
    latest_version, update_text = module_version, "\n⚠️ Не удалось проверить обновления"
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
                        update_text = f"\n⚠️ Доступна новая версия: {latest_version}\nОбнови: [GitHub](https://github.com/AresUser1/KoteModules/releases/latest)"
                    else:
                        update_text = ""
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
    text = f"{info_emoji} KoteUserBot\nOwner: {owner_username}\n\nVersion: {module_version}\nBranch: {branch}\nUptime: {uptime}\nPrefix: {prefix}\nPlatform: {platform}\n{update_text}\n\n{premium_emoji} Developed with 💖 by Kote"
    parsed_text, entities = parser.parse(text)
    try:
        channel = await client.get_entity("@KoteUserBotMedia")
        msg = await client.get_messages(channel, ids=2)
        if not msg or not hasattr(msg, 'media') or not msg.media: raise ValueError("Видео не найдено")
        video_file = msg.media.document
        await client.send_file(event.chat_id, file=video_file, caption=parsed_text, formatting_entities=entities, reply_to=event.message.id)
        await event.message.delete()
    except Exception as e:
        print(f"[Debug] Ошибка отправки медиа: {str(e)}")
        await send_error_log(f"Ошибка отправки медиа: {str(e)}", "version_handler", event)
        await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*setprefix\s+(.+)$', x)))
@error_handler
async def setprefix_handler(event):
    if not await is_owner(event): return
    new_prefix = event.pattern_match.group(1).strip() if event.pattern_match else None
    if not new_prefix:
        text = f"**Ошибка:** Укажите новый префикс! Пример: `{CONFIG['prefix']}setprefix !`"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    if len(new_prefix) > 5:
        text = "**Ошибка:** Префикс не должен быть длиннее 5 символов!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    old_prefix = CONFIG['prefix']
    CONFIG['prefix'] = new_prefix
    save_config()
    config_emoji = await get_emoji('config')
    text = f"**{config_emoji} Префикс изменён с `{old_prefix}` на `{new_prefix}`!**"
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*status$', x)))
@error_handler
async def status_handler(event):
    if not await is_owner(event): return
    uptime = get_uptime()
    prefix = CONFIG['prefix']
    silent_status = "Включены" if SILENT_TAGS_ENABLED else "Выключены"
    bot_status = "Включен" if BOT_ENABLED else "Выключен"
    config_emoji, silent_emoji, rocket_emoji = await get_emoji('config'), await get_emoji('silent'), await get_emoji('rocket')
    text = f"**{config_emoji} Статус KoteUserBot:**\n\n**{rocket_emoji} Время работы:** {uptime}\n**Префикс:** `{prefix}`\n**Бот:** {bot_status}\n**{silent_emoji} Silent Tags:** {silent_status}\n"
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*stags\s*(on|off)?$', x)))
@error_handler
async def stags_handler(event):
    if not await is_owner(event): return
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

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*stconfig(?:\s+(.+))?$', x)))
@error_handler
async def stconfig_handler(event):
    if not await is_owner(event): return
    try:
        silent_emoji = await get_emoji('silent')
        args = event.pattern_match.group(1)
        if not args:
            text = f"**{silent_emoji} Настройки Silent Tags:**\n\n**Статус:** {'включены' if SILENT_TAGS_ENABLED else 'выключены'}\n"
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
                text = f"**{silent_emoji} Ошибка:** Неверный формат! Используйте `{CONFIG['prefix']}help stconfig` для справки."
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
                action, identifier = parts[1].lower(), parts[2].strip()
                if param.endswith('_users'):
                    if identifier == 'this':
                        text = f"**{silent_emoji} Ошибка:** Для `{param}` укажите @username или ID, а не `this`!"
                        parsed_text, entities = parser.parse(text)
                        await safe_edit_message(event, parsed_text, entities)
                        return
                    entity_id = await get_user_id(event)
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
                        entity_id = abs((await event.get_chat()).id)
                    else:
                        try:
                            entity_id = abs(int(identifier))
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

@client.on(events.NewMessage(incoming=True))
async def silent_tags_watcher(event):
    global FW_PROTECT
    if not event.mentioned or not SILENT_TAGS_ENABLED: return
    print(f"[SilentTags] Обработка упоминания: chat_id={event.chat_id}, sender_id={event.sender_id}")
    try:
        if owner_id is None:
            print("[SilentTags] owner_id не установлен, пропуск")
            return
        try:
            sender = await event.get_sender()
            if sender is None:
                print(f"[SilentTags] Отправитель не определён, пропуск: chat_id={event.chat_id}")
                return
            sender_id, is_sender_bot = sender.id, getattr(sender, 'bot', False)
        except Exception as e:
            print(f"[SilentTags] Ошибка получения отправителя: {str(e)}")
            return
        if sender_id == owner_id:
            print(f"[SilentTags] Пропуск упоминания от самого бота: sender_id={sender_id}")
            return
        try:
            chat = await event.get_chat()
            chat_title = getattr(chat, 'title', 'Private Chat')
        except Exception as e:
            print(f"[SilentTags] Ошибка получения чата: {str(e)}")
            chat_title = "Неизвестный чат"
        normalized_chat_id = int(str(event.chat_id).replace('-100', '')) if str(event.chat_id).startswith('-100') else abs(event.chat_id)
        print(f"[SilentTags] normalized_chat_id={normalized_chat_id}, chat_title={chat_title}, is_bot={is_sender_bot}")
        print(f"[SilentTags] ignore_chats={SILENT_TAGS_CONFIG['ignore_chats']}, use_chat_whitelist={SILENT_TAGS_CONFIG['use_chat_whitelist']}, ignore_bots={SILENT_TAGS_CONFIG['ignore_bots']}")
        if SILENT_TAGS_CONFIG['ignore_bots'] and is_sender_bot:
            await client.send_read_acknowledge(event.chat_id, clear_mentions=True)
            print(f"[SilentTags] Упоминание от бота помечено как прочитанное и пропущено (ignore_bots=true): chat_id={event.chat_id}, sender_id={sender_id}")
            return
        
        if (sender_id in SILENT_TAGS_CONFIG['ignore_users']) or (SILENT_TAGS_CONFIG['ignore_blocked'] and sender_id in BLOCKED_USERS):
            print(f"[SilentTags] Упоминание пропущено: chat_id={event.chat_id}, normalized_chat_id={normalized_chat_id}, sender_id={sender_id}")
            return
        
        chat_ignored = (normalized_chat_id in SILENT_TAGS_CONFIG['ignore_chats'])
        cid = event.chat_id
        if chat_ignored:
            print(f"[SilentTags] Упоминание полностью игнорируется: chat_id={cid}")
            return
        if cid in FW_PROTECT and len([t for t in FW_PROTECT[cid] if t > time.time()]) > FW_PROTECT_LIMIT:
            print(f"[SilentTags] Антифлуд сработал для chat_id={cid}")
            return
        if not hasattr(globals(), '_ratelimit'): globals()['_ratelimit'] = []
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
                if cid in _ratelimit: _ratelimit.remove(cid)
        try:
            await client.send_read_acknowledge(event.chat_id, clear_mentions=True)
            print(f"[SilentTags] Упоминание помечено как прочитанное: chat_id={cid}")
        except Exception as e:
            print(f"[SilentTags] Ошибка при отметке сообщения как прочитанного: {str(e)}")
        
        group_link = f"t.me/c/{str(normalized_chat_id)}" if not isinstance(chat, types.User) else ""
        user_name = getattr(sender, 'first_name', 'Unknown') or getattr(sender, 'title', 'Unknown')
        silent_emoji = EMOJI_SET['regular']['silent']
        message_text = (f"{silent_emoji} Вас упомянули в <a href=\"{group_link}\">{chat_title}</a> пользователем <a href=\"tg://openmessage?user_id={sender_id}\">{user_name}</a>\n<b>Сообщение:</b>\n<code>{event.raw_text}</code>\n<b>Ссылка:</b> <a href=\"t.me/c/{str(normalized_chat_id)}/{event.id}\">перейти</a>")
        try:
            await send_log(message_text, "silent_tags_watcher", event, is_tag_log=True)
            print(f"[SilentTags] Упоминание отправлено в KoteUserBotSilence: chat_id={cid}, sender_id={sender_id}")
        except Exception as e:
            print(f"[SilentTags] Ошибка отправки лога в KoteUserBotSilence: {str(e)}")
        if cid not in FW_PROTECT: FW_PROTECT[cid] = []
        FW_PROTECT[cid].append(time.time() + 5 * 60)
    except Exception as e:
        print(f"[SilentTags] Ошибка в silent_tags_watcher: {str(e)}")
        try:
            await send_log(str(e), "silent_tags_watcher", event)
        except Exception as e2:
            print(f"[SilentTags] Ошибка при логировании: {str(e2)}")

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*adminsave\s+(\w+)$', x)))
@error_handler
async def adminsave_handler(event):
    if not await is_owner(event): return
    config_name = event.pattern_match.group(1)
    save_admin_config(config_name, ADMIN_RIGHTS_CONFIG)
    text = f"✅ **Текущие настройки прав сохранены как `{config_name}`.**"
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*adminload(?:\s+(\w+))?$', x)))
@error_handler
async def adminload_handler(event):
    global ADMIN_RIGHTS_CONFIG
    if not await is_owner(event): return
    
    config_name = event.pattern_match.group(1)
    if not config_name:
        text = "❌ **Ошибка:** Укажите имя конфига для загрузки."
    elif config_name in ADMIN_CONFIGS:
        ADMIN_RIGHTS_CONFIG = ADMIN_CONFIGS[config_name].copy()
        for right, value in ADMIN_RIGHTS_CONFIG.items():
            save_admin_right(right, value)
        text = f"✅ **Конфиг прав `{config_name}` загружен как текущий.**"
    else:
        text = f"❌ **Ошибка:** Конфиг с именем `{config_name}` не найден."

    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*admincfgs$', x)))
@error_handler
async def admincfgs_handler(event):
    if not await is_owner(event): return
    if not ADMIN_CONFIGS:
        text = "📋 **Нет сохраненных конфигураций прав.**"
    else:
        text = "📋 **Сохраненные конфигурации прав:**\n\n"
        for name in ADMIN_CONFIGS.keys():
            text += f"• `{name}`\n"
    
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*backup$', x)))
@error_handler
async def backup_handler(event):
    if not await is_owner(event): return
    try:
        now = datetime.datetime.now().strftime('%Y-%m-%d_%H-%M')
        archive_name, files_to_backup = f'kote_backup_{now}.zip', ['.env', 'koteuserbot.db', 'whitelists.json']
        backed_up_files = []
        with zipfile.ZipFile(archive_name, 'w', zipfile.ZIP_DEFLATED) as zipf:
            for file in files_to_backup:
                if os.path.exists(file):
                    zipf.write(file)
                    backed_up_files.append(file)
        if not backed_up_files:
            text = "**Ошибка:** Нет файлов для бэкапа!"
            parsed_text, entities = parser.parse(text)
            await safe_edit_message(event, parsed_text, entities)
            await send_error_log("Не найдено файлов для создания бэкапа", "backup_handler", event)
            return
        me = await client.get_me()
        await client.send_file(me.id, archive_name, caption=f'📦 Бэкап KoteUserBot ({now})')
        log_message = f"📦 Создан и отправлен бэкап KoteUserBot\n<b>Время:</b> {now}\n<b>Файлы:</b> {', '.join(backed_up_files)}\n<b>Размер:</b> {os.path.getsize(archive_name) / 1024:.2f} КБ"
        await send_error_log(log_message, "backup_handler", event, is_test=True)
        os.remove(archive_name)
        text = f"**📦 Бэкап ({now}) создан и отправлен в избранное!**"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
    except Exception as e:
        error_msg = f"Ошибка при создании бэкапа: {str(e)}"
        await send_error_log(error_msg, "backup_handler", event)
        text = f"**Ошибка:** Не удалось создать бэкап: {str(e)}"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*profile(?:(?:\s+(@?\S+))?(?:\s+(groups))?)?$', x)))
@error_handler
async def profile_handler(event):
    if not await is_owner(event): return
    
    show_groups = 'groups' in event.raw_text.lower()
    user_entity = await get_target_user(event)

    if not user_entity:
        text = "**Ошибка:** Не удалось найти пользователя. Укажите его через @/ID или ответьте на сообщение."
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    try:
        user_full = await client(GetFullUserRequest(user_entity.id))
        user = user_full.users[0]
        
        name_emoji, username_emoji = await get_emoji('name'), await get_emoji('username')
        id_emoji, premium_emoji, rp_nick_emoji = await get_emoji('id'), await get_emoji('premium'), await get_emoji('rp_nick')
        
        username = f"@{user.username}" if user.username else "Нет"
        premium_status = "Да" if user.premium else "Нет"
        
        # Используем новую функцию для определения статуса
        last_seen = format_last_seen(user.status)
        
        global_nick = get_global_nick(user.id, event.chat_id)
        rp_nick = get_rp_nick(user.id, event.chat_id)
        display_name = get_universal_display_name(user, event.chat_id)

        text = (f"**{name_emoji} Профиль пользователя:**\n\n"
                f"**Имя:** {display_name}\n"
                f"**Username:** {username}\n"
                f"**{id_emoji} ID:** {user.id}\n")

        if global_nick:
            text += f"**Глобальный ник:** `{global_nick}`\n"
        if rp_nick:
            text += f"**{rp_nick_emoji} RP-ник:** `{rp_nick}`\n"

        text += (f"**{premium_emoji} Premium:** {premium_status}\n"
                 f"**Последний раз онлайн:** {last_seen}\n")

        if show_groups:
            common_chats_text = "**Общие группы:** "
            try:
                common = await client(functions.messages.GetCommonChatsRequest(user_id=user.id, max_id=0, limit=100))
                common_chats = [chat.title for chat in common.chats if isinstance(chat, (types.Chat, types.Channel))]
                if common_chats:
                    common_chats_text += ", ".join(common_chats)
                else:
                    common_chats_text += "Нет"
            except Exception as e:
                common_chats_text += "Ошибка при получении"
            text += f"{common_chats_text}\n"

        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
    except Exception as e:
        await send_error_log(str(e), "profile_handler", event)
        text = f"**Ошибка:** Не удалось получить профиль: {str(e)}"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*dice$', x)))
@error_handler
async def dice_handler(event):
    if not await is_owner(event): return
    dice_emoji = await get_emoji('dice')
    await client.send_message(event.chat_id, f"{dice_emoji} Бросаем кубик!", file=types.InputMediaDice('🎲'))
    await event.message.delete()

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*typing\s+([\d]+[smhdy])$', x)))
@error_handler
async def typing_handler(event):
    if not await is_owner(event): return
    if TYPING_STATE['running']:
        text = f"**Ошибка:** Имитация набора уже активна! Используйте `{CONFIG['prefix']}stoptyping`."
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    time_str = event.pattern_match.group(1).strip()
    try:
        time_value, unit = int(time_str[:-1]), time_str[-1].lower()
        time_units = {'s': 1, 'm': 60, 'h': 3600, 'd': 86400, 'y': 31536000}
        if unit not in time_units:
            text = "**Ошибка:** Неверная единица времени! Используйте s, m, h, d, y."
            parsed_text, entities = parser.parse(text)
            await safe_edit_message(event, parsed_text, entities)
            return
        duration = time_value * time_units[unit]
        if duration > 3600:
            text = "**Ошибка:** Максимальная длительность — 1 час!"
            parsed_text, entities = parser.parse(text)
            await safe_edit_message(event, parsed_text, entities)
            return
    except ValueError:
        text = f"**Ошибка:** Укажите число и единицу времени! Пример: `{CONFIG['prefix']}typing 10s`"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    TYPING_STATE['running'], TYPING_STATE['chat_id'] = True, event.chat_id
    async def typing_task():
        try:
            end_time = time.time() + duration
            while time.time() < end_time and TYPING_STATE['running']:
                await client(functions.messages.SetTypingRequest(peer=event.chat_id, action=types.SendMessageTypingAction()))
                await asyncio.sleep(5)
        except Exception as e:
            await send_error_log(str(e), "typing_task", event)
        finally:
            TYPING_STATE['running'], TYPING_STATE['task'], TYPING_STATE['chat_id'] = False, None, None
    TYPING_STATE['task'] = client.loop.create_task(typing_task())
    typing_emoji = await get_emoji('typing')
    text = f"**{typing_emoji} Имитация набора начата на {time_str}!**"
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*stoptyping$', x)))
@error_handler
async def stoptyping_handler(event):
    if not await is_owner(event): return
    if not TYPING_STATE['running']:
        text = "**Ошибка:** Имитация набора не активна!"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    TYPING_STATE['running'] = False
    if TYPING_STATE['task']:
        TYPING_STATE['task'].cancel()
        TYPING_STATE['task'] = None
    typing_emoji = await get_emoji('typing')
    text = f"**{typing_emoji} Имитация набора остановлена!**"
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*weather\s+(.+)$', x)))
@error_handler
async def weather_handler(event):
    if not await is_owner(event): return
    city = event.pattern_match.group(1).strip().replace(' ', '+')
    if not city:
        text = f"**Ошибка:** Укажите город! Пример: `{CONFIG['prefix']}weather Москва`"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    try:
        weather_emoji = await get_emoji('weather')
        text = f"**{weather_emoji} Запрашиваем погоду для {city.replace('+', ' ')}...**"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        weather_conditions = {
            'clear': 'Ясно', 'sunny': 'Солнечно', 'partly cloudy': 'Переменная облачность', 'cloudy': 'Облачно',
            'overcast': 'Пасмурно', 'mist': 'Туман', 'fog': 'Густой туман', 'light rain': 'Лёгкий дождь',
            'rain': 'Дождь', 'heavy rain': 'Сильный дождь', 'showers': 'Ливни', 'light snow': 'Лёгкий снег',
            'snow': 'Снег', 'heavy snow': 'Сильный снег', 'thunderstorm': 'Гроза'
        }
        async with aiohttp.ClientSession() as session:
            url = f"http://wttr.in/{city}?lang=ru&format=%l:+%c+%t+%w+%h+%p"
            async with session.get(url) as response:
                if response.status != 200: raise Exception(f"Ошибка wttr.in: Статус {response.status}")
                weather_data = await response.text()
                if "Unknown location" in weather_data: raise Exception("Город не найден")
                parts = weather_data.strip().split(': ', 1)
                if len(parts) < 2: raise Exception("Некорректный ответ от wttr.in")
                location, data = parts[0], parts[1].split()
                if len(data) < 5: raise Exception("Некорректный ответ от wttr.in")
                temp_index = next(i for i, x in enumerate(data) if x.startswith('+') or x.startswith('-'))
                condition = ' '.join(data[:temp_index]).lower()
                temp, wind, humidity, precip = data[temp_index], data[temp_index + 1], data[temp_index + 2], data[temp_index + 3]
                condition_ru = weather_conditions.get(condition, condition.capitalize())
                text = f"{weather_emoji} Погода в {location}:\n\n**Состояние:** {condition_ru}\n**Температура:** {temp}\n**Ветер:** {wind}\n**Влажность:** {humidity}\n**Осадки:** {precip}\n"
                parsed_text, entities = parser.parse(text)
                await safe_edit_message(event, parsed_text, entities)
    except Exception as e:
        error_msg = f"Ошибка получения погоды для '{city}': {str(e)}"
        await send_error_log(error_msg, "weather_handler", event)
        text = f"**{weather_emoji} Ошибка:** Не удалось получить погоду: {str(e)}"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*addrp\s+([^|]+)\s*\|\s*([^|]+)\s*\|\s*([\s\S]+)$', x, re.DOTALL)))
@error_handler
async def addrp_handler(event):
    has_permission = await is_owner(event) or event.sender_id in RP_CREATORS
    if not has_permission:
        return

    match = event.pattern_match
    aliases_str, action, emoji_text = match.group(1).strip(), match.group(2).strip(), match.group(3).strip()
    aliases = [alias.lower() for alias in aliases_str.split('/') if alias]

    if not aliases:
        text = f"❌ **Ошибка:** Вы не указали ни одной команды/алиаса.\nИспользуйте формат: `{CONFIG['prefix']}addrp команда|действие|эмодзи`"
        parsed_text, entities = parser.parse(text)
        await client.send_message(event.chat_id, parsed_text, formatting_entities=entities, reply_to=event.message.id)
        return

    premium_emoji_ids = []
    standard_emojis_parts = []

    if event.message.entities:
        from telethon.tl.types import MessageEntityCustomEmoji
        for entity in event.message.entities:
            if isinstance(entity, MessageEntityCustomEmoji):
                premium_emoji_ids.append(entity.document_id)
                emoji_char_bytes = event.raw_text.encode('utf-16-le')
                entity_bytes = emoji_char_bytes[entity.offset*2:(entity.offset + entity.length)*2]
                standard_emojis_parts.append(entity_bytes.decode('utf-16-le'))

    standard_emoji = "".join(standard_emojis_parts) if standard_emojis_parts else emoji_text

    for alias in aliases:
        RP_COMMANDS[alias] = {'action': action, 'premium_emoji_ids': premium_emoji_ids, 'standard_emoji': standard_emoji}
        save_rp_command(alias, action, premium_emoji_ids, standard_emoji)

    success_emoji = await get_emoji('success')
    text = f"**{success_emoji} RP-команда(ы) `{', '.join(aliases)}` добавлена(ы)!**"
    if premium_emoji_ids:
        text += f"\n*Сохранено {len(premium_emoji_ids)} премиум-эмодзи. Обычная версия: {standard_emoji}*"

    parsed_text, entities = parser.parse(text)
    if event.out:
        await safe_edit_message(event, parsed_text, entities)
    else:
        await client.send_message(event.chat_id, parsed_text, formatting_entities=entities, reply_to=event.id)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*delrp\s+(.+)$', x)))
@error_handler
async def delrp_handler(event):
    has_permission = await is_owner(event) or event.sender_id in RP_CREATORS
    if not has_permission:
        return
        
    command = event.pattern_match.group(1).lower().strip()
    if command in RP_COMMANDS:
        del RP_COMMANDS[command]
        delete_rp_command(command)
        text = f"🗑️ **RP-команда `{command}` удалена!**"
    else:
        text = f"❌ **Ошибка:** Команда `{command}` не найдена."
    
    parsed_text, entities = parser.parse(text)
    if event.out:
        await safe_edit_message(event, parsed_text, entities)
    else:
        await client.send_message(event.chat_id, parsed_text, formatting_entities=entities, reply_to=event.id)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*rplist$', x)))
@error_handler
async def rplist_handler(event):
    has_permission = await is_owner(event) or event.sender_id in RP_CREATORS
    if not has_permission:
        return

    if not RP_COMMANDS:
        text = "📋 **Список RP-команд пуст!**"
        parsed_text, entities = parser.parse(text)
        if event.out:
            await safe_edit_message(event, parsed_text, entities)
        else:
            await client.send_message(event.chat_id, parsed_text, formatting_entities=entities, reply_to=event.id)
        return

    text = "📋 **Доступные RP-команды:**\n"
    actions = defaultdict(lambda: {'aliases': [], 'emoji_data': {}})

    for cmd, data in RP_COMMANDS.items():
        prem_ids_tuple = tuple(sorted(data.get('premium_emoji_ids', [])))
        key = (data['action'], prem_ids_tuple, data['standard_emoji'])
        actions[key]['aliases'].append(cmd)
        actions[key]['emoji_data'] = {'premium_emoji_ids': data.get('premium_emoji_ids', []), 'standard_emoji': data['standard_emoji']}

    is_premium = await is_premium_user()
    for key, val in actions.items():
        action, prem_ids_tuple, std_emoji = key

        # --- ИСПРАВЛЕНИЕ ЗДЕСЬ ---
        # Теперь собираем все эмодзи, а не один
        final_emoji = ""
        if is_premium and prem_ids_tuple:
            placeholders = std_emoji or '✨' * len(prem_ids_tuple)
            emoji_links = []
            for i, emoji_id in enumerate(prem_ids_tuple):
                placeholder_char = placeholders[i] if i < len(placeholders) else '✨'
                emoji_links.append(f"[{placeholder_char}](emoji/{emoji_id})")
            final_emoji = "".join(emoji_links)
        else:
            final_emoji = std_emoji

        aliases_str = ', '.join(f"`{a}`" for a in sorted(val['aliases']))
        text += f"• {aliases_str} - {action} {final_emoji}\n"

    parsed_text, entities = parser.parse(text)
    if event.out:
        await safe_edit_message(event, parsed_text, entities)
    else:
        await client.send_message(event.chat_id, parsed_text, formatting_entities=entities, reply_to=event.id)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*rp\s+access\s+(add|remove)(?:\s+(.+))?$', x, re.IGNORECASE)))
@error_handler
async def rp_access_handler(event):
    if not await is_owner(event): return
    if not event.is_group and not event.is_channel:
        text = "❌ **Ошибка:** Эту команду можно использовать только в чатах."
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
        
    action = event.pattern_match.group(1).lower()
    identifier = (event.pattern_match.group(2) or "").lower().strip()
    chat_id = event.chat_id
    success_emoji = await get_emoji('success')
    
    if identifier == 'all':
        if action == 'add':
            RP_PUBLIC_CHATS.add(chat_id)
            toggle_rp_public_access(chat_id, True)
            text = f"**{success_emoji} Доступ к RP-командам в этом чате открыт для ВСЕХ!**"
        else: # remove
            RP_PUBLIC_CHATS.discard(chat_id)
            toggle_rp_public_access(chat_id, False)
            text = f"🗑️ **Публичный доступ к RP-командам в этом чате ЗАКРЫТ!**"
            
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    user = await get_target_user(event)
    
    if not user:
        text = f"❌ **Ошибка:** Не удалось найти пользователя"
        if identifier:
            text += f" `{identifier}`."
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    
    user_id = user.id
    display_name = get_universal_display_name(user, event.chat_id)
    
    if action == 'add':
        RP_ACCESS_LIST[chat_id].add(user_id)
        toggle_rp_access(chat_id, user_id, True)
        text = f"**{success_emoji} Пользователь `{display_name}` получил доступ к RP-командам в этом чате.**"
    else: # remove
        RP_ACCESS_LIST[chat_id].discard(user_id)
        toggle_rp_access(chat_id, user_id, False)
        text = f"🗑️ **Пользователь `{display_name}` лишен доступа к RP-командам в этом чате.**"
        
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*rp\s+access\s+list$', x, re.IGNORECASE)))
@error_handler
async def rp_access_list_handler(event):
    if not await is_owner(event): return
    if not event.is_group and not event.is_channel:
        text = "❌ **Ошибка:** Эту команду можно использовать только в чатах."
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    chat_id = event.chat_id
    if chat_id in RP_PUBLIC_CHATS:
        text = "✅ **Доступ к RP-командам в этом чате открыт для всех.**"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    access_list = RP_ACCESS_LIST.get(chat_id)
    text = "🔒 **Список пользователей с доступом к RP в этом чате:**\n\n"
    if not access_list:
        text += "Никому не выдан индивидуальный доступ."
    else:
        user_lines = []
        for user_id in access_list:
            try:
                user = await client.get_entity(user_id)
                username = f"@{user.username}" if user.username else f"ID: {user_id}"
                display_name = get_universal_display_name(user, event.chat_id)
                user_lines.append(f"• {display_name} ({username})")
            except Exception:
                user_lines.append(f"• Не удалось найти пользователя (ID: {user_id})")
        text += "\n".join(user_lines)
    
    text += "\n\n**Владелец бота всегда имеет доступ.**"
    
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*addrpcreator\s+(.+)$', x)))
@error_handler
async def addrpcreator_handler(event):
    if not await is_owner(event): return
    identifier = event.pattern_match.group(1).strip()
    user = await get_target_user(event)
    if not user:
        text = f"❌ **Ошибка:** Не удалось найти пользователя `{identifier}`."
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    if user.id in RP_CREATORS:
        text = f"❌ **Ошибка:** Пользователь `{identifier}` уже является создателем RP."
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    RP_CREATORS.add(user.id)
    add_rp_creator(user.id)
    success_emoji = await get_emoji('success')
    text = f"**{success_emoji} Пользователь `{identifier}` теперь может создавать RP-команды!**"
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*delrpcreator\s+(.+)$', x)))
@error_handler
async def delrpcreator_handler(event):
    if not await is_owner(event): return
    identifier = event.pattern_match.group(1).strip()
    user = await get_target_user(event)
    if not user:
        text = f"❌ **Ошибка:** Не удалось найти пользователя `{identifier}`."
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    if user.id not in RP_CREATORS:
        text = f"❌ **Ошибка:** Пользователь `{identifier}` не является создателем RP."
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    RP_CREATORS.remove(user.id)
    remove_rp_creator(user.id)
    text = f"🗑️ **Пользователь `{identifier}` больше не может создавать RP-команды.**"
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*rp\s+(on|off)$', x, re.IGNORECASE)))
@error_handler
async def rp_toggle_handler(event):
    if not await is_owner(event): return
    action = event.pattern_match.group(1).lower()
    chat_id = event.chat_id
    success_emoji = await get_emoji('success')

    if action == 'on':
        if chat_id in RP_ENABLED_CHATS:
            text = "✅ **RP-команды уже включены в этом чате.**"
        else:
            RP_ENABLED_CHATS.add(chat_id)
            toggle_rp_chat(chat_id, True)
            text = f"**{success_emoji} RP-команды теперь ВКЛЮЧЕНЫ в этом чате.**"
    else:
        if chat_id not in RP_ENABLED_CHATS:
            text = "ℹ️ **RP-команды и так были выключены в этом чате.**"
        else:
            RP_ENABLED_CHATS.discard(chat_id)
            toggle_rp_chat(chat_id, False)
            text = f"🗑️ **RP-команды теперь ВЫКЛЮЧЕНЫ в этом чате.**"
    
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*listrpcreators$', x)))
@error_handler
async def listrpcreators_handler(event):
    if not await is_owner(event): return
    text = "👑 **Список создателей RP-команд:**\n\n"
    if not RP_CREATORS:
        text += "Список пуст. Только владелец может создавать команды."
    else:
        user_lines = []
        for user_id in RP_CREATORS:
            try:
                user = await client.get_entity(user_id)
                username = f"@{user.username}" if user.username else f"ID: {user_id}"
                user_lines.append(f"• {get_universal_display_name(user)} ({username})")
            except Exception:
                user_lines.append(f"• Не удалось найти пользователя (ID: {user_id})")
        text += "\n".join(user_lines)
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*setrpnick((?:\s+-g)?)\s+([\s\S]+)$', x)))
@error_handler
async def setrpnick_handler(event):
    if not await is_owner(event): return

    is_global = bool(event.pattern_match.group(1))
    text_args = event.pattern_match.group(2).strip()
    
    user = await get_target_user(event)
    parts = text_args.split()
    nickname = text_args
    
    if not user:
        try:
            potential_user = await client.get_entity(parts[0])
            if isinstance(potential_user, types.User):
                user = potential_user
                nickname = " ".join(parts[1:])
        except (ValueError, TypeError, AttributeError): pass

    if not user:
        await safe_edit_message(event, "**❌ Ошибка:** Не удалось найти пользователя.", [])
        return
    if not nickname:
        await safe_edit_message(event, "**❌ Ошибка:** Вы не указали никнейм.", [])
        return

    chat_id = 0 if is_global else event.chat_id
    set_rp_nick(user.id, chat_id, nickname)
    
    nick_type = "Глобальный RP-ник" if is_global else "RP-ник для этого чата"
    text = f"✅ **{nick_type} для `{get_universal_display_name(user, event.chat_id)}` установлен на:** `{nickname}`."
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*delrpnick((?:\s+-g)?)(?:\s+(@?\S+))?$', x)))
@error_handler
async def delrpnick_handler(event):
    if not await is_owner(event): return
    
    is_global = bool(event.pattern_match.group(1))
    user = await get_target_user(event)
    
    if not user:
        await safe_edit_message(event, "**❌ Ошибка:** Не удалось найти пользователя.", [])
        return
        
    display_name = get_universal_display_name(user, event.chat_id).replace('[','').replace(']','')

    if is_global:
        # Логика для удаления глобального ника
        if get_rp_nick(user.id, 0):
            delete_rp_nick(user.id, 0)
            text = f"🗑️ **Глобальный RP-ник для `{display_name}` удалён.**"
        else:
            text = f"ℹ️ У пользователя `{display_name}` не установлен глобальный RP-ник."
    else:
        # Новая логика: отключаем ник для текущего чата, устанавливая "none"
        set_rp_nick(user.id, event.chat_id, 'none')
        text = f"✅ **Отображение RP-ника для `{display_name}` в этом чате отключено.** Теперь будет использоваться его настоящее имя."

    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)
    
@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*rpnick(?:\s+(@?\S+))?$', x)))
@error_handler
async def rpnick_handler(event):
    if not await is_owner(event): return

    user = await get_target_user(event)
    if not user:
        await safe_edit_message(event, "**❌ Ошибка:** Не удалось найти пользователя.", [])
        return

    chat_nick = get_rp_nick(user.id, event.chat_id)
    global_nick = get_rp_nick(user.id, 0)
    rp_nick_emoji = await get_emoji('rp_nick')
    
    # Очищаем имя перед выводом
    display_name = get_universal_display_name(user, event.chat_id).replace('[','').replace(']','')
    
    text = f"**{rp_nick_emoji} RP-ники для `{display_name}`:**\n"
    text += f"• **В этом чате:** `{chat_nick}`\n" if chat_nick else "• **В этом чате:** не установлен\n"
    text += f"• **Глобальный:** `{global_nick}`" if global_nick else "• **Глобальный:** не установлен"
    
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage)
@error_handler
async def generic_rp_handler(event):
    if not event.raw_text: return

    is_prefixed = event.raw_text.startswith(CONFIG['prefix'])
    command_text = event.raw_text[len(CONFIG['prefix']):] if is_prefixed else event.raw_text
    
    parts = command_text.split()
    if not parts: return

    command = parts[0].lower()

    if command not in RP_COMMANDS: return
    if event.chat_id not in RP_ENABLED_CHATS: return
    
    if event.is_group and not await is_owner(event):
        is_public_chat = event.chat_id in RP_PUBLIC_CHATS
        has_personal_access = event.sender_id in RP_ACCESS_LIST.get(event.chat_id, set())
        if not (is_public_chat or has_personal_access): return

    rp_data = RP_COMMANDS[command]
    sender = await event.get_sender()
    sender_display_name = await get_rp_display_name(sender, event.chat_id)

    # --- ИСПРАВЛЕНИЕ ЗДЕСЬ ---
    # Возвращаем простую и надёжную очистку имени от символов, ломающих ссылку.
    clean_sender_name = sender_display_name.replace('[', '').replace(']', '')
    sender_link = f"[{clean_sender_name}](tg://user?id={sender.id})"

    target_user = None
    raw_args_text = ""
    
    command_and_args = command_text.split(maxsplit=1)
    reply = await event.get_reply_message()
    if reply:
        target_user = await reply.get_sender()
        if len(command_and_args) > 1: raw_args_text = command_and_args[1]
    elif len(command_and_args) > 1:
        args_part = command_and_args[1]
        args_parts = args_part.split(maxsplit=1)
        try:
            potential_target = await client.get_entity(args_parts[0])
            target_user = potential_target
            if len(args_parts) > 1: raw_args_text = args_parts[1]
        except Exception:
            target_user, raw_args_text = None, args_part
    
    if not target_user and event.is_private: target_user = await event.get_chat()

    target_link = ""
    if target_user:
        target_display_name = await get_rp_display_name(target_user, event.chat_id)
        # Возвращаем простую и надёжную очистку.
        clean_target_name = target_display_name.replace('[', '').replace(']', '')
        target_link = f" [{clean_target_name}](tg://user?id={target_user.id})"

    me = await client.get_me()
    is_premium = me.premium
    
    base_emoji_md = ""
    prem_ids = rp_data.get('premium_emoji_ids', [])
    if is_premium and prem_ids:
        placeholders = rp_data.get('standard_emoji', '')
        if placeholders and len(placeholders) == len(prem_ids):
            emoji_links = []
            for i, doc_id in enumerate(prem_ids):
                emoji_links.append(f"[{placeholders[i]}](emoji/{doc_id})")
            base_emoji_md = "".join(emoji_links)
        elif prem_ids:
            placeholder = placeholders[0] if placeholders else '✨'
            base_emoji_md = f"[{placeholder}](emoji/{random.choice(prem_ids)})"
    else:
        base_emoji_md = rp_data.get('standard_emoji', '')

    rp_action = rp_data['action']
    
    if not target_link:
        final_md_text = f"{base_emoji_md} | {sender_link} **{rp_action}** самого/саму себя"
    else:
        final_md_text = f"{base_emoji_md} | {sender_link} **{rp_action}**{target_link}"

    final_text, final_entities = parser.parse(final_md_text)
    final_entities = final_entities or []
    
    if raw_args_text:
        comment_emoji_md = await get_emoji('comment')
        parsed_emoji_text, emoji_entities = parser.parse(comment_emoji_md)
        comment_prefix = f"\n{parsed_emoji_text} "
        
        len_before_prefix_utf16 = len(final_text.encode('utf-16-le')) // 2
        if emoji_entities:
            entity = emoji_entities[0]
            entity.offset += len_before_prefix_utf16 + 1
            final_entities.append(entity)
        
        len_before_comment_utf16 = len_before_prefix_utf16 + len(comment_prefix.encode('utf-16-le')) // 2
        if event.message.entities:
            try:
                original_comment_offset_utf16 = (event.raw_text.encode('utf-16-le')).find(raw_args_text.encode('utf-16-le')) // 2
                original_comment_len_utf16 = len(raw_args_text.encode('utf-16-le')) // 2
                offset_difference = len_before_comment_utf16 - original_comment_offset_utf16
                
                for entity in event.message.entities:
                    if entity.offset >= original_comment_offset_utf16 and \
                       (entity.offset + entity.length) <= (original_comment_offset_utf16 + original_comment_len_utf16):
                        new_entity_dict = entity.to_dict()
                        new_entity_dict.pop('_', None)
                        new_entity_dict['offset'] = entity.offset + offset_difference
                        new_entity = type(entity)(**new_entity_dict)
                        final_entities.append(new_entity)
            except Exception as e:
                await send_error_log(f"Ошибка обработки сущностей комментария: {e}", "generic_rp_handler", event)

        final_text += comment_prefix + raw_args_text

    if event.out:
        await safe_edit_message(event, final_text, final_entities)
    else:
        await client.send_message(event.chat_id, final_text, formatting_entities=final_entities, reply_to=event.id)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*nonick\s+add((?:\s+-g)?)\s+([\s\S]+)$', x)))
@error_handler
async def nonick_add_handler(event):
    if not await is_owner(event): return

    is_global = bool(event.pattern_match.group(1))
    text_args = event.pattern_match.group(2).strip()
    
    user = await get_target_user(event)
    parts = text_args.split()
    nickname = text_args
    
    if not user:
        try:
            potential_user = await client.get_entity(parts[0])
            if isinstance(potential_user, types.User):
                user = potential_user
                nickname = " ".join(parts[1:])
        except: pass

    if not user:
        await safe_edit_message(event, "**❌ Ошибка:** Не удалось найти пользователя.", [])
        return
    if not nickname:
        await safe_edit_message(event, "**❌ Ошибка:** Вы не указали никнейм.", [])
        return
    
    chat_id = 0 if is_global else event.chat_id
    set_global_nick(user.id, chat_id, nickname)
    
    nick_type = "Глобальный ник" if is_global else "Ник для этого чата"
    text = f"✅ **{nick_type} для `{get_display_name(user)}` установлен на:** `{nickname}`."
    
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*nonick\s+del((?:\s+-g)?)(?:\s+(@?\S+))?$', x)))
@error_handler
async def nonick_del_handler(event):
    if not await is_owner(event): return
    
    is_global = bool(event.pattern_match.group(1))
    user = await get_target_user(event)
    
    if not user:
        await safe_edit_message(event, "**❌ Ошибка:** Не удалось найти пользователя.", [])
        return
        
    display_name = get_universal_display_name(user, event.chat_id).replace('[','').replace(']','')

    if is_global:
        # Логика для удаления глобального ника
        if get_global_nick(user.id, 0):
            delete_global_nick(user.id, 0)
            text = f"🗑️ **Глобальный ник для `{display_name}` удалён.**"
        else:
            text = f"ℹ️ У пользователя `{display_name}` не установлен глобальный ник."
    else:
        # Новая логика: отключаем ник для текущего чата, устанавливая "none"
        set_global_nick(user.id, event.chat_id, 'none')
        text = f"✅ **Отображение универсального ника для `{display_name}` в этом чате отключено.**"

    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*nonick\s+list((?:\s+-g)?)$', x)))
@error_handler
async def nonick_list_handler(event):
    if not await is_owner(event): return
    
    is_global_list = bool(event.pattern_match.group(1))
    chat_id = 0 if is_global_list else event.chat_id
    nick_dict = GLOBAL_NICKS[chat_id]
    list_type = "глобальных ников" if is_global_list else "ников для этого чата"
    
    if not nick_dict:
        text = f"📋 **Список {list_type} пуст.**"
    else:
        text = f"📋 **Список {list_type}:**\n\n"
        for user_id, nick in nick_dict.items():
            try:
                user = await client.get_entity(user_id)
                text += f"• `{get_display_name(user)}` (`{user_id}`) -> `{nick}`\n"
            except Exception:
                text += f"• `ID: {user_id}` -> `{nick}`\n"
                
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*block(?:\s+(@?\S+))?$', x)))
@error_handler
async def block_handler(event):
    if not await is_owner(event): return
    user_to_block = await get_target_user(event)
    if not user_to_block:
        text = "❌ **Ошибка:** Не удалось найти пользователя для блокировки."
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    try:
        await client(BlockRequest(id=user_to_block.id))
        
        # Логика для нового .blocklist
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute("INSERT OR IGNORE INTO bot_blocklist (user_id) VALUES (?)", (user_to_block.id,))
        conn.commit()
        conn.close()
        if user_to_block.id not in BOT_BLOCKED_USERS:
            BOT_BLOCKED_USERS.add(user_to_block.id)

        text = f"🔒 **Пользователь `{get_universal_display_name(user_to_block, event.chat_id)}` успешно заблокирован.**"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
    except Exception as e:
        text = f"❌ **Ошибка блокировки:** {str(e)}"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        await send_error_log(str(e), "block_handler", event)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*unblock(?:\s+(@?\S+))?$', x)))
@error_handler
async def unblock_handler(event):
    if not await is_owner(event): return
    user_to_unblock = await get_target_user(event)
    if not user_to_unblock:
        text = "❌ **Ошибка:** Не удалось найти пользователя для разблокировки."
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return
    try:
        await client(UnblockRequest(id=user_to_unblock.id))
        
        # Логика для нового .blocklist
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute("DELETE FROM bot_blocklist WHERE user_id = ?", (user_to_unblock.id,))
        conn.commit()
        conn.close()
        BOT_BLOCKED_USERS.discard(user_to_unblock.id)

        text = f"🔓 **Пользователь `{get_universal_display_name(user_to_unblock, event.chat_id)}` успешно разблокирован.**"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
    except Exception as e:
        text = f"❌ **Ошибка разблокировки:** {str(e)}"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        await send_error_log(str(e), "unblock_handler", event)

@client.on(events.NewMessage(pattern=lambda x: re.match(rf'^{re.escape(CONFIG["prefix"])}\s*blocklist$', x)))
@error_handler
async def blocklist_handler(event):
    if not await is_owner(event): return
    
    if not BOT_BLOCKED_USERS:
        text = "🚫 **Список заблокированных через бота пуст.**"
        parsed_text, entities = parser.parse(text)
        await safe_edit_message(event, parsed_text, entities)
        return

    text = "🚫 **Заблокированные через юзербот:**\n\n"
    users_info = []
    for user_id in BOT_BLOCKED_USERS:
        try:
            user = await client.get_entity(user_id)
            users_info.append(f"• `{get_universal_display_name(user, event.chat_id)}` (ID: `{user.id}`)")
        except Exception:
            users_info.append(f"• `Не удалось получить инфо` (ID: `{user_id}`)")
    
    text += "\n".join(users_info)
    parsed_text, entities = parser.parse(text)
    await safe_edit_message(event, parsed_text, entities)

def debug_db():
    print("[Debug] Отладка базы данных")
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        print("[Debug] Таблицы в базе данных:")
        cursor.execute("SELECT name FROM sqlite_master WHERE type='table';")
        tables = cursor.fetchall()
        for table in tables:
            print(f"[Debug] Таблица: {table[0]}")
            cursor.execute(f"SELECT * FROM {table[0]}")
            rows = cursor.fetchall()
            for row in rows: print(f"[Debug] Запись: {row}")
        conn.close()
    except Exception as e:
        print(f"[Error] Ошибка отладки базы данных: {e}")

async def main():
    global owner_id, BOT_BLOCKED_USERS
    async with client:
        print("[Debug] Запуск основной функции внутри контекста клиента")
        try:
            print("[Debug] Инициализация базы данных")
            init_db()
            print("[Debug] Загрузка конфигурации")
            load_config()
            print("[Debug] Загрузка белых списков")
            load_whitelists()
            print("[Debug] Загрузка конфигурации Silent Tags")
            load_silent_tags_config()
            print("[Debug] Загрузка конфигурации RP")
            load_rp_config()
            print("[Debug] Загрузка создателей RP")
            load_rp_creators()
            print("[Debug] Загрузка конфигурации прав администратора")
            load_admin_rights_config()
            print("[Debug] Загрузка конфигов админки")
            load_admin_configs()
            print("[Debug] Загрузка конфига тегов")
            load_tag_config()
            print("[Debug] Загрузка глобальных ников")
            load_global_nicks()
            print("[Debug] Загрузка RP-ников")
            load_rp_nicks()
            print("[Debug] Загрузка списка заблокированных")
            load_bot_blocklist()
            print("[Debug] Отладка базы данных")
            debug_db()
            me = await client.get_me()
            owner_id = me.id
            print(f"[Debug] Owner ID: {owner_id}")
            
            await send_error_log("KoteUserBot запущен!", "main", is_test=True)
            print("[Debug] KoteUserBot успешно запущен и ожидает событий...")
            await client.run_until_disconnected()
        except Exception as e:
            error_msg = f"Критическая ошибка в main: {str(e)}\n\nTraceback: {''.join(traceback.format_tb(e.__traceback__))}"
            print(f"[Critical] {error_msg}")
            if client.is_connected(): await send_error_log(error_msg, "main")
            raise

if __name__ == '__main__':
    print("[Debug] Запуск KoteUserBot")
    try:
        asyncio.run(main())
        if RESTART_FLAG:
            print("[Info] Перезапуск бота...")
            os.execv(sys.executable, ['python'] + sys.argv)
    except KeyboardInterrupt:
        print("\n[Debug] Бот остановлен пользователем.")
    except Exception as e:
        print(f"[Critical] Критическая ошибка при запуске: {e}")
        print(traceback.format_exc())
    finally:
        print("[Debug] Завершение работы.")