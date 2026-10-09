-- /root/modules/tagger.lua
-- Мощный модуль теггера для Kotogram на Lua
-- Поддержка: .tag, .itag, .etag, .stoptag, .cleartag, .nonick, .helps, .tagdelay, .tagusers

name = "tagger"
description = "Продвинутый теггер участников чата с вайтлистами и кастомными никами"
author = "SynForge"
version = "3.6.0"

-- Telegram Premium Emoji IDs
local EMOJI_SETTINGS  = "6032742198179532882" -- ⚙️
local EMOJI_TAG       = "5890727932011223292" -- 🏷️
local EMOJI_WHITELIST = "5778299625370817409" -- 📋
local EMOJI_SUCCESS   = "5774022692642492953" -- ✅
local EMOJI_STOP      = "6030563507299160824" -- 🛑
local EMOJI_DELAY     = "5983150113483134607" -- ⏱️
local EMOJI_USERS     = "6032609071373226027" -- 👥
local EMOJI_ADMINS    = "5456133934376997871" -- 👮
local EMOJI_ERROR     = "5778527486270770928" -- ❌
local EMOJI_INFO      = "5879785854284599288" -- ℹ️

local function prem(emoji, id)
    if id and id ~= "" and id ~= 0 then
        return "[" .. emoji .. "](tg://emoji?id=" .. id .. ")"
    end
    return emoji
end

local function html_escape(str)
    if not str then return "" end
    return tostring(str)
end

-- Вспомогательные функции для БД участников чата
local function get_chat_users(chat_id)
    local raw = koto.db.get("chat_users_" .. tostring(chat_id)) or ""
    local users = {}
    for uid_str in raw:gmatch("%S+") do
        local uid = tonumber(uid_str)
        if uid then
            table.insert(users, uid)
        end
    end
    return users
end

local function save_chat_users(chat_id, users_list)
    local parts = {}
    for _, uid in ipairs(users_list) do
        table.insert(parts, tostring(uid))
    end
    koto.db.set("chat_users_" .. tostring(chat_id), table.concat(parts, " "))
end

local function add_chat_user(chat_id, user_id)
    if not user_id or user_id <= 0 then return end
    local users = get_chat_users(chat_id)
    for _, uid in ipairs(users) do
        if uid == user_id then return end
    end
    if #users >= 500 then
        table.remove(users, 1)
    end
    table.insert(users, user_id)
    save_chat_users(chat_id, users)
end

-- Вспомогательные функции для вайтлиста
local function get_whitelist(chat_id)
    local raw = koto.db.get("wl_" .. tostring(chat_id)) or ""
    local wl = {}
    for uid_str in raw:gmatch("%S+") do
        local uid = tonumber(uid_str)
        if uid then wl[uid] = true end
    end
    return wl
end

local function save_whitelist(chat_id, wl)
    local parts = {}
    for uid, _ in pairs(wl) do
        table.insert(parts, tostring(uid))
    end
    koto.db.set("wl_" .. tostring(chat_id), table.concat(parts, " "))
end

-- Хранилище ID отправленных сообщений теггера (для .cleartag)
local function record_tag_msg(chat_id, msg_id)
    if not msg_id or msg_id <= 0 then return end
    local key = "tag_msgs_" .. tostring(chat_id)
    local raw = koto.db.get(key) or ""
    local ids = {}
    for mid_str in raw:gmatch("%S+") do
        local mid = tonumber(mid_str)
        if mid and mid ~= msg_id then table.insert(ids, mid) end
    end
    if #ids >= 100 then
        table.remove(ids, 1)
    end
    table.insert(ids, msg_id)
    local parts = {}
    for _, mid in ipairs(ids) do
        table.insert(parts, tostring(mid))
    end
    koto.db.set(key, table.concat(parts, " "))
end

local function get_tag_msgs(chat_id)
    local key = "tag_msgs_" .. tostring(chat_id)
    local raw = koto.db.get(key) or ""
    local ids = {}
    for mid_str in raw:gmatch("%S+") do
        local mid = tonumber(mid_str)
        if mid then table.insert(ids, mid) end
    end
    return ids
end

local function clear_tag_msgs(chat_id)
    local key = "tag_msgs_" .. tostring(chat_id)
    koto.db.del(key)
end

-- Получение кастомного ника
local function get_nickname(chat_id, user_id)
    local local_nick = koto.db.get("nick_" .. tostring(chat_id) .. "_" .. tostring(user_id))
    if local_nick and local_nick ~= "" then return local_nick end
    local global_nick = koto.db.get("nick_0_" .. tostring(user_id))
    if global_nick and global_nick ~= "" then return global_nick end
    return "Пользователь " .. tostring(user_id)
end

