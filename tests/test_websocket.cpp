#include "support/TestRunner.hpp"

#include "support/ScopedEnvironment.hpp"
#include "support/TestDb.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

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

std::string clientFrame(unsigned char opcode, const std::string& payload, bool fin = true)
{
    std::string frame(1, static_cast<char>((fin ? 0x80 : 0) | opcode));
    if (payload.size() < 126)
    {
        frame.push_back(static_cast<char>(0x80 | payload.size()));
    }
    else
    {
        const int lengthBytes = payload.size() <= 65535 ? 2 : 8;
        frame.push_back(static_cast<char>(0x80 | (lengthBytes == 2 ? 126 : 127)));
        for (int shift = (lengthBytes - 1) * 8; shift >= 0; shift -= 8)
        {
            frame.push_back(static_cast<char>((static_cast<std::uint64_t>(payload.size()) >> shift) & 0xFF));
        }
    }
    const unsigned char mask[4] = {0x11, 0x22, 0x33, 0x44};
    frame.append(reinterpret_cast<const char*>(mask), 4);
    for (std::size_t i = 0; i < payload.size(); ++i)
    {
        frame.push_back(static_cast<char>(static_cast<unsigned char>(payload[i]) ^ mask[i % 4]));
    }
    return frame;
}

std::string readExact(int fd, std::size_t size)
{
    std::string data(size, '\0');
    std::size_t received = 0;
    while (received < size)
    {
        const ssize_t count = ::recv(fd, data.data() + received, size - received, 0);
        if (count <= 0)
        {
            throw std::runtime_error("ws receive failed or timed out");
        }
        received += static_cast<std::size_t>(count);
    }
    return data;
}

void expectFrame(int fd, unsigned char opcode, const std::string& payload)
{
    const auto header = readExact(fd, 2);
    EXPECT_EQ(static_cast<unsigned char>(header[0]), static_cast<unsigned char>(0x80 | opcode));
    EXPECT_EQ(static_cast<unsigned char>(header[1]) & 0x80, 0);
    std::uint64_t length = static_cast<unsigned char>(header[1]) & 0x7F;
    if (length >= 126)
    {
        const auto extended = readExact(fd, length == 126 ? 2 : 8);
        length = 0;
        for (char byte : extended)
        {
            length = (length << 8) | static_cast<unsigned char>(byte);
        }
    }
    EXPECT_EQ(length, static_cast<std::uint64_t>(payload.size()));
    EXPECT_EQ(readExact(fd, payload.size()), payload);
}

void expectClose(int fd, unsigned int code)
{
    const auto header = readExact(fd, 2);
    EXPECT_EQ(static_cast<unsigned char>(header[0]), 0x88);
    const auto length = static_cast<unsigned char>(header[1]);
    EXPECT_TRUE(length >= 2 && length <= 125);
    const auto payload = readExact(fd, length);
    const auto receivedCode = (static_cast<unsigned char>(payload[0]) << 8) | static_cast<unsigned char>(payload[1]);
    EXPECT_EQ(static_cast<unsigned int>(receivedCode), code);
    char extra;
    EXPECT_EQ(::recv(fd, &extra, 1, 0), 0);
}

int connectWebSocket(int port)
{
    const int fd = connectTo("127.0.0.1", port);
    if (fd < 0)
    {
        throw std::runtime_error("ws connect failed");
    }
    try
    {
        sendAll(fd,
                "GET /ws/echo HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n"
                "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Sec-WebSocket-Version: 13\r\n\r\n");
        EXPECT_TRUE(recvAll(fd).find(" 101 ") != std::string::npos);
    }
    catch (...)
    {
        ::close(fd);
        throw;
    }
    return fd;
}

}  // namespace

