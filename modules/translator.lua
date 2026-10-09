-- /root/modules/translator.lua
-- Модуль нейропереводчика для Kotogram на Lua
-- Поддерживает: .tr [lang] (по реплаю), .tr <lang> <текст>, .autotr [lang|off], .detect

name = "translator"
description = "Мгновенный переводчик сообщений с поддержкой автоперевода чата"
author = "SynForge"
version = "2.1.0"

-- Telegram Premium Emoji IDs
local EMOJI_GLOBE   = "5370956966833167156" -- 🌐
local EMOJI_SUCCESS = "5776375003280838798" -- ✅
local EMOJI_ERROR   = "5778527486270770928" -- ❌
local EMOJI_INFO    = "5879785854284599288" -- ℹ️
local EMOJI_DETECT  = "5373059124407839352" -- 👁️

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

-- Локальный кэш сообщений для переводчика
local function cache_msg_text(chat_id, msg_id, text)
    if not chat_id or not msg_id or not text or text == "" then return end
    koto.db.set("tr_msg_" .. tostring(chat_id) .. "_" .. tostring(msg_id), text)
    local recent_key = "tr_recents_" .. tostring(chat_id)
    local raw = koto.db.get(recent_key) or ""
    local ids = {}
    for mid in raw:gmatch("%S+") do
        if mid ~= tostring(msg_id) then table.insert(ids, mid) end
    end
    if #ids >= 100 then
        local oldest = table.remove(ids, 1)
        koto.db.del("tr_msg_" .. tostring(chat_id) .. "_" .. oldest)
    end
    table.insert(ids, tostring(msg_id))
    koto.db.set(recent_key, table.concat(ids, " "))
end

local function get_cached_msg_text(chat_id, msg_id)
    if not chat_id or not msg_id then return nil end
    return koto.db.get("tr_msg_" .. tostring(chat_id) .. "_" .. tostring(msg_id))
end

-- Декодер юникодных последовательностей \uXXXX в UTF-8
local function decode_unicode(str)
    if not str then return "" end
    local res = str:gsub("\\u(%x%x%x%x)", function(hex)
        local n = tonumber(hex, 16)
        if n < 0x80 then
            return string.char(n)
        elseif n < 0x800 then
            return string.char(0xC0 + math.floor(n / 64), 0x80 + (n % 64))
        else
            return string.char(0xE0 + math.floor(n / 4096), 0x80 + (math.floor(n / 64) % 64), 0x80 + (n % 64))
        end
    end)
    res = res:gsub('\\"', '"'):gsub('\\n', '\n'):gsub('\\/', '/')
    return res
end

-- Проверка наличия кириллицы в тексте
local function has_cyrillic(str)
    if not str then return false end
    -- В UTF-8 кириллические символы начинаются с байтов 0xD0 или 0xD1
    for i = 1, #str - 1 do
        local b1 = str:byte(i)
        local b2 = str:byte(i + 1)
        if (b1 == 0xD0 and b2 >= 0x80 and b2 <= 0xBF) or
           (b1 == 0xD1 and b2 >= 0x80 and b2 <= 0x9F) then
            return true
        end
    end
    return false
end

-- Сетевой запрос на перевод
local function translate_text(text, target_lang, source_lang)
    target_lang = (target_lang or "ru"):lower()
    local is_cyr = has_cyrillic(text)

    -- Если язык не указан явно или равен "ru", но текст уже на кириллице -> переводим на английский
    if target_lang == "ru" and is_cyr then
        target_lang = "en"
        source_lang = "ru"
    end

    if not source_lang or source_lang == "autodetect" then
        if is_cyr then
            source_lang = "ru"
        else
            source_lang = "autodetect"
        end
    end

    local query = url_encode(text)
    local pair = source_lang .. "|" .. target_lang
    local url = "https://api.mymemory.translated.net/get?q=" .. query .. "&langpair=" .. pair

    local resp = koto.http_get(url)
    if not resp or resp == "" then
        return nil, "Пустой ответ от сервера перевода"
    end

    local translated = resp:match('"translatedText"%s*:%s*"(.-)"')
    if not translated then
        return nil, "Не удалось разобрать перевод"
    end

    local clean = decode_unicode(translated)
    if clean:find("PLEASE SELECT TWO DISTINCT LANGUAGES") then
        if target_lang == "ru" then
            return translate_text(text, "en", "ru")
        else
            return text, nil
        end
    end

    return clean, nil, target_lang
end

