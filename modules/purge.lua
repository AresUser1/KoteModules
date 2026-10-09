-- /root/modules/purge.lua
-- Модуль умной зачистки сообщений для Kotogram на Lua
-- Поддерживает: .purge, .purgeme, .purgeuser, .purgemedia, .purgebots

name = "purge"
description = "Хирургическая и массовая зачистка сообщений в чатах"
author = "SynForge"
version = "2.1.0"

-- Telegram Premium Emoji IDs
local EMOJI_TRASH   = "5465665476971388723" -- 🗑️
local EMOJI_SUCCESS = "5776375003280838798" -- ✅
local EMOJI_ERROR   = "5778527486270770928" -- ❌
local EMOJI_INFO    = "5879785854284599288" -- ℹ️
local EMOJI_TIMER   = "5983150113483134607" -- ⏱️

local function prem(emoji, id)
    if id and id ~= "" and id ~= 0 then
        return "[" .. emoji .. "](tg://emoji?id=" .. id .. ")"
    end
    return emoji
end

-- Кэширование истории сообщений в koto.db (формат: mid:sid:out:media:is_bot)
local function record_msg(chat_id, mid, sid, is_out, has_media, is_bot)
    if not mid or mid <= 0 then return end
    local key = "hist_" .. tostring(chat_id)
    local raw = koto.db.get(key) or ""
    local entries = {}
    for entry in raw:gmatch("%S+") do
        table.insert(entries, entry)
    end
    if #entries >= 300 then
        table.remove(entries, 1)
    end
    local tag = tostring(mid) .. ":" .. tostring(sid or 0) .. ":" .. (is_out and "1" or "0") .. ":" .. (has_media and "1" or "0") .. ":" .. (is_bot and "1" or "0")
    table.insert(entries, tag)
    koto.db.set(key, table.concat(entries, " "))
end

local function get_history(chat_id)
    local key = "hist_" .. tostring(chat_id)
    local raw = koto.db.get(key) or ""
    local list = {}
    for entry in raw:gmatch("%S+") do
        local mid, sid, is_out, media, bot = entry:match("^(%d+):([%-%d]+):(%d):(%d):?(%d?)$")
        if mid then
            table.insert(list, {
                mid = tonumber(mid),
                sid = tonumber(sid),
                out = (is_out == "1"),
                media = (media == "1"),
                is_bot = (bot == "1")
            })
        end
    end
    return list
end

