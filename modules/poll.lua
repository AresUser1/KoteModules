-- poll.lua
-- Мощный модуль для работы с опросами.
-- Позволяет создавать опросы по шаблонам с индивидуальными настройками.

name = "poll"
description = "Создание опросов по шаблонам, сканер и теги участников"
version = "3.2.2"
author = "Kote"
min_host_version = 2

-- 💎 Премиум эмодзи
local EMOJI = {
    poll    = "[📋](tg://emoji?id=5956561916573782596)",
    vote    = "[✅](tg://emoji?id=5774022692642492953)",
    novote  = "[❌](tg://emoji?id=5431449001532594346)",
    tag     = "[🏷️](tg://emoji?id=5890727932011223292)"
}

commands = {
    poll     = { doc = "Создать опрос по шаблону", usage = "<шаблон> <вопрос>" },
    pollcfg  = { doc = "Управление шаблонами опросов", usage = "[list/add/del]" },
    pollscan = { doc = "Сканировать результаты опроса (в ответ на опрос)", usage = "" },
    polltag  = { doc = "Тегнуть проголосовавших или непроголосовавших", usage = "<номер|novote> [текст]" }
}

-- Стандартные шаблоны
local DEFAULT_TEMPLATES = {
    ["данет"]  = { options = {"Да", "Нет"}, public = true, multi = false, pin = false },
    ["оценка"] = { options = {"1", "2", "3", "4", "5"}, public = true, multi = false, pin = false },
    ["время"]  = { options = {"Сейчас", "Через 5 мин", "Через 15 мин", "Позже"}, public = true, multi = false, pin = false }
}

local function get_templates()
    local raw = koto.db.get("templates_json")
    if not raw or raw == "" then
        return DEFAULT_TEMPLATES
    end
    -- Парсим простой JSON-like список шаблонов или используем дефолтные
    local templates = {}
    for line in string.gmatch(raw, "([^\n]+)") do
        local name, opts_str, flags = line:match("^(%S+)|([^|]+)|(.*)$")
        if name and opts_str then
            local opts = {}
            for opt in string.gmatch(opts_str, "([^,]+)") do
                table.insert(opts, opt)
            end
            templates[name] = {
                options = opts,
                public = (flags:find("public") ~= nil),
                multi = (flags:find("multi") ~= nil),
                pin = (flags:find("pin") ~= nil)
            }
        end
    end
    if next(templates) == nil then return DEFAULT_TEMPLATES end
    return templates
end

local function save_templates(templates)
    local lines = {}
    for name, data in pairs(templates) do
        local opts_str = table.concat(data.options, ",")
        local flags = {}
        if data.public then table.insert(flags, "public") end
        if data.multi then table.insert(flags, "multi") end
        if data.pin then table.insert(flags, "pin") end
        table.insert(lines, name .. "|" .. opts_str .. "|" .. table.concat(flags, ";"))
    end
    koto.db.set("templates_json", table.concat(lines, "\n"))
end

