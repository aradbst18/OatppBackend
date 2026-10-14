#include "support/TestRunner.hpp"

#include "support/TestDb.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include <openssl/sha.h>

#include "app/application/Application.hpp"
#include "app/config/AppSettings.hpp"

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
    ::inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    timeval tv{};
    tv.tv_sec = 3;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        ::close(fd);
        return -1;
    }
    return fd;
}

std::string recvAll(int fd, int attempts = 30)
{
    std::string out;
    char buf[4096];
    for (int i = 0; i < attempts; ++i)
    {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0)
        {
            break;
        }
        out.append(buf, static_cast<std::size_t>(n));
        if (out.find("\r\n\r\n") != std::string::npos)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return out;
}

void sendAll(int fd, const std::string& data)
{
    std::size_t sent = 0;
    while (sent < data.size())
    {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0)
        {
            throw std::runtime_error("ws send failed");
        }
        sent += static_cast<std::size_t>(n);
    }
}

// Minimal base64 (for Sec-WebSocket-Accept logging only; the test uses a fixed key).
std::string base64Encode(const unsigned char* data, std::size_t len)
{
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < len; i += 3)
    {
        const unsigned v = (static_cast<unsigned>(data[i]) << 16) | ((i + 1 < len ? static_cast<unsigned>(data[i + 1]) : 0) << 8) |
                           (i + 2 < len ? static_cast<unsigned>(data[i + 2]) : 0);
        out.push_back(k[(v >> 18) & 63]);
        out.push_back(k[(v >> 12) & 63]);
        out.push_back(i + 1 < len ? k[(v >> 6) & 63] : '=');
        out.push_back(i + 2 < len ? k[v & 63] : '=');
    }
    return out;
}

}  // namespace

TEST_CASE("websocket: base64 helper sanity")
{
    const std::string in = "hello";
    const std::string enc = base64Encode(reinterpret_cast<const unsigned char*>(in.data()), in.size());
    EXPECT_EQ(enc, std::string("aGVsbG8="));
}

TEST_CASE("websocket: echo roundtrip")
{
    // Boot the full application on the first free port in the test range.
    std::unique_ptr<application::Application> holder;
    int port = 0;
    for (int p = 18281; p < 18300; ++p)
    {
        config::AppSettings s = config::AppSettings::defaults();
        s.host = "127.0.0.1";
        s.port = static_cast<std::uint16_t>(p);
        s.databasePath = ":memory:";
#if defined(APP_DATABASE_SQLCIPHER)
        s.databaseKey = tests::testDatabaseKey();
#endif  // APP_DATABASE_SQLCIPHER
        s.jwtSecret = "test-secret-123";
        s.jwtIssuer = "test-issuer";
        s.logLevel = "ERROR";
        s.logFilePath = "";  // keep tests hermetic: no log files on disk
        try
        {
            holder = std::make_unique<application::Application>(std::move(s));
            holder->initialize();
            port = p;
            break;
        }
        catch (...)
        {
            holder.reset();
        }
    }
    EXPECT_TRUE(holder != nullptr);
    if (!holder)
    {
        return;
    }

    application::Application& app = *holder;
    std::thread serverThread(
        [&app]()
        {
            app.run();
        });

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
    EXPECT_TRUE(ready);

    bool echoOk = false;
    std::string failure;
    try
    {
        int fd = connectTo("127.0.0.1", port);
        if (fd < 0)
        {
            throw std::runtime_error("connect failed");
        }
        const std::string key = "dGhlIHNhbXBsZSBub25jZQ==";
        const std::string hs = "GET /ws/echo HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n" +
                               std::string("Connection: Upgrade\r\nSec-WebSocket-Key: ") + key + "\r\nSec-WebSocket-Version: 13\r\n\r\n";
        sendAll(fd, hs);
        const std::string resp = recvAll(fd);
        if (resp.find("101") == std::string::npos)
        {
            throw std::runtime_error("expected 101 upgrade, got: " + resp.substr(0, 200));
        }
        // Send one masked text frame "hello".
        const std::string payload = "hello";
        std::string frame;
        frame.push_back(static_cast<char>(0x81));                   // FIN + text
        frame.push_back(static_cast<char>(0x80 | payload.size()));  // masked, len 5
        const unsigned char mask[4] = {0x11, 0x22, 0x33, 0x44};
        frame.append(reinterpret_cast<const char*>(mask), 4);
        for (std::size_t i = 0; i < payload.size(); ++i)
        {
            frame.push_back(static_cast<char>(payload[i] ^ mask[i % 4]));
        }
        sendAll(fd, frame);
        // Read server frame (unmasked). The header and payload may arrive in
        // separate TCP segments, so loop until the full frame is present.
        std::string got;
        char buf[4096];
        for (int i = 0; i < 50 && got.size() < 2; ++i)
        {
            const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0)
            {
                break;
            }
            got.append(buf, static_cast<std::size_t>(n));
        }
        if (got.size() >= 2)
        {
            const std::size_t len = static_cast<unsigned char>(got[1]) & 0x7F;
            for (int i = 0; i < 50 && got.size() < 2 + len; ++i)
            {
                const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
                if (n <= 0)
                {
                    break;
                }
                got.append(buf, static_cast<std::size_t>(n));
            }
        }
        if (got.size() >= 2)
        {
            const unsigned char op = static_cast<unsigned char>(got[0]) & 0x0F;
            const std::size_t len = static_cast<unsigned char>(got[1]) & 0x7F;
            if (op == 0x1 && got.size() >= 2 + len)
            {
                const std::string text = got.substr(2, len);
                echoOk = (text == "Server echo: hello");
                if (!echoOk)
                {
                    failure = "unexpected echo payload: " + text;
                }
            }
            else
            {
                failure = "unexpected opcode/len";
            }
        }
        else
        {
            failure = "no frame received";
        }
        ::close(fd);
    }
    catch (const std::exception& ex)
    {
        failure = ex.what();
    }
    app.stop();
    int dfd = connectTo("127.0.0.1", port);
    if (dfd >= 0)
    {
        ::close(dfd);
    }
    serverThread.join();
    EXPECT_TRUE(echoOk);
}
