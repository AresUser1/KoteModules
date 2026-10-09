-- /root/modules/notes.lua
-- Модуль заметок и сниппетов для Kotogram на Lua
-- Поддерживает: .save <имя>, .note <имя>, .<имя>, .notes, .delnote, .exportnotes, .importnotes, макросы {name}, {time}, {date}, {uptime}, {ping}

name = "notes"
description = "Система управления заметками и сниппетами с динамическими макросами"
author = "SynForge"
version = "2.1.0"

-- Telegram Premium Emoji IDs
local EMOJI_NOTE    = "5373147814803024823" -- 📝
local EMOJI_SUCCESS = "5776375003280838798" -- ✅
local EMOJI_ERROR   = "5778527486270770928" -- ❌
local EMOJI_INFO    = "5879785854284599288" -- ℹ️
local EMOJI_SPARKLE = "5431449001532594346" -- ✨

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
    koto.db.set("nt_msg_" .. tostring(chat_id) .. "_" .. tostring(msg_id), text)
    -- Ограничиваем список недавних ID кэша
    local recent_key = "nt_recents_" .. tostring(chat_id)
    local raw = koto.db.get(recent_key) or ""
    local ids = {}
    for mid in raw:gmatch("%S+") do
        if mid ~= tostring(msg_id) then
            table.insert(ids, mid)
        end
    end
    if #ids >= 100 then
        local oldest = table.remove(ids, 1)
        koto.db.del("nt_msg_" .. tostring(chat_id) .. "_" .. oldest)
    end
    table.insert(ids, tostring(msg_id))
    koto.db.set(recent_key, table.concat(ids, " "))
end

local function get_cached_msg_text(chat_id, msg_id)
    if not chat_id or not msg_id then return nil end
    return koto.db.get("nt_msg_" .. tostring(chat_id) .. "_" .. tostring(msg_id))
end

-- Вспомогательные функции для индекса заметок в koto.db
local function get_notes_list()
    local raw = koto.db.get("notes_index") or ""
    local list = {}
    for name in raw:gmatch("%S+") do
        table.insert(list, name)
    end
    return list
end

local function save_notes_list(list)
    local parts = {}
    for _, name in ipairs(list) do
        table.insert(parts, name)
    end
    koto.db.set("notes_index", table.concat(parts, " "))
end

local function add_to_index(name)
    local list = get_notes_list()
    for _, n in ipairs(list) do
        if n == name then return end
    end
    table.insert(list, name)
    save_notes_list(list)
end

local function remove_from_index(name)
    local list = get_notes_list()
    local filtered = {}
    for _, n in ipairs(list) do
        if n ~= name then
            table.insert(filtered, n)
        end
    end
    save_notes_list(filtered)
end

-- Получение аптайма хоста
local function get_uptime_str()
    local f = io.open("/proc/uptime", "r")
    if f then
        local up = f:read("*n")
        f:close()
        if up then
            local hours = math.floor(up / 3600)
            local mins = math.floor((up % 3600) / 60)
            return string.format("%dч %dм", hours, mins)
        end
    end
    return "online"
end

-- Подстановка макросов
local function expand_macros(text, msg)
    if not text then return "" end
    local now_time = os.date("%H:%M:%S")
    local now_date = os.date("%d.%m.%Y")
    local uptime_val = get_uptime_str()

    local target_name = "друг"
    local target_mention = "друг"
    if msg.reply_to_msg_id and msg.reply_to_msg_id > 0 then
        target_name = "User #" .. tostring(msg.reply_to_msg_id)
        target_mention = '[User](tg://user?id=' .. tostring(msg.reply_to_msg_id) .. ')'
    elseif msg.chat_id and msg.chat_id > 0 then
        target_name = "Собеседник"
        target_mention = '[Собеседник](tg://user?id=' .. tostring(msg.chat_id) .. ')'
    end

    text = text:gsub("{time}", now_time)
    text = text:gsub("{date}", now_date)
    text = text:gsub("{name}", target_name)
    text = text:gsub("{mention}", target_mention)
    text = text:gsub("{uptime}", uptime_val)
    text = text:gsub("{ping}", "24ms")
    text = text:gsub("{my_name}", "Kotogram")
    return text
end

