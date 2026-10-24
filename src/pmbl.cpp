// Native adaptation of the supplied PMBL.py 3.0.4.
// Original: Copyright 2022 hikariatama, GNU AGPLv3.
// SPDX-License-Identifier: AGPL-3.0-only
#include "module_abi.h"
#include "nlohmann/json.hpp"
#include "policy.hpp"

#include <charconv>
#include <ctime>
#include <functional>
#include <limits>
#include <mutex>
#include <sstream>
#include <set>

namespace {
using Json = nlohmann::json;
using namespace pmbl;
constexpr const char* version = "0.2.0";
std::mutex mutex;

struct Error : std::runtime_error { using std::runtime_error::runtime_error; };
std::string trim(std::string_view value) {
    auto white = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!value.empty() && white(value.front())) value.remove_prefix(1);
    while (!value.empty() && white(value.back())) value.remove_suffix(1);
    return std::string(value);
}
std::string lower(std::string value) {
    for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
std::string args(koto_ctx* ctx, const koto_api* api) {
    const char* raw = api->cmd_args(ctx); return raw ? trim(raw) : "";
}
std::string read(const std::function<int(char*, int)>& getter, size_t cap = 65536) {
    std::vector<char> buffer(4096);
    int n = getter(buffer.data(), static_cast<int>(buffer.size()));
    if (n < 0) return {};
    if (static_cast<size_t>(n) >= cap) throw Error("Данные слишком велики для безопасной обработки.");
    if (static_cast<size_t>(n) >= buffer.size()) {
        buffer.resize(static_cast<size_t>(n) + 1);
        n = getter(buffer.data(), static_cast<int>(buffer.size()));
    }
    if (n < 0) return {};
    if (static_cast<size_t>(n) >= buffer.size()) throw Error("Данные изменились во время чтения. Повтори команду.");
    return std::string(buffer.data(), static_cast<size_t>(n));
}
Json call(koto_ctx* ctx, const koto_api* api, const char* method, const Json& input = Json::object()) {
    if (api->host_version(ctx)<74 || !api->host_call_json) throw Error("Нужен KoteLoader 0.3.54 или новее.");
    auto request=input.dump();
    auto text=read([&](char* out,int n) {return api->host_call_json(ctx,method,request.c_str(),out,n);},256*1024+1);
    auto result=Json::parse(text,nullptr,false);
    if (!result.is_object() || result.value("schema",0)!=1 || !result.value("ok",false))
        throw Error(std::string(method)+": "+(result.is_object()?result.value("error",std::string("invalid_response")):"invalid_response"));
    return result;
}
Json peer_ref(long long uid) { return {{"type","user"},{"id",uid}}; }
Json peer_input(long long uid) { return {{"peer",peer_ref(uid)}}; }
void require_services(koto_ctx* ctx,const koto_api* api) {
    auto caps=call(ctx,api,"capabilities");
    for (auto name:{"telegram.peer.v1","telegram.history.v1","telegram.dialogs.v1"}) {
        const auto& list=caps.at("read");
        if (std::find(list.begin(),list.end(),name)==list.end()) throw Error(std::string("Хост не поддерживает ")+name);
    }
    for (auto name:{"telegram.block.v1","telegram.unblock.v1","telegram.report_spam.v1","telegram.clear_history.v1"}) {
        const auto& list=caps.at("actions");
        if (std::find(list.begin(),list.end(),name)==list.end()) throw Error(std::string("Хост не поддерживает ")+name);
    }
}
void action(koto_ctx* ctx,const koto_api* api,const char* method,long long uid,bool clear=false) {
    auto input=peer_input(uid);
    if (clear) {input["just_clear"]=true;input["revoke"]=false;input["max_id"]=0;}
    auto request=input.dump();
    if (!api->host_action_json || api->host_action_json(ctx,method,request.c_str())!=1)
        throw Error(std::string("Хост отклонил действие ")+method);
}
std::string db(koto_ctx* ctx, const koto_api* api, const std::string& key) {
    return read([&](char* out, int n) { return api->db_get(ctx, key.c_str(), out, n); });
}
std::string cfg(koto_ctx* ctx, const koto_api* api, const char* key, const std::string& fallback) {
    if (!api->cfg_get) return fallback;
    bool found = false;
    auto result = read([&](char* out, int n) { int size = api->cfg_get(ctx, key, out, n); found = size >= 0; return size; });
    return found ? result : fallback;
}
bool boolean(std::string value) {
    value = lower(trim(value));
    if (value == "1" || value == "true" || value == "on" || value == "yes") return true;
    if (value == "0" || value == "false" || value == "off" || value == "no") return false;
    throw Error("Ожидается 0 или 1.");
}
long long integer(std::string_view value, long long minimum, long long maximum) {
    long long result = 0;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || ec != std::errc{} || ptr != value.data() + value.size() || result < minimum || result > maximum)
        throw Error("Некорректное число или ID пользователя.");
    return result;
}
std::string prefix(koto_ctx* ctx, const koto_api* api) {
    auto value = read([&](char* out, int n) { return api->setting_get(ctx, "prefix", out, n); });
    return value.empty() ? "." : value;
}
std::string account(koto_ctx* ctx, const koto_api* api) {
    long long me = api->get_me(ctx);
    if (me <= 0) throw Error("Не удалось определить текущий Telegram-аккаунт.");
    return "account:" + std::to_string(me) + ":";
}
std::string user_key(koto_ctx* ctx, const koto_api* api, const char* kind, long long uid) {
    return account(ctx, api) + kind + ":" + std::to_string(uid);
}
Config config(koto_ctx* ctx, const koto_api* api) {
    Config c;
    c.enabled = api->db_get_int(ctx, (account(ctx, api) + "enabled").c_str(), 0) != 0;
    c.ignore_contacts = boolean(cfg(ctx, api, "ignore_contacts", "1"));
    c.ignore_active = boolean(cfg(ctx, api, "ignore_active", "1"));
    c.active_threshold = static_cast<int>(integer(cfg(ctx, api, "active_threshold", "5"), 1, 200));
    c.report_spam = boolean(cfg(ctx, api, "report_spam", "0"));
    c.delete_dialog = boolean(cfg(ctx, api, "delete_dialog", "0"));
    c.silent = boolean(cfg(ctx, api, "silent", "0"));
    c.whitelist_only = boolean(cfg(ctx, api, "whitelist_only", "0"));
    c.custom_message = cfg(ctx, api, "custom_message", "");
    c.photo = cfg(ctx, api, "photo", c.photo);
    if (c.custom_message.size() > 3000) throw Error("Сообщение незнакомцу слишком длинное (максимум 3000 байт).");
    if (!c.photo.empty() && (c.photo.rfind("https://", 0) != 0 || c.photo.size() > 2048 || c.photo.find_first_of("\r\n\t ") != std::string::npos))
        throw Error("В настройке photo разрешена только HTTPS-ссылка или пустое значение.");
    return c;
}
void reply(koto_ctx* ctx, const koto_api* api, const std::string& text) {
    api->c_reset(ctx); api->c_text(ctx, text.c_str());
    if (api->is_outgoing(ctx)) api->c_edit(ctx, KOTO_NO_LINK_PREVIEW);
    else api->c_reply(ctx, KOTO_NO_LINK_PREVIEW);
}
void saved(koto_ctx* ctx, const koto_api* api, const std::string& text) {
    api->c_reset(ctx); api->c_text(ctx, text.c_str()); api->c_send(ctx, api->get_me(ctx), KOTO_NO_LINK_PREVIEW);
}
std::string short_text(const std::string& text, size_t bytes = 2200) {
    if (text.size() <= bytes) return text;
    size_t end = bytes;
    while (end && (static_cast<unsigned char>(text[end]) & 192) == 128) --end;
    return text.substr(0, end) + "…";
}
std::string name(koto_ctx* ctx, const koto_api* api, long long uid) {
    auto result = read([&](char* out, int n) { return api->user_name(ctx, uid, out, n); });
    return result.empty() ? std::to_string(uid) : short_text(result, 200);
}
void approve(koto_ctx* ctx, const koto_api* api, long long uid, const char* reason) {
    api->db_set(ctx, user_key(ctx, api, "allow", uid).c_str(), reason);
}
long long target(koto_ctx* ctx, const koto_api* api) {
    std::string value = args(ctx, api);
    if (!value.empty()) {
        if (value.front() == '@') {
            long long uid = api->resolve(ctx, value.c_str());
            if (uid <= 0) throw Error("Пользователь не найден.");
            return uid;
        }
        return integer(value, 1, std::numeric_limits<long long>::max());
    }
    long long uid = api->reply_sender_id ? api->reply_sender_id(ctx) : 0;
    if (uid > 0) return uid;
    if (api->chat_type(ctx) == KOTO_CHAT_PRIVATE && api->chat_id(ctx) > 0 && api->chat_id(ctx) != api->get_me(ctx)) return api->chat_id(ctx);
    throw Error("Укажи ID, @username или ответь на сообщение пользователя.");
}
void command(koto_ctx* ctx, const koto_api* api, const std::function<void()>& fn) noexcept {
    try {
        if (!api->check_permission(ctx, KOTO_LEVEL_OWNER)) return;
        std::unique_lock lock(mutex, std::try_to_lock);
        if (!lock.owns_lock()) { reply(ctx, api, "PMBL занят обработкой сообщения. Повтори команду."); return; }
        fn();
    } catch (const Error& error) { reply(ctx, api, std::string("❌ PMBL: ") + error.what()); }
    catch (...) { reply(ctx, api, "❌ PMBL: ошибка данных. Проверь настройки модуля."); }
}

