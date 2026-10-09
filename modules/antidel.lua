-- /root/modules/antidel.lua
-- Модуль Anti-Delete & Edit Tracker для Kotogram на Lua
-- Поддерживает: .antidel on/off, .antidel pms on/off, .antidel log, .deleted, .history, .antidel copy <id>

name = "antidel"
description = "Мониторинг, кэширование и восстановление удалённых/отредактированных сообщений"
author = "SynForge"
version = "2.1.0"

-- Telegram Premium Emoji IDs
local EMOJI_EYE     = "5373059124407839352" -- 👁️
local EMOJI_SUCCESS = "5776375003280838798" -- ✅
local EMOJI_ERROR   = "5778527486270770928" -- ❌
local EMOJI_INFO    = "5879785854284599288" -- ℹ️
local EMOJI_NOTE    = "5373147814803024823" -- 📝
local EMOJI_BELL    = "5372999518038006461" -- 🔔

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

-- Кэширование сообщения в базу данных koto.db
local function cache_message(chat_id, msg_id, sender_id, text)
    if not chat_id or not msg_id then return end

    local pms_only = koto.db.get("antidel_pms_only") == "1"
    -- В Telegram peer_id > 0 это ЛС, peer_id < 0 это группы/каналы
    if pms_only and chat_id < 0 then
        return
    end

    local key = "ad_msg_" .. tostring(chat_id) .. "_" .. tostring(msg_id)
    local old_text = koto.db.get(key)

    -- Если сообщение уже было сохранено и текст изменился -> фиксируем правку (edit)
    if old_text and old_text ~= "" and old_text ~= text then
        local hist_key = "ad_edits_" .. tostring(chat_id) .. "_" .. tostring(msg_id)
        local hist = koto.db.get(hist_key) or ""
        local new_entry = "[" .. os.date("%H:%M:%S") .. "] " .. old_text
        if hist ~= "" then
            hist = hist .. "\n---\n" .. new_entry
        else
            hist = new_entry
        end
        koto.db.set(hist_key, hist)

        -- Если включена пересылка логов в Избранное
        local target = koto.db.get("antidel_log_target") or "saved"
        if target == "saved" and koto.copy then
            pcall(function() koto.copy(msg_id) end)
        end
    end

    koto.db.set(key, text or "")

    -- Добавляем ID в список недавних сообщений чата
    local list_key = "ad_recent_" .. tostring(chat_id)
    local raw_list = koto.db.get(list_key) or ""
    local ids = {}
    for mid in raw_list:gmatch("%S+") do
        if mid ~= tostring(msg_id) then
            table.insert(ids, mid)
        end
    end
    if #ids >= 100 then
        local oldest = table.remove(ids, 1)
        koto.db.del("ad_msg_" .. tostring(chat_id) .. "_" .. oldest)
    end
    table.insert(ids, tostring(msg_id))
    koto.db.set(list_key, table.concat(ids, " "))
end

