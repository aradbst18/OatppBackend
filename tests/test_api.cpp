#include "support/TestRunner.hpp"

#include "support/TestDb.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include "app/application/Application.hpp"
#include "app/config/AppSettings.hpp"
#include "app/errors/AppException.hpp"
#include "app/errors/ErrorMapper.hpp"
#include "app/middlewares/AuthInterceptor.hpp"
#include "app/middlewares/CorsInterceptor.hpp"
#include "app/middlewares/RateLimitInterceptor.hpp"

using namespace app;

namespace
{

int connectTo(const std::string& host, int port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return -1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));

    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)
    {
        ::close(fd);
        return -1;
    }

    // 2s timeout for connect phase.
    timeval tv{};
    tv.tv_sec = 2;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        ::close(fd);
        return -1;
    }

    return fd;
}

std::string httpRoundTrip(const std::string& host, int port, const std::string& request)
{
    int fd = connectTo(host, port);
    if (fd < 0)
    {
        throw std::runtime_error("cannot connect to test server");
    }

    std::size_t sent = 0;
    while (sent < request.size())
    {
        const ssize_t n = ::send(fd, request.data() + sent, request.size() - sent, 0);
        if (n <= 0)
        {
            ::close(fd);
            throw std::runtime_error("send failed");
        }

        sent += static_cast<std::size_t>(n);
    }

    std::string out;
    char buf[4096];

    // Read until the server closes or timeout. Responses are small; one or two
    // recvs suffice, but loop until EAGAIN/timeout with Content-Length handled
    // by the caller parsing what we got.
    for (int i = 0; i < 20; ++i)
    {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0)
        {
            break;
        }

        out.append(buf, static_cast<std::size_t>(n));

        // Heuristic: stop once headers + declared body arrived.
        const auto hdrEnd = out.find("\r\n\r\n");
        if (hdrEnd != std::string::npos)
        {
            const auto clPos = out.find("Content-Length:");
            if (clPos != std::string::npos && clPos < hdrEnd)
            {
                const auto eol = out.find("\r\n", clPos);
                const int len = std::stoi(out.substr(clPos + 15, eol - clPos - 15));
                if (static_cast<int>(out.size() - hdrEnd - 4) >= len)
                {
                    break;
                }
            }
            else if (out.find("Transfer-Encoding: chunked") == std::string::npos)
            {
                // No body framing info; give the server one more recv then stop.
                const ssize_t m = ::recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
                if (m > 0)
                {
                    out.append(buf, static_cast<std::size_t>(m));
                }

                break;
            }
        }
    }

    ::close(fd);

    return out;
}

int statusOf(const std::string& response)
{
    // "HTTP/1.1 200 OK"
    const auto sp1 = response.find(' ');
    const auto sp2 = response.find(' ', sp1 + 1);

    return std::stoi(response.substr(sp1 + 1, sp2 - sp1 - 1));
}

std::string bodyOf(const std::string& response)
{
    const auto pos = response.find("\r\n\r\n");

    return pos == std::string::npos ? "" : response.substr(pos + 4);
}

config::AppSettings testSettings(int port)
{
    config::AppSettings s = config::AppSettings::defaults();
    s.host = "127.0.0.1";
    s.port = static_cast<std::uint16_t>(port);
    tests::applyTestDatabaseSettings(s);
    s.jwtSecret = "test-secret-123456789012345678901234";
    s.jwtIssuer = "test-issuer";
    s.jwtTtlSeconds = 3600;
    s.logLevel = "ERROR";
    s.logPath = "";  // keep tests hermetic: no log files on disk

    return s;
}

struct TestServer
{
    std::unique_ptr<application::Application> app;
    std::thread thread;
    int port{0};

    ~TestServer()
    {
        stop();
    }

    void stop()
    {
        if (app)
        {
            app->stop();
        }

        if (thread.joinable())
        {
            // Unblock accept() with a dummy connection, then join.
            int fd = connectTo("127.0.0.1", port);
            if (fd >= 0)
            {
                ::close(fd);
            }

            thread.join();
        }
    }
};

