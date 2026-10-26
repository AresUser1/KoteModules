-- usermanage.lua
-- Модуль для управления пользователями и контактами.
-- Позволяет блокировать, разблокировать, добавлять и удалять из контактов.

name = "usermanage"
description = "Управление пользователями, блокировками и контактами"
version = "1.0.5"
author = "Kote"
min_host_version = 2

-- 💎 Премиум эмодзи
local EMOJI = {
    contact = "[📝](tg://emoji?id=5256230583717079814)",
    error   = "[🧐](tg://emoji?id=5370706614800097423)",
    success = "[✅](tg://emoji?id=5774022692642492953)",
    block   = "[🔒](tg://emoji?id=6037249452824072506)",
    unblock = "[🔓](tg://emoji?id=6037496202990194718)",
    list    = "[🚫](tg://emoji?id=5920046907782074235)",
    trash   = "[🗑️](tg://emoji?id=5879896690210639947)"
}

commands = {
    block     = { doc = "Заблокировать пользователя", usage = "[@user/ID]" },
    unblock   = { doc = "Разблокировать пользователя", usage = "[@user/ID]" },
    blocklist = { doc = "Показать список заблокированных ботом", usage = "" },
    ct        = { doc = "Добавить в контакты", usage = "[@user/ID] <Имя>" },
    mct       = { doc = "Добавить в контакты взаимно", usage = "[@user/ID] <Имя>" },
    rct       = { doc = "Удалить из контактов", usage = "[@user/ID]" }
}

-- Хелперы для работы со списком блокировок в koto.db
local function get_blocklist()
    local raw = koto.db.get("blocklist") or ""
    local set = {}
    for id_str in string.gmatch(raw, "([^,]+)") do
        local uid = tonumber(id_str)
        if uid then set[uid] = true end
    end
    return set
end

local function save_blocklist(set)
    local list = {}
    for uid, _ in pairs(set) do
        table.insert(list, tostring(uid))
    end
    koto.db.set("blocklist", table.concat(list, ","))
end

-- Определение целевого пользователя
local function resolve_target(msg, arg)
    arg = arg and arg:match("^%s*(.-)%s*$") or ""
    if arg ~= "" then
        -- Если передан username или ID
        local num_id = tonumber(arg)
        if num_id then
            local name = msg:user_name(num_id)
            if not name or name == "" then name = "ID " .. tostring(num_id) end
            return num_id, name
        end
        local clean_uname = arg:gsub("^@", "")
        -- Попробуем найти ID через хост-сервис или профиль
        return clean_uname, arg
    end

    -- Если ответ на сообщение
    if msg.reply and msg.reply.sender_id and msg.reply.sender_id ~= 0 then
        local uid = msg.reply.sender_id
        local name = msg:user_name(uid)
        if not name or name == "" then name = "ID " .. tostring(uid) end
        return uid, name
    end

    return nil, nil
end

