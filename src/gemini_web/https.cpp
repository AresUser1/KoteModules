#include "https.hpp"

#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <climits>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace gweb {

std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}
std::string trim(std::string_view s) {
    auto white = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && white(s.front())) s.remove_prefix(1);
    while (!s.empty() && white(s.back())) s.remove_suffix(1);
    return std::string(s);
}
std::string encode(std::string_view s) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out += c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
std::string uuid() {
    std::array<unsigned char, 16> bytes{};
    if (RAND_bytes(bytes.data(), bytes.size()) != 1) throw Error("random", "Не удалось получить случайные данные.");
    bytes[6] = (bytes[6] & 15) | 64;
    bytes[8] = (bytes[8] & 63) | 128;
    constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out += '-';
        out += hex[bytes[i] >> 4]; out += hex[bytes[i] & 15];
    }
    return out;
}

Url Url::parse(const std::string& url) {
    if (url.rfind("https://", 0) != 0 || url.find_first_of("\r\n\t ") != std::string::npos)
        throw Error("url", "Допускаются только корректные HTTPS-адреса.");
    size_t end = url.find_first_of("/?#", 8);
    std::string authority = url.substr(8, end == std::string::npos ? end : end - 8);
    if (authority.empty() || authority.find('@') != std::string::npos)
        throw Error("url", "Некорректное имя HTTPS-сервера.");
    Url result;
    size_t colon = authority.find(':');
    result.host = lower(authority.substr(0, colon));
    if (result.host.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-") != std::string::npos)
        throw Error("url", "Некорректное имя HTTPS-сервера.");
    if (colon != std::string::npos) {
        unsigned port = 0;
        auto p = std::string_view(authority).substr(colon + 1);
        auto [ptr, ec] = std::from_chars(p.data(), p.data() + p.size(), port);
        if (ec != std::errc{} || ptr != p.data() + p.size() || !port || port > 65535)
            throw Error("url", "Некорректный порт HTTPS.");
        result.port = static_cast<unsigned short>(port);
    }
    result.path = end == std::string::npos ? "/" : url.substr(end);
    if (auto fragment = result.path.find('#'); fragment != std::string::npos) result.path.resize(fragment);
    if (result.path.empty() || result.path.front() != '/') result.path.insert(0, "/");
    return result;
}
std::string Url::origin() const {
    return "https://" + host + (port == 443 ? "" : ":" + std::to_string(port));
}
std::string Response::header(std::string_view name) const {
    for (const auto& [key, value] : headers) if (lower(key) == lower(std::string(name))) return value;
    return {};
}

static size_t number(std::string_view input, int base) {
    size_t value = 0;
    auto [p, ec] = std::from_chars(input.data(), input.data() + input.size(), value, base);
    if (input.empty() || ec != std::errc{} || p != input.data() + input.size() || value > max_response_bytes)
        throw Error("http", "Некорректная или слишком большая длина ответа.");
    return value;
}
void HttpDecoder::append(std::string_view data) {
    if (response_.body.size() + data.size() > max_response_bytes) throw Error("http", "Ответ превышает 24 МиБ.");
    response_.body.append(data);
}
void HttpDecoder::feed(std::string_view data) {
    wire_bytes_ += data.size();
    if (wire_bytes_ > max_response_bytes + 2 * 1024 * 1024) throw Error("http", "Слишком большой HTTP-ответ.");
    if (done_) return;
    buffer_.append(data);
    consume();
}
void HttpDecoder::consume() {
    if (!headers_) {
        size_t end = buffer_.find("\r\n\r\n");
        if (end == std::string::npos) {
            if (buffer_.size() > 65536) throw Error("http", "Слишком большие HTTP-заголовки.");
            return;
        }
        if (end > 65536) throw Error("http", "Слишком большие HTTP-заголовки.");
        size_t line = buffer_.find("\r\n");
        if (line < 12 || (buffer_.compare(0, 9, "HTTP/1.1 ") && buffer_.compare(0, 9, "HTTP/1.0 ")))
            throw Error("http", "Сервер вернул некорректный HTTP-ответ.");
        auto status = std::string_view(buffer_).substr(9, 3);
        int code = 0;
        auto [p, ec] = std::from_chars(status.data(), status.data() + status.size(), code);
        if (ec != std::errc{} || p != status.data() + 3 || code < 100 || code > 599)
            throw Error("http", "Некорректный HTTP-статус.");
        response_.status = code;
        size_t pos = line + 2;
        while (pos < end) {
            size_t next = buffer_.find("\r\n", pos);
            std::string_view h(buffer_.data() + pos, next - pos);
            size_t colon = h.find(':');
            if (colon == std::string_view::npos || colon == 0) throw Error("http", "Некорректный HTTP-заголовок.");
            response_.headers.emplace_back(lower(std::string(h.substr(0, colon))), trim(h.substr(colon + 1)));
            pos = next + 2;
        }
        buffer_.erase(0, end + 4);
        if (code >= 100 && code < 200 && code != 101) {
            response_ = {}; consume(); return;
        }
        headers_ = true;
        if (code == 204 || code == 304) { done_ = true; return; }
        std::string te = lower(response_.header("transfer-encoding"));
        if (!te.empty() && te != "chunked") throw Error("http", "Неподдерживаемое HTTP-кодирование.");
        chunked_ = te == "chunked";
        if (!chunked_) {
            std::string length = response_.header("content-length");
            if (length.empty()) until_eof_ = true;
            else {
                remaining_ = number(length, 10);
                for (const auto& [key, value] : response_.headers)
                    if (key == "content-length" && number(value, 10) != remaining_)
                        throw Error("http", "Противоречивые HTTP-длины.");
            }
        }
    }
    if (until_eof_) { append(buffer_); buffer_.clear(); return; }
    if (!chunked_) {
        size_t n = std::min(remaining_, buffer_.size());
        append(std::string_view(buffer_).substr(0, n)); buffer_.erase(0, n); remaining_ -= n;
        done_ = remaining_ == 0;
        return;
    }
    while (!done_) {
        if (trailers_) {
            size_t end = buffer_.find("\r\n");
            if (end == std::string::npos) {
                if (buffer_.size() + trailer_bytes_ > 65536) throw Error("http", "Слишком большие HTTP trailers.");
                return;
            }
            trailer_bytes_ += end + 2;
            if (trailer_bytes_ > 65536) throw Error("http", "Слишком большие HTTP trailers.");
            done_ = end == 0; buffer_.erase(0, end + 2); continue;
        }
        if (chunk_crlf_) {
            if (buffer_.size() < 2) return;
            if (buffer_.compare(0, 2, "\r\n")) throw Error("http", "Повреждённый HTTP chunk.");
            buffer_.erase(0, 2); chunk_crlf_ = false; chunk_size_ = true;
        }
        if (chunk_size_) {
            size_t end = buffer_.find("\r\n");
            if (end == std::string::npos) {
                if (buffer_.size() > 1024) throw Error("http", "Повреждённый заголовок HTTP chunk.");
                return;
            }
            std::string value = trim(std::string_view(buffer_).substr(0, end));
            if (auto ext = value.find(';'); ext != std::string::npos) value.resize(ext);
            remaining_ = number(value, 16); buffer_.erase(0, end + 2); chunk_size_ = false;
            if (!remaining_) { trailers_ = true; continue; }
        }
        size_t n = std::min(buffer_.size(), remaining_);
        append(std::string_view(buffer_).substr(0, n)); buffer_.erase(0, n); remaining_ -= n;
        if (remaining_) return;
        chunk_crlf_ = true;
    }
}
void HttpDecoder::eof() {
    if (headers_ && until_eof_) done_ = true;
    if (!done_) throw Error("http", "Соединение закрыто до получения полного ответа.");
}
Response HttpDecoder::take() {
    if (!done_) throw Error("http", "HTTP-ответ ещё не завершён.");
    std::string encoding = lower(response_.header("content-encoding"));
    if (!encoding.empty() && encoding != "identity") {
        if (encoding != "gzip" && encoding != "deflate") throw Error("http", "Неподдерживаемое сжатие ответа.");
        z_stream stream{};
        if (inflateInit2(&stream, encoding == "gzip" ? 31 : 15) != Z_OK) throw Error("http", "Ошибка декомпрессии.");
        struct End { z_stream* s; ~End() { inflateEnd(s); } } end{&stream};
        stream.next_in = reinterpret_cast<Bytef*>(response_.body.data());
        stream.avail_in = static_cast<uInt>(response_.body.size());
        std::string plain;
        std::array<unsigned char, 16384> out{};
        int rc;
        do {
            stream.next_out = out.data(); stream.avail_out = out.size();
            rc = inflate(&stream, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END) throw Error("http", "Повреждённый сжатый ответ.");
            plain.append(reinterpret_cast<char*>(out.data()), out.size() - stream.avail_out);
            if (plain.size() > max_response_bytes) throw Error("http", "Распакованный ответ превышает 24 МиБ.");
        } while (rc != Z_STREAM_END);
        response_.body = std::move(plain);
    }
    return std::move(response_);
}

namespace {
using Clock = std::chrono::steady_clock;
struct Fd {
    int value = -1;
    explicit Fd(int fd = -1) : value(fd) {}
    ~Fd() { if (value >= 0) close(value); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};
void wait_fd(int fd, bool write, Clock::time_point deadline) {
    while (true) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (left <= 0) throw Error("timeout", "Истекло время ожидания HTTPS-ответа.");
        pollfd p{fd, static_cast<short>(write ? POLLOUT : POLLIN), 0};
        int rc = poll(&p, 1, static_cast<int>(std::min<long long>(left, INT_MAX)));
        if (rc > 0) return;
        if (rc < 0 && errno != EINTR) throw Error("socket", "Ошибка ожидания сетевого соединения.");
    }
}
int connect_host(const Url& url, Clock::time_point deadline) {
    addrinfo hints{}; hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
    addrinfo* raw = nullptr;
    int rc = getaddrinfo(url.host.c_str(), std::to_string(url.port).c_str(), &hints, &raw);
    if (rc != 0) throw Error("dns", "Не удалось разрешить имя " + url.host + ". Проверь VPN или сеть.");
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses(raw, freeaddrinfo);
    int last_errno = 0;
    for (auto* a = raw; a; a = a->ai_next) {
        Fd fd(socket(a->ai_family, a->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, a->ai_protocol));
        if (fd.value < 0) { last_errno = errno; continue; }
        rc = connect(fd.value, a->ai_addr, a->ai_addrlen);
        if (rc < 0 && errno == EINPROGRESS) {
            // Do not let an unreachable IPv6 route use the entire request budget.
            try { wait_fd(fd.value, true, std::min(deadline, Clock::now() + std::chrono::seconds(6))); }
            catch (const Error&) { last_errno = ETIMEDOUT; continue; }
            int error = 0; socklen_t n = sizeof(error);
            if (getsockopt(fd.value, SOL_SOCKET, SO_ERROR, &error, &n) != 0) error = errno;
            rc = error ? -1 : 0; last_errno = error;
        } else if (rc < 0) last_errno = errno;
        if (rc == 0) { int result = fd.value; fd.value = -1; return result; }
        if (Clock::now() >= deadline) break;
    }
    if (last_errno == EACCES || last_errno == EPERM)
        throw Error("permission", "Android запретил модулю сеть в этом APK. Тест без обновления APK не пройден.");
    throw Error("connect", "Не удалось подключиться к " + url.host + ". Проверь VPN или сеть.");
}

// MSG_NOSIGNAL prevents a disconnected peer from terminating the Android process.
BIO_METHOD* socket_bio_method() {
    static const auto method = [] {
        std::unique_ptr<BIO_METHOD, decltype(&BIO_meth_free)> m(BIO_meth_new(BIO_TYPE_SOURCE_SINK, "gweb socket"), BIO_meth_free);
        if (!m) throw Error("tls", "Не удалось создать TLS BIO.");
        BIO_meth_set_create(m.get(), [](BIO* b) { BIO_set_init(b, 1); return 1; });
        BIO_meth_set_destroy(m.get(), [](BIO*) { return 1; });
        BIO_meth_set_write(m.get(), [](BIO* b, const char* data, int len) {
            BIO_clear_retry_flags(b);
            int fd = static_cast<int>(reinterpret_cast<intptr_t>(BIO_get_data(b)));
            int n;
#ifdef GWEB_TESTING
            // The build sandbox forbids sendto even for socketpair IPC. Tests
            // exclusively supply AF_UNIX socketpairs to connected(). Production
            // always uses MSG_NOSIGNAL on its own TCP connections.
            n = static_cast<int>(write(fd, data, static_cast<size_t>(len)));
#else
            n = static_cast<int>(send(fd, data, static_cast<size_t>(len), MSG_NOSIGNAL));
#endif
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) BIO_set_retry_write(b);
            return n;
        });
        BIO_meth_set_read(m.get(), [](BIO* b, char* data, int len) {
            BIO_clear_retry_flags(b);
            int fd = static_cast<int>(reinterpret_cast<intptr_t>(BIO_get_data(b)));
            int n = static_cast<int>(recv(fd, data, static_cast<size_t>(len), 0));
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) BIO_set_retry_read(b);
            return n;
        });
        BIO_meth_set_ctrl(m.get(), [](BIO*, int cmd, long, void*) -> long { return cmd == BIO_CTRL_FLUSH ? 1 : 0; });
        return m;
    }();
    return method.get();
}

