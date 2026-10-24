#pragma once
#include "https.hpp"
#include "nlohmann/json.hpp"

namespace gweb {
using Json = nlohmann::json;

const Json& field(const Json& value, std::initializer_list<size_t> path);
std::string string_field(const Json& value, std::initializer_list<size_t> path);
std::vector<Json> frames(std::string_view wire);
std::string html_value(std::string_view html, const std::string& key);
std::vector<std::string> split_text(std::string_view text, size_t utf16_limit = 3500);

class Cookies {
public:
    struct Cookie { std::string name, value, domain, path; long long expires = 0; bool host_only = false; };
    void login(const std::string& psid, const std::string& psidts);
    void receive(const Url& source, const Headers& headers);
    std::string header(const Url& url) const;
    std::string get(const std::string& name) const;
    Json dump() const;
    void load(const Json& data);
private:
    std::vector<Cookie> cookies_;
};

struct Model {
    std::string key, id, label, category;
    int number = 1, capacity = 1, capacity_field = 12;
    Json selector(const std::string& session_id, bool extended = false) const;
    Json dump() const;
    static Model load(const Json& data);
};

struct Chat {
    Json metadata = Json::array({"", "", "", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, ""});
    unsigned turns = 0;
    static Chat load(const Json& data);
    Json dump() const;
};
struct Answer {
    std::string text;
    Chat chat;
};

// This object owns no threads or persistent sockets. Its state is persisted by
// the caller, including rotated cookies; unloading a module needs no callbacks.
class Client {
public:
    explicit Client(Transport transport = HttpsTransport{});
    Cookies cookies;
    std::vector<Model> models;
    std::string identity, selected = "auto", access_token, build_label, page_session, language = "en";
    std::string client_session, push_id = "feeds/mcudyrk2a4khkz";
    long long initialized_at = 0, rotated_at = 0;
    unsigned request_id = 10000;
    int timeout = 150;

    void load(const Json& data);
    Json dump() const;
    void init(bool force = false);
    void rotate();
    void discover();
    Answer ask(const std::string& prompt, const Chat& chat);
    Response probe();
    const Model* model(const std::string& name) const;
    std::string model_key(const std::string& name) const;
private:
    Transport transport_;
    Response request(const std::string& target, Request req, bool redirect = true);
    std::string query();
    Response rpc(const std::string& id, const Json& payload);
};

Answer parse_answer(std::string_view wire, const Chat& before);
Json make_prompt(const std::string& prompt, const Chat& chat,
                 const std::string& language, const std::string& request_uuid, int model_number);
#ifdef GWEB_TESTING
void module_test_transport(Transport transport);
#endif

} // namespace gweb
