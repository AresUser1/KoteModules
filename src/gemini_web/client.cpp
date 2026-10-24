#include "client.hpp"

#include <algorithm>
#include <charconv>
#include <ctime>
#include <limits>

namespace gweb {
namespace {
constexpr const char* app = "https://gemini.google.com/app";
constexpr const char* generate = "https://gemini.google.com/_/BardChatUi/data/assistant.lamda.BardFrontendService/StreamGenerate";
constexpr const char* batch = "https://gemini.google.com/_/BardChatUi/data/batchexecute";
constexpr const char* browser = "Mozilla/5.0 (Linux; Android 13) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/145.0.0.0 Mobile Safari/537.36";
bool domain_matches(const std::string& host, const std::string& domain) {
    return host == domain || (host.size() > domain.size() &&
        host.compare(host.size() - domain.size(), domain.size(), domain) == 0 &&
        host[host.size() - domain.size() - 1] == '.');
}
bool google_host(const std::string& host) { return domain_matches(host, "google.com"); }
bool cookie_name(const std::string& name) {
    return !name.empty() && name.size() < 256 && name.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.!#$%&'*+^`|~") == std::string::npos;
}
bool cookie_value(const std::string& value) {
    return value.size() < 16384 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return c > 32 && c < 127 && c != ';' && c != ',' && c != '"' && c != '\\';
    });
}
void status_error(int status, const std::string& operation) {
    if (status >= 200 && status < 300) return;
    if (status == 401) throw Error("auth", "Google отклонил cookies. Сохрани новую сессию через .gweblogin.");
    if (status == 403) throw Error("http403", "Google вернул HTTP 403. Возможны ограничения сессии, сети или клиента. Это не обязательно истёкшие cookies.");
    if (status == 429) throw Error("quota", "Google ограничил частоту запросов (HTTP 429). Подожди перед повтором.");
    if (status >= 500) throw Error("upstream", "Google временно недоступен: HTTP " + std::to_string(status) + ". Память диалога сохранена.");
    throw Error("http", operation + ": HTTP " + std::to_string(status) + ".");
}
bool contains_number(const Json& values, int n) {
    return values.is_array() && std::find(values.begin(), values.end(), Json(n)) != values.end();
}
void visit_parts(const Json& root, const std::function<void(const Json&)>& callback, unsigned depth = 0) {
    if (!root.is_array() || depth > 32) return;
    if (!root.empty() && root[0].is_string()) callback(root);
    else for (const auto& item : root) visit_parts(item, callback, depth + 1);
}
void generation_error(const Json& part) {
    const auto& error = field(part, {5, 2, 0, 1, 0});
    if (!error.is_number_integer() || error == 0) return;
    int code = error.get<int>();
    switch (code) {
        case 1037: throw Error("quota", "Лимит этой модели исчерпан (1037). Выбери другую через .gwebmodel или подожди.");
        case 1050: throw Error("model", "Модель не совпадает с моделью диалога (1050). Используй .gwebclear перед сменой модели.");
        case 1052: throw Error("model", "Google отклонил параметры модели (1052). Выполни .gwebrefresh и проверь .gwebmodel.");
        case 1060: throw Error("restricted", "Google временно ограничил доступ с этой сети (1060). Проверь доступ в браузере или VPN.");
        case 1013: throw Error("upstream", "Временная ошибка Google (1013). Повтори позже; диалог сохранён.");
        default: throw Error("protocol", "Google вернул код ошибки " + std::to_string(code) + ". Диалог сохранён.");
    }
}
Json parse_json_string(const Json& value) {
    if (!value.is_string()) return nullptr;
    auto text = value.get<std::string>();
    return Json::parse(text, nullptr, false);
}
} // namespace

const Json& field(const Json& value, std::initializer_list<size_t> path) {
    static const Json none;
    const Json* current = &value;
    for (size_t i : path) {
        if (!current->is_array() || current->empty()) return none;
        const Json* next = i < current->size() ? &(*current)[i] : nullptr;
        // JSPB can move a positional field into the final sparse object. Its
        // keys use 1-based field numbers, irrespective of the array length.
        if (!next || next->is_null() || next->is_object() || (next->is_array() && next->empty())) {
            const auto& sparse = current->back();
            const auto key = std::to_string(i + 1);
            if (sparse.is_object() && sparse.contains(key)) next = &sparse[key];
            else if (!next || next->is_object()) return none;
        }
        if (!next) return none;
        current = next;
    }
    return *current;
}
std::string string_field(const Json& value, std::initializer_list<size_t> path) {
    const auto& data = field(value, path);
    return data.is_string() ? data.get<std::string>() : "";
}

