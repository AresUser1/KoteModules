-- /root/modules/tt_dl.lua
-- TikTok Downloader модуль для Kotogram на Lua
-- Поддержка: .tt <ссылка>, .tt (по реплаю), .tt caption full/short

name = "tt_dl"
description = "Скачивание видео и фото из TikTok без водяного знака через TikWM API"
author = "SynForge"
version = "2.1.0"

-- Telegram Premium Emoji IDs
local EMOJI_DOWNLOAD = "5406726206101919864" -- 📥
local EMOJI_VIDEO    = "5370697920704159828" -- 🎬
local EMOJI_SUCCESS  = "5776375003280838798" -- ✅
local EMOJI_ERROR    = "5778527486270770928" -- ❌
local EMOJI_INFO     = "5879785854284599288" -- ℹ️
local EMOJI_GEAR     = "6032742198179532882" -- ⚙️

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

local function url_encode(str)
    if not str then return "" end
    return str:gsub("([^%w%-%_%.%~])", function(c)
        return string.format("%%%02X", string.byte(c))
    end)
end

-- Локальный кэш текста сообщений для работы с реплаями
local function cache_msg_text(chat_id, msg_id, text)
    if not chat_id or not msg_id or not text or text == "" then return end
    koto.db.set("tt_msg_" .. tostring(chat_id) .. "_" .. tostring(msg_id), text)
    local recent_key = "tt_recents_" .. tostring(chat_id)
    local raw = koto.db.get(recent_key) or ""
    local ids = {}
    for mid in raw:gmatch("%S+") do
        if mid ~= tostring(msg_id) then table.insert(ids, mid) end
    end
    if #ids >= 50 then
        local oldest = table.remove(ids, 1)
        koto.db.del("tt_msg_" .. tostring(chat_id) .. "_" .. oldest)
    end
    table.insert(ids, tostring(msg_id))
    koto.db.set(recent_key, table.concat(ids, " "))
end

local function get_cached_msg_text(chat_id, msg_id)
    if not chat_id or not msg_id then return nil end
    return koto.db.get("tt_msg_" .. tostring(chat_id) .. "_" .. tostring(msg_id))
end

-- Корректный парсинг строковых значений JSON с учётом экранированных кавычек \"
local function json_get_string(json, key)
    local needle = '"' .. key .. '"%s*:%s*"'
    local s_start, s_end = json:find(needle)
    if not s_end then return "" end

    local chars = {}
    local i = s_end + 1
    local len = #json
    local escaped = false

    while i <= len do
        local c = json:sub(i, i)
        if escaped then
            if c == '"' then table.insert(chars, '"')
            elseif c == '\\' then table.insert(chars, '\\')
            elseif c == '/' then table.insert(chars, '/')
            elseif c == 'n' then table.insert(chars, '\n')
            elseif c == 'r' then -- ignore
            elseif c == 't' then table.insert(chars, '\t')
            else
                table.insert(chars, '\\')
                table.insert(chars, c)
            end
            escaped = false
        elseif c == '\\' then
            escaped = true
        elseif c == '"' then
            break
        else
            table.insert(chars, c)
        end
        i = i + 1
    end

    return table.concat(chars)
end

local function json_get_number(json, key)
    local pat = '"' .. key .. '"%s*:%s*(%-?%d+)'
    local val = json:match(pat)
    return tonumber(val) or 0
end

local function json_get_images(json)
    local images = {}
    local arr_content = json:match('"images"%s*:%s*%[(.-)%]')
    if arr_content then
        for img_url in arr_content:gmatch('"(.-)"') do
            local clean = img_url:gsub('\\/', '/')
            table.insert(images, clean)
        end
    end
    return images
end