function on_message(msg)
    local chat_id = msg.chat_id
    local sender_id = msg.sender_id
    local msg_id = msg.id
    local text = msg.text or ""

    -- 1. Фоновый сбор участников чата
    if chat_id and sender_id and sender_id > 0 and not msg.out then
        add_chat_user(chat_id, sender_id)
    end

    -- Если это исходящее сообщение тега нашего юзербота — регистрируем его ID для .cleartag
    if msg.out and chat_id and msg_id and text:find(EMOJI_TAG) then
        record_tag_msg(chat_id, msg_id)
    end

    -- Команды исполняются только от нашего аккаунта
    if not msg.out then return end

    local trimmed = text:match("^%s*(.-)%s*$")
    if trimmed == "" then return end

    -- .stoptag
    if trimmed:sub(1, 8) == ".stoptag" then
        koto.db.set("active_tag_" .. tostring(chat_id), "0")
        koto.edit(prem("🛑", EMOJI_STOP) .. " **Тегирование остановлено!**\n" ..
                  "> Флаг теггера успешно сброшен в koto.db.")
        return
    end

    -- .cleartag [count]
    if trimmed:sub(1, 9) == ".cleartag" then
        local count_arg = trimmed:sub(10):match("^%s*(%d*)%s*$")
        local max_clear = tonumber(count_arg)

        local ids = get_tag_msgs(chat_id)
        if #ids == 0 and not max_clear then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Сообщений теггера для очистки не найдено.**")
            return
        end

        koto.edit(prem("⏱️", EMOJI_DELAY) .. " **Очистка сообщений теггера...**")
        local count = 0
        if #ids > 0 then
            for _, mid in ipairs(ids) do
                koto.delete_msg(mid)
                count = count + 1
            end
            clear_tag_msgs(chat_id)
        elseif max_clear and max_clear > 0 then
            for mid = msg_id - 1, msg_id - max_clear, -1 do
                koto.delete_msg(mid)
                count = count + 1
            end
        end

        koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Очистка завершена!**\n" ..
                  "> Удалено сообщений тегов: `" .. count .. "`")
        return
    end

    -- .tagdelay <sec>
    if trimmed:sub(1, 9) == ".tagdelay" then
        local arg = trimmed:sub(10):match("^%s*(%d+)%s*$")
        local sec = tonumber(arg)
        if not sec or sec < 1 then
            local cur = koto.db.get("tag_delay_" .. tostring(chat_id)) or "3"
            koto.edit(prem("⏱️", EMOJI_DELAY) .. " **Задержка теггера:** `" .. cur .. " сек.`\n" ..
                      "> Использование: `.tagdelay <сек>`")
            return
        end
        koto.db.set("tag_delay_" .. tostring(chat_id), tostring(sec))
        koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Задержка теггера установлена:** `" .. sec .. " сек.`")
        return
    end

    -- .tagusers <count>
    if trimmed:sub(1, 9) == ".tagusers" then
        local arg = trimmed:sub(10):match("^%s*(%d+)%s*$")
        local cnt = tonumber(arg)
        if not cnt or cnt < 1 or cnt > 15 then
            local cur = koto.db.get("tag_chunk_" .. tostring(chat_id)) or "5"
            koto.edit(prem("👥", EMOJI_USERS) .. " **Юзеров в одном сообщении:** `" .. cur .. "`\n" ..
                      "> Использование: `.tagusers <1..15>`")
            return
        end
        koto.db.set("tag_chunk_" .. tostring(chat_id), tostring(cnt))
        koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Количество юзеров в пачке:** `" .. cnt .. "`")
        return
    end

    -- .helps (Вайтлист)
    if trimmed == ".helps" or trimmed == ".tag wl" then
        local wl = get_whitelist(chat_id)
        local lines = {}
        for uid, _ in pairs(wl) do
            table.insert(lines, "• `" .. uid .. "` (" .. html_escape(get_nickname(chat_id, uid)) .. ")")
        end
        local content = #lines > 0 and table.concat(lines, "\n") or "__Вайтлист чата пуст.__"
        koto.edit(prem("📋", EMOJI_WHITELIST) .. " **Вайтлист теггера чата:**\n" ..
                  "> " .. content .. "\n" ..
                  "> __Управление:__ `.tag add @user/id` / `.tag remove @user/id`")
        return
    end

    -- .tag add / .add
    if trimmed:sub(1, 8) == ".tag add" or trimmed:sub(1, 4) == ".add" then
        local rest = trimmed:match("^%.%a+%s+add%s+(%S+)") or trimmed:match("^%.add%s+(%S+)")
        local target_id = tonumber(rest)
        if not target_id and msg.reply_to_msg_id and msg.reply_to_msg_id > 0 then
            target_id = msg.reply_to_msg_id
        end
        if not target_id then
            koto.edit(prem("❌", EMOJI_ERROR) .. " **Укажите ID или ответьте на сообщение:** `.tag add <id>`")
            return
        end
        local wl = get_whitelist(chat_id)
        wl[target_id] = true
        save_whitelist(chat_id, wl)
        koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Пользователь добавлен в вайтлист:** `" .. target_id .. "`")
        return
    end

    -- .tag remove / .remove
    if trimmed:sub(1, 11) == ".tag remove" or trimmed:sub(1, 7) == ".remove" then
        local rest = trimmed:match("^%.%a+%s+remove%s+(%S+)") or trimmed:match("^%.remove%s+(%S+)")
        local target_id = tonumber(rest)
        if not target_id then
            koto.edit(prem("❌", EMOJI_ERROR) .. " **Укажите ID:** `.tag remove <id>`")
            return
        end
        local wl = get_whitelist(chat_id)
        wl[target_id] = nil
        save_whitelist(chat_id, wl)
        koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Пользователь удален из вайтлиста:** `" .. target_id .. "`")
        return
    end

    -- .nonick add/del/list
    if trimmed:sub(1, 7) == ".nonick" then
        local sub = trimmed:sub(8):match("^%s*(.-)%s*$")
        local action, user_token, nick = sub:match("^(%a+)%s+(%S+)%s*(.*)$")
        if action == "add" and user_token and nick ~= "" then
            local is_global = nick:find("%-g") ~= nil
            nick = nick:gsub("%s*%-g%s*", "")
            local uid = tonumber(user_token)
            if uid then
                local k = is_global and ("nick_0_" .. uid) or ("nick_" .. chat_id .. "_" .. uid)
                koto.db.set(k, nick)
                koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Кастомный ник сохранён:**\n" ..
                          "> ID: `" .. uid .. "` | Ник: **" .. html_escape(nick) .. "** (" .. (is_global and "Глобально" or "Локально") .. ")")
                return
            end
        elseif action == "del" and user_token then
            local uid = tonumber(user_token)
            if uid then
                koto.db.del("nick_" .. chat_id .. "_" .. uid)
                koto.db.del("nick_0_" .. uid)
                koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Кастомный ник удалён для:** `" .. uid .. "`")
                return
            end
        end
        koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Использование .nonick:**\n" ..
                  "> • `.nonick add <ID> <Ник> [-g]`\n" ..
                  "> • `.nonick del <ID>`")
        return
    end

    -- .tag / .itag / .etag
    local is_itag = trimmed:sub(1, 5) == ".itag"
    local is_etag = trimmed:sub(1, 5) == ".etag"
    local is_tag = (trimmed:sub(1, 4) == ".tag") and not is_itag and not is_etag

    if is_tag or is_itag or is_etag then
        local raw_args = ""
        if is_itag or is_etag then
            raw_args = trimmed:sub(6):match("^%s*(.-)%s*$")
        else
            raw_args = trimmed:sub(5):match("^%s*(.-)%s*$")
        end

        local custom_text = raw_args
        if custom_text == "" then
            custom_text = "Внимание, общий сбор!"
        end

        local all_users = get_chat_users(chat_id)
        if #all_users == 0 then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Список участников пуст.**\n" ..
                      "> Модуль собирает участников автоматически по мере их активности в чате.")
            return
        end

        local wl = get_whitelist(chat_id)
        local filtered = {}

        for _, uid in ipairs(all_users) do
            if is_etag or is_tag then
                -- В обычном и exclude режиме пропускаем вайтлист
                if not wl[uid] then
                    table.insert(filtered, uid)
                end
            elseif is_itag then
                -- В include режиме тегаем только тех, кто в вайтлисте
                if wl[uid] then
                    table.insert(filtered, uid)
                end
            end
        end

        if #filtered == 0 then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Нет подходящих пользователей для тега (все в вайтлисте или отфильтрованы).**")
            return
        end

        local chunk_size = tonumber(koto.db.get("tag_chunk_" .. tostring(chat_id)) or "5")
        if chunk_size < 1 then chunk_size = 5 end

        koto.delete()
        koto.db.set("active_tag_" .. tostring(chat_id), "1")

        koto.log("Теггер запущен в чате " .. tostring(chat_id) .. ": пользователей " .. tostring(#filtered))

        local i = 1
        while i <= #filtered do
            -- Проверяем прерывание через stoptag
            if koto.db.get("active_tag_" .. tostring(chat_id)) == "0" then
                break
            end

            local mentions = {}
            for c = 0, chunk_size - 1 do
                local u = filtered[i + c]
                if u then
                    local n = get_nickname(chat_id, u)
                    table.insert(mentions, '[' .. html_escape(n) .. '](tg://user?id=' .. u .. ')')
                end
            end

            local out_msg = prem("🏷️", EMOJI_TAG) .. " **" .. html_escape(custom_text) .. "**\n" ..
                            "> " .. table.concat(mentions, " • ") .. ""
            koto.reply(out_msg)

            i = i + chunk_size
        end

        koto.db.set("active_tag_" .. tostring(chat_id), "0")
        return
    end
end