std::string state_text(koto_ctx* ctx, const koto_api* api, const Config& c) {
    std::string a=account(ctx,api);
    auto last_error=db(ctx,api,a+"last_error");
    return std::string("🛡 PMBL ")+version+"\nЗащита: "+(c.enabled?"включена":"выключена")+
        "\nРежим: "+(c.whitelist_only?"только белый список":"контакты, начало переписки и активность")+
        "\nЖалоба на спам: "+(c.report_spam?"да":"нет")+"\nОчистка истории у себя: "+(c.delete_dialog?"да":"нет")+
        "\nУведомление незнакомцу: "+(c.silent?"выключено":"включено")+
        "\nЗапросов на блокировку: "+std::to_string(api->db_get_int(ctx,(a+"ban_requests").c_str(),0))+
        (last_error.empty()?"":"\nПоследняя ошибка проверки: "+short_text(last_error,600));
}
void toggle(koto_ctx* ctx, const koto_api* api) {
    command(ctx, api, [&] {
        Config c = config(ctx, api); auto arg = lower(args(ctx, api));
        bool enable = !c.enabled;
        if (arg == "off" || arg == "0") enable = false;
        else if (arg == "on" || arg == "1") enable = true;
        else if (arg == "strict") {
            api->cfg_set(ctx, "whitelist_only", "1"); c.whitelist_only = true; enable = true;
        } else if (!arg.empty()) throw Error("Использование: " + prefix(ctx, api) + "pmbl [on|off|strict].");
        if (enable) require_services(ctx,api);
        api->db_set_int(ctx, (account(ctx, api) + "enabled").c_str(), enable ? 1 : 0); c.enabled = enable;
        reply(ctx, api, state_text(ctx, api, c));
    });
}
void status(koto_ctx* ctx, const koto_api* api) {
    command(ctx, api, [&] { reply(ctx, api, state_text(ctx, api, config(ctx, api))); });
}
void allow(koto_ctx* ctx, const koto_api* api) {
    command(ctx, api, [&] {
        long long uid = target(ctx, api);
        approve(ctx, api, uid, "manual");
        auto key = user_key(ctx, api, "ban_attempt", uid);
        bool unblock = api->db_get_int(ctx, key.c_str(), 0) != 0;
        if (unblock) action(ctx,api,"telegram.unblock.v1",uid);
        api->db_del(ctx, key.c_str());
        reply(ctx, api, "✅ " + name(ctx, api, uid) + " (" + std::to_string(uid) + ") добавлен в белый список PMBL." +
            (unblock ? " Отправлен запрос разблокировки." : ""));
    });
}
void deny(koto_ctx* ctx, const koto_api* api) {
    command(ctx, api, [&] {
        long long uid = target(ctx, api);
        api->db_del(ctx, user_key(ctx, api, "allow", uid).c_str());
        api->db_del(ctx, user_key(ctx, api, "ban_attempt", uid).c_str());
        reply(ctx, api, "Пользователь " + std::to_string(uid) + " удалён из белого списка PMBL. Следующее входящее сообщение будет проверено заново.");
    });
}
void allow_list(koto_ctx* ctx, const koto_api* api) {
    command(ctx, api, [&] {
        std::string all = read([&](char* out, int n) { return api->db_keys(ctx, out, n); }, 256 * 1024);
        std::string start = account(ctx, api) + "allow:";
        std::istringstream keys(all); std::string key, text = "Белый список PMBL:\n"; unsigned shown = 0, total = 0;
        while (std::getline(keys, key)) if (key.rfind(start, 0) == 0) {
            ++total;
            if (shown < 50) { text += key.substr(start.size()) + " · " + db(ctx, api, key) + "\n"; ++shown; }
        }
        text += "Всего: " + std::to_string(total);
        if (total > shown) text += "; показаны первые 50";
        reply(ctx, api, text);
    });
}
void settings(koto_ctx* ctx, const koto_api* api) {
    command(ctx, api, [&] {
        std::istringstream in(args(ctx, api)); std::string report, remove, extra;
        in >> report >> remove >> extra;
        if ((report != "0" && report != "1") || (remove != "0" && remove != "1") || !extra.empty())
            throw Error("Пример: " + prefix(ctx, api) + "pmblsett 0 0 (жалоба, удаление диалога).");
        require_services(ctx,api);
        api->cfg_set(ctx, "report_spam", report.c_str()); api->cfg_set(ctx, "delete_dialog", remove.c_str());
        reply(ctx, api, state_text(ctx, api, config(ctx, api)));
    });
}
void ban_last(koto_ctx* ctx, const koto_api* api) {
    command(ctx,api,[&] {
        auto wanted=static_cast<size_t>(integer(args(ctx,api),1,10000));
        require_services(ctx,api);
        std::set<long long> users;
        // Read both folders completely before queuing any destructive action.
        for (int folder:{0,1}) {
            Json cursor=nullptr;std::set<std::string> cursors;
            bool complete=false;
            for (int page=0;page<200;++page) {
                Json input={{"limit",100},{"folder_id",folder},{"exclude_pinned",true}};
                if (!cursor.is_null()) input["offset"]=cursor;
                auto result=call(ctx,api,"telegram.dialogs.v1",input);
                if (!result.at("dialogs").is_array()) throw Error("Некорректный список диалогов.");
                for (const auto& dialog:result.at("dialogs")) {
                    const auto& peer=dialog.at("peer");
                    if (!dialog.at("pinned").is_boolean()) throw Error("Нет признака закреплённого диалога; блокировки не запрошены.");
                    if (dialog.at("pinned").get<bool>() || peer.at("type")!="user") continue;
                    auto uid=peer.at("id").get<long long>();
                    if (!service(uid,api->get_me(ctx))) users.insert(uid);
                }
                if (users.size()>10000) throw Error("Более 10000 диалогов; команда остановлена до блокировок.");
                if (result.at("complete").get<bool>()) {complete=true;break;}
                cursor=result.at("next");
                if (!cursor.is_object() || !cursors.insert(cursor.dump()).second)
                    throw Error("Хост не продвинул страницу диалогов; блокировки не запрошены.");
            }
            if (!complete) throw Error("Не удалось получить все диалоги; блокировки не запрошены.");
        }
        std::vector<std::pair<long long,long long>> dates;
        for (auto uid:users) {
            auto input=peer_input(uid);input["limit"]=1;input["oldest"]=true;input["include_text"]=false;
            auto history=call(ctx,api,"telegram.history.v1",input);
            const auto& messages=history.at("messages");
            if (!messages.is_array()) throw Error("Некорректная история; блокировки не запрошены.");
            if (messages.empty()) continue;
            auto date=messages.front().at("date").get<long long>();
            if (date<=0) throw Error("Нет даты начала диалога; блокировки не запрошены.");
            dates.emplace_back(date,uid);
        }
        std::sort(dates.begin(),dates.end(),std::greater<>());
        auto count=std::min(wanted,dates.size());
        for (size_t i=0;i<count;++i) {
            auto uid=dates[i].second;
            action(ctx,api,"telegram.block.v1",uid);
            action(ctx,api,"telegram.clear_history.v1",uid,true);
            api->db_set_int(ctx,user_key(ctx,api,"ban_attempt",uid).c_str(),std::time(nullptr));
            // A later manual unblock must not leave the dialog pre-approved.
            api->db_del(ctx,user_key(ctx,api,"allow",uid).c_str());
        }
        reply(ctx,api,"PMBL: запрошены блокировка и очистка истории у себя для "+std::to_string(count)+" последних начатых личных диалогов. Закреплённые диалоги пропущены.");
    });
}
void configure(koto_ctx* ctx, const koto_api* api) {
    command(ctx, api, [&] {
        std::string input = args(ctx, api); auto split = input.find_first_of(" \t");
        if (split == std::string::npos) throw Error("Использование: " + prefix(ctx, api) + "pmblcfg <silent|report_spam|photo|custom_message|ignore_contacts|ignore_active|active_threshold> <значение>.");
        std::string key = input.substr(0, split), value = trim(std::string_view(input).substr(split + 1));
        if (key == "silent" || key == "report_spam" || key == "delete_dialog" || key == "ignore_contacts" || key == "ignore_active" || key == "whitelist_only") value = boolean(value) ? "1" : "0";
        else if (key == "active_threshold") value = std::to_string(integer(value, 1, 200));
        else if (key == "custom_message") { if (value == "clear") value.clear(); if (value.size() > 3000) throw Error("Сообщение превышает 3000 байт."); }
        else if (key == "photo") {
            if (value == "off" || value == "clear") value.clear();
            if (!value.empty() && (value.rfind("https://", 0) != 0 || value.size() > 2048 || value.find_first_of("\r\n\t ") != std::string::npos))
                throw Error("Для photo укажи HTTPS-ссылку или off.");
        } else throw Error("Неизвестная настройка PMBL.");
        api->cfg_set(ctx, key.c_str(), value.c_str());
        reply(ctx, api, "✅ Настройка PMBL сохранена: " + key + ".");
    });
}

