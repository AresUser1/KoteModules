#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace gweb {

struct Error : std::runtime_error {
    std::string stage;
    Error(std::string where, const std::string& message)
        : std::runtime_error(message), stage(std::move(where)) {}
};

using Headers = std::vector<std::pair<std::string, std::string>>;
struct Url {
    std::string host, path;
    unsigned short port = 443;
    static Url parse(const std::string& url);
    std::string origin() const;
};
struct Request {
    std::string method = "GET";
    Headers headers;
    std::string body;
    int timeout_seconds = 120;
};
struct Response {
    int status = 0;
    Headers headers;
    std::string body;
    std::string header(std::string_view name) const;
};

constexpr size_t max_response_bytes = 24 * 1024 * 1024;
std::string lower(std::string s);
std::string trim(std::string_view s);
std::string encode(std::string_view s);
std::string uuid();

// Incremental HTTP/1.1 framing: arbitrary TLS record boundaries are supported.
class HttpDecoder {
public:
    void feed(std::string_view bytes);
    void eof();
    bool done() const { return done_; }
    Response take();
private:
    Response response_;
    std::string buffer_;
    size_t remaining_ = 0, wire_bytes_ = 0, trailer_bytes_ = 0;
    bool headers_ = false, chunked_ = false, until_eof_ = false;
    bool chunk_size_ = true, chunk_crlf_ = false, trailers_ = false, done_ = false;
    void consume();
    void append(std::string_view data);
};

// No dependency on the APK's GET-only ABI, Java, curl executable or Python.
class HttpsTransport {
public:
    Response operator()(const std::string& url, const Request& request) const;
#ifdef GWEB_TESTING
    Response connected(const std::string& url, const Request& request,
                       int owned_fd, const std::string& trusted_ca_file) const;
#endif
};
using Transport = std::function<Response(const std::string&, const Request&)>;

} // namespace gweb
