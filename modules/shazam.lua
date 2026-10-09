-- /root/modules/shazam.lua
-- Модуль Shazam & Music Recognizer для Kotogram на Lua
-- Поддерживает: .music <запрос>, .lyrics <артист - трек>, .shazam (по реплаю)

name = "shazam"
description = "Поиск музыки, текстов песен и распознавание треков"
author = "SynForge"
version = "2.1.0"

-- Telegram Premium Emoji IDs
local EMOJI_MUSIC   = "5370697920704159828" -- 🎵
local EMOJI_SPARKLE = "5431449001532594346" -- ✨
local EMOJI_SUCCESS = "5776375003280838798" -- ✅
local EMOJI_ERROR   = "5778527486270770928" -- ❌
local EMOJI_INFO    = "5879785854284599288" -- ℹ️
local EMOJI_DL      = "5406726206101919864" -- 📥

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

-- Локальный кэш текста сообщений для распознавания по реплаю
local function cache_msg_text(chat_id, msg_id, text)
    if not chat_id or not msg_id or not text or text == "" then return end
    koto.db.set("sh_msg_" .. tostring(chat_id) .. "_" .. tostring(msg_id), text)
    local recent_key = "sh_recents_" .. tostring(chat_id)
    local raw = koto.db.get(recent_key) or ""
    local ids = {}
    for mid in raw:gmatch("%S+") do
        if mid ~= tostring(msg_id) then table.insert(ids, mid) end
    end
    if #ids >= 100 then
        local oldest = table.remove(ids, 1)
        koto.db.del("sh_msg_" .. tostring(chat_id) .. "_" .. oldest)
    end
    table.insert(ids, tostring(msg_id))
    koto.db.set(recent_key, table.concat(ids, " "))
end

local function get_cached_msg_text(chat_id, msg_id)
    if not chat_id or not msg_id then return nil end
    return koto.db.get("sh_msg_" .. tostring(chat_id) .. "_" .. tostring(msg_id))
end

-- Поиск трека в Apple iTunes Music API
local function search_track(query)
    if not query or query == "" then return nil end
    local url = "https://itunes.apple.com/search?term=" .. url_encode(query) .. "&entity=song&limit=1"
    local resp = koto.http_get(url)
    if not resp or resp == "" then return nil end

    local track_name = resp:match('"trackName"%s*:%s*"(.-)"')
    local artist_name = resp:match('"artistName"%s*:%s*"(.-)"')
    local collection_name = resp:match('"collectionName"%s*:%s*"(.-)"')
    local preview_url = resp:match('"previewUrl"%s*:%s*"(.-)"')
    local track_url = resp:match('"trackViewUrl"%s*:%s*"(.-)"')
    local release_date = resp:match('"releaseDate"%s*:%s*"(%d%d%d%d)')
    local millis_str = resp:match('"trackTimeMillis"%s*:%s*(%d+)')
    local millis = tonumber(millis_str) or 0
    local duration_sec = math.floor(millis / 1000)

    if not track_name then return nil end

    if preview_url then
        preview_url = preview_url:gsub('\\/', '/')
    end
    if track_url then
        track_url = track_url:gsub('\\/', '/')
    end

    return {
        track = track_name,
        artist = artist_name or "Неизвестен",
        album = collection_name or "Сингл",
        preview = preview_url,
        url = track_url,
        year = release_date or "2024",
        duration = string.format("%d:%02d", math.floor(duration_sec / 60), duration_sec % 60)
    }
end

-- Поиск текста песни в lyrics.ovh
local function search_lyrics(artist, song)
    local url = "https://api.lyrics.ovh/v1/" .. url_encode(artist) .. "/" .. url_encode(song)
    local resp = koto.http_get(url)
    if not resp or resp == "" then return nil end

    local lyrics = resp:match('"lyrics"%s*:%s*"(.-)"')
    if not lyrics then return nil end

    lyrics = lyrics:gsub('\\r\\n', '\n'):gsub('\\n', '\n'):gsub('\\"', '"')
    return lyrics
end