std::optional<bool> bool_field(const Json& value, const char* key) {
    if (value.is_object() && value.contains(key) && value[key].is_boolean()) return value[key].get<bool>();
    return std::nullopt;
}
void ban(koto_ctx* ctx, const koto_api* api, const Config& c, const Peer& peer) {
    auto now = static_cast<long long>(std::time(nullptr));
    auto attempt_key = user_key(ctx, api, "ban_attempt", peer.user);
    auto last = api->db_get_int(ctx, attempt_key.c_str(), 0);
    if (last > 0 && now >= last && now - last < 30) return;

    NotificationLimit limit;
    std::string rate_key = account(ctx, api) + "notifications";
    auto times = Json::parse(db(ctx, api, rate_key), nullptr, false);
    if (times.is_array()) for (const auto& item : times) if (item.is_number_integer()) limit.timestamps.push_back(item.get<std::int64_t>());
    if (!c.silent && limit.take(now)) {
        api->c_reset(ctx);
        const std::string text = c.custom_message.empty() ?
            "😃 Добрый день •ᴗ•\nЮнит «SIGMA» защищает этот аккаунт. Вы не подтверждены для личных сообщений. Связаться с владельцем можно в общем чате. Вы будете заблокированы." : c.custom_message;
        api->c_text(ctx, text.c_str());
        if (!c.photo.empty() && api->c_send_file && text.size() <= 900) api->c_send_file(ctx, peer.user, c.photo.c_str(), 0);
        else api->c_send(ctx, peer.user, KOTO_NO_LINK_PREVIEW);
    }
    api->db_set(ctx, rate_key.c_str(), Json(limit.timestamps).dump().c_str());
    // Announce an intent, not an unconfirmed Telegram result.
    const char* raw = api->msg_text(ctx);
    saved(ctx, api, "🛡 PMBL: запрошена блокировка " + name(ctx, api, peer.user) + " (" + std::to_string(peer.user) + ").\n"
        "Жалоба на спам: " + (c.report_spam ? "запрошена" : "нет") + "\nОчистка истории у себя: " + (c.delete_dialog?"запрошена":"нет") + "\n\nСообщение:\n" + short_text(raw && *raw ? raw : "[медиа или служебное сообщение]"));
    action(ctx,api,"telegram.block.v1",peer.user);
    if (c.report_spam) action(ctx,api,"telegram.report_spam.v1",peer.user);
    if (c.delete_dialog) action(ctx,api,"telegram.clear_history.v1",peer.user,true);
    api->db_set_int(ctx, attempt_key.c_str(), now);
    auto counter = account(ctx, api) + "ban_requests";
    auto count = api->db_get_int(ctx, counter.c_str(), 0);
    if (count < std::numeric_limits<long long>::max()) api->db_set_int(ctx, counter.c_str(), std::max(0LL, count) + 1);
    // A failed Telegram action may be retried after 30s; never whitelist a ban.
}
int watch(koto_ctx* ctx, const koto_api* api) noexcept {
    try {
        std::unique_lock lock(mutex, std::try_to_lock);
        if (!lock.owns_lock()) return 0;
        Peer p; p.me = api->get_me(ctx); p.user = api->sender_id(ctx); p.chat = api->chat_id(ctx); p.message = api->msg_id(ctx);
        p.private_chat = api->chat_type(ctx) == KOTO_CHAT_PRIVATE; p.outgoing = api->is_outgoing(ctx) != 0;
        if (!p.private_chat || p.outgoing || p.chat != p.user || service(p.user, p.me)) return 0;
        auto c = config(ctx, api); if (!c.enabled) return 0;
        p.approved = !db(ctx, api, user_key(ctx, api, "allow", p.user)).empty();
        if (p.approved) return 0;
        p.trusted = api->user_level && api->user_level(ctx) >= KOTO_LEVEL_TRUSTED;
        const char* raw = api->msg_text(ctx); p.has_text = raw && *raw;
        if (p.trusted) {approve(ctx,api,p.user,"trusted");return 0;}
        auto profile=call(ctx,api,"telegram.peer.v1",peer_input(p.user));
        const auto& user=profile.at("user");
        if (user.at("id").get<long long>()!=p.user) throw Error("Профиль относится к другому пользователю.");
        p.bot=bool_field(user,"is_bot");p.contact=bool_field(user,"is_contact");
        auto early=decide(c,p);
        if (early.action==Action::approve) {approve(ctx,api,p.user,early.reason);return 0;}
        if (!c.whitelist_only) {
            if (p.has_text) {
                auto input=peer_input(p.user);input["limit"]=1;input["oldest"]=true;input["include_text"]=false;
                auto history=call(ctx,api,"telegram.history.v1",input);
                const auto& rows=history.at("messages");
                if (rows.is_array() && !rows.empty() && rows.front().at("sender_id").is_number_integer())
                    p.first_sender=rows.front().at("sender_id").get<long long>();
            }
            early=decide(c,p);
            if (early.action==Action::approve) {approve(ctx,api,p.user,early.reason);return 0;}
            if (c.ignore_active) {
                auto input=peer_input(p.user);input["limit"]=200;input["include_text"]=false;
                auto history=call(ctx,api,"telegram.history.v1",input);
                const auto& rows=history.at("messages");
                if (!rows.is_array()) throw Error("Некорректная история.");
                unsigned own=0;
                for (const auto& m:rows) {
                    if (!m.contains("sender_id") || !m.at("sender_id").is_number_integer()) throw Error("Нет отправителя в истории.");
                    if (m.at("sender_id").get<long long>()==p.me) ++own;
                }
                p.own_messages=own;
            }
        }
        auto decision = decide(c, p);
        if (decision.action!=Action::insufficient_data) api->db_del(ctx,(account(ctx,api)+"last_error").c_str());
        else api->db_set(ctx,(account(ctx,api)+"last_error").c_str(),decision.reason);
        if (decision.action == Action::approve) approve(ctx, api, p.user, decision.reason);
        else if (decision.action == Action::block) ban(ctx, api, c, p);
        else if (decision.action == Action::insufficient_data) {
            auto key = account(ctx, api) + "last_missing_data_notice";
            auto now = static_cast<long long>(std::time(nullptr));
            if (now - api->db_get_int(ctx, key.c_str(), 0) >= 300) {
                saved(ctx, api, "PMBL пропустил сообщение: недостаточно данных для безопасной проверки (" + std::string(decision.reason) + "). Статус: " + prefix(ctx, api) + "pmblstatus.");
                api->db_set_int(ctx, key.c_str(), now);
            }
        }
    } catch (const std::exception& error) {
        try {api->db_set(ctx,(account(ctx,api)+"last_error").c_str(),short_text(error.what(),600).c_str());} catch (...) {}
    } catch (...) {} // Never let an exception cross the C ABI or turn missing data into a ban.
    return 0;
}