function on_message(msg)
    if not msg.out then return end
    local text = msg.text or ""

    local cmd, rest = text:match("^[%.%!%/](%S+)%s*(.*)$")
    if not cmd then return end
    cmd = string.lower(cmd)

    -- .block [@user/ID]
    if cmd == "block" then
        local target_uid, target_name = resolve_target(msg, rest)
        if not target_uid then
            msg:edit(EMOJI.error .. " **Ошибка: Не удалось найти пользователя.**\n> Укажите @username/ID или ответьте на сообщение.", "md")
            return
        end

        -- Вызываем блокировку через нативный сервис ядра
        local req = '{"user_id": ' .. (tonumber(target_uid) or 0) .. '}'
        if type(target_uid) == "string" and not tonumber(target_uid) then
            req = '{"username": "' .. target_uid .. '"}'
        end
        local ok = koto.host_action_json("telegram.block.v1", req)

        -- Сохраняем в локальный блок-лист
        if tonumber(target_uid) then
            local bl = get_blocklist()
            bl[tonumber(target_uid)] = true
            save_blocklist(bl)
        end

        msg:edit(EMOJI.block .. " **Пользователь **`" .. target_name .. "`** заблокирован.**", "md")
        return
    end

    -- .unblock [@user/ID]
    if cmd == "unblock" then
        local target_uid, target_name = resolve_target(msg, rest)
        if not target_uid then
            msg:edit(EMOJI.error .. " **Ошибка: Не удалось найти пользователя.**\n> Укажите @username/ID или ответьте на сообщение.", "md")
            return
        end

        local req = '{"user_id": ' .. (tonumber(target_uid) or 0) .. '}'
        if type(target_uid) == "string" and not tonumber(target_uid) then
            req = '{"username": "' .. target_uid .. '"}'
        end
        koto.host_action_json("telegram.unblock.v1", req)

        if tonumber(target_uid) then
            local bl = get_blocklist()
            bl[tonumber(target_uid)] = nil
            save_blocklist(bl)
        end

        msg:edit(EMOJI.unblock .. " **Пользователь **`" .. target_name .. "`** разблокирован.**", "md")
        return
    end

    -- .blocklist
    if cmd == "blocklist" then
        local bl = get_blocklist()
        local count = 0
        local lines = {}
        for uid, _ in pairs(bl) do
            count = count + 1
            local name = msg:user_name(uid)
            if not name or name == "" then name = "Пользователь" end
            table.insert(lines, "• `" .. name .. "` (ID: `" .. uid .. "`)")
        end

        if count == 0 then
            msg:edit(EMOJI.list .. " **Список заблокированных через юзербот пуст.**", "md")
            return
        end

        local out = EMOJI.list .. " **Заблокированные через юзербот (" .. count .. "):**\n\n"
        out = out .. "**> " .. table.concat(lines, "\n> ")
        msg:edit(out, "md")
        return
    end

    -- .ct [@user/ID] <Имя>
    if cmd == "ct" then
        local first_arg, extra = rest:match("^(%S+)%s+(.+)$")
        local target_uid, target_name = nil, nil
        local name_to_set = ""

        if msg.reply and msg.reply.sender_id and msg.reply.sender_id ~= 0 then
            target_uid = msg.reply.sender_id
            name_to_set = rest:match("^%s*(.-)%s*$")
        elseif first_arg and extra then
            target_uid, _ = resolve_target(msg, first_arg)
            name_to_set = extra:match("^%s*(.-)%s*$")
        end

        if not target_uid or not name_to_set or name_to_set == "" then
            msg:edit(EMOJI.contact .. " **Использование:** `.ct [@user/ID] <Имя>`\n> Или ответьте на сообщение: `.ct <Имя>`", "md")
            return
        end

        local req = string.format('{"user_id": %s, "first_name": "%s", "mutual": false}', tostring(target_uid), name_to_set)
        koto.host_action_json("telegram.contact.add.v1", req)

        msg:edit(EMOJI.success .. " **Контакт **`" .. name_to_set .. "`** добавлен.**", "md")
        return
    end

    -- .mct [@user/ID] <Имя>
    if cmd == "mct" then
        local first_arg, extra = rest:match("^(%S+)%s+(.+)$")
        local target_uid, target_name = nil, nil
        local name_to_set = ""

        if msg.reply and msg.reply.sender_id and msg.reply.sender_id ~= 0 then
            target_uid = msg.reply.sender_id
            name_to_set = rest:match("^%s*(.-)%s*$")
        elseif first_arg and extra then
            target_uid, _ = resolve_target(msg, first_arg)
            name_to_set = extra:match("^%s*(.-)%s*$")
        end

        if not target_uid or not name_to_set or name_to_set == "" then
            msg:edit(EMOJI.contact .. " **Использование:** `.mct [@user/ID] <Имя>`\n> Или ответьте на сообщение: `.mct <Имя>`", "md")
            return
        end

        local req = string.format('{"user_id": %s, "first_name": "%s", "mutual": true}', tostring(target_uid), name_to_set)
        koto.host_action_json("telegram.contact.add.v1", req)

        msg:edit(EMOJI.success .. " **Взаимный контакт **`" .. name_to_set .. "`** добавлен.**", "md")
        return
    end

    -- .rct [@user/ID]
    if cmd == "rct" then
        local target_uid, target_name = resolve_target(msg, rest)
        if not target_uid then
            msg:edit(EMOJI.error .. " **Ошибка: Пользователь не найден.**\n> Укажите @username/ID или ответьте на сообщение.", "md")
            return
        end

        local req = string.format('{"user_id": %s}', tostring(target_uid))
        koto.host_action_json("telegram.contact.delete.v1", req)

        msg:edit(EMOJI.trash .. " **Контакт **`" .. target_name .. "`** удалён.**", "md")
        return
    end
end