std::vector<Json> frames(std::string_view wire) {
    // Frame lengths differ across frontend builds (bytes vs JS code units).
    // Locate complete JSON envelopes structurally, honouring quoted strings.
    if (wire.size() > max_response_bytes) throw Error("protocol", "Слишком большой ответ Gemini.");
    size_t pos = 0;
    if (wire.substr(0, 3) == "\xef\xbb\xbf") pos = 3;
    while (pos < wire.size() && (wire[pos] == '\r' || wire[pos] == '\n' || wire[pos] == ' ')) ++pos;
    if (wire.substr(pos, 4) == ")]}'") pos += 4;
    std::vector<Json> result;
    while (pos < wire.size()) {
        char c = wire[pos];
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t' || (c >= '0' && c <= '9')) { ++pos; continue; }
        if (c != '[' && c != '{') throw Error("protocol", "Вместо RPC-ответа Google вернул другой формат. Проверь .gwebtest.");
        size_t start = pos;
        unsigned depth = 0; bool quoted = false, escaped = false;
        for (; pos < wire.size(); ++pos) {
            c = wire[pos];
            if (quoted) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') quoted = false;
                continue;
            }
            if (c == '"') quoted = true;
            else if (c == '[' || c == '{') {
                if (++depth > 128) throw Error("protocol", "Слишком глубокий RPC-ответ.");
            } else if (c == ']' || c == '}') {
                if (--depth == 0) { ++pos; break; }
            }
        }
        if (depth || quoted) throw Error("protocol", "Ответ Google оборвался внутри RPC-фрейма. Диалог сохранён.");
        Json value = Json::parse(wire.substr(start, pos - start), nullptr, false);
        if (value.is_discarded()) throw Error("protocol", "Google вернул некорректный JSON-фрейм.");
        result.push_back(std::move(value));
    }
    if (result.empty()) throw Error("protocol", "Google вернул пустой RPC-ответ.");
    return result;
}

std::string html_value(std::string_view html, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t search = 0;
    while (true) {
        size_t pos = html.find(needle, search);
        if (pos == std::string_view::npos) return {};
        search = pos + needle.size(); pos = search;
        while (pos < html.size() && (html[pos] == ' ' || html[pos] == '\t' || html[pos] == '\n')) ++pos;
        if (pos == html.size() || html[pos++] != ':') continue;
        while (pos < html.size() && (html[pos] == ' ' || html[pos] == '\t' || html[pos] == '\n')) ++pos;
        if (pos == html.size() || html[pos] != '"') continue;
        size_t begin = pos++;
        bool escaped = false;
        for (; pos < html.size() && pos - begin < 16384; ++pos) {
            char c = html[pos];
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') {
                Json value = Json::parse(html.substr(begin, pos - begin + 1), nullptr, false);
                if (value.is_string()) return value.get<std::string>();
                break;
            }
        }
    }
}

std::vector<std::string> split_text(std::string_view text, size_t limit) {
    if (limit < 2) throw Error("text", "Лимит сообщения слишком мал.");
    std::vector<std::string> chunks;
    size_t begin = 0;
    while (begin < text.size()) {
        size_t pos = begin, units = 0, preferred = begin;
        while (pos < text.size()) {
            unsigned char c = static_cast<unsigned char>(text[pos]);
            size_t n = c < 128 ? 1 : c < 224 ? 2 : c < 240 ? 3 : 4;
            size_t cost = n == 4 ? 2 : 1;
            if (units + cost > limit) break;
            if (pos + n > text.size()) throw Error("text", "Повреждённый UTF-8 в ответе.");
            pos += n; units += cost;
            if (c == '\n' || c == ' ') preferred = pos;
        }
        if (pos < text.size() && preferred > begin && preferred - begin > (pos - begin) / 2) pos = preferred;
        chunks.emplace_back(text.substr(begin, pos - begin)); begin = pos;
    }
    return chunks;
}