const koto_command commands[] = {
    {"pmbl", toggle, KOTO_LEVEL_OWNER, "Включить/выключить PMBL; strict — явный режим только белого списка.", "[on|off|strict]"},
    {"pmblstatus", status, KOTO_LEVEL_OWNER, "Статус защиты и настройки PMBL.", nullptr},
    {"allowpm", allow, KOTO_LEVEL_OWNER, "Разрешить пользователю писать в ЛС; снять блокировку, запрошенную PMBL.", "[ID|@username|reply]"},
    {"denypm", deny, KOTO_LEVEL_OWNER, "Удалить пользователя из белого списка PMBL.", "[ID|@username|reply]"},
    {"pmlist", allow_list, KOTO_LEVEL_OWNER, "Показать белый список PMBL текущего аккаунта.", nullptr},
    {"pmblsett", settings, KOTO_LEVEL_OWNER, "Настроить жалобы на спам и удаление диалога (0/1).", "<report_spam> <delete_dialog>"},
    {"pmblcfg", configure, KOTO_LEVEL_OWNER, "Изменить настройку PMBL.", "<ключ> <значение>"},
    {"pmbanlast", ban_last, KOTO_LEVEL_OWNER, "Заблокировать и очистить у себя N последних начатых личных диалогов.", "<число>"},
};
const koto_setting_field fields[] = {
    {"ignore_contacts", "Игнорировать контакты (не применяется в strict)", KOTO_FIELD_BOOL, "1", nullptr},
    {"ignore_active", "Игнорировать активную переписку (не применяется в strict)", KOTO_FIELD_BOOL, "1", nullptr},
    {"active_threshold", "Порог своих сообщений для доверия (не применяется в strict)", KOTO_FIELD_INT, "5", nullptr},
    {"custom_message", "Текст уведомления незнакомцу; пусто — стандартный", KOTO_FIELD_STR, "", nullptr},
    {"photo", "Фото уведомления (HTTPS); пусто — только текст", KOTO_FIELD_URL, "https://github.com/hikariatama/assets/raw/master/unit_sigma.png", nullptr},
    {"report_spam", "Сообщать о спаме после запроса блокировки", KOTO_FIELD_BOOL, "0", nullptr},
    {"delete_dialog", "Очистить историю у себя после блокировки", KOTO_FIELD_BOOL, "0", nullptr},
    {"silent", "Не уведомлять незнакомца", KOTO_FIELD_BOOL, "0", nullptr},
    {"whitelist_only", "Явный режим белого списка без проверки контактов и старой переписки", KOTO_FIELD_BOOL, "0", nullptr},
};
const koto_module module = {
    KOTO_MODULE_ABI, "pmbl", "PMBL: защита ЛС, доверие контактам и переписке, блокировка, жалобы и очистка истории.",
    version, 74, KOTO_MOD_NO_TWIN, commands, static_cast<int>(sizeof(commands) / sizeof(commands[0])),
    watch, KOTO_WATCH_INCOMING, nullptr, fields, static_cast<int>(sizeof(fields) / sizeof(fields[0])), nullptr
};
} // namespace

extern "C" __attribute__((visibility("default"))) const koto_module* koto_module_register() { return &module; }