function on_message(msg)
    local chat_id = msg.chat_id
    local msg_id = msg.id
    local sender_id = msg.sender_id
    local is_out = msg.out or false
    local text = msg.text or ""

    -- Определяем, является ли сообщение медиафайлом (фото/стикер/кружок без подписи или со ссылкой)
    local has_media = (msg.media ~= nil) or (text == "") or text:find("%.mp4") or text:find("%.jpg") or text:find("%.png")
    -- Определяем ботов: команды со слэшем /, сервисные сообщения или отрицательный sender_id (каналы/анонимы)
    local is_bot = (text:sub(1, 1) == "/") or (sender_id and sender_id < 0)

    -- Кэшируем каждое сообщение чата
    if chat_id and msg_id then
        record_msg(chat_id, msg_id, sender_id, is_out, has_media, is_bot)
    end

    if not is_out then return end

    local trimmed = text:match("^%s*(.-)%s*$")
    if trimmed == "" then return end

    -- .purgeme <N>
    if trimmed:sub(1, 8) == ".purgeme" then
        local count_str = trimmed:sub(9):match("^%s*(%d+)%s*$")
        local limit = tonumber(count_str) or 10
        if limit > 100 then limit = 100 end

        local t0 = os.clock and os.clock() or 0
        koto.delete()
        local history = get_history(chat_id)
        local deleted = 0

        for i = #history, 1, -1 do
            local item = history[i]
            if item.out and item.mid ~= msg_id then
                koto.delete_msg(item.mid)
                deleted = deleted + 1
                if deleted >= limit then break end
            end
        end

        local report = prem("🗑️", EMOJI_TRASH) .. " **Очистка своих сообщений (.purgeme)**\n" ..
                       "> " ..
                       prem("✅", EMOJI_SUCCESS) .. " **Удалено своих сообщений:** `" .. deleted .. "`\n" ..
                       ""
        koto.reply(report)
        return
    end

    -- .purgeuser <id> [N]
    if trimmed:sub(1, 10) == ".purgeuser" then
        local rest = trimmed:sub(11):match("^%s*(.-)%s*$")
        local target_str, limit_str = rest:match("^(%S+)%s*(%d*)$")
        local target_id = tonumber(target_str)
        local limit = tonumber(limit_str) or 20
        if limit > 100 then limit = 100 end

        if not target_id then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Использование:** `.purgeuser <ID> [кол-во]`")
            return
        end

        koto.delete()
        local history = get_history(chat_id)
        local deleted = 0

        for i = #history, 1, -1 do
            local item = history[i]
            if item.sid == target_id and item.mid ~= msg_id then
                koto.delete_msg(item.mid)
                deleted = deleted + 1
                if deleted >= limit then break end
            end
        end

        local report = prem("🗑️", EMOJI_TRASH) .. " **Очистка сообщений пользователя**\n" ..
                       "> " ..
                       "• **Пользователь:** `" .. target_id .. "`\n" ..
                       "> • " .. prem("✅", EMOJI_SUCCESS) .. " **Удалено сообщений:** `" .. deleted .. "`" ..
                       ""
        koto.reply(report)
        return
    end

    -- .purgemedia [N]
    if trimmed:sub(1, 11) == ".purgemedia" then
        local limit_str = trimmed:sub(12):match("^%s*(%d*)%s*$")
        local limit = tonumber(limit_str) or 20
        if limit > 100 then limit = 100 end

        koto.delete()
        local history = get_history(chat_id)
        local deleted = 0

        for i = #history, 1, -1 do
            local item = history[i]
            if item.media and item.mid ~= msg_id then
                koto.delete_msg(item.mid)
                deleted = deleted + 1
                if deleted >= limit then break end
            end
        end

        local report = prem("🗑️", EMOJI_TRASH) .. " **Очистка медиа-сообщений (.purgemedia)**\n" ..
                       "> " ..
                       prem("✅", EMOJI_SUCCESS) .. " **Удалено медиафайлов:** `" .. deleted .. "`" ..
                       ""
        koto.reply(report)
        return
    end

    -- .purgebots [N]
    if trimmed:sub(1, 10) == ".purgebots" then
        local limit_str = trimmed:sub(11):match("^%s*(%d*)%s*$")
        local limit = tonumber(limit_str) or 20
        if limit > 100 then limit = 100 end

        koto.delete()
        local history = get_history(chat_id)
        local deleted = 0

        for i = #history, 1, -1 do
            local item = history[i]
            -- Удаляем только ботов, каналы и сервисные сообщения, сохраняя сообщения реальных участников
            if item.is_bot and not item.out and item.mid ~= msg_id then
                koto.delete_msg(item.mid)
                deleted = deleted + 1
                if deleted >= limit then break end
            end
        end

        local report = prem("🗑️", EMOJI_TRASH) .. " **Очистка ботов и сервисных сообщений**\n" ..
                       "> " ..
                       prem("✅", EMOJI_SUCCESS) .. " **Удалено сообщений ботов:** `" .. deleted .. "`" ..
                       ""
        koto.reply(report)
        return
    end

    -- .purge (по реплаю или с числом)
    if trimmed:sub(1, 6) == ".purge" then
        local arg = trimmed:sub(7):match("^%s*(.-)%s*$")
        local count = tonumber(arg)
        local reply_to = msg.reply_to_msg_id

        if reply_to and reply_to > 0 then
            -- Очистка от reply_to до текущего msg_id
            local start_id = math.min(reply_to, msg_id)
            local end_id = math.max(reply_to, msg_id)
            local deleted = 0

            koto.delete()

            for id = end_id - 1, start_id, -1 do
                koto.delete_msg(id)
                deleted = deleted + 1
                if deleted >= 100 then break end
            end

            local report = prem("🗑️", EMOJI_TRASH) .. " **Smart Purge завершён!**\n" ..
                           "> " ..
                           prem("✅", EMOJI_SUCCESS) .. " **Зачищен диапазон ID:** `" .. start_id .. " .. " .. end_id .. "`\n" ..
                           "> • **Удалено сообщений:** `" .. deleted .. "`" ..
                           ""
            koto.reply(report)
            return
        elseif count and count > 0 then
            -- Очистка последних N сообщений по ID
            if count > 100 then count = 100 end
            koto.delete()

            for id = msg_id - 1, msg_id - count, -1 do
                koto.delete_msg(id)
            end

            local report = prem("🗑️", EMOJI_TRASH) .. " **Smart Purge завершён!**\n" ..
                           "> " ..
                           prem("✅", EMOJI_SUCCESS) .. " **Удалено последних сообщений:** `" .. count .. "`" ..
                           ""
            koto.reply(report)
            return
        else
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Smart Purge & Cleaner:**\n" ..
                      "> " ..
                      "• `.purge` (по реплаю) — удалить от ответа до текущего\n" ..
                      "> • `.purge <N>` — удалить последние N сообщений\n" ..
                      "> • `.purgeme <N>` — удалить свои сообщения\n" ..
                      "> • `.purgeuser <ID> [N]` — удалить сообщения юзера\n" ..
                      "> • `.purgemedia [N]` — удалить только медиа\n" ..
                      "> • `.purgebots [N]` — зачистить ботов" ..
                      "")
            return
        end
    end
end