void Cookies::login(const std::string& psid, const std::string& psidts) {
    if (psid.empty() || !cookie_value(psid) || !cookie_value(psidts))
        throw Error("cookies", "Нужны только значения cookies без имён и кавычек. Инструкция: .gwebcookies.");
    cookies_.clear();
    cookies_.push_back({"__Secure-1PSID", psid, "google.com", "/", 0, false});
    if (!psidts.empty()) cookies_.push_back({"__Secure-1PSIDTS", psidts, "google.com", "/", 0, false});
}
void Cookies::receive(const Url& source, const Headers& headers) {
    if (!google_host(source.host)) return;
    for (const auto& [name, raw] : headers) {
        if (lower(name) != "set-cookie" || raw.size() > 20000) continue;
        size_t end = raw.find(';'), equal = raw.find('=');
        if (equal == std::string::npos || equal >= end) continue;
        Cookie cookie{trim(std::string_view(raw).substr(0, equal)),
                      trim(std::string_view(raw).substr(equal + 1, end == std::string::npos ? end : end - equal - 1)),
                      source.host, "/", 0, true};
        if (!cookie_name(cookie.name) || !cookie_value(cookie.value)) continue;
        bool accepted = true, have_max_age = false;
        while (end != std::string::npos) {
            size_t start = end + 1; end = raw.find(';', start);
            std::string attr = trim(std::string_view(raw).substr(start, end == std::string::npos ? end : end - start));
            size_t eq = attr.find('=');
            std::string key = lower(trim(std::string_view(attr).substr(0, eq)));
            std::string value = eq == std::string::npos ? "" : trim(std::string_view(attr).substr(eq + 1));
            if (key == "domain") {
                value = lower(value); if (!value.empty() && value.front() == '.') value.erase(0, 1);
                if (!google_host(value) || !domain_matches(source.host, value)) { accepted = false; break; }
                cookie.domain = value; cookie.host_only = false;
            } else if (key == "path" && !value.empty() && value[0] == '/') cookie.path = value;
            else if (key == "max-age") {
                long long age = 0;
                auto [p, ec] = std::from_chars(value.data(), value.data() + value.size(), age);
                if (ec == std::errc{} && p == value.data() + value.size()) {
                    cookie.expires = age <= 0 ? 1 : std::time(nullptr) + std::min<long long>(age, 400LL * 86400);
                    have_max_age = true;
                }
            } else if (key == "expires" && !have_max_age) {
                std::tm date{};
                if (strptime(value.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &date))
                    cookie.expires = std::max<long long>(1, timegm(&date));
            }
        }
        if (!accepted) continue;
        cookies_.erase(std::remove_if(cookies_.begin(), cookies_.end(), [&](const Cookie& old) {
            return old.name == cookie.name && old.domain == cookie.domain && old.path == cookie.path;
        }), cookies_.end());
        if (!cookie.value.empty() && (cookie.expires == 0 || cookie.expires > std::time(nullptr))) {
            if (cookies_.size() >= 128) continue;
            cookies_.push_back(std::move(cookie));
        }
    }
}
std::string Cookies::header(const Url& url) const {
    if (!google_host(url.host)) return {};
    std::string result;
    for (const auto& cookie : cookies_) {
        if (cookie.expires && cookie.expires <= std::time(nullptr)) continue;
        if (cookie.host_only ? url.host != cookie.domain : !domain_matches(url.host, cookie.domain)) continue;
        if (url.path.rfind(cookie.path, 0) != 0) continue;
        if (url.path.size() > cookie.path.size() && cookie.path.back() != '/' &&
            url.path[cookie.path.size()] != '/' && url.path[cookie.path.size()] != '?') continue;
        if (!result.empty()) result += "; ";
        result += cookie.name + "=" + cookie.value;
    }
    return result;
}
std::string Cookies::get(const std::string& name) const {
    for (auto it = cookies_.rbegin(); it != cookies_.rend(); ++it)
        if (it->name == name && (!it->expires || it->expires > std::time(nullptr))) return it->value;
    return {};
}
Json Cookies::dump() const {
    Json result = Json::array();
    for (const auto& c : cookies_)
        result.push_back({{"name", c.name}, {"value", c.value}, {"domain", c.domain},
                          {"path", c.path}, {"expires", c.expires}, {"host_only", c.host_only}});
    return result;
}
void Cookies::load(const Json& data) {
    cookies_.clear();
    if (!data.is_array()) return;
    for (const auto& entry : data) {
        if (!entry.is_object() || cookies_.size() >= 128) break;
        Cookie c{entry.value("name", ""), entry.value("value", ""), entry.value("domain", ""),
                 entry.value("path", "/"), entry.value("expires", 0LL), entry.value("host_only", false)};
        if (cookie_name(c.name) && cookie_value(c.value) && google_host(c.domain) &&
            !c.path.empty() && c.path.front() == '/' && (!c.expires || c.expires > std::time(nullptr)))
            cookies_.push_back(std::move(c));
    }
}