TEST_CASE("websocket: echo roundtrip")
{
    const tests::ScopedEnvironment username("APP_SEED_ADMIN_USERNAME", "admin");
    const tests::ScopedEnvironment password("APP_SEED_ADMIN_PASSWORD", "TestAdmin8391");
    // Boot the full application on the first free port in the test range.
    std::unique_ptr<application::Application> holder;
    int port = 0;

    for (int p = 18281; p < 18300; ++p)
    {
        config::AppSettings s = config::AppSettings::defaults();
        s.host = "127.0.0.1";
        s.port = static_cast<std::uint16_t>(p);
        tests::applyTestDatabaseSettings(s);
        s.jwtSecret = "test-secret-123456789012345678901234";
        s.jwtIssuer = "test-issuer";
        s.logLevel = "ERROR";
        s.logPath = "";  // keep tests hermetic: no log files on disk

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

    std::string failure;
    int fd = -1;
    try
    {
        fd = connectWebSocket(port);
        const auto expectPong = [&]()
        {
            // A pong is an ordering barrier: an extra or premature echo fails immediately.
            sendAll(fd, clientFrame(0x9, "barrier"));
            expectFrame(fd, 0xA, "barrier");
        };
        const auto expectEcho = [&](unsigned char opcode, const std::string& payload)
        {
            sendAll(fd, clientFrame(opcode, payload));
            expectFrame(fd, opcode, opcode == 0x1 ? "Server echo: " + payload : payload);
            expectPong();
        };
        const std::vector<std::string> payloads = {"hello",
                                                   "",
                                                   std::string("a\0b", 3),
                                                   std::string("\0ab", 3),
                                                   std::string("ab\0", 3),
                                                   std::string("\0", 1),
                                                   std::string(125, 'a'),
                                                   std::string(126, 'a'),
                                                   std::string(1023, 'a'),
                                                   std::string(1024, 'a'),
                                                   std::string(1025, 'a'),
                                                   std::string(2048, 'a'),
                                                   std::string(16384, 'a'),
                                                   std::string(65536, 'a'),
                                                   std::string(1023, 'a') + "\xE2\x82\xAC",
                                                   "\xC2\x80\xDF\xBF\xE0\xA0\x80\xED\x9F\xBF\xEE\x80\x80\xEF\xBF\xBF"
                                                   "\xF0\x90\x80\x80\xF4\x8F\xBF\xBF"};
        for (const auto opcode : {static_cast<unsigned char>(0x1), static_cast<unsigned char>(0x2)})
        {
            for (const auto& payload : payloads)
            {
                expectEcho(opcode, payload);
            }
            sendAll(fd, clientFrame(opcode, "", false));
            expectPong();
            sendAll(fd, clientFrame(0x0, "a", false));
            expectPong();
            sendAll(fd, clientFrame(0x0, std::string("\0b", 2), false));
            expectPong();
            sendAll(fd, clientFrame(0x0, ""));
            expectFrame(fd, opcode, opcode == 0x1 ? std::string("Server echo: a\0b", 16) : std::string("a\0b", 3));
            expectPong();
            expectEcho(opcode, "after fragmentation");
            sendAll(fd, clientFrame(opcode, "", false) + clientFrame(0x0, ""));
            expectFrame(fd, opcode, opcode == 0x1 ? "Server echo: " : "");
            expectPong();
        }
        sendAll(fd, clientFrame(0x1, "\xE2", false));
        expectPong();
        sendAll(fd, clientFrame(0x0, "\x82", false));
        sendAll(fd, clientFrame(0x0, "\xAC"));
        expectFrame(fd, 0x1, "Server echo: \xE2\x82\xAC");
        expectEcho(0x1, "after split UTF-8");

        constexpr std::size_t limit = 1024 * 1024;
        for (const auto opcode : {static_cast<unsigned char>(0x1), static_cast<unsigned char>(0x2)})
        {
            expectEcho(opcode, std::string(limit, 'a'));
        }
        for (const auto opcode : {static_cast<unsigned char>(0x1), static_cast<unsigned char>(0x2)})
        {
            sendAll(fd, clientFrame(opcode, std::string(limit + 1, 'a')));
            expectClose(fd, 1009);
            ::close(fd);
            fd = -1;
            fd = connectWebSocket(port);

            sendAll(fd, clientFrame(opcode, std::string(limit, 'a'), false));
            expectPong();
            sendAll(fd, clientFrame(0x0, "a"));
            expectClose(fd, 1009);
            ::close(fd);
            fd = -1;
            fd = connectWebSocket(port);
        }
        // The reader must abort an oversized frame without waiting for its remaining bytes.
        const auto oversized = clientFrame(0x1, std::string(limit + 4096, 'a'));
        sendAll(fd, oversized.substr(0, 14 + limit + 1));
        expectClose(fd, 1009);
        ::close(fd);
        fd = -1;

        for (const auto& malformed : {"\x80", "\xC0\x80", "\xE0\x80\x80", "\xED\xA0\x80", "\xF0\x80\x80\x80",
                                      "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xE2\x82", "\xE2\x28\xAC"})
        {
            fd = connectWebSocket(port);
            sendAll(fd, clientFrame(0x1, malformed));
            expectClose(fd, 1007);
            ::close(fd);
            fd = -1;
        }
        fd = connectWebSocket(port);
        expectEcho(0x2, "\xFF\x80");
    }
    catch (const std::exception& ex)
    {
        failure = ex.what();
    }
    if (fd >= 0)
    {
        ::close(fd);
    }

    app.stop();

    int dfd = connectTo("127.0.0.1", port);
    if (dfd >= 0)
    {
        ::close(dfd);
    }

    serverThread.join();

    EXPECT_EQ(failure, std::string{});
}
