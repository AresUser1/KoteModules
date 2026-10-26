-- moomod.lua
-- Модуль для взаимодействия с @moolokobot (MooBot).
-- Поддержка форматирования текста и Премиум Эмодзи.

name = "moomod"
description = "Взаимодействие с игрой @moolokobot (MooBot)"
version = "1.3.0"
author = "Kote"
min_host_version = 2

-- 💎 Настройки Премиум Эмодзи
local ICON_WAIT  = "[⏳](tg://emoji?id=5891211339170326418)"
local ICON_INFO  = "[🐮](tg://emoji?id=5393123749924984737)"
local ICON_ERROR = "[⚠️](tg://emoji?id=6030563507299160824)"

local MOO_BOT = "@moolokobot"

commands = {
    ["лес"]   = { doc = "Отправить /forest боту", usage = "" },
    ["таск"]  = { doc = "Отправить /task боту", usage = "" },
    ["крафт"] = { doc = "Отправить /craft боту", usage = "" },
    ["мун"]   = { doc = "Узнать количество муней", usage = "" },
    ["мукри"] = { doc = "Узнать количество кристаллов", usage = "" },
    ["мукр"]  = { doc = "Узнать баланс крови", usage = "" },
    ["бб"]    = { doc = "Узнать профиль теленка", usage = "" },
    ["мудон"] = { doc = "Узнать прайс доната", usage = "[число]" }
}

local function start_moo_request(msg, bot_cmd, title)
    -- 1. Показываем статус ожидания
    local wait_text = ICON_WAIT .. " **Запрос к " .. title .. "...**"
    msg:edit(wait_text, "md")

    -- 2. Запоминаем контекст запроса в локальной БД
    koto.db.set("pending_chat_id", tostring(msg.chat_id))
    koto.db.set("pending_msg_id", tostring(msg.id))
    koto.db.set("pending_title", title)
    koto.db.set("pending_time", tostring(os.time()))

    -- 3. Отправляем команду боту
    koto.send(MOO_BOT, bot_cmd)
end

function on_message(msg)
    local text = msg.text or ""

    -- Обработка команд пользователя
    if msg.out then
        local cmd, args = text:match("^[%.%!%/](%S+)%s*(.*)$")
        if cmd then
            cmd = string.lower(cmd)
            if cmd == "лес" or cmd == "forest" then
                start_moo_request(msg, "/forest", "Лес")
                return
            elseif cmd == "таск" or cmd == "task" then
                start_moo_request(msg, "/task", "Задание")
                return
            elseif cmd == "крафт" or cmd == "craft" then
                start_moo_request(msg, "/craft", "Крафт")
                return
            elseif cmd == "мун" then
                start_moo_request(msg, "мун", "Муни")
                return
            elseif cmd == "мукри" then
                start_moo_request(msg, "мукри", "Кристаллы")
                return
            elseif cmd == "мукр" then
                start_moo_request(msg, "мукр", "Кровь")
                return
            elseif cmd == "бб" then
                start_moo_request(msg, "бб", "Профиль")
                return
            elseif cmd == "мудон" then
                local bot_cmd = (args and args ~= "") and ("мудон " .. args) or "мудон"
                start_moo_request(msg, bot_cmd, "Донат")
                return
            end
        end
        return
    end

    -- Обработка ответа от @moolokobot
    local pending_chat = koto.db.get("pending_chat_id")
    if pending_chat and pending_chat ~= "" then
        local sender_uname = string.lower(msg:user_username() or "")
        -- Проверяем, что сообщение пришло от @moolokobot
        if sender_uname == "moolokobot" or msg.text:find("🐮") or msg.text:find("Корова") or msg.text:find("Телёнок") then
            local orig_chat = tonumber(pending_chat)
            local orig_msg = tonumber(koto.db.get("pending_msg_id") or "0")
            local title = koto.db.get("pending_title") or "MooBot"

            -- Очищаем состояние запроса
            koto.db.del("pending_chat_id")
            koto.db.del("pending_msg_id")
            koto.db.del("pending_title")
            koto.db.del("pending_time")

            -- Формируем красивый ответ
            local bot_reply = msg.text or ""
            local final_text = ICON_INFO .. " **" .. title .. ":**\n\n" .. bot_reply

            if orig_chat and orig_msg and orig_msg ~= 0 then
                koto.edit_message(orig_chat, orig_msg, final_text, "md")
            else
                koto.respond(final_text, "md")
            end

            -- Удаляем сообщение от бота в ЛС
            koto.delete_msg(msg.chat_id, msg.id)
            return
        end
    end
end