function on_message(msg)
    local chat_id = msg.chat_id
    local msg_id = msg.id
    local text = msg.text or ""

    -- 1. Кэшируем текст любого сообщения чата для реплаев
    if chat_id and msg_id and text ~= "" then
        cache_msg_text(chat_id, msg_id, text)
    end

    -- 2. Режим синхронного автоперевода входящих сообщений (autotr)
    if not msg.out and chat_id and text ~= "" then
        local autotr_lang = koto.db.get("autotr_" .. tostring(chat_id))
        if autotr_lang and autotr_lang ~= "" and autotr_lang ~= "off" then
            local trans, err, eff_lang = translate_text(text, autotr_lang)
            if trans and trans ~= text then
                local res = prem("🌐", EMOJI_GLOBE) .. " **Автоперевод [" .. (eff_lang or autotr_lang):upper() .. "]:**\n" ..
                            "> " .. html_escape(trans) .. ""
                koto.reply(res)
            end
        end
    end

    -- Команды модуля исполняются только от нас
    if not msg.out then return end

    local trimmed = text:match("^%s*(.-)%s*$")
    if trimmed == "" then return end

    -- .autotr [lang|off]
    if trimmed:sub(1, 7) == ".autotr" then
        local arg = trimmed:sub(8):match("^%s*(%S*)%s*$")
        if arg == "off" then
            koto.db.del("autotr_" .. tostring(chat_id))
            koto.edit(prem("✅", EMOJI_SUCCESS) .. " **Автоперевод для этого чата отключён.**")
            return
        elseif arg ~= "" then
            koto.db.set("autotr_" .. tostring(chat_id), arg:lower())
            koto.edit(prem("🌐", EMOJI_GLOBE) .. " **Автоперевод включён:** язык перевода `" .. arg:upper() .. "`")
            return
        else
            local cur = koto.db.get("autotr_" .. tostring(chat_id)) or "выключен"
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Текущий автоперевод чата:** `" .. cur .. "`\n" ..
                      "> Использование:\n• `.autotr ru` — включить перевод на русский\n• `.autotr en` — на английский\n• `.autotr off` — выключить")
            return
        end
    end

    -- .detect (по реплаю)
    if trimmed == ".detect" then
        local reply_id = msg.reply_to_msg_id
        if not reply_id or reply_id == 0 then
            koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Ответьте на сообщение командой .detect для определения языка.**")
            return
        end

        local target_text = get_cached_msg_text(chat_id, reply_id)
        if not target_text or target_text == "" then
            koto.edit(prem("❌", EMOJI_ERROR) .. " **Текст сообщения не найден в кэше.**")
            return
        end

        local is_cyr = has_cyrillic(target_text)
        local detected = is_cyr and "Русский / Кириллица (RU)" or "Латиница / English (EN)"

        koto.edit(prem("👁️", EMOJI_DETECT) .. " **Определение языка сообщения:**\n" ..
                  "> • **Язык текста:** `" .. detected .. "`\n" ..
                  "> • **Фрагмент:** __" .. html_escape(target_text:sub(1, 150)) .. "__")
        return
    end

    -- .tr [lang] (по реплаю) ИЛИ .tr <lang> <текст>
    if trimmed:sub(1, 3) == ".tr" then
        local rest = trimmed:sub(4):match("^%s*(.-)%s*$")
        local reply_id = msg.reply_to_msg_id

        if reply_id and reply_id > 0 then
            -- Перевод сообщения по реплаю
            local target_lang = rest ~= "" and rest:match("^(%S+)") or "ru"
            local cached = get_cached_msg_text(chat_id, reply_id)
            if not cached or cached == "" then
                koto.edit(prem("❌", EMOJI_ERROR) .. " **Текст сообщения не найден в кэше для перевода.**")
                return
            end

            koto.edit(prem("🌐", EMOJI_GLOBE) .. " **Перевожу...**")
            local res, err, eff_lang = translate_text(cached, target_lang)
            if not res then
                koto.edit(prem("❌", EMOJI_ERROR) .. " **Ошибка перевода:** `" .. html_escape(err or "сбой API") .. "`")
                return
            end

            local out = prem("🌐", EMOJI_GLOBE) .. " **Перевод [" .. (eff_lang or target_lang):upper() .. "]:**\n" ..
                        "> " .. html_escape(res) .. ""
            koto.edit(out)
            return
        else
            -- Перевод введённого текста: .tr <lang> <текст>
            local lang, text_to_tr = rest:match("^(%S+)%s+(.+)$")
            if not lang or not text_to_tr then
                koto.edit(prem("ℹ️", EMOJI_INFO) .. " **Использование переводчика:**\n" ..
                          "> " ..
                          "• По реплаю: `.tr [язык]` (например: `.tr en`)\n" ..
                          "> • Вручную: `.tr <язык> <текст>`\n" ..
                          "> • Синхронный перевод: `.autotr [язык|off]`" ..
                          "")
                return
            end

            koto.edit(prem("🌐", EMOJI_GLOBE) .. " **Перевод...**")
            local res, err, eff_lang = translate_text(text_to_tr, lang)
            if not res then
                koto.edit(prem("❌", EMOJI_ERROR) .. " **Ошибка перевода:** `" .. html_escape(err or "сбой API") .. "`")
                return
            end

            local out = prem("🌐", EMOJI_GLOBE) .. " **[" .. (eff_lang or lang):upper() .. "]:** " .. html_escape(res)
            koto.edit(out)
            return
        end
    end
end