function on_message(msg)
    local chat_id = msg.chat_id
    local msg_id = msg.id
    local text = msg.text or ""

    -- 1. Кэшируем сообщения для работы реплаев
    if chat_id and msg_id and text ~= "" then
        cache_msg_text(chat_id, msg_id, text)
    end

    if not msg.out then return end

    local trimmed = text:match("^%s*(.-)%s*$")
    if trimmed == "" then return end

    -- .music <запрос>
    if trimmed:sub(1, 6) == ".music" then
        local query = trimmed:sub(7):match("^%s*(.-)%s*$")
        if query == "" then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Использование:** `.music <название трека / исполнитель>`\n" ..
                      "> __Пример:__ `.music The Weeknd Blinding Lights`")
            return
        end

        koto.edit(prem("🎵", EMOJI_MUSIC) .. " **Поиск музыки:** __" .. html_escape(query) .. "...__")
        local info = search_track(query)

        if not info then
            koto.edit(prem("❌", EMOJI_ERROR) .. " **Трек не найден по запросу:** `" .. html_escape(query) .. "`")
            return
        end

        local card = prem("🎵", EMOJI_MUSIC) .. " **" .. html_escape(info.artist) .. " — " .. html_escape(info.track) .. "**\n" ..
                     "> " ..
                     "• **Альбом:** " .. html_escape(info.album) .. "\n" ..
                     "> • **Год:** `" .. info.year .. "`\n" ..
                     "> • **Длительность:** `" .. info.duration .. "`" ..
                     "\n"

        if info.preview then
            card = card .. prem("📥", EMOJI_DL) .. ' [**[Прослушать MP3 отрывок]**](' .. info.preview .. ') • '
        end
        if info.url then
            card = card .. '[**[Apple Music]**](' .. info.url .. ')\n'
        else
            card = card .. "\n"
        end
        card = card .. "__Поиск текста:__ `.lyrics " .. html_escape(info.artist) .. " - " .. html_escape(info.track) .. "`"

        koto.edit(card)
        return
    end

    -- .lyrics <артист - песня>
    if trimmed:sub(1, 7) == ".lyrics" then
        local query = trimmed:sub(8):match("^%s*(.-)%s*$")
        local artist, song = "", ""

        if query == "" and msg.reply_to_msg_id and msg.reply_to_msg_id > 0 then
            query = get_cached_msg_text(chat_id, msg.reply_to_msg_id) or ""
        end

        if query:find("%-") then
            artist, song = query:match("^(.-)%s*-%s*(.+)$")
        else
            artist = query
            song = query
        end

        if not artist or artist == "" then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Использование:** `.lyrics <Исполнитель - Трек>`")
            return
        end

        koto.edit(prem("🎵", EMOJI_MUSIC) .. " **Поиск текста:** __" .. html_escape(artist) .. " - " .. html_escape(song) .. "...__")
        local l_text = search_lyrics(artist, song)

        if not l_text or l_text == "" then
            -- Пробуем найти через iTunes точное название трека
            local info = search_track(query)
            if info then
                l_text = search_lyrics(info.artist, info.track)
            end
        end

        if not l_text or l_text == "" then
            koto.edit(prem("❌", EMOJI_ERROR) .. " **Текст песни не найден.** Проверьте правильность написания.")
            return
        end

        -- Ограничение длины для вывода в Telegram
        if #l_text > 3500 then
            l_text = l_text:sub(1, 3500) .. "\n\n...(текст сокращён)..."
        end

        local card = prem("🎵", EMOJI_MUSIC) .. " **Текст песни: " .. html_escape(artist) .. " — " .. html_escape(song) .. "**\n" ..
                     "> " .. html_escape(l_text) .. ""
        koto.edit(card)
        return
    end

    -- .shazam (по реплаю)
    if trimmed:sub(1, 7) == ".shazam" then
        local reply_id = msg.reply_to_msg_id
        local is_dl = trimmed:find("%-d") ~= nil

        -- Извлекаем запрос из аргументов или текста отвеченного сообщения
        local query = trimmed:sub(8):gsub("%s*%-d%s*", ""):match("^%s*(.-)%s*$")

        if query == "" and reply_id and reply_id > 0 then
            local cached = get_cached_msg_text(chat_id, reply_id)
            if cached and cached ~= "" then
                -- Если в сообщении есть ссылка или поисковая фраза
                query = cached:match("https?://%S+") or cached
            end
        end

        if query == "" then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Распознавание музыки (.shazam):**\n" ..
                      "> " ..
                      "Ответьте командой на аудио/видео с подписью или укажите запрос:\n" ..
                      "> • `.shazam <строка из трека / ссылка>`\n" ..
                      "> • `.shazam -d <запрос>` — со ссылкой на MP3\n" ..
                      "> • `.music <название>` — поиск по каталогу" ..
                      "")
            return
        end

        koto.edit(prem("✨", EMOJI_SPARKLE) .. " **Распознавание трека...**")

        local info = search_track(query)
        if not info then
            koto.edit(prem("❌", EMOJI_ERROR) .. " **Shazam: трек не найден по запросу:** `" .. html_escape(query) .. "`")
            return
        end

        local card = prem("✨", EMOJI_SPARKLE) .. " **Shazam распознал трек:**\n" ..
                     "> " ..
                     "🎵 **" .. html_escape(info.artist) .. " — " .. html_escape(info.track) .. "**\n" ..
                     "> • **Альбом:** " .. html_escape(info.album) .. " (" .. info.year .. ")\n" ..
                     "> • **Длительность:** `" .. info.duration .. "`" ..
                     "\n"

        if is_dl and info.preview then
            card = card .. prem("📥", EMOJI_DL) .. ' **[[Скачать полный MP3 отрывок]](' .. info.preview .. ')**'
        elseif info.preview then
            card = card .. prem("📥", EMOJI_DL) .. ' [[Слушать превью]](' .. info.preview .. ')'
        end

        koto.edit(card)
        return
    end
end
