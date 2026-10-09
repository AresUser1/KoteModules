-- /root/modules/spam.lua
-- Продвинутый спам-модуль на Lua для Kotogram
-- Поддерживает: .spam <кол-во> <текст>, .fastspam [текст], .stopspam, .spamhelp

name = "spam"
description = "Продвинутый спаммер с форматированием, премиум-эмодзи и отменой"
author = "SynForge"
version = "2.1.0"

-- Telegram Premium Emoji IDs
local EMOJI_ROCKET  = "5445284980978621387" -- 🚀
local EMOJI_SUCCESS = "5776375003280838798" -- ✅
local EMOJI_ERROR   = "5778527486270770928" -- ❌
local EMOJI_INFO    = "5879785854284599288" -- ℹ️
local EMOJI_STOP    = "6030563507299160824" -- 🛑
local EMOJI_FIRE    = "5431449001532594346" -- 🔥

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

function on_message(msg)
    if not msg.out then return end

    local text = msg.text or ""
    local trimmed = text:match("^%s*(.-)%s*$")
    if trimmed == "" then return end

    -- 1. Справка: .spamhelp
    if trimmed == ".spamhelp" or trimmed == ".spam help" then
        koto.edit(prem("🚀", EMOJI_ROCKET) .. " **Kotogram Lua Spammer:**\n" ..
                  "> " ..
                  "• `.spam <кол-во> <текст>` — отправить N сообщений\n" ..
                  "> • `.fastspam [текст]` — быстрая пачка из 5 сообщений\n" ..
                  "> • `.stopspam` — принудительно прервать активный спам" ..
                  "\n" ..
                  "> __Пример:__ `.spam 10 Привет мир!`")
        return
    end

    -- 2. Остановка: .stopspam
    if trimmed == ".stopspam" then
        local active = koto.db.get("active_spam")
        if active == "1" then
            koto.db.set("active_spam", "0")
            koto.delete()
            koto.respond(prem("🛑", EMOJI_STOP) .. " **Спам принудительно остановлен!**")
        else
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Активных задач спама не обнаружено.**")
        end
        return
    end

    -- 3. Быстрый спам: .fastspam [текст]
    if trimmed:sub(1, 9) == ".fastspam" then
        local user_text = trimmed:sub(10):match("^%s*(.-)%s*$")
        if user_text == "" then
            user_text = prem("🔥", EMOJI_FIRE) .. " **Fast Spam from Kotogram Lua!**"
        end

        koto.delete()
        koto.log("Lua fastspam запущен: 5 сообщений")

        for i = 1, 5 do
            koto.respond(user_text)
        end
        return
    end

    -- 4. Основная команда: .spam <кол-во> <текст>
    if trimmed:sub(1, 5) == ".spam" then
        local rest = trimmed:sub(6):match("^%s*(.-)%s*$")
        if rest == "" then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Использование:** `.spam <кол-во> <текст>`\n" ..
                      "> __Пример:__ `.spam 5 Привет мир`")
            return
        end

        -- Парсим первое число (количество)
        local count_str, spam_text = rest:match("^(%d+)%s*(.*)$")
        local count = tonumber(count_str)

        if not count then
            count = 10
            spam_text = rest
        end

        if spam_text == nil or spam_text == "" then
            spam_text = prem("🚀", EMOJI_ROCKET) .. " **Kotogram Spam Message**"
        end

        -- Ограничение безопасности для защиты аккаунта от бана
        if count > 100 then
            count = 100
        end

        koto.db.set("active_spam", "1")
        koto.db.set("spam_count", tostring(count))
        koto.log("Lua spam запущен: " .. tostring(count) .. " раз(а)")
        koto.delete()

        for i = 1, count do
            if koto.db.get("active_spam") == "0" then
                koto.log("Спам прерван на итерации " .. i)
                break
            end
            koto.respond(spam_text)
        end

        koto.db.set("active_spam", "0")
        return
    end
end