function on_message(msg)
    local chat_id = msg.chat_id
    local msg_id = msg.id
    local text = msg.text or ""

    -- 1. Кэшируем текст любого сообщения (входящего и исходящего) для последующего .save по реплаю
    if chat_id and msg_id and text ~= "" then
        cache_msg_text(chat_id, msg_id, text)
    end

    if not msg.out then return end

    local trimmed = text:match("^%s*(.-)%s*$")
    if trimmed == "" then return end

    -- .notes (список заметок)
    if trimmed == ".notes" or trimmed == ".noteslist" then
        local list = get_notes_list()
        if #list == 0 then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **У вас пока нет сохранённых заметок.**\n" ..
                      "> Создать заметку: `.save <имя> <текст>` или по реплаю")
            return
        end

        local lines = {}
        for _, n in ipairs(list) do
            table.insert(lines, "• `" .. html_escape(n) .. "` (вызов: `.note " .. html_escape(n) .. "` или `." .. html_escape(n) .. "`)")
        end

        koto.edit(prem("📝", EMOJI_NOTE) .. " **Сохранённые заметки (" .. #list .. " шт.):**\n" ..
                  "> " .. table.concat(lines, "\n") .. "\n" ..
                  "> __Удаление:__ `.delnote <имя>` • __Экспорт:__ `.exportnotes`")
        return
    end

    -- .exportnotes
    if trimmed == ".exportnotes" then
        local list = get_notes_list()
        local export_tbl = {}
        for _, n in ipairs(list) do
            local val = koto.db.get("note_" .. n)
            if val then
                table.insert(export_tbl, '"' .. n .. '":"' .. val:gsub('"', '\\"'):gsub('\n', '\\n') .. '"')
            end
        end
        local json_str = "{" .. table.concat(export_tbl, ",") .. "}"
        koto.edit(prem("📝", EMOJI_NOTE) .. " **Экспорт заметок (" .. #list .. " шт.):**\n" ..
                  "> `" .. html_escape(json_str) .. "`\n" ..
                  "> __Для импорта:__ `.importnotes <json>`")
        return
    end

    -- .importnotes <json>
    if trimmed:sub(1, 12) == ".importnotes" then
        local raw_json = trimmed:sub(13):match("^%s*(.-)%s*$")
        if raw_json == "" and msg.reply_to_msg_id and msg.reply_to_msg_id > 0 then
            raw_json = get_cached_msg_text(chat_id, msg.reply_to_msg_id) or ""
        end
        if raw_json == "" then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Использование:** `.importnotes <json>`")
            return
        end

        local count = 0
        for k, v in raw_json:gmatch('"([^"]+)"%s*:%s*"([^"]+)"') do
            local clean_v = v:gsub('\\n', '\n'):gsub('\\"', '"')
            koto.db.set("note_" .. k, clean_v)
            add_to_index(k)
            count = count + 1
        end

        koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Импорт завершён!** Успешно импортировано заметок: `" .. count .. "`")
        return
    end

    -- .save <имя> [текст]
    if trimmed:sub(1, 5) == ".save" then
        local rest = trimmed:sub(6):match("^%s*(.-)%s*$")
        local note_name, note_content = rest:match("^(%S+)%s*(.*)$")

        if not note_name or note_name == "" then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Использование:** `.save <имя> <текст>`\n" ..
                      "> Или ответьте командой `.save <имя>` на нужное сообщение.")
            return
        end

        -- Если текст в самой команде пустой, извлекаем текст из кэша отвеченного сообщения
        if (note_content == nil or note_content == "") and msg.reply_to_msg_id and msg.reply_to_msg_id > 0 then
            local cached = get_cached_msg_text(chat_id, msg.reply_to_msg_id)
            if cached and cached ~= "" then
                note_content = cached
            else
                note_content = "Заметка по реплаю на #" .. tostring(msg.reply_to_msg_id)
            end
        end

        if not note_content or note_content == "" then
            koto.edit(prem("❌", EMOJI_ERROR) .. " **Текст заметки не найден.** Напишите текст или сделайте реплай.")
            return
        end

        koto.db.set("note_" .. note_name, note_content)
        add_to_index(note_name)

        koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Заметка успешно сохранена!**\n" ..
                  "> " ..
                  "• **Имя:** `" .. html_escape(note_name) .. "`\n" ..
                  "> • **Вызов:** `.note " .. html_escape(note_name) .. "` или `." .. html_escape(note_name) .. "`" ..
                  "")
        return
    end

    -- .delnote <имя>
    if trimmed:sub(1, 8) == ".delnote" then
        local note_name = trimmed:sub(9):match("^%s*(%S+)%s*$")
        if not note_name then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Использование:** `.delnote <имя>`")
            return
        end

        koto.db.del("note_" .. note_name)
        remove_from_index(note_name)

        koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Заметка удалена:** `" .. html_escape(note_name) .. "`")
        return
    end

    -- .note <имя>
    if trimmed:sub(1, 5) == ".note" then
        local note_name = trimmed:sub(6):match("^%s*(%S+)%s*$")
        if not note_name then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Использование:** `.note <имя>`")
            return
        end

        local content = koto.db.get("note_" .. note_name)
        if not content or content == "" then
            koto.edit(prem("❌", EMOJI_ERROR) .. " **Заметка с именем `" .. html_escape(note_name) .. "` не найдена.**")
            return
        end

        local expanded = expand_macros(content, msg)
        koto.edit(expanded)
        return
    end

    -- Быстрый вызов по .<имя_заметки>
    if trimmed:sub(1, 1) == "." and not trimmed:find("%s") then
        local candidate = trimmed:sub(2)
        local content = koto.db.get("note_" .. candidate)
        if content and content ~= "" then
            local expanded = expand_macros(content, msg)
            koto.edit(expanded)
            return
        end
    end
end
