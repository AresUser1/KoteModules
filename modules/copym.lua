-- copym.lua
-- Модуль для копирования сообщений (включая защищенный контент) в «Избранное».
-- Автор: Kote / Kotogram Lua ABI

name = "copym"
description = "Копирование сообщений (в т.ч. защищенных) в Избранное"
author = "Kote"
version = "1.2.0"
update_url = "https://raw.githubusercontent.com/AresUser1/Kotogram/main/copym.lua"

-- Список команд модуля для отображения в .help и .modules
commands = {
    copy = { doc = "Копирование сообщений (в т.ч. защищенных) в Избранное", usage = "[ответом на сообщение]" },
    hcopy = { doc = "Тихое копирование сообщений в Избранное без лишних сообщений", usage = "[ответом на сообщение]" }
}

function on_message(msg)
    if not msg.out then return end

    local text = msg.text or ""
    -- Ищем команду .copy или .hcopy (регистронезависимо)
    local lower_text = string.lower(text)
    local is_copy = lower_text:sub(1, 5) == ".copy"
    local is_hcopy = lower_text:sub(1, 6) == ".hcopy"

    if not is_copy and not is_hcopy then return end

    local silent = is_hcopy
    local reply_id = msg.reply_to_msg_id

    if not reply_id or reply_id == 0 then
        if not silent then
            koto.edit("❌ Ответьте на сообщение, которое нужно сохранить.")
        else
            koto.delete()
        end
        return
    end

    if silent then
        koto.delete()
        koto.copy(reply_id)
        koto.log("Тихое копирование сообщения #" .. reply_id .. " в Избранное")
    else
        koto.edit("⌛️ Копирую сообщение в Избранное...")
        koto.copy(reply_id)
        koto.edit("✅ Сообщение скопировано в Избранное!")
        koto.log("Копирование сообщения #" .. reply_id .. " в Избранное выполнено")
    end
end
