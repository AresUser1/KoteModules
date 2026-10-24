#include "module_abi.h"
#include "client.hpp"
#include "rich_text.hpp"
#include <set>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <zlib.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace gweb;
static unsigned checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #expr); } while (0)

template<class Function> void expect_error(const std::string& stage, Function fn) {
    try { fn(); } catch (const Error& error) { CHECK(error.stage == stage); return; }
    throw std::runtime_error("Expected error: " + stage);
}

static Json list(size_t size) {
    Json result = Json::array(); while (result.size() < size) result.push_back(nullptr); return result;
}
static std::string framed(const Json& root, bool prefix = true) {
    auto text = root.dump(-1, ' ', false);
    return (prefix ? ")]}'\n\n" : "") + std::to_string(text.size()) + "\n" + text + "\n";
}
static Json reply_part(const std::string& text, bool complete = true) {
    Json body = list(28), candidate = list(9);
    body[1] = Json::array({"c_test", "r_test"});
    candidate[0] = "rc_test"; candidate[1] = Json::array({text});
    candidate[8] = Json::array({complete ? 2 : 1});
    body[4] = Json::array({candidate});
    if (complete) body[25] = "continuation-token";
    return Json::array({"wrb.fr", nullptr, body.dump(), nullptr, nullptr, nullptr});
}
static std::string reply_wire(const std::string& text, bool complete = true) {
    return framed(Json::array({reply_part(text, complete)}));
}
static std::string decode(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') out += ' ';
        else if (s[i] == '%' && i + 2 < s.size()) {
            out += static_cast<char>(std::stoi(std::string(s.substr(i + 1, 2)), nullptr, 16)); i += 2;
        } else out += s[i];
    }
    return out;
}
static Json form(const std::string& data) {
    Json out = Json::object(); size_t pos = 0;
    while (pos < data.size()) {
        size_t end = data.find('&', pos), eq = data.find('=', pos);
        CHECK(eq != std::string::npos && (end == std::string::npos || eq < end));
        out[decode(std::string_view(data).substr(pos, eq - pos))] = decode(std::string_view(data).substr(eq + 1, end == std::string::npos ? end : end - eq - 1));
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return out;
}

static void test_http() {
    const std::string body = "Ответ 🌍\n";
    std::ostringstream hex; hex << std::hex << body.size();
    const std::string wire = "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nSet-Cookie: A=1\r\nSet-Cookie: B=2\r\n\r\n" +
        hex.str() + ";extension=yes\r\n" + body + "\r\n0\r\nX-Trailer: done\r\n\r\n";
    for (size_t fragment : {1U, 2U, 7U, 4096U}) {
        HttpDecoder parser;
        for (size_t i = 0; i < wire.size(); i += fragment) parser.feed(std::string_view(wire).substr(i, fragment));
        CHECK(parser.done()); auto response = parser.take(); CHECK(response.status == 200);
        CHECK(response.body == body); CHECK(response.headers.size() == 3);
    }
    HttpDecoder length;
    length.feed("HTTP/1.1 503 Unavailable\r\nContent-Length: 3\r\n\r\nno!");
    CHECK(length.done()); CHECK(length.take().body == "no!");
    HttpDecoder eof; eof.feed("HTTP/1.0 200 OK\r\n\r\nhello"); CHECK(!eof.done()); eof.eof(); CHECK(eof.take().body == "hello");
    expect_error("http", [] { HttpDecoder p; p.feed("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nx"); p.eof(); });
    expect_error("http", [] { HttpDecoder p; p.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc??"); });
    expect_error("http", [] { HttpDecoder p; p.feed("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nx"); });
    expect_error("http", [] { HttpDecoder p; p.feed("HTTP/1.1 200 OK\r\nContent-Length: 999999999\r\n\r\n"); });
    expect_error("http", [] { HttpDecoder p; p.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: strange\r\n\r\n"); });

    z_stream compressor{}; CHECK(deflateInit2(&compressor, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    std::string compressed(256, '\0');
    compressor.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(body.data())); compressor.avail_in = body.size();
    compressor.next_out = reinterpret_cast<Bytef*>(compressed.data()); compressor.avail_out = compressed.size();
    CHECK(deflate(&compressor, Z_FINISH) == Z_STREAM_END);
    compressed.resize(compressor.total_out); deflateEnd(&compressor);
    HttpDecoder gzip;
    gzip.feed("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: " + std::to_string(compressed.size()) + "\r\n\r\n" + compressed);
    CHECK(gzip.take().body == body);
}

static void test_protocol() {
    std::string text = "Привет 🌍\n```cpp\nstd::string s = \"[{}]\\\\\";\n```";
    std::string wire = framed(Json::array({reply_part("Привет", false)}));
    wire += framed(Json::array({reply_part(text)}), false);
    auto answer = parse_answer(wire, Chat{});
    CHECK(answer.text == text); CHECK(answer.chat.turns == 1);
    CHECK(answer.chat.metadata[0] == "c_test"); CHECK(answer.chat.metadata[1] == "r_test");
    CHECK(answer.chat.metadata[2] == "rc_test"); CHECK(answer.chat.metadata[9] == "continuation-token");
    CHECK(Chat::load(answer.chat.dump()).metadata == answer.chat.metadata);
    expect_error("incomplete", [] { parse_answer(reply_wire("partial", false), Chat{}); });
    expect_error("protocol", [] { frames(")]}'\n123\n[[\"unclosed\""); });
    expect_error("protocol", [] { frames("<html>login</html>"); });
    expect_error("empty", [] { parse_answer(framed(Json::array({Json::array({"wrb.fr", nullptr, "[]"})})), Chat{}); });
    for (const auto& [code, stage] : std::map<int, std::string>{{1037, "quota"}, {1050, "model"}, {1052, "model"}, {1060, "restricted"}, {1013, "upstream"}, {9999, "protocol"}}) {
        Json part = list(6); part[0] = "wrb.fr"; part[5] = Json::array({nullptr, nullptr, Json::array({Json::array({nullptr, Json::array({code})})})});
        expect_error(stage, [&] { parse_answer(framed(Json::array({part})), Chat{}); });
    }
    auto payload = make_prompt(text, answer.chat, "ru", "test-uuid", 3);
    CHECK(payload.size() == 81); CHECK(payload[0][0] == text); CHECK(payload[2] == answer.chat.metadata);
    CHECK(payload[59] == "test-uuid"); CHECK(payload[79] == 3);
    CHECK(Json::parse(Json::parse(Json::array({nullptr, payload.dump()}).dump())[1].get<std::string>()) == payload);
    CHECK(html_value(R"(<script>{"SNlM0e":"escaped\"token","cfb2h" : "build\u003d1"}</script>)", "SNlM0e") == "escaped\"token");
    CHECK(html_value(R"({"cfb2h" : "build\u003d1"})", "cfb2h") == "build=1");
    CHECK(html_value("<html>nothing</html>", "SNlM0e").empty());
    std::string long_text;
    for (int i = 0; i < 8000; ++i) long_text += "А🌍 ";
    auto chunks = split_text(long_text);
    std::string restored;
    for (const auto& chunk : chunks) {
        CHECK(!chunk.empty());
        Json encoded = chunk; CHECK(Json::parse(encoded.dump()).get<std::string>() == chunk);
        unsigned units = 0;
        for (unsigned char c : chunk) if ((c & 192) != 128) units += c >= 240 ? 2 : 1;
        CHECK(units <= 3500); restored += chunk;
    }
    CHECK(restored == long_text);
    CHECK(encode("a&b=Привет +") == "a%26b%3D%D0%9F%D1%80%D0%B8%D0%B2%D0%B5%D1%82%20%2B");
    expect_error("url", [] { Url::parse("http://gemini.google.com"); });
    expect_error("url", [] { Url::parse("https://google.com@evil.invalid/"); });
    expect_error("url", [] { Url::parse("https://google.com/\r\nCookie: no"); });
    CHECK(Url::parse("https://google.com?x=1").path == "/?x=1");
}

static void test_cookies() {
    Cookies jar; jar.login("secret-psid", "secret-ts");
    CHECK(jar.header(Url::parse("https://gemini.google.com/app")).find("secret-psid") != std::string::npos);
    CHECK(jar.header(Url::parse("https://accounts.google.com/RotateCookies")).find("secret-ts") != std::string::npos);
    CHECK(jar.header(Url::parse("https://google.com.evil.invalid/")).empty());
    CHECK(jar.header(Url::parse("https://content-push.googleapis.com/upload")).empty());
    jar.receive(Url::parse("https://accounts.google.com/RotateCookies"), {{"Set-Cookie", "__Secure-1PSIDTS=rotated; Domain=.google.com; Path=/; Secure; Max-Age=3600"}});
    CHECK(jar.get("__Secure-1PSIDTS") == "rotated");
    Cookies copy; copy.load(jar.dump()); CHECK(copy.get("__Secure-1PSIDTS") == "rotated");
    jar.receive(Url::parse("https://gemini.google.com/app"), {{"set-cookie", "hostcookie=onlyhere; Path=/app"}});
    CHECK(jar.header(Url::parse("https://accounts.google.com/app")).find("hostcookie") == std::string::npos);
    CHECK(jar.header(Url::parse("https://gemini.google.com/application")).find("hostcookie") == std::string::npos);
    CHECK(jar.header(Url::parse("https://gemini.google.com/app?q=x")).find("hostcookie") != std::string::npos);
    jar.receive(Url::parse("https://gemini.google.com/app"), {{"set-cookie", "bad=value; Domain=evil.invalid"}});
    CHECK(jar.get("bad").empty());
    jar.receive(Url::parse("https://gemini.google.com/app"), {{"set-cookie", "hostcookie=deleted; Path=/app; Expires=Thu, 01 Jan 1970 00:00:00 GMT"}});
    CHECK(jar.get("hostcookie").empty());
    jar.receive(Url::parse("https://accounts.google.com/RotateCookies"), {{"set-cookie", "__Secure-1PSIDTS=deleted; Domain=google.com; Max-Age=0"}});
    CHECK(jar.get("__Secure-1PSIDTS").empty());
    expect_error("cookies", [] { Cookies c; c.login("abc\r\nInjected: yes", ""); });
}

struct FakeGoogle {
    unsigned requests = 0, generations = 0, rotations = 0;
    int generate_status = 200, account_status = 1000;
    bool omit_status = false, sparse_status = false;
    Json last_prompt;
    std::string answer_text="Проверено 🌍";
    std::function<void()> before_generate;
    Response operator()(const std::string& target, const Request& req) {
        ++requests;
        Url url = Url::parse(target);
        CHECK(url.host == "gemini.google.com" || url.host == "accounts.google.com");
        if (url.path == "/app") {
            CHECK(req.method == "GET");
            return {200, {}, R"(<script>{"SNlM0e":"fake-access-token","cfb2h":"boq_build","FdrFJe":"12345","TuX5cc":"ru","qKIAYe":"push-test"}</script>)"};
        }
        if (url.host == "accounts.google.com") {
            CHECK(url.path == "/RotateCookies"); CHECK(req.method == "POST"); ++rotations;
            return {200, {{"set-cookie", "__Secure-1PSIDTS=new-token; Domain=.google.com; Path=/; Max-Age=3600"}}, "[]"};
        }
        auto fields = form(req.body); CHECK(fields["at"] == "fake-access-token");
        auto envelope = Json::parse(fields["f.req"].get<std::string>());
        if (url.path.find("batchexecute") != std::string::npos) {
            CHECK(envelope[0][0][0] == "otAQ7b"); CHECK(envelope[0][0][1] == "[]");
            Json data = list(18), flash = list(20), pro = list(20);
            if (!omit_status) data[14] = account_status;
            flash[0] = "flash-live-id"; flash[1] = "Fast"; flash[11] = "Gemini Flash (live)"; flash[17] = 1;
            pro[0] = "pro-live-id"; pro[1] = "Pro"; pro[11] = "Gemini Pro (live)"; pro[17] = 3;
            data[15] = Json::array({flash, pro}); data[16] = Json::array(); data[17] = Json::array({115});
            if (sparse_status) {
                Json sparse = {{"16",data[15]},{"17",data[16]},{"18",data[17]}};
                if (!omit_status) sparse["15"] = data[14];
                data = Json::array({sparse});
            }
            return {200, {}, framed(Json::array({Json::array({"wrb.fr", "otAQ7b", data.dump()})}))};
        }
        CHECK(url.path.find("StreamGenerate") != std::string::npos); CHECK(req.method == "POST");
        CHECK(envelope[0].is_null());
        last_prompt = Json::parse(envelope[1].get<std::string>()); ++generations;
        if(before_generate)before_generate();
        return {generate_status, {}, generate_status == 200 ? reply_wire(answer_text) : "not a JSON response"};
    }
};

static void test_client() {
    FakeGoogle google;
    Client client([&](const std::string& url, const Request& req) { return google(url, req); });
    client.cookies.login("fake-psid", "fake-ts"); client.identity = "account-a";
    client.init(); CHECK(client.models.size() == 2); CHECK(client.language == "ru");
    CHECK(client.model("pro")->id == "pro-live-id"); CHECK(client.model("flash")->capacity == 4);
    CHECK(client.model("pro")->selector("session")[11] == 4);
    Model tier13 = *client.model("pro"); tier13.capacity_field = 13; tier13.capacity = 2;
    CHECK(tier13.selector("session")[11].is_null()); CHECK(tier13.selector("session")[12] == 2);
    CHECK(tier13.selector("session")[15] == 3);
    client.selected = client.model_key("pro");
    auto first = client.ask("a&b=\"Привет\"", Chat{});
    CHECK(first.text == "Проверено 🌍"); CHECK(google.last_prompt[79] == 3);
    auto second = client.ask("Продолжи", first.chat);
    CHECK(google.last_prompt[2][0] == "c_test"); CHECK(google.last_prompt[2][9] == "continuation-token"); CHECK(second.chat.turns == 2);
    Client restored([&](const std::string& url, const Request& req) { return google(url, req); });
    restored.load(client.dump()); CHECK(restored.selected == "pro-live-id"); CHECK(restored.identity == "account-a");
    unsigned requests = google.requests;
    restored.ask("После перезапуска", second.chat); CHECK(google.requests == requests + 1);
    restored.rotated_at = 1; restored.initialized_at = 1; restored.init();
    CHECK(google.rotations == 1); CHECK(restored.cookies.get("__Secure-1PSIDTS") == "new-token");
    google.omit_status = true; restored.init(true);
    CHECK(restored.models.size() == 2); CHECK(restored.ask("Normal omitted status", Chat{}).text == "Проверено 🌍");
    google.sparse_status = true; restored.init(true);
    CHECK(restored.models.size() == 2); CHECK(restored.model("pro")->id == "pro-live-id");
    google.omit_status = false;
    google.account_status = 1016; expect_error("auth", [&] { restored.init(true); });
    google.sparse_status = false; google.account_status = 1000; restored.init(true);
    google.generate_status = 503; auto saved = second.chat.dump(); unsigned sends = google.generations;
    expect_error("upstream", [&] { restored.ask("не повторять", second.chat); });
    CHECK(google.generations == sends + 1); CHECK(second.chat.dump() == saved);
    expect_error("model", [&] { restored.model_key("nonexistent"); });
    google.account_status = 1016;
    expect_error("auth", [&] { restored.init(true); });
    Client redirect([](const std::string&, const Request&) { return Response{302, {{"Location", "https://evil.invalid/steal"}}, ""}; });
    redirect.cookies.login("secret", "");
    expect_error("redirect", [&] { redirect.probe(); });
}

// A real verified TLS connection over socketpair. No internet, DNS, Google
// account or TCP listener is required, even in the restricted build environment.
struct Certificate {
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1"), EVP_PKEY_free};
    std::unique_ptr<X509, decltype(&X509_free)> cert{X509_new(), X509_free};
    std::string path;
    Certificate() {
        CHECK(key && cert); CHECK(X509_set_version(cert.get(), 2) == 1);
        ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);
        X509_gmtime_adj(X509_getm_notBefore(cert.get()), -60); X509_gmtime_adj(X509_getm_notAfter(cert.get()), 3600);
        X509_set_pubkey(cert.get(), key.get());
        X509_NAME* name = X509_get_subject_name(cert.get());
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("localhost"), -1, -1, 0);
        X509_set_issuer_name(cert.get(), name);
        auto add = [&](int nid, const char* value) {
            X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, nullptr, nid, const_cast<char*>(value));
            CHECK(ext != nullptr); X509_add_ext(cert.get(), ext, -1); X509_EXTENSION_free(ext);
        };
        add(NID_basic_constraints, "critical,CA:TRUE"); add(NID_subject_alt_name, "DNS:localhost");
        CHECK(X509_sign(cert.get(), key.get(), EVP_sha256()) > 0);
        char file[] = "/tmp/gweb-test-ca-XXXXXX"; int fd = mkstemp(file); CHECK(fd >= 0); path = file;
        FILE* out = fdopen(fd, "w"); CHECK(out != nullptr); CHECK(PEM_write_X509(out, cert.get()) == 1); fclose(out);
    }
    ~Certificate() { if (!path.empty()) unlink(path.c_str()); }
};

static void tls_exchange(Certificate& certificate, const std::string& hostname, bool success, const std::string& ca_file) {
    int fds[2]; CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
    CHECK(context != nullptr); CHECK(SSL_CTX_use_certificate(context.get(), certificate.cert.get()) == 1);
    CHECK(SSL_CTX_use_PrivateKey(context.get(), certificate.key.get()) == 1);
    std::string received; std::atomic<bool> server_ok{false};
    std::jthread server([&] {
        timeval time{3, 0}; setsockopt(fds[1], SOL_SOCKET, SO_RCVTIMEO, &time, sizeof(time));
        setsockopt(fds[1], SOL_SOCKET, SO_SNDTIMEO, &time, sizeof(time));
        SSL* ssl = SSL_new(context.get()); SSL_set_fd(ssl, fds[1]);
        int accepted = SSL_accept(ssl);
        if (accepted == 1) {
            char buf[256]; int n;
            while ((n = SSL_read(ssl, buf, sizeof(buf))) > 0) {
                received.append(buf, static_cast<size_t>(n));
                if (received.find("\r\n\r\nrequest-body") != std::string::npos) break;
            }
            std::string response = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n";
            size_t offset = 0;
            while (offset < response.size()) {
                n = SSL_write(ssl, response.data() + offset, static_cast<int>(std::min<size_t>(7, response.size() - offset)));
                if (n <= 0) break;
                offset += static_cast<size_t>(n);
            }
            server_ok = offset == response.size();
        } else if (success) {
            std::cerr << "Server SSL_accept: " << SSL_get_error(ssl, accepted) << " errno=" << errno << '\n';
            ERR_print_errors_fp(stderr);
        }
        SSL_free(ssl); close(fds[1]);
    });
    Request req; req.method = "POST"; req.body = "request-body"; req.timeout_seconds = 3;
    req.headers = {{"Content-Type", "application/json"}};
    if (success) {
        auto response = HttpsTransport{}.connected("https://" + hostname + "/test", req, fds[0], ca_file);
        CHECK(response.status == 200); CHECK(response.body == "hello world");
    } else expect_error("certificate", [&] { HttpsTransport{}.connected("https://" + hostname + "/test", req, fds[0], ca_file); });
    server.join();
    if (success) {
        CHECK(server_ok.load()); CHECK(received.find("POST /test HTTP/1.1\r\n") == 0);
        CHECK(received.find("Content-Length: 12\r\n") != std::string::npos);
        CHECK(received.find("request-body") != std::string::npos);
    }
}

static bool entity(const gweb::ui::Text& text,int type,int offset,int length,long long document=0) {
    return std::any_of(text.entities.begin(),text.entities.end(),[&](const auto& e) {
        return e.type==type && e.offset==offset && e.length==length && e.document_id==document;
    });
}
static void valid_entities(const gweb::ui::Text& text) {
    using namespace gweb::ui;
    int size=units(text.text);std::set<int> boundaries{0};int offset=0;
    for(size_t i=0;i<text.text.size();) {auto n=character_bytes(text.text,i);offset+=n==4?2:1;i+=n;boundaries.insert(offset);}
    for(const auto& e:text.entities) {
        CHECK(e.offset>=0 && e.length>0 && e.offset+e.length<=size);
        CHECK(boundaries.count(e.offset) && boundaries.count(e.offset+e.length));
        int end_units=0;size_t end_byte=0;
        while(end_units<e.offset+e.length){auto n=character_bytes(text.text,end_byte);end_byte+=n;end_units+=n==4?2:1;}
        CHECK(!space(text.text[end_byte-1]));
        if(e.type==KOTO_ENT_CUSTOM_EMOJI)CHECK(e.length<=2 && e.document_id>0);
        if(e.type==KOTO_ENT_PRE || e.type==KOTO_ENT_CODE) for(const auto& other:text.entities)
            if(&e!=&other)CHECK(other.offset>=e.offset+e.length || other.offset+other.length<=e.offset);
    }
}
static void test_rich_text() {
    using namespace gweb::ui;
    auto text=markdown("### Заголовок\n* **Жирный** и *курсив*, `snake_case`\n> **Цитата**\n[Сайт](https://example.com/a(b))");
    CHECK(text.text=="Заголовок\n• Жирный и курсив, snake_case\nЦитата\nСайт");valid_entities(text);
    CHECK(entity(text,KOTO_ENT_BOLD,0,9));
    CHECK(std::any_of(text.entities.begin(),text.entities.end(),[](const auto& e){return e.type==KOTO_ENT_URL && e.url=="https://example.com/a(b)";}));
    CHECK(std::any_of(text.entities.begin(),text.entities.end(),[](const auto& e){return e.type==KOTO_ENT_BLOCKQUOTE;}));
    for(const auto& input:{"**a *b* c**","*a **b** c*","***both***","**a *b***","*a **b***"}) {
        auto value=markdown(input);valid_entities(value);CHECK(value.text.find('*')==std::string::npos);
        CHECK(std::any_of(value.entities.begin(),value.entities.end(),[](const auto& e){return e.type==KOTO_ENT_ITALIC;}));
        CHECK(std::any_of(value.entities.begin(),value.entities.end(),[](const auto& e){return e.type==KOTO_ENT_BOLD;}));
    }
    auto literal=markdown(R"(snake_case C:\tmp\file \*literal\* **unclosed)");
    CHECK(literal.text==R"(snake_case C:\tmp\file *literal* **unclosed)");
    auto code=markdown("```lua\nlocal x = \"**bold** ✅ ❌ 🧹\"\n-- `literal`\n```\n**После**");
    CHECK(code.text=="local x = \"**bold** ✅ ❌ 🧹\"\n-- `literal`\nПосле");valid_entities(code);
    CHECK(std::count_if(code.entities.begin(),code.entities.end(),[](const auto& e){return e.type==KOTO_ENT_PRE;})==1);
    CHECK(std::none_of(code.entities.begin(),code.entities.end(),[](const auto& e){return e.type==KOTO_ENT_CUSTOM_EMOJI;}));
    auto inline_code=markdown("**a `**literal** ✅` b**");valid_entities(inline_code);
    CHECK(inline_code.text=="a **literal** ✅ b");CHECK(inline_code.entities.size()==3);
    auto multi=markdown("`` `tick` `` and ~~deleted~~");CHECK(multi.text=="`tick` and deleted");valid_entities(multi);
    auto open=markdown("~~~python\nif True:\n    pass\n");CHECK(open.text=="if True:\n    pass\n");valid_entities(open);
    auto icons=markdown("🌍 **✅ Готово**\n❌ Ошибка\n🧹 Память");valid_entities(icons);
    CHECK(entity(icons,KOTO_ENT_CUSTOM_EMOJI,3,1,success_emoji));
    CHECK(std::any_of(icons.entities.begin(),icons.entities.end(),[](const auto& e){return e.document_id==failure_emoji && e.length==1;}));
    CHECK(std::any_of(icons.entities.begin(),icons.entities.end(),[](const auto& e){return e.document_id==clear_emoji && e.length==2;}));
    CHECK(plain("✅ done").entities.front().document_id==success_emoji);
    auto unsafe=markdown("[literal](javascript:alert(1))");CHECK(unsafe.entities.empty());CHECK(unsafe.text=="[literal](javascript:alert(1))");
    // Parse first, then split: every piece of a large code fence stays code.
    std::string lua;for(int i=0;i<500;++i)lua+="print(\"Привет 🌍 **literal** ✅\")\n";
    auto large=markdown("### Lua\n```lua\n"+lua+"```\n**Готово**");auto chunks=split(large);CHECK(chunks.size()>2);
    std::string restored;
    for(const auto& piece:chunks){CHECK(units(piece.text)<=3500);CHECK(piece.entities.size()<=80);valid_entities(piece);restored+=piece.text;}
    CHECK(restored==large.text);CHECK(restored=="Lua\n"+lua+"Готово");
    CHECK(std::none_of(large.entities.begin(),large.entities.end(),[](const auto& e){return e.type==KOTO_ENT_CUSTOM_EMOJI;}));
    auto long_bold=markdown("**"+std::string(9000,'a')+"**");chunks=split(long_bold);restored.clear();
    for(const auto& piece:chunks){valid_entities(piece);CHECK(entity(piece,KOTO_ENT_BOLD,0,units(piece.text)));restored+=piece.text;}CHECK(restored==long_bold.text);
    std::string dense;for(int i=0;i<400;++i)dense+="**a** ";auto many=markdown(dense);restored.clear();
    for(const auto& piece:split(many)){CHECK(piece.entities.size()<=80);valid_entities(piece);restored+=piece.text;}CHECK(restored==many.text);
    // Deterministic malformed/unbalanced syntax exercises bounds and termination.
    unsigned seed=7331;const std::string alphabet="ab_ *~`[]()>#\\\n";
    for(int i=0;i<120;++i){std::string input;for(int j=0;j<120;++j){seed=seed*1664525+1013904223;input+=alphabet[seed%alphabet.size()];}valid_entities(markdown(input));}
}

struct koto_ctx {
    std::map<std::string, std::string> db;
    std::vector<std::string> messages,kinds;
    std::vector<gweb::ui::Text> rendered;
    std::vector<gweb::ui::Entity> entities;
    bool outgoing=true;
    std::string input, reply, composed;
    long long chat = 42;
    int level = KOTO_LEVEL_OWNER;
};
static void test_module() {
    koto_api api{};
    api.db_get = [](koto_ctx* c, const char* key, char* out, int len) {
        auto it = c->db.find(key); if (it == c->db.end()) return -1;
        int n = static_cast<int>(it->second.size());
        if (len > 0) { std::memcpy(out, it->second.data(), static_cast<size_t>(std::min(n, len - 1))); out[std::min(n, len - 1)] = 0; }
        return n;
    };
    api.db_set = [](koto_ctx* c, const char* key, const char* value) { c->db[key] = value; };
    api.db_del = [](koto_ctx* c, const char* key) { c->db.erase(key); };
    api.db_keys = [](koto_ctx* c, char* out, int len) {
        std::string result; for (const auto& entry : c->db) result += entry.first + "\n";
        int n = static_cast<int>(result.size());
        if (len > 0) { std::memcpy(out, result.data(), static_cast<size_t>(std::min(n, len - 1))); out[std::min(n, len - 1)] = 0; }
        return n;
    };
    api.check_permission = [](koto_ctx* c, int level) { return c->level >= level ? 1 : 0; };
    api.cmd_args = [](koto_ctx* c) { return c->input.c_str(); };
    api.reply_text = [](koto_ctx* c) { return c->reply.c_str(); };
    api.setting_get = [](koto_ctx*, const char*, char* out, int len) { if (len > 1) { out[0] = '.'; out[1] = 0; } return 1; };
    api.chat_id = [](koto_ctx* c) { return c->chat; };
    api.is_outgoing = [](koto_ctx* c) { return c->outgoing?1:0; };
    api.c_reset = [](koto_ctx* c) { c->composed.clear();c->entities.clear(); };
    api.c_text = [](koto_ctx* c, const char* text) { c->composed += text; };
    api.c_fmt = [](koto_ctx* c,const char* text,int type) {auto offset=gweb::ui::units(c->composed);c->composed+=text;c->entities.push_back({type,offset,gweb::ui::units(text),0,{}});};
    api.c_entity=[](koto_ctx* c,int type,int offset,int length,long long document,const char* url) {c->entities.push_back({type,offset,length,document,url?url:""});};
    api.c_edit=[](koto_ctx* c,int) {c->messages.push_back(c->composed);c->kinds.push_back("edit");c->rendered.push_back({c->composed,c->entities});};
    api.c_reply=[](koto_ctx* c,int) {c->messages.push_back(c->composed);c->kinds.push_back("reply");c->rendered.push_back({c->composed,c->entities});};
    const koto_module* module = koto_module_register();
    CHECK(module->abi_version == 3); CHECK(std::string(module->name) == "gemini_web"); CHECK(module->command_count == 10);
    CHECK(module->min_host_version <= 26);
    FakeGoogle google; module_test_transport([&](const std::string& url, const Request& req) { return google(url, req); });
    koto_ctx context;
    bool expect_progress=false;
    google.before_generate=[&] {if(expect_progress) {CHECK(!context.messages.empty());CHECK(context.messages.back().find("Думаю…")!=std::string::npos);}};
    auto call = [&](const std::string& name, const std::string& arguments = "") {
        context.input = arguments; context.messages.clear();context.rendered.clear();context.kinds.clear();
        for (int i = 0; i < module->command_count; ++i) if (name == module->commands[i].name) {
            module->commands[i].handler(&context, &api); return;
        }
        throw std::runtime_error("Command missing: " + name);
    };
    auto output = [&] { std::string result; for (const auto& m : context.messages) result += m; return result; };
    context.reply = "test-psid\ntest-psidts";
    call("gweblogin"); context.reply.clear();
    CHECK(output().find("test-psid") == std::string::npos); CHECK(google.requests == 0);
    CHECK(Json::parse(context.db["state"])["cookies"].size() == 2);
    call("gwebtest"); CHECK(output().find("инициализация") != std::string::npos);
    expect_progress=true;call("gweb", "привет"); CHECK(output().find("Проверено 🌍") != std::string::npos);
    CHECK(context.messages.front().find("Думаю…")!=std::string::npos);CHECK(context.messages.back().find("Думаю…")==std::string::npos);
    std::string chat_key;
    for (const auto& [key, _] : context.db) if (key.rfind("chat:", 0) == 0) chat_key = key;
    CHECK(!chat_key.empty()); auto stored = context.db[chat_key];
    google.generate_status = 503;
    call("gweb", "ошибка сервера"); CHECK(output().find("503") != std::string::npos);
    CHECK(context.db[chat_key] == stored);
    CHECK(context.rendered.back().entities.front().document_id==gweb::ui::failure_emoji);
    google.generate_status = 200;
    call("gwebmodel", "pro"); call("gweb", "новая модель");
    unsigned chats = 0; for (const auto& [key, _] : context.db) if (key.rfind("chat:", 0) == 0) ++chats;
    CHECK(chats == 2); CHECK(google.last_prompt[2][0] == "");
    context.chat = 100; context.reply = "текст реплая"; call("gweb"); CHECK(google.last_prompt[0][0] == context.reply);
    context.reply.clear(); context.chat = 42; call("gwebclear");
    chats = 0; for (const auto& [key, _] : context.db) if (key.rfind("chat:", 0) == 0) ++chats;
    CHECK(chats == 1);
    call("gwebtimeout", "240"); CHECK(Json::parse(context.db["state"])["timeout"] == 240);
    call("gwebtimeout", "999"); CHECK(output().find("300") != std::string::npos);
    call("gwebstatus"); CHECK(output().find("test-psid") == std::string::npos); CHECK(output().find("fake-access-token") == std::string::npos);
    google.answer_text="### Ответ\n✅ **Готово**\n```lua\nprint(\"**literal** 🧹\")\n```";
    call("gweb","форматирование");CHECK(context.messages.size()==2);CHECK(context.kinds==std::vector<std::string>({"edit","edit"}));
    auto rendered=context.rendered.back();valid_entities(rendered);
    CHECK(rendered.text.find("###")==std::string::npos);CHECK(rendered.text.find("```lua")==std::string::npos);
    CHECK(rendered.text.find("print(\"**literal** 🧹\")")!=std::string::npos);
    CHECK(std::any_of(rendered.entities.begin(),rendered.entities.end(),[](const auto& e){return e.document_id==gweb::ui::success_emoji;}));
    CHECK(std::none_of(rendered.entities.begin(),rendered.entities.end(),[](const auto& e){return e.document_id==gweb::ui::clear_emoji;}));
    std::string long_lua;for(int i=0;i<300;++i)long_lua+="print(\"Привет 🌍 **literal** ✅\")\n";
    google.answer_text="```lua\n"+long_lua+"```";call("gweb","длинный код");CHECK(context.messages.size()>3);
    std::string joined;
    for(size_t i=1;i<context.rendered.size();++i) {
        const auto& piece=context.rendered[i];valid_entities(piece);CHECK(gweb::ui::units(piece.text)<=4096);
        auto body=piece.text;if(i==1)body=body.substr(body.find("\n\n")+2);joined+=body;
        CHECK(std::any_of(piece.entities.begin(),piece.entities.end(),[](const auto& e){return e.type==KOTO_ENT_PRE;}));
    }
    CHECK(joined==long_lua);
    expect_progress=false;context.outgoing=false;google.answer_text="**Ответ доверенному**";call("gweb","входящий запрос");
    CHECK(context.messages.size()==1);CHECK(context.kinds.back()=="reply");valid_entities(context.rendered.back());
    context.outgoing=true;expect_progress=true;
    call("gwebclear");CHECK(context.rendered.back().entities.front().document_id==gweb::ui::clear_emoji);
    call("gweb","");CHECK(context.messages.size()==1);CHECK(output().find("Думаю…")==std::string::npos);
    context.level = KOTO_LEVEL_ALL; unsigned requests = google.requests; call("gweb", "нет прав");
    CHECK(google.requests == requests); CHECK(context.messages.empty()); context.level = KOTO_LEVEL_OWNER;
    auto before_login = context.db["state"];
    call("gweblogin", "raw-secret-must-be-rejected");
    CHECK(context.db["state"] == before_login); CHECK(output().find("raw-secret-must-be-rejected") == std::string::npos);
    context.reply = "account-b"; call("gweblogin"); context.reply.clear();
    for (const auto& [key, _] : context.db) CHECK(key.rfind("chat:", 0) != 0);
    call("gweblogout"); CHECK(context.db.find("state") == context.db.end() || Json::parse(context.db["state"])["cookies"].empty());
    if (context.db.find("state") != context.db.end()) {
        auto state = context.db["state"]; CHECK(state.find("account-b") == std::string::npos);
    }
    call("gweb", "без входа"); CHECK(output().find("[auth]") != std::string::npos);

    // Dual persistence test with cache_dir fallback
    std::string test_cache = std::filesystem::temp_directory_path().string() + "/gweb_test_cache_" + std::to_string(std::time(nullptr));
    std::filesystem::create_directories(test_cache);
    std::string cache_dir_holder = test_cache;
    api.cache_dir = [](koto_ctx*, char* out, int len) -> int {
        if (!out || len <= 0) return 0;
        std::string d = std::filesystem::temp_directory_path().string();
        std::snprintf(out, len, "%s", d.c_str());
        return static_cast<int>(d.size());
    };
    context.reply = "persist-psid persist-psidts"; call("gweblogin"); context.reply.clear();
    std::string sfile = std::filesystem::temp_directory_path().string() + "/gemini_web_session.json";
    CHECK(std::filesystem::exists(sfile));
    // Simulate APK restart / DB loss:
    context.db.erase("state");
    call("gwebstatus");
    CHECK(output().find("сохранены") != std::string::npos);
    CHECK(context.db.find("state") != context.db.end());
    CHECK(context.db["state"].find("persist-psid") != std::string::npos);
    call("gweblogout");
    CHECK(!std::filesystem::exists(sfile));
    std::filesystem::remove_all(test_cache);
    module_test_transport({});
}

int main() {
    std::signal(SIGPIPE, SIG_IGN); // Test TLS server only; production uses MSG_NOSIGNAL.
    try {
        test_http(); std::cout << "HTTP framing, compression and truncation: OK\n";
        test_protocol(); std::cout << "Google envelopes, cumulative replies, metadata and UTF-16 splitting: OK\n";
        test_cookies(); std::cout << "Cookie scope, rotation persistence, deletion and injection rejection: OK\n";
        test_client(); std::cout << "Client bootstrap, account models, restart, errors and no POST replay: OK\n";
        Certificate certificate, other;
        tls_exchange(certificate, "localhost", true, certificate.path);
        // Android CA filenames need not match OpenSSL 3's subject hash.
        std::string ca_dir = certificate.path + "-dir";
        std::filesystem::create_directory(ca_dir);
        std::filesystem::copy_file(certificate.path, ca_dir + "/00000000.0");
        try { tls_exchange(certificate, "localhost", true, ca_dir); }
        catch (...) { std::filesystem::remove_all(ca_dir); throw; }
        std::filesystem::remove_all(ca_dir);
        tls_exchange(certificate, "wrong.example", false, certificate.path);
        tls_exchange(certificate, "localhost", false, other.path);
        std::cout << "Real TLS over socketpair: POST, CA verification, hostname rejection, untrusted-CA rejection: OK\n";
        test_rich_text(); std::cout << "Markdown entities, code/emoji preservation and formatted UTF-16 splitting: OK\n";
        test_module(); std::cout << "ABI command integration, permissions, account/model separation, clear and logout: OK\n";
        std::cout << checks << " checks passed. Google live requests and Android runtime not tested.\n";
    } catch (const Error& error) {
        std::cerr << "FAILED [" << error.stage << "]: " << error.what() << '\n';
        ERR_print_errors_fp(stderr); return 1;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n'; return 1;
    }
}