Json Model::selector(const std::string& session, bool extended) const {
    Json value = Json::array({1, nullptr, nullptr, nullptr, id, nullptr, nullptr, 0,
                              Json::array({4, 5, 6, 8}), nullptr, nullptr, nullptr, nullptr, nullptr, number});
    if (capacity_field == 13) value.insert(value.begin() + 11, nullptr);
    value[static_cast<size_t>(capacity_field - 1)] = capacity;
    value.push_back(extended ? 2 : 1); value.push_back(session);
    return value;
}
Json Model::dump() const {
    return {{"key", key}, {"id", id}, {"label", label}, {"category", category},
            {"number", number}, {"capacity", capacity}, {"capacity_field", capacity_field}};
}
Model Model::load(const Json& j) {
    Model m{j.value("key", ""), j.value("id", ""), j.value("label", ""), j.value("category", ""),
            j.value("number", 1), j.value("capacity", 1), j.value("capacity_field", 12)};
    if (m.id.empty() || m.id.size() > 128 || (m.capacity_field != 12 && m.capacity_field != 13) || m.number < 0 || m.number > 10000)
        throw Error("state", "Повреждён список моделей. Выполни .gwebrefresh.");
    return m;
}
Chat Chat::load(const Json& data) {
    Chat chat;
    if (data.is_object()) {
        const auto meta = data.value("metadata", Json());
        if (meta.is_array() && meta.size() <= 32) {
            chat.metadata = meta;
            while (chat.metadata.size() < 10) chat.metadata.push_back(nullptr);
        }
        chat.turns = data.value("turns", 0U);
    }
    return chat;
}
Json Chat::dump() const { return {{"metadata", metadata}, {"turns", turns}}; }

Client::Client(Transport transport) : transport_(std::move(transport)) { client_session = uuid(); }
void Client::load(const Json& j) {
    if (!j.is_object()) return;
    cookies.load(j.value("cookies", Json::array()));
    identity = j.value("identity", ""); selected = j.value("selected", "auto");
    access_token = j.value("access_token", ""); build_label = j.value("build_label", "");
    page_session = j.value("page_session", ""); language = j.value("language", "en");
    client_session = j.value("client_session", client_session); push_id = j.value("push_id", push_id);
    initialized_at = j.value("initialized_at", 0LL); rotated_at = j.value("rotated_at", 0LL);
    request_id = j.value("request_id", 10000U); timeout = std::clamp(j.value("timeout", 150), 30, 300);
    models.clear();
    for (const auto& model : j.value("models", Json::array())) {
        if (models.size() >= 64) break;
        models.push_back(Model::load(model));
    }
}
Json Client::dump() const {
    Json list = Json::array(); for (const auto& m : models) list.push_back(m.dump());
    return {{"cookies", cookies.dump()}, {"identity", identity}, {"selected", selected},
            {"access_token", access_token}, {"build_label", build_label}, {"page_session", page_session},
            {"language", language}, {"client_session", client_session}, {"push_id", push_id},
            {"initialized_at", initialized_at}, {"rotated_at", rotated_at}, {"request_id", request_id},
            {"timeout", timeout}, {"models", list}};
}