std::unique_ptr<TestServer> startServer()
{
    for (int port = 18081; port < 18100; ++port)
    {
        auto srv = std::make_unique<TestServer>();
        try
        {
            srv->app = std::make_unique<application::Application>(testSettings(port));
            srv->app->initialize();
        }
        catch (...)
        {
            continue;  // port in use or bind issue; try next
        }

        srv->port = port;
        srv->thread = std::thread(
            [a = srv->app.get()]()
            {
                a->run();
            });

        // Wait for readiness (up to ~3s).
        bool ready = false;
        for (int i = 0; i < 60; ++i)
        {
            int fd = connectTo("127.0.0.1", port);
            if (fd >= 0)
            {
                ::close(fd);
                ready = true;
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (ready)
        {
            return srv;
        }

        srv->stop();
    }

    throw std::runtime_error("could not start test server on any port");
}

std::string loginAndGetToken(const std::string& host, int port)
{
    const std::string payload = R"({"username":"admin","password":"Admin1234"})";
    const std::string req =
        "POST /auth/login HTTP/1.1\r\nHost: x\r\nContent-Type: "
        "application/json\r\nContent-Length: " +
        std::to_string(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload;

    const std::string resp = httpRoundTrip(host, port, req);
    if (statusOf(resp) != 200)
    {
        throw std::runtime_error("login failed in test setup: " + resp);
    }

    const std::string body = bodyOf(resp);
    const auto pos = body.find("access_token");
    if (pos == std::string::npos)
    {
        throw std::runtime_error("login response has no access_token: " + body);
    }

    const auto q1 = body.find('"', body.find(':', pos));
    const auto q2 = body.find('"', q1 + 1);

    return body.substr(q1 + 1, q2 - q1 - 1);
}

}  // namespace

TEST_CASE("api: login returns 200 with token")
{
    auto srv = startServer();

    const std::string token = loginAndGetToken("127.0.0.1", srv->port);
    EXPECT_TRUE(!token.empty());
}

TEST_CASE("api: login rejects bad credentials with 401")
{
    auto srv = startServer();

    const std::string payload = R"({"username":"admin","password":"Wrong1234"})";
    const std::string req =
        "POST /auth/login HTTP/1.1\r\nHost: x\r\nContent-Type: "
        "application/json\r\nContent-Length: " +
        std::to_string(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload;

    const std::string resp = httpRoundTrip("127.0.0.1", srv->port, req);
    EXPECT_EQ(statusOf(resp), 401);
}

TEST_CASE("api: login with Int8 overflow returns 400")
{
    auto srv = startServer();

    const std::string payload = R"({"username":"admin","password":"Admin1234","pinCode":64654646})";
    const std::string req =
        "POST /auth/login HTTP/1.1\r\nHost: x\r\nContent-Type: "
        "application/json\r\nContent-Length: " +
        std::to_string(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload;

    const std::string resp = httpRoundTrip("127.0.0.1", srv->port, req);

    // Deserialization range failure must fail closed with 400 (never 2xx, never
    // silently accepted). The parser message must not echo the offending value.
    EXPECT_EQ(statusOf(resp), 400);
    EXPECT_TRUE(bodyOf(resp).find("64654646") == std::string::npos);
}

TEST_CASE("api: who-am-i without token returns 401")
{
    auto srv = startServer();

    const std::string req = "GET /auth/who-am-i HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
    const std::string resp = httpRoundTrip("127.0.0.1", srv->port, req);
    EXPECT_EQ(statusOf(resp), 401);
}

TEST_CASE("api: who-am-i with token returns 200")
{
    auto srv = startServer();

    const std::string token = loginAndGetToken("127.0.0.1", srv->port);
    const std::string req =
        "GET /auth/who-am-i HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer " + token + "\r\nConnection: close\r\n\r\n";
    const std::string resp = httpRoundTrip("127.0.0.1", srv->port, req);

    EXPECT_EQ(statusOf(resp), 200);
    EXPECT_TRUE(bodyOf(resp).find("admin") != std::string::npos);
}

TEST_CASE("api: who-am-i preserves special characters in claims without delimiter collision")
{
    auto srv = startServer();

    auth::JwtService jwt("test-secret-123456789012345678901234", "test-issuer", std::chrono::seconds(3600));
    const std::string token = jwt.create("user|with|pipes", "viewer");
    const std::string req =
        "GET /auth/who-am-i HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer " + token + "\r\nConnection: close\r\n\r\n";

    const std::string resp = httpRoundTrip("127.0.0.1", srv->port, req);
    EXPECT_EQ(statusOf(resp), 200);
    EXPECT_TRUE(bodyOf(resp).find("\"username\":\"user|with|pipes\"") != std::string::npos);
    EXPECT_TRUE(bodyOf(resp).find("\"role\":\"viewer\"") != std::string::npos);
}

TEST_CASE("api: interceptor public/private classification")
{
    auto jwt = std::make_shared<auth::JwtService>("test-secret-123456789012345678901234", "test-issuer",
                                                  std::chrono::seconds(3600));
    middlewares::AuthInterceptor interceptor(jwt);

    EXPECT_TRUE(interceptor.isPublicPath("POST", "/auth/login"));

#ifdef APP_WITH_SWAGGER
    EXPECT_TRUE(interceptor.isPublicPath("GET", "/swagger/ui"));
#else
    // Swagger routes are not mounted in this build, so they stay protected.
    EXPECT_FALSE(interceptor.isPublicPath("GET", "/swagger/ui"));
#endif  // APP_WITH_SWAGGER

    EXPECT_TRUE(interceptor.isPublicPath("GET", "/ws/echo"));
    EXPECT_FALSE(interceptor.isPublicPath("GET", "/auth/who-am-i"));
    EXPECT_FALSE(interceptor.isPublicPath("POST", "/auth/login-evil"));
    EXPECT_TRUE(interceptor.isPublicPath("POST", "/auth/refresh"));
}

TEST_CASE("api: refresh token endpoint roundtrip and rotation")
{
    auto srv = startServer();

    // 1. Login
    const std::string loginPayload = R"({"username":"admin","password":"Admin1234"})";
    const std::string loginReq =
        "POST /auth/login HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\nContent-Length: " +
        std::to_string(loginPayload.size()) + "\r\nConnection: close\r\n\r\n" + loginPayload;
    const std::string loginResp = httpRoundTrip("127.0.0.1", srv->port, loginReq);
    EXPECT_EQ(statusOf(loginResp), 200);

    const std::string body = bodyOf(loginResp);
    const std::string key = "\"refresh_token\":\"";
    const auto pos = body.find(key);
    EXPECT_TRUE(pos != std::string::npos);
    const auto endPos = body.find('"', pos + key.size());
    EXPECT_TRUE(endPos != std::string::npos);
    const std::string refreshToken = body.substr(pos + key.size(), endPos - pos - key.size());
    EXPECT_FALSE(refreshToken.empty());

    // 2. Refresh with refresh token
    const std::string refreshPayload = R"({"refresh_token":")" + refreshToken + R"("})";
    const std::string refreshReq =
        "POST /auth/refresh HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\nContent-Length: " +
        std::to_string(refreshPayload.size()) + "\r\nConnection: close\r\n\r\n" + refreshPayload;
    const std::string refreshResp = httpRoundTrip("127.0.0.1", srv->port, refreshReq);
    EXPECT_EQ(statusOf(refreshResp), 200);

    const std::string refBody = bodyOf(refreshResp);
    const std::string tokenKey = "\"access_token\":\"";
    const auto tokPos = refBody.find(tokenKey);
    EXPECT_TRUE(tokPos != std::string::npos);
    const auto tokEndPos = refBody.find('"', tokPos + tokenKey.size());
    const std::string newAccessToken = refBody.substr(tokPos + tokenKey.size(), tokEndPos - tokPos - tokenKey.size());

    // 3. who-am-i with new access token succeeds
    const std::string whoReq = "GET /auth/who-am-i HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer " + newAccessToken +
                               "\r\nConnection: close\r\n\r\n";
    const std::string whoResp = httpRoundTrip("127.0.0.1", srv->port, whoReq);
    EXPECT_EQ(statusOf(whoResp), 200);

    // 4. Re-using the old refresh token fails with 401 (rotation)
    const std::string reRefreshResp = httpRoundTrip("127.0.0.1", srv->port, refreshReq);
    EXPECT_EQ(statusOf(reRefreshResp), 401);
}

TEST_CASE("api: error mapper maps codes to http status")
{
    EXPECT_EQ(errors::toHttpStatus(errors::ErrorCode::RequestInvalid), 400);
    EXPECT_EQ(errors::toHttpStatus(errors::ErrorCode::RequestMissing), 400);
    EXPECT_EQ(errors::toHttpStatus(errors::ErrorCode::InvalidCredentials), 401);
    EXPECT_EQ(errors::toHttpStatus(errors::ErrorCode::InvalidToken), 401);
    EXPECT_EQ(errors::toHttpStatus(errors::ErrorCode::TokenExpired), 401);
    EXPECT_EQ(errors::toHttpStatus(errors::ErrorCode::ResourceAccessDenied), 403);
    EXPECT_EQ(errors::toHttpStatus(errors::ErrorCode::ResourceNotFound), 404);
    EXPECT_EQ(errors::toHttpStatus(errors::ErrorCode::ValidationInvalidValue), 422);
    EXPECT_EQ(errors::toHttpStatus(errors::ErrorCode::DatabaseQueryFailed), 500);
    EXPECT_EQ(errors::toHttpStatus(errors::ErrorCode::ConfigurationInvalid), 500);
    EXPECT_EQ(errors::toHttpStatus(errors::ErrorCode::ExternalServiceInvalidResponse), 502);
    EXPECT_EQ(errors::statusFor(errors::ErrorCode::InvalidCredentials).code, 401);
    EXPECT_EQ(errors::toHttpStatus(errors::ErrorCode::RateLimited), 429);
}

TEST_CASE("api: rate limiter window and key isolation")
{
    middlewares::RateLimitInterceptor limiter(3, std::chrono::seconds(10));

    EXPECT_TRUE(limiter.checkAllowed("POST /auth/login:10.0.0.1"));
    EXPECT_TRUE(limiter.checkAllowed("POST /auth/login:10.0.0.1"));
    EXPECT_TRUE(limiter.checkAllowed("POST /auth/login:10.0.0.1"));
    EXPECT_FALSE(limiter.checkAllowed("POST /auth/login:10.0.0.1"));

    // Distinct client IP is not blocked
    EXPECT_TRUE(limiter.checkAllowed("POST /auth/login:10.0.0.2"));
}

TEST_CASE("api: rate limiter blocks requests exceeding threshold with 429")
{
    auto srv = startServer();

    const std::string payload = R"({"username":"admin","password":"WrongPassword123"})";
    const std::string req =
        "POST /auth/login HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\n"
        "X-Forwarded-For: 198.51.100.42\r\nContent-Length: " +
        std::to_string(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload;

    // Send up to the 10-request default limit
    for (int i = 0; i < 10; ++i)
    {
        const std::string resp = httpRoundTrip("127.0.0.1", srv->port, req);
        EXPECT_EQ(statusOf(resp), 401);
    }

    // 11th request from the same IP must be rate-limited with HTTP 429
    const std::string blockedResp = httpRoundTrip("127.0.0.1", srv->port, req);
    EXPECT_EQ(statusOf(blockedResp), 429);
    EXPECT_TRUE(bodyOf(blockedResp).find("Rate limit exceeded") != std::string::npos);
}

TEST_CASE("api: cors preflight OPTIONS returns 204 with headers")
{
    auto srv = startServer();

    const std::string req =
        "OPTIONS /auth/login HTTP/1.1\r\nHost: x\r\nOrigin: http://localhost:3000\r\n"
        "Access-Control-Request-Method: POST\r\nConnection: close\r\n\r\n";
    const std::string resp = httpRoundTrip("127.0.0.1", srv->port, req);

    EXPECT_EQ(statusOf(resp), 204);
    EXPECT_TRUE(resp.find("Access-Control-Allow-Origin: http://localhost:3000") != std::string::npos ||
                resp.find("Access-Control-Allow-Origin: *") != std::string::npos);
    EXPECT_TRUE(resp.find("Access-Control-Allow-Methods:") != std::string::npos);
    EXPECT_TRUE(resp.find("Access-Control-Allow-Headers:") != std::string::npos);
}

TEST_CASE("api: cors attaches headers to responses")
{
    auto srv = startServer();

    const std::string payload = R"({"username":"admin","password":"Admin1234"})";
    const std::string req =
        "POST /auth/login HTTP/1.1\r\nHost: x\r\nOrigin: https://app.example.com\r\nContent-Type: "
        "application/json\r\nContent-Length: " +
        std::to_string(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload;
    const std::string resp = httpRoundTrip("127.0.0.1", srv->port, req);

    EXPECT_EQ(statusOf(resp), 200);
    EXPECT_TRUE(resp.find("Access-Control-Allow-Origin:") != std::string::npos);
}

TEST_CASE("api: cors origin filtering unit test")
{
    middlewares::CorsInterceptor cors("http://localhost:3000, https://trusted.example.com");

    EXPECT_TRUE(cors.isOriginAllowed("http://localhost:3000"));
    EXPECT_TRUE(cors.isOriginAllowed("https://trusted.example.com"));
    EXPECT_FALSE(cors.isOriginAllowed("https://evil.com"));
}
