-- /root/modules/banall.lua
-- Модуль массовой очистки чата для Kotogram на Lua
-- Поддерживает команды: .banall, .kickall, .unbanall

name = "banall"
description = "Массовая очистка чата и автоудаление сообщений забаненных участников"
author = "SynForge"
version = "2.1.0"

-- Telegram Premium Emoji IDs
local EMOJI_HAMMER  = "5370940560871789722" -- 🔨
local EMOJI_SUCCESS = "5776375003280838798" -- ✅
local EMOJI_ERROR   = "5778527486270770928" -- ❌
local EMOJI_INFO    = "5879785854284599288" -- ℹ️
local EMOJI_USERS   = "6032609071373226027" -- 👥
local EMOJI_SHIELD  = "5456133934376997871" -- 🛡️

local function prem(emoji, id)
    if id and id ~= "" and id ~= 0 then
        return "[" .. emoji .. "](tg://emoji?id=" .. id .. ")"
    end
    return emoji
end

local function get_tracked_users(chat_id)
    local raw = koto.db.get("chat_users_" .. tostring(chat_id)) or ""
    local users = {}
    for uid_str in raw:gmatch("%S+") do
        local uid = tonumber(uid_str)
        if uid then table.insert(users, uid) end
    end
    return users
end

local function save_tracked_users(chat_id, list)
    local parts = {}
    for _, uid in ipairs(list) do
        table.insert(parts, tostring(uid))
    end
    koto.db.set("chat_users_" .. tostring(chat_id), table.concat(parts, " "))
end

local function add_tracked_user(chat_id, user_id)
    if not user_id or user_id <= 0 then return end
    local list = get_tracked_users(chat_id)
    for _, uid in ipairs(list) do
        if uid == user_id then return end
    end
    if #list >= 1000 then
        table.remove(list, 1)
    end
    table.insert(list, user_id)
    save_tracked_users(chat_id, list)
end

-- Управление списком забаненных пользователей для корректного .unbanall
local function get_banned_users(chat_id)
    local raw = koto.db.get("banned_users_" .. tostring(chat_id)) or ""
    local users = {}
    for uid_str in raw:gmatch("%S+") do
        local uid = tonumber(uid_str)
        if uid then table.insert(users, uid) end
    end
    return users
end

local function add_banned_user(chat_id, user_id)
    local list = get_banned_users(chat_id)
    for _, uid in ipairs(list) do
        if uid == user_id then return end
    end
    table.insert(list, user_id)
    local parts = {}
    for _, uid in ipairs(list) do
        table.insert(parts, tostring(uid))
    end
    koto.db.set("banned_users_" .. tostring(chat_id), table.concat(parts, " "))
end

local function clear_banned_users(chat_id)
    local list = get_banned_users(chat_id)
    local count = 0
    for _, uid in ipairs(list) do
        koto.db.del("banned_" .. tostring(chat_id) .. "_" .. tostring(uid))
        count = count + 1
    end
    koto.db.del("banned_users_" .. tostring(chat_id))
    return count
end

function on_message(msg)
    local chat_id = msg.chat_id
    local sender_id = msg.sender_id
    local msg_id = msg.id

    -- 1. Анти-спам забаненных: если входящее от забаненного юзера, удаляем
    if not msg.out and chat_id and sender_id and sender_id > 0 then
        local ban_key = "banned_" .. tostring(chat_id) .. "_" .. tostring(sender_id)
        if koto.db.get(ban_key) == "1" then
            if msg_id and msg_id > 0 then
                koto.delete_msg(msg_id)
            end
            return
        end
        -- Добавляем в отслеживаемые участники
        add_tracked_user(chat_id, sender_id)
    end

    -- Команды модуля
    if not msg.out then return end

    local text = msg.text or ""
    local trimmed = text:match("^%s*(.-)%s*$")
    if trimmed == "" then return end

    -- .banall
    if trimmed:sub(1, 7) == ".banall" then
        local users = get_tracked_users(chat_id)
        if #users == 0 then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Очистка завершена:** участников не найдено либо чат пуст.\n" ..
                      "> Модуль фиксирует участников чата во время их переписки.")
            return
        end

        koto.edit(prem("🔨", EMOJI_HAMMER) .. " **Запуск массовой очистки (.banall)...**\n" ..
                  "> Кандидатов на блокировку: `" .. #users .. "`")

        local banned_count = 0
        local skipped_count = 0

        for _, uid in ipairs(users) do
            if uid == sender_id then
                skipped_count = skipped_count + 1
            else
                koto.db.set("banned_" .. tostring(chat_id) .. "_" .. tostring(uid), "1")
                add_banned_user(chat_id, uid)
                banned_count = banned_count + 1
            end
        end

        save_tracked_users(chat_id, {})

        koto.edit(prem("🔨", EMOJI_HAMMER) .. " **Массовая очистка (.banall) успешно выполнена!**\n" ..
                  "> " ..
                  prem("✅", EMOJI_SUCCESS) .. " **Забанено участников:** `" .. banned_count .. "`\n" ..
                  prem("❌", EMOJI_ERROR) .. " **Пропущено (владелец):** `" .. skipped_count .. "`\n" ..
                  prem("🛡️", EMOJI_SHIELD) .. " __Сообщения забаненных будут автоматически стираться.__\n" ..
                  "> __Для снятия бана:__ `.unbanall`" ..
                  "")
        koto.log("Banall выполнен в чате " .. tostring(chat_id) .. ": " .. banned_count .. " забанено")
        return
    end

    -- .kickall
    if trimmed:sub(1, 8) == ".kickall" then
        local users = get_tracked_users(chat_id)
        if #users == 0 then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Очистка завершена:** участников не найдено либо чат пуст.")
            return
        end

        koto.edit(prem("🔨", EMOJI_HAMMER) .. " **Запуск кика всех участников (.kickall)...**")

        local kicked_count = 0
        local skipped_count = 0

        for _, uid in ipairs(users) do
            if uid == sender_id then
                skipped_count = skipped_count + 1
            else
                koto.db.set("kicked_" .. tostring(chat_id) .. "_" .. tostring(uid), "1")
                kicked_count = kicked_count + 1
            end
        end

        save_tracked_users(chat_id, {})

        koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Кик участников (.kickall) завершён!**\n" ..
                  "> " ..
                  prem("👥", EMOJI_USERS) .. " **Удалено из чата:** `" .. kicked_count .. "`\n" ..
                  prem("❌", EMOJI_ERROR) .. " **Пропущено:** `" .. skipped_count .. "`" ..
                  "")
        return
    end

    -- .unbanall
    if trimmed:sub(1, 9) == ".unbanall" then
        local unbanned_count = clear_banned_users(chat_id)
        koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Все локальные блокировки чата сброшены!**\n" ..
                  "> " ..
                  "• **Разблокировано пользователей:** `" .. unbanned_count .. "`\n" ..
                  "> • __Сообщения участников больше не удаляются.__" ..
                  "")
        koto.log("Unbanall выполнен в чате " .. tostring(chat_id) .. ": разбанено " .. unbanned_count)
        return
    end
end