Response Client::request(const std::string& target, Request req, bool redirect) {
    std::string url = target;
    // Only bootstrap GETs follow redirects, restricted to HTTPS Google domains.
    // A generation POST is never replayed automatically.
    for (int attempt = 0; attempt < 6; ++attempt) {
        Url parsed = Url::parse(url);
        if (!google_host(parsed.host) || parsed.port != 443) throw Error("redirect", "Google перенаправил запрос на неожиданный адрес.");
        Request outgoing = req;
        outgoing.headers.emplace_back("User-Agent", browser);
        outgoing.headers.emplace_back("Accept", "*/*");
        auto cookie = cookies.header(parsed);
        if (!cookie.empty()) outgoing.headers.emplace_back("Cookie", cookie);
        Response result = transport_(url, outgoing);
        cookies.receive(parsed, result.headers);
        if (result.status < 300 || result.status > 399) return result;
        if (!redirect || req.method != "GET") throw Error("auth", "Google перенаправил запрос. Выполни .gwebrefresh или обнови cookies.");
        std::string location = result.header("location");
        if (location.empty()) throw Error("redirect", "Пустое перенаправление Google.");
        if (location.rfind("//", 0) == 0) location = "https:" + location;
        else if (location.front() == '/') location = parsed.origin() + location;
        else if (location.rfind("https://", 0) != 0) throw Error("redirect", "Неподдерживаемое перенаправление Google.");
        Url next = Url::parse(location);
        if (next.host == "accounts.google.com" || next.host == "consent.google.com")
            throw Error("auth", "Google требует вход или подтверждение в браузере. Открой gemini.google.com и обнови cookies.");
        url = location;
    }
    throw Error("redirect", "Слишком много перенаправлений Google.");
}
Response Client::probe() {
    Request req; req.timeout_seconds = 35;
    return request(app, req);
}
void Client::rotate() {
    if (cookies.get("__Secure-1PSID").empty()) throw Error("auth", "Сначала сохрани cookies через .gweblogin.");
    Request req; req.method = "POST"; req.timeout_seconds = 30;
    req.headers = {{"Content-Type", "application/json"}, {"Origin", "https://accounts.google.com"}};
    req.body = "[000,\"-0000000000000000000\"]";
    Response result = request("https://accounts.google.com/RotateCookies", req, false);
    status_error(result.status, "Обновление cookies"); rotated_at = std::time(nullptr);
}
std::string Client::query() {
    request_id = (request_id + 100000) % 2000000000U;
    std::string q = "?hl=" + encode(language) + "&_reqid=" + std::to_string(request_id) + "&rt=c";
    if (!build_label.empty()) q += "&bl=" + encode(build_label);
    if (!page_session.empty()) q += "&f.sid=" + encode(page_session);
    return q;
}
Response Client::rpc(const std::string& id, const Json& payload) {
    Request req; req.method = "POST"; req.timeout_seconds = 35;
    Json selector = Json::array({1, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                 Json::array({4, 5, 6, 8}), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr});
    selector.push_back(client_session);
    req.headers = {{"Content-Type", "application/x-www-form-urlencoded;charset=utf-8"},
                   {"Origin", "https://gemini.google.com"}, {"Referer", "https://gemini.google.com/"},
                   {"X-Same-Domain", "1"}, {"x-goog-ext-525001261-jspb", selector.dump()},
                   {"x-goog-ext-73010989-jspb", "[0]"}};
    Json envelope = Json::array({Json::array({Json::array({id, payload.dump(), nullptr, "generic"})})});
    req.body = "at=" + encode(access_token) + "&f.req=" + encode(envelope.dump());
    auto result = request(std::string(batch) + query() + "&rpcids=" + encode(id) + "&source-path=%2Fapp", req, false);
    status_error(result.status, "Получение состояния аккаунта"); return result;
}
void Client::discover() {
    Response result = rpc("otAQ7b", Json::array());
    bool found = false;
    std::vector<Model> available;
    for (const auto& envelope : frames(result.body)) visit_parts(envelope, [&](const Json& part) {
        if (string_field(part, {0}) != "wrb.fr" || string_field(part, {1}) != "otAQ7b") return;
        Json body = parse_json_string(field(part, {2}));
        if (!body.is_array()) return;
        found = true;
        const auto& status = field(body, {14});
        // The web protocol omits the normal/default status on some accounts.
        // Explicit rejection codes still fail; absence alone is not an error.
        if (!status.is_null() && !status.is_number_integer())
            throw Error("protocol", "Google изменил тип поля статуса аккаунта. Cookies сохранены; значения ответа не выводились.");
        if (status.is_number_integer() && status != 1000) {
            int code = status.get<int>();
            if (code == 1016) throw Error("auth", "Сессия Google не авторизована (1016). Обнови cookies.");
            if (code == 1040 || code == 1042) throw Error("account", "Открой Gemini в браузере и прими обновлённые условия сервиса.");
            throw Error("account", "Аккаунт Google ограничивает доступ к Gemini. Код " + std::to_string(code) + ". Проверь вход в браузере.");
        }
        int capacity = 1, capacity_field = 12;
        const auto& tiers = field(body, {16}); const auto& capabilities = field(body, {17});
        if (contains_number(tiers, 21)) { capacity = 1; capacity_field = 13; }
        else if (contains_number(tiers, 22)) { capacity = 2; capacity_field = 13; }
        else if (contains_number(capabilities, 115)) capacity = 4;
        else if (contains_number(tiers, 16) || contains_number(capabilities, 106)) capacity = 3;
        else if (contains_number(tiers, 8) || contains_number(capabilities, 19)) capacity = 2;
        const auto& list = field(body, {15});
        if (!list.is_array()) return;
        for (const auto& item : list) {
            Model model; model.id = string_field(item, {0});
            if (model.id.empty() || model.id.size() > 128 || available.size() >= 64) continue;
            model.category = string_field(item, {1});
            if (model.category.empty()) model.category = string_field(item, {10});
            model.label = string_field(item, {11});
            if (model.label.empty()) model.label = string_field(item, {19});
            if (model.label.empty()) model.label = model.category.empty() ? model.id : model.category;
            const auto& n = field(item, {17}).is_number_integer() ? field(item, {17}) : field(item, {9});
            model.number = n.is_number_integer() ? n.get<int>() : 1;
            model.key = model.number == 1 ? "flash" : model.number == 3 ? "pro" : model.number == 6 ? "lite" : model.id;
            if (std::any_of(available.begin(), available.end(), [&](const Model& m) { return m.key == model.key; })) model.key = model.id;
            model.capacity = capacity; model.capacity_field = capacity_field;
            available.push_back(std::move(model));
        }
    });
    if (!found) throw Error("protocol", "Google изменил формат состояния аккаунта. Текстовый запрос в режиме auto можно проверить отдельно.");
    models = std::move(available);
}
void Client::init(bool force) {
    if (cookies.get("__Secure-1PSID").empty()) throw Error("auth", "Cookies не сохранены. Инструкция по входу: .gwebcookies.");
    long long now = std::time(nullptr);
    if (!force && !access_token.empty() && now >= initialized_at && now - initialized_at < 600) return;
    initialized_at = 0;
    if (rotated_at && now - rotated_at >= 600) rotate();
    Response response = probe(); status_error(response.status, "Открытие Gemini");
    access_token = html_value(response.body, "SNlM0e");
    if (access_token.empty() && (!rotated_at || now - rotated_at >= 60)) {
        rotate(); response = probe(); status_error(response.status, "Открытие Gemini");
        access_token = html_value(response.body, "SNlM0e");
    }
    if (access_token.empty()) throw Error("auth", "В странице Gemini нет токена сессии SNlM0e. Проверь cookies, вход и доступность Gemini в браузере.");
    build_label = html_value(response.body, "cfb2h"); page_session = html_value(response.body, "FdrFJe");
    auto lang = html_value(response.body, "TuX5cc"); if (!lang.empty()) language = lang;
    auto push = html_value(response.body, "qKIAYe"); if (!push.empty()) push_id = push;
    // A failing account-status RPC must not silently turn into a guest request.
    discover();
    initialized_at = std::time(nullptr);
    if (!rotated_at) rotated_at = initialized_at;
}
const Model* Client::model(const std::string& name) const {
    for (const auto& m : models)
        if (lower(name) == lower(m.key) || lower(name) == lower(m.id) || lower(name) == lower(m.category) || lower(name) == lower(m.label)) return &m;
    return nullptr;
}
std::string Client::model_key(const std::string& name) const {
    if (lower(name) == "auto") return "auto";
    if (const Model* found = model(name)) return found->id;
    throw Error("model", "Эта модель не найдена у аккаунта. Список: .gwebmodel; обновление: .gwebrefresh.");
}