function on_message(msg)
    local chat_id = msg.chat_id
    local msg_id = msg.id
    local text = msg.text or ""

    if chat_id and msg_id and text ~= "" then
        cache_msg_text(chat_id, msg_id, text)
    end

    if not msg.out then return end

    local trimmed = text:match("^%s*(.-)%s*$")
    if trimmed == "" then return end

    -- Проверка команды .tt
    if trimmed:sub(1, 3) ~= ".tt" then return end

    local rest = trimmed:sub(4):match("^%s*(.-)%s*$")

    -- Подкоманда .tt caption full / short
    if rest:sub(1, 7) == "caption" then
        local mode = rest:sub(8):match("^%s*(%a+)%s*$")
        if mode then mode = mode:lower() end

        if mode == "full" then
            koto.db.set("caption_full", "1")
            koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Режим подписи TikTok:** `полный` (название + кредиты)")
            return
        elseif mode == "short" then
            koto.db.set("caption_full", "0")
            koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Режим подписи TikTok:** `краткий` (только ссылки)")
            return
        else
            local cur = koto.db.get("caption_full") == "0" and "short" or "full"
            koto.edit(prem("⚙️", EMOJI_GEAR) .. " **Текущий режим подписи:** `" .. cur .. "`\n" ..
                      "> Использование: `.tt caption full` или `.tt caption short`")
            return
        end
    end

    -- Если ссылка в команде или в отвеченном сообщении
    local url = rest:match("(https?://%S+)")
    if not url and msg.reply_to_msg_id and msg.reply_to_msg_id > 0 then
        local reply_text = get_cached_msg_text(chat_id, msg.reply_to_msg_id) or ""
        url = reply_text:match("(https?://%S+)")
    end

    if not url then
        koto.edit(prem("ℹ️", EMOJI_INFO) .. " **TikTok Downloader:**\n" ..
                  "> " ..
                  "• Использование: `.tt <ссылка на TikTok>`\n" ..
                  "> • Или ответьте `.tt` на сообщение с ссылкой\n" ..
                  "> • Настройка подписи: `.tt caption full / short`" ..
                  "")
        return
    end

    koto.edit(prem("📥", EMOJI_DOWNLOAD) .. " **Загрузка данных из TikTok...**")

    -- Запрос к TikWM API через встроенный HTTP-клиент Kotogram
    local api_url = "https://www.tikwm.com/api/?url=" .. url_encode(url)
    local json_resp = koto.http_get(api_url)

    if not json_resp or json_resp == "" then
        koto.edit(prem("❌", EMOJI_ERROR) .. " **Пустой ответ от API TikWM. Проверьте соединение.**")
        return
    end

    local code = json_get_number(json_resp, "code")
    if code ~= 0 then
        local err_msg = json_get_string(json_resp, "msg")
        if err_msg == "" then err_msg = "Ошибка распознавания ссылки TikTok" end
        koto.edit(prem("❌", EMOJI_ERROR) .. " **Ошибка API:**\n> " .. html_escape(err_msg) .. "")
        return
    end

    local title = json_get_string(json_resp, "title")
    local play_url = json_get_string(json_resp, "play")
    local wm_url = json_get_string(json_resp, "wmplay")
    local author = json_get_string(json_resp, "nickname")
    local duration = json_get_number(json_resp, "duration")
    local images = json_get_images(json_resp)

    local is_full = koto.db.get("caption_full") ~= "0"

    -- Если это слайд-шоу картинок
    if #images > 0 then
        local img_links = {}
        for i = 1, math.min(#images, 8) do
            table.insert(img_links, '[Фото ' .. i .. '](' .. images[i] .. ')')
        end

        local text_out = prem("🎬", EMOJI_VIDEO) .. " **TikTok Фотоальбом:**\n"
        if is_full and title ~= "" then
            text_out = text_out .. "> " .. html_escape(title) .. "\n"
        end
        text_out = text_out .. "**Кадры (" .. #images .. " шт.):** " .. table.concat(img_links, " • ") .. "\n" ..
                   prem("📥", EMOJI_DOWNLOAD) .. " __Скачано через Kotogram__"
        koto.edit(text_out)
        return
    end

    -- Если это видео
    local dl_link = play_url ~= "" and play_url or wm_url
    if dl_link == "" then
        koto.edit(prem("❌", EMOJI_ERROR) .. " **Не найдена прямая ссылка на видео в ответе TikWM.**")
        return
    end

    local text_out = prem("🎬", EMOJI_VIDEO) .. " **TikTok Video:**\n"
    if is_full and title ~= "" then
        text_out = text_out .. "> " .. html_escape(title) .. "\n"
    end

    text_out = text_out ..
               "• **Автор:** `" .. html_escape(author ~= "" and author or "Неизвестен") .. "`\n" ..
               "> • **Длительность:** `" .. duration .. " сек.`\n\n" ..
               prem("📥", EMOJI_DOWNLOAD) .. ' [**[Скачать MP4 без водяного знака]**](' .. dl_link .. ')\n' ..
               "> __Kotogram TT Engine__"

    koto.edit(text_out)
    koto.log("TikTok контент успешно загружен: " .. url)
end