bool load_ca_directory(SSL_CTX* ctx, const std::string& path) {
    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(path.c_str()), closedir);
    if (!dir) return false;
    bool found = false;
    while (auto* entry = readdir(dir.get())) {
        if (entry->d_name[0] == '.') continue;
        std::string file = path + "/" + entry->d_name;
        std::unique_ptr<FILE, decltype(&fclose)> pem(fopen(file.c_str(), "r"), fclose);
        if (!pem) continue;
        std::unique_ptr<X509, decltype(&X509_free)> cert(PEM_read_X509(pem.get(), nullptr, nullptr, nullptr), X509_free);
        if (cert && X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx), cert.get()) == 1) found = true;
        ERR_clear_error();
    }
    return found;
}

Response exchange(const Url& url, const Request& request, int fd,
                  Clock::time_point deadline, const std::string& ca_file = {}) {
    using Ctx = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>;
    using Tls = std::unique_ptr<SSL, decltype(&SSL_free)>;
    ERR_clear_error();
    Ctx ctx(SSL_CTX_new(TLS_client_method()), SSL_CTX_free);
    if (!ctx) throw Error("tls", "Не удалось создать TLS-клиент.");
    SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION);
    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER, nullptr);
    bool trust = false;
    if (!ca_file.empty()) {
        struct stat info{};
        if (stat(ca_file.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) trust = load_ca_directory(ctx.get(), ca_file);
        else trust = SSL_CTX_load_verify_locations(ctx.get(), ca_file.c_str(), nullptr) == 1;
    }
    else {
#ifdef __ANDROID__
        // Android filenames can use OpenSSL's *old* subject hash. Load the PEM
        // certificates explicitly instead of relying on OpenSSL 3's CApath hash.
        // Android 14+ moved the active system store into Conscrypt's APEX.
        for (const char* path : {"/apex/com.android.conscrypt/cacerts", "/system/etc/security/cacerts"}) {
            trust = load_ca_directory(ctx.get(), path);
            if (trust) break; // Use the active APEX store when it is available.
        }
#else
        trust = SSL_CTX_set_default_verify_paths(ctx.get()) == 1;
#endif
    }
    if (!trust) throw Error("certificates", "Не удалось открыть системные корневые сертификаты. TLS-проверка не отключалась.");
    Tls ssl(SSL_new(ctx.get()), SSL_free);
    if (!ssl || SSL_set_tlsext_host_name(ssl.get(), url.host.c_str()) != 1 || SSL_set1_host(ssl.get(), url.host.c_str()) != 1)
        throw Error("tls", "Ошибка настройки проверки имени TLS-сервера.");
    SSL_set_hostflags(ssl.get(), X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    constexpr unsigned char alpn[] = {8, 'h','t','t','p','/','1','.','1'};
    SSL_set_alpn_protos(ssl.get(), alpn, sizeof(alpn));
    BIO* bio = BIO_new(socket_bio_method());
    if (!bio) throw Error("tls", "Не удалось создать TLS-соединение.");
    BIO_set_data(bio, reinterpret_cast<void*>(static_cast<intptr_t>(fd)));
    SSL_set_bio(ssl.get(), bio, bio);
    auto retry = [&](int result, const char* stage) {
        int error = SSL_get_error(ssl.get(), result);
        if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
            wait_fd(fd, error == SSL_ERROR_WANT_WRITE, deadline); return;
        }
        if (SSL_get_verify_result(ssl.get()) != X509_V_OK)
            throw Error("certificate", "Сертификат HTTPS-сервера не прошёл проверку. Проверь дату телефона и VPN.");
#ifdef GWEB_TESTING
        fprintf(stderr, "TLS test failure: stage=%s result=%d ssl_error=%d errno=%d (%s)\n", stage, result, error, errno, strerror(errno));
        ERR_print_errors_fp(stderr);
#endif
        throw Error(stage, "TLS-соединение прервано или отклонено сервером.");
    };
    int rc;
    while ((rc = SSL_connect(ssl.get())) != 1) retry(rc, "handshake");
    std::string wire = request.method + " " + url.path + " HTTP/1.1\r\nHost: " + url.host;
    if (url.port != 443) wire += ":" + std::to_string(url.port);
    wire += "\r\nConnection: close\r\nAccept-Encoding: identity\r\n";
    for (const auto& [name, value] : request.headers) {
        if (name.empty() || name.find_first_of("\r\n: ") != std::string::npos || value.find_first_of("\r\n") != std::string::npos)
            throw Error("http", "Недопустимый HTTP-заголовок.");
        std::string key = lower(name);
        if (key == "host" || key == "connection" || key == "content-length" || key == "transfer-encoding" || key == "accept-encoding")
            throw Error("http", "Попытка переопределить служебный HTTP-заголовок.");
        wire += name + ": " + value + "\r\n";
    }
    if (request.method != "GET" || !request.body.empty()) wire += "Content-Length: " + std::to_string(request.body.size()) + "\r\n";
    wire += "\r\n"; wire += request.body;
    size_t sent = 0;
    while (sent < wire.size()) {
        if (Clock::now() >= deadline) throw Error("timeout", "Истекло время отправки HTTPS-запроса.");
        rc = SSL_write(ssl.get(), wire.data() + sent, static_cast<int>(std::min<size_t>(wire.size() - sent, 16384)));
        if (rc > 0) sent += static_cast<size_t>(rc); else retry(rc, "send");
    }
    HttpDecoder decoder;
    std::array<char, 16384> buffer{};
    while (!decoder.done()) {
        if (Clock::now() >= deadline) throw Error("timeout", "Gemini не завершил ответ за отведённое время. История сохранена.");
        rc = SSL_read(ssl.get(), buffer.data(), buffer.size());
        if (rc > 0) decoder.feed(std::string_view(buffer.data(), static_cast<size_t>(rc)));
        else if (SSL_get_error(ssl.get(), rc) == SSL_ERROR_ZERO_RETURN) { decoder.eof(); break; }
        else retry(rc, "receive");
    }
    return decoder.take();
}
} // namespace

Response HttpsTransport::operator()(const std::string& target, const Request& request) const {
    Url url = Url::parse(target);
    auto deadline = Clock::now() + std::chrono::seconds(std::clamp(request.timeout_seconds, 5, 300));
    Fd fd(connect_host(url, deadline));
    return exchange(url, request, fd.value, deadline);
}
#ifdef GWEB_TESTING
Response HttpsTransport::connected(const std::string& target, const Request& request,
                                  int owned_fd, const std::string& trusted_ca_file) const {
    Fd fd(owned_fd);
    int flags = fcntl(fd.value, F_GETFL, 0);
    if (flags < 0 || fcntl(fd.value, F_SETFL, flags | O_NONBLOCK) < 0) throw Error("socket", "fcntl failed");
    return exchange(Url::parse(target), request, fd.value,
                    Clock::now() + std::chrono::seconds(request.timeout_seconds), trusted_ca_file);
}
#endif
} // namespace gweb