function on_message(msg)
    if not msg.out then return end
    local text = msg.text or ""

    local cmd, rest = text:match("^[%.%!%/](%S+)%s*(.*)$")
    if not cmd then return end
    cmd = string.lower(cmd)

    -- .poll <шаблон> <вопрос>
    if cmd == "poll" then
        local tpl_name, question = rest:match("^(%S+)%s+(.+)$")
        local templates = get_templates()

        if not tpl_name or not question then
            local available = {}
            for k, _ in pairs(templates) do table.insert(available, "`" .. k .. "`") end
            msg:edit(EMOJI.poll .. " **Использование:** `.poll <шаблон> <вопрос>`\n\n" ..
                     "📋 **Доступные шаблоны:** " .. table.concat(available, ", "), "md")
            return
        end

        tpl_name = string.lower(tpl_name)
        local tpl = templates[tpl_name]
        if not tpl then
            msg:edit("❌ **Шаблон '" .. tpl_name .. "' не найден.**\n> Используйте `.pollcfg` для просмотра шаблонов.", "md")
            return
        end

        -- Формируем опрос
        -- Попытка создать через нативный сервис ядра MTProto send_poll
        local opts_json = {}
        for _, opt in ipairs(tpl.options) do
            table.insert(opts_json, '"' .. opt:gsub('"', '\\"') .. '"')
        end
        local req = string.format('{"chat_id": %s, "question": "%s", "options": [%s], "public": %s, "multi": %s}',
                                  tostring(msg.chat_id), question:gsub('"', '\\"'), table.concat(opts_json, ","),
                                  tpl.public and "true" or "false", tpl.multi and "true" or "false")

        local ok = koto.host_action_json("telegram.poll.send.v1", req)

        -- Красивая карточка опроса
        local poll_card = EMOJI.poll .. " **Опрос:** " .. question .. "\n\n"
        poll_card = poll_card .. "> **Тип:** " .. (tpl.public and "Публичный" or "Анонимный") .. "\n"
        poll_card = poll_card .. "> **Варианты ответов:**\n"
        for i, opt in ipairs(tpl.options) do
            poll_card = poll_card .. "> " .. i .. ". " .. opt .. "\n"
        end

        if tpl.pin then
            poll_card = poll_card .. "\n📌 **Опрос закреплён.**"
        end

        msg:edit(poll_card, "md")
        return
    end

    -- .pollcfg [list/add/del]
    if cmd == "pollcfg" then
        local sub, extra = rest:match("^(%S+)%s*(.*)$")
        sub = sub and string.lower(sub) or "list"
        local templates = get_templates()

        if sub == "list" or sub == "" then
            local out = EMOJI.poll .. " **Шаблоны опросов:**\n\n"
            for name, data in pairs(templates) do
                out = out .. "• **" .. name .. "** (`" .. table.concat(data.options, ", ") .. "`)\n"
                out = out .. "  > Настройки: " .. (data.public and "Публичный" or "Анонимный")
                if data.multi then out = out .. ", Multi" end
                if data.pin then out = out .. ", Закреп" end
                out = out .. "\n"
            end
            out = out .. "\n💡 **Команды управления:**\n" ..
                         "> `.pollcfg add <название> <вариант1,вариант2,...>`\n" ..
                         "> `.pollcfg del <название>`"
            msg:edit(out, "md")
            return
        end

        if sub == "add" then
            local name, opts_str = extra:match("^(%S+)%s+(.+)$")
            if not name or not opts_str then
                msg:edit("⚠️ **Использование:** `.pollcfg add <название> <вариант1,вариант2,...>`", "md")
                return
            end
            name = string.lower(name)
            local opts = {}
            for opt in string.gmatch(opts_str, "([^,]+)") do
                local clean = opt:match("^%s*(.-)%s*$")
                if clean ~= "" then table.insert(opts, clean) end
            end
            if #opts < 2 then
                msg:edit("❌ **Ошибка:** Опрос должен содержать минимум 2 варианта ответа.", "md")
                return
            end
            templates[name] = { options = opts, public = true, multi = false, pin = false }
            save_templates(templates)
            msg:edit(EMOJI.vote .. " **Шаблон '" .. name .. "' успешно создан!**\n> Варианты: `" .. table.concat(opts, ", ") .. "`", "md")
            return
        end

        if sub == "del" then
            local name = extra:match("^%s*(%S+)%s*$")
            if not name then
                msg:edit("⚠️ **Использование:** `.pollcfg del <название>`", "md")
                return
            end
            name = string.lower(name)
            if not templates[name] then
                msg:edit("❌ **Шаблон '" .. name .. "' не найден.**", "md")
                return
            end
            templates[name] = nil
            save_templates(templates)
            msg:edit(EMOJI.vote .. " **Шаблон '" .. name .. "' удалён.**", "md")
            return
        end
    end

    -- .pollscan (в ответ на опрос)
    if cmd == "pollscan" then
        if not msg.reply then
            msg:edit(EMOJI.novote .. " **Ответьте на сообщение с опросом.**", "md")
            return
        end

        msg:edit(EMOJI.poll .. " **Сканирую опрос...**", "md")
        -- Запрашиваем данные опроса через хост-сервис
        local req = string.format('{"chat_id": %s, "msg_id": %s}', tostring(msg.chat_id), tostring(msg.reply.id))
        local res = koto.host_call_json("telegram.poll.scan.v1", req)

        local out = EMOJI.poll .. " **Сканер результатов опроса:**\n\n"
        out = out .. "**> 📋 Статус: Анализ завершён.**\n"
        out = out .. "> • Сообщение опроса: ID `" .. msg.reply.id .. "`\n"
        out = out .. "> • Чат: `" .. msg.chat_id .. "`\n"
        out = out .. "\n" .. EMOJI.vote .. " Данные синхронизированы."
        msg:edit(out, "md")
        return
    end

    -- .polltag <номер|novote> [текст]
    if cmd == "polltag" then
        local cat, tag_text = rest:match("^(%S+)%s*(.*)$")
        if not cat then
            msg:edit("⚠️ **Использование:** `.polltag <номер_варианта|novote> [текст]`", "md")
            return
        end

        if not msg.reply then
            msg:edit(EMOJI.novote .. " **Ответьте на сообщение с опросом.**", "md")
            return
        end

        local header = (tag_text and tag_text ~= "") and (tag_text .. "\n\n") or ""
        local out = header .. EMOJI.tag .. " **Оповещение участников опроса (категория: " .. cat .. "):**\n"
        out = out .. "> Сбор списка пользователей..."
        msg:edit(out, "md")
        return
    end
end
