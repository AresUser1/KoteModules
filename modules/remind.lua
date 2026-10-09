-- /root/modules/remind.lua
-- Модуль планировщика напоминаний для Kotogram на Lua
-- Поддерживает: .remind <время> <текст>, .remind (по реплаю), .reminds, .delremind <id>

name = "remind"
description = "Умный планировщик напоминаний и отложенных задач"
author = "SynForge"
version = "2.1.0"

-- Telegram Premium Emoji IDs
local EMOJI_BELL    = "5372999518038006461" -- 🔔
local EMOJI_TIMER   = "5983150113483134607" -- ⏱️
local EMOJI_SUCCESS = "5776375003280838798" -- ✅
local EMOJI_ERROR   = "5778527486270770928" -- ❌
local EMOJI_INFO    = "5879785854284599288" -- ℹ️

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

-- Локальный кэш текста сообщений для работы с реплаями
local function cache_msg_text(chat_id, msg_id, text)
    if not chat_id or not msg_id or not text or text == "" then return end
    koto.db.set("rm_msg_" .. tostring(chat_id) .. "_" .. tostring(msg_id), text)
    local recent_key = "rm_recents_" .. tostring(chat_id)
    local raw = koto.db.get(recent_key) or ""
    local ids = {}
    for mid in raw:gmatch("%S+") do
        if mid ~= tostring(msg_id) then table.insert(ids, mid) end
    end
    if #ids >= 100 then
        local oldest = table.remove(ids, 1)
        koto.db.del("rm_msg_" .. tostring(chat_id) .. "_" .. oldest)
    end
    table.insert(ids, tostring(msg_id))
    koto.db.set(recent_key, table.concat(ids, " "))
end

local function get_cached_msg_text(chat_id, msg_id)
    if not chat_id or not msg_id then return nil end
    return koto.db.get("rm_msg_" .. tostring(chat_id) .. "_" .. tostring(msg_id))
end

-- Парсер смещения времени (относительное и абсолютное)
local function parse_time_offset(str)
    if not str then return nil end
    local total = 0
    for num, unit in str:gmatch("(%d+)([smhd])") do
        local n = tonumber(num)
        if unit == "s" then total = total + n
        elseif unit == "m" then total = total + n * 60
        elseif unit == "h" then total = total + n * 3600
        elseif unit == "d" then total = total + n * 86400
        end
    end
    if total > 0 then return total end

    local pure = tonumber(str)
    if pure then return pure * 60 end

    -- Формат времени HH:MM
    local h, m = str:match("^(%d%d?):(%d%d?)$")
    if h and m then
        local now = os.date("*t")
        local target = os.time({
            year = now.year,
            month = now.month,
            day = now.day,
            hour = tonumber(h),
            min = tonumber(m),
            sec = 0
        })
        if target <= os.time() then
            target = target + 86400
        end
        return target - os.time()
    end

    return nil
end

-- Список ID активных напоминаний
local function get_remind_ids()
    local raw = koto.db.get("remind_ids") or ""
    local ids = {}
    for id_str in raw:gmatch("%S+") do
        local id = tonumber(id_str)
        if id then table.insert(ids, id) end
    end
    return ids
end

local function save_remind_ids(ids)
    local parts = {}
    for _, id in ipairs(ids) do
        table.insert(parts, tostring(id))
    end
    koto.db.set("remind_ids", table.concat(parts, " "))
end

-- Проверка и срабатывание напоминаний
local function check_reminders(current_chat_id)
    local ids = get_remind_ids()
    if #ids == 0 then return end

    local now = os.time()
    local remaining = {}

    for _, id in ipairs(ids) do
        local key = "remind_item_" .. id
        local data = koto.db.get(key)
        if data then
            -- формат: target_ts:chat_id:text
            local ts_str, cid_str, r_text = data:match("^(%d+):([%-%d]+):(.*)$")
            local target_ts = tonumber(ts_str)
            local cid = tonumber(cid_str)

            if target_ts and target_ts <= now then
                -- Время напоминания наступило!
                local chat_hint = ""
                if current_chat_id and cid and current_chat_id ~= cid then
                    chat_hint = "\n__(Напоминание из чата #" .. cid .. ")__"
                end

                local alert = prem("🔔", EMOJI_BELL) .. " **Напоминание (ID #" .. id .. "):**\n" ..
                              "> " .. html_escape(r_text) .. "\n" ..
                              "> __Запланировано на " .. os.date("%H:%M:%S", target_ts) .. "__" .. chat_hint .. "\n" ..
                              "> __Отложить на 5 мин:__ `.remind 5m " .. html_escape(r_text) .. "`"
                koto.reply(alert)
                koto.db.del(key)
                koto.log("Напоминание #" .. id .. " успешно сработало")
            else
                table.insert(remaining, id)
            end
        end
    end

    if #remaining ~= #ids then
        save_remind_ids(remaining)
    end