Json make_prompt(const std::string& prompt, const Chat& chat,
                 const std::string& language, const std::string& request_uuid, int model_number) {
    // Positional fields of Google's JSPB wire format, independently encoded.
    Json fields = Json::array(); for (int i = 0; i < 81; ++i) fields.push_back(nullptr);
    fields[0] = Json::array({prompt, 0, nullptr, nullptr, nullptr, nullptr, 0});
    fields[1] = Json::array({language}); fields[2] = chat.metadata;
    fields[6] = Json::array({1}); fields[7] = 1; fields[10] = 1; fields[11] = 0;
    fields[17] = Json::array({Json::array({0})}); fields[18] = 0;
    fields[27] = 1; fields[30] = Json::array({4}); fields[41] = Json::array({1});
    fields[53] = 0; fields[59] = request_uuid; fields[61] = Json::array();
    fields[68] = 1; fields[79] = model_number; fields[80] = 1;
    return fields;
}
Answer parse_answer(std::string_view wire, const Chat& before) {
    Answer answer; answer.chat = before;
    bool got_text = false, has_completion_flag = false, completed = false, has_context = false;
    for (const auto& envelope : frames(wire)) visit_parts(envelope, [&](const Json& part) {
        generation_error(part);
        if (string_field(part, {0}) == "er") throw Error("protocol", "Google вернул RPC-ошибку. Выполни .gwebtest; диалог сохранён.");
        if (string_field(part, {0}) != "wrb.fr") return;
        Json body = parse_json_string(field(part, {2}));
        if (!body.is_array()) return;
        const auto& meta = field(body, {1});
        if (meta.is_array()) {
            for (size_t i = 0; i < std::min<size_t>(meta.size(), 10); ++i)
                if (!meta[i].is_null() && !(meta[i].is_string() && meta[i].get_ref<const std::string&>().empty())) answer.chat.metadata[i] = meta[i];
        }
        const auto& context = field(body, {25});
        if (context.is_string()) { answer.chat.metadata[9] = context; has_context = true; }
        const auto& candidates = field(body, {4});
        if (!candidates.is_array() || candidates.empty()) return;
        const auto& candidate = candidates[0];
        std::string rcid = string_field(candidate, {0});
        std::string text = string_field(candidate, {1, 0});
        if (rcid.empty()) return;
        answer.chat.metadata[2] = rcid;
        if (!text.empty()) {
            answer.text = std::move(text); got_text = true;
            const auto& indicator = field(candidate, {8, 0});
            if (indicator.is_number_integer()) { has_completion_flag = true; completed = indicator == 2; }
        }
    });
    if (!got_text) throw Error("empty", "Google не вернул текст. Возможны ограничение запроса или изменение формата ответа. Диалог сохранён.");
    if (has_completion_flag && !completed && !has_context)
        throw Error("incomplete", "Поток Google завершился до окончания генерации. Частичный ответ не сохранён как полный; предыдущий диалог сохранён.");
    // Frames contain cumulative text; only the last snapshot is the final reply.
    if (answer.chat.turns < std::numeric_limits<unsigned>::max()) ++answer.chat.turns;
    return answer;
}
Answer Client::ask(const std::string& prompt, const Chat& chat) {
    if (prompt.empty()) throw Error("prompt", "Укажи вопрос или ответь командой на текст.");
    if (prompt.size() > 128 * 1024) throw Error("prompt", "Запрос превышает 128 КиБ.");
    init();
    const Model* selected_model = selected == "auto" ? nullptr : model(selected);
    if (selected != "auto" && !selected_model) throw Error("model", "Выбранная модель больше не доступна. Проверь .gwebmodel.");
    std::string request_uuid = uuid();
    Json payload = make_prompt(prompt, chat, language, request_uuid, selected_model ? selected_model->number : 1);
    Json envelope = Json::array({nullptr, payload.dump()});
    Request req; req.method = "POST"; req.timeout_seconds = timeout;
    req.headers = {{"Content-Type", "application/x-www-form-urlencoded;charset=utf-8"},
                   {"Origin", "https://gemini.google.com"}, {"Referer", "https://gemini.google.com/"},
                   {"X-Same-Domain", "1"}, {"x-goog-ext-525005358-jspb", Json::array({request_uuid, 1}).dump()}};
    if (selected_model) {
        req.headers.emplace_back("x-goog-ext-525001261-jspb", selected_model->selector(client_session).dump());
        req.headers.emplace_back("x-goog-ext-73010989-jspb", "[0]");
        req.headers.emplace_back("x-goog-ext-73010990-jspb", "[0,0,0]");
    }
    req.body = "at=" + encode(access_token) + "&f.req=" + encode(envelope.dump());
    Response result = request(std::string(generate) + query(), req, false);
    if (result.status == 400 || result.status == 401 || result.status == 403) initialized_at = 0;
    status_error(result.status, "Генерация");
    return parse_answer(result.body, chat);
}
} // namespace gweb