function on_message(msg)
    local chat_id = msg.chat_id
    local msg_id = msg.id
    local sender_id = msg.sender_id
    local text = msg.text or ""

    local is_enabled = koto.db.get("antidel_enabled") ~= "0"

    -- 1. Кэширование всех входящих и исходящих сообщений
    if is_enabled and chat_id and msg_id and text ~= "" then
        cache_message(chat_id, msg_id, sender_id, text)
    end

    -- Обработка команд только от нашего аккаунта
    if not msg.out then return end

    local trimmed = text:match("^%s*(.-)%s*$")
    if trimmed == "" then return end

    -- .antidel on / off
    if trimmed == ".antidel on" then
        koto.db.set("antidel_enabled", "1")
        koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Anti-Delete & Edit Tracker включён!**\n" ..
                  "> Все сообщения и редакции фиксируются в локальном кэше.")
        return
    elseif trimmed == ".antidel off" then
        koto.db.set("antidel_enabled", "0")
        koto.edit(prem("🛑", EMOJI_ERROR) .. " **Anti-Delete & Edit Tracker отключён.**")
        return
    end

    -- .antidel pms on / off
    if trimmed:sub(1, 12) == ".antidel pms" then
        local mode = trimmed:sub(13):match("^%s*(%a+)%s*$")
        if mode == "on" then
            koto.db.set("antidel_pms_only", "1")
            koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Режим Anti-Delete:** `только ЛС` (группы игнорируются).")
            return
        elseif mode == "off" then
            koto.db.set("antidel_pms_only", "0")
            koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Режим Anti-Delete:** `все чаты` (ЛС и группы).")
            return
        else
            local cur = koto.db.get("antidel_pms_only") == "1" and "только ЛС" or "все чаты"
            koto.edit(prem("👁️", EMOJI_EYE) .. " **Фильтр чатов Anti-Delete:** `" .. cur .. "`\n" ..
                      "> Использование: `.antidel pms on` или `.antidel pms off`")
            return
        end
    end

    -- .antidel copy <id> / .restore <id>
    if trimmed:sub(1, 13) == ".antidel copy" or trimmed:sub(1, 8) == ".restore" then
        local arg = trimmed:match("%s+(%d+)%s*$")
        local target_id = tonumber(arg)
        if not target_id and msg.reply_to_msg_id and msg.reply_to_msg_id > 0 then
            target_id = msg.reply_to_msg_id
        end

        if not target_id then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Использование:** `.restore <ID>` или ответьте на сообщение.")
            return
        end

        koto.copy(target_id)
        local m_text = koto.db.get("ad_msg_" .. tostring(chat_id) .. "_" .. target_id)
        local text_snippet = m_text and ("\n> __" .. html_escape(m_text:sub(1, 120)) .. "__") or ""

        koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Сообщение #" .. target_id .. " переслано в Избранное!**" .. text_snippet)
        return
    end

    -- .antidel log <id/saved>
    if trimmed:sub(1, 12) == ".antidel log" then
        local target = trimmed:sub(13):match("^%s*(.-)%s*$")
        if target == "" or target == "saved" then
            koto.db.set("antidel_log_target", "saved")
            koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Логирование переключено в «Избранное» (Saved Messages).**")
        else
            koto.db.set("antidel_log_target", target)
            koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Логирование удалений установлено в канал/чат:** `" .. html_escape(target) .. "`")
        end
        return
    end

    -- .antidel help
    if trimmed == ".antidel" or trimmed == ".antidel help" then
        local status = koto.db.get("antidel_enabled") == "0" and "Выключен ❌" or "Включён ✅"
        local pms = koto.db.get("antidel_pms_only") == "1" and "Только ЛС" or "Все чаты"
        local target = koto.db.get("antidel_log_target") or "saved"
        koto.edit(prem("👁️", EMOJI_EYE) .. " **Ghost Anti-Delete & Edit Tracker:**\n" ..
                  "> • **Статус:** `" .. status .. "`\n" ..
                  "> • **Охват:** `" .. pms .. "`\n" ..
                  "> • **Назначение логов:** `" .. target .. "`\n" ..
                  ">\n" ..
                  "> • `.antidel on/off` — тумблер мониторинга\n" ..
                  "> • `.antidel pms on/off` — фильтр ЛС/группы\n" ..
                  "> • `.antidel log <id/saved>` — канал для логов\n" ..
                  "> • `.history` (по реплаю) — показать историю правок\n" ..
                  "> • `.deleted [N]` — показать последние зафиксированные\n" ..
                  "> • `.restore <ID>` — скопировать в Избранное (koto.copy)")
        return
    end

    -- .history (по реплаю на отредактированное сообщение)
    if trimmed == ".history" then
        local reply_id = msg.reply_to_msg_id
        if not reply_id or reply_id == 0 then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Ответьте на сообщение (.history), чтобы посмотреть историю правок.**")
            return
        end

        local hist_key = "ad_edits_" .. tostring(chat_id) .. "_" .. tostring(reply_id)
        local edits = koto.db.get(hist_key)

        if not edits or edits == "" then
            local current = koto.db.get("ad_msg_" .. tostring(chat_id) .. "_" .. tostring(reply_id))
            if current and current ~= "" then
                koto.edit(prem("ℹ️", EMOJI_INFO) .. " **История правок сообщения #" .. reply_id .. ":**\n" ..
                          "> Сообщение зафиксировано в кэше, но ещё не изменялось:\n__" .. html_escape(current) .. "__")
            else
                koto.edit(prem("❌", EMOJI_ERROR) .. " **История правок сообщения #" .. reply_id .. " не найдена в кэше.**")
            end
            return
        end

        koto.edit(prem("📝", EMOJI_NOTE) .. " **История правок сообщения #" .. reply_id .. ":**\n" ..
                  "> " .. html_escape(edits) .. "")
        return
    end

    -- .deleted [N]
    if trimmed:sub(1, 8) == ".deleted" then
        local count_str = trimmed:sub(9):match("^%s*(%d*)%s*$")
        local count = tonumber(count_str) or 5
        if count > 20 then count = 20 end

        local list_key = "ad_recent_" .. tostring(chat_id)
        local raw_list = koto.db.get(list_key) or ""
        local ids = {}
        for mid in raw_list:gmatch("%S+") do
            table.insert(ids, mid)
        end

        if #ids == 0 then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **В кэше чата пока нет сохранённых сообщений.**")
            return
        end

        local lines = {}
        local taken = 0
        for i = #ids, 1, -1 do
            local mid = ids[i]
            local m_text = koto.db.get("ad_msg_" .. tostring(chat_id) .. "_" .. mid)
            if m_text and m_text ~= "" then
                table.insert(lines, "• `#" .. mid .. "`: " .. html_escape(m_text:sub(1, 100)))
                taken = taken + 1
                if taken >= count then break end
            end
        end

        koto.edit(prem("👁️", EMOJI_EYE) .. " **Недавние зафиксированные сообщения чата:**\n" ..
                  "> " .. table.concat(lines, "\n") .. "\n" ..
                  "> __Для копирования в Избранное используйте:__ `.restore <ID>`")
        return
    end
end