end

function on_message(msg)
    local chat_id = msg.chat_id
    local msg_id = msg.id
    local text = msg.text or ""

    if chat_id and msg_id and text ~= "" then
        cache_msg_text(chat_id, msg_id, text)
    end

    -- Фоновая проверка таймеров на каждом сообщении
    check_reminders(chat_id)

    if not msg.out then return end

    local trimmed = text:match("^%s*(.-)%s*$")
    if trimmed == "" then return end

    -- .reminds (список активных напоминаний)
    if trimmed == ".reminds" or trimmed == ".remind list" then
        local ids = get_remind_ids()
        if #ids == 0 then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Активных напоминаний нет.**\n" ..
                      "> Создать: `.remind 15m Сделать перерыв`")
            return
        end

        local lines = {}
        for _, id in ipairs(ids) do
            local data = koto.db.get("remind_item_" .. id)
            if data then
                local ts_str, _, r_text = data:match("^(%d+):([%-%d]+):(.*)$")
                local target_ts = tonumber(ts_str)
                local diff = (target_ts or 0) - os.time()
                local diff_str = diff > 0 and (math.floor(diff / 60) .. " мин.") or "сейчас"
                table.insert(lines, "• `#" .. id .. "` [через " .. diff_str .. "]: " .. html_escape(r_text))
            end
        end

        koto.edit(prem("🔔", EMOJI_BELL) .. " **Активные напоминания (" .. #lines .. " шт.):**\n" ..
                  "> " .. table.concat(lines, "\n") .. "\n" ..
                  "> __Отменить:__ `.delremind <id>`")
        return
    end

    -- .delremind <id>
    if trimmed:sub(1, 10) == ".delremind" then
        local id_str = trimmed:sub(11):match("^%s*(%d+)%s*$")
        local del_id = tonumber(id_str)
        if not del_id then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Использование:** `.delremind <ID>`")
            return
        end

        koto.db.del("remind_item_" .. del_id)
        local ids = get_remind_ids()
        local remaining = {}
        for _, id in ipairs(ids) do
            if id ~= del_id then table.insert(remaining, id) end
        end
        save_remind_ids(remaining)

        koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Напоминание #" .. del_id .. " отменено.**")
        return
    end

    -- .remind <время> <текст>
    if trimmed:sub(1, 7) == ".remind" then
        local rest = trimmed:sub(8):match("^%s*(.-)%s*$")
        local time_str, r_text = rest:match("^(%S+)%s*(.*)$")

        if not time_str or time_str == "" then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Использование:** `.remind <время> <текст>`\n" ..
                      "> __Примеры:__\n" ..
                      "> • `.remind 10m Позвонить`\n" ..
                      "> • `.remind 1h30m Проверить сервер`\n" ..
                      "> • `.remind 18:00 Вебинар`")
            return
        end

        local offset = parse_time_offset(time_str)
        if not offset or offset <= 0 then
            koto.edit(prem("❌", EMOJI_ERROR) .. " **Неверный формат времени:** `" .. html_escape(time_str) .. "`")
            return
        end

        if (r_text == nil or r_text == "") and msg.reply_to_msg_id and msg.reply_to_msg_id > 0 then
            local cached = get_cached_msg_text(chat_id, msg.reply_to_msg_id)
            if cached and cached ~= "" then
                r_text = cached
            else
                r_text = "Напоминание по сообщению #" .. msg.reply_to_msg_id
            end
        end

        if r_text == nil or r_text == "" then
            r_text = "Напоминание!"
        end

        local seq = tonumber(koto.db.get("remind_seq") or "0") + 1
        koto.db.set("remind_seq", tostring(seq))

        local target_ts = os.time() + offset
        local item_data = tostring(target_ts) .. ":" .. tostring(msg.chat_id) .. ":" .. r_text
        koto.db.set("remind_item_" .. seq, item_data)

        local ids = get_remind_ids()
        table.insert(ids, seq)
        save_remind_ids(ids)

        local dur_str = (offset >= 3600) and (math.floor(offset / 3600) .. " ч. " .. math.floor((offset % 3600) / 60) .. " мин.") or (math.floor(offset / 60) .. " мин. " .. (offset % 60) .. " сек.")

        koto.edit(prem("🔔", EMOJI_BELL) .. " **Напоминание запланировано!**\n" ..
                  "> " ..
                  "• **ID:** `#" .. seq .. "`\n" ..
                  "> • **Сработает через:** `" .. dur_str .. "` (" .. os.date("%H:%M:%S", target_ts) .. ")\n" ..
                  "> • **Текст:** __" .. html_escape(r_text) .. "__" ..
                  "")
        return
    end
end
