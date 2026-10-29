#include "support/TestRunner.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

#include "oatpp/network/tcp/server/ConnectionProvider.hpp"

#include "app/errors/AppException.hpp"
#include "app/infrastructure/ClientIpResolver.hpp"
#include "app/middlewares/RateLimitInterceptor.hpp"

using app::infrastructure::ClientIpResolver;
using app::middlewares::RateLimitInterceptor;

namespace
{

std::shared_ptr<RateLimitInterceptor::IncomingRequest> requestWithPeer(
    const char* peer, const oatpp::web::protocol::http::Headers& headers = {}, const char* path = "/auth/login",
    const char* method = "POST")
{
    using ExtendedConnection = oatpp::network::tcp::server::ConnectionProvider::ExtendedConnection;
    oatpp::data::stream::Context::Properties properties;
    if (peer != nullptr)
    {
        properties.put(ExtendedConnection::PROPERTY_PEER_ADDRESS, peer);
    }
    // Oatpp inspects descriptor flags even when the test only reads stream metadata.
    const int descriptor = ::open("/dev/null", O_RDWR);
    EXPECT_TRUE(descriptor >= 0);
    std::shared_ptr<ExtendedConnection> connection;
    try
    {
        connection = std::make_shared<ExtendedConnection>(descriptor, std::move(properties));
    }
    catch (...)
    {
        ::close(descriptor);
        throw;
    }
    oatpp::web::protocol::http::RequestStartingLine line;
    line.method = method;
    line.path = path;
    line.protocol = "HTTP/1.1";
    return RateLimitInterceptor::IncomingRequest::createShared(connection, line, headers, nullptr, nullptr);
}

}  // namespace

TEST_CASE("rate_limit: routed aliases share endpoint quotas and fail closed")
{
    RateLimitInterceptor::Clock::time_point now{};
    RateLimitInterceptor::Options options;
    options.maxRequests = 1;
    RateLimitInterceptor limiter(options,
                                 [&now]
                                 {
                                     return now;
                                 });
    limiter.addProtectedEndpoint("POST /custom/login");
    for (const char* invalid : {"", "POST", " /auth/login", "POST "})
    {
        EXPECT_THROW_AS(limiter.addProtectedEndpoint(invalid), std::invalid_argument);
    }

    for (const std::string endpoint : {"/auth/login", "/auth/refresh", "/custom/login"})
    {
        const std::vector<std::string> aliases = {endpoint,           endpoint + "?probe=1", endpoint + "?probe=2",
                                                  endpoint + "?",     endpoint + "/",        "/" + endpoint,
                                                  endpoint.substr(1), endpoint + "//"};
        EXPECT_TRUE(limiter.intercept(requestWithPeer("192.0.2.7", {}, aliases[1].c_str())) == nullptr);
        for (const auto& alias : aliases)
        {
            EXPECT_THROW_AS(limiter.intercept(requestWithPeer("192.0.2.7", {}, alias.c_str())),
                            app::errors::ratelimit::Exceeded);
            EXPECT_THROW_AS(limiter.intercept(requestWithPeer(nullptr, {}, alias.c_str())),
                            app::errors::ratelimit::Exceeded);
        }
        EXPECT_TRUE(limiter.intercept(requestWithPeer("192.0.2.8", {}, endpoint.c_str())) == nullptr);
        EXPECT_TRUE(limiter.intercept(requestWithPeer("192.0.2.7", {}, endpoint.c_str(), "GET")) == nullptr);
        EXPECT_TRUE(limiter.intercept(requestWithPeer("192.0.2.7", {}, (endpoint + "-evil").c_str())) == nullptr);
    }

    now += options.window;
    EXPECT_TRUE(limiter.intercept(requestWithPeer("192.0.2.7", {}, "/auth/login?after-expiry")) == nullptr);
}

TEST_CASE("rate_limit: ignores forwarding from untrusted peers and normalizes IPs")
{
    ClientIpResolver resolver;
    EXPECT_EQ(resolver.resolve("192.0.2.7", "198.51.100.1").value(), "192.0.2.7");
    EXPECT_EQ(resolver.resolve("::ffff:192.0.2.7", "garbage").value(), "192.0.2.7");
    EXPECT_EQ(resolver.resolve("2001:0DB8:0:0::1").value(), "2001:db8::1");
    EXPECT_EQ(resolver.resolve("192.0.2.7", std::string(5000, 'x')).value(), "192.0.2.7");
    for (const auto* invalid : {"", "unknown", "192.0.2.7:1234", "[::1]", "127.1"})
    {
        EXPECT_FALSE(resolver.resolve(invalid, "198.51.100.1").has_value());
    }
}

TEST_CASE("rate_limit: walks proxy chains from the trusted end")
{
    ClientIpResolver resolver(" 192.0.2.10, 2001:db8::10 ");
    EXPECT_EQ(resolver.resolve("192.0.2.10", " 198.51.100.7 , 2001:db8::10 ").value(), "198.51.100.7");
    // Client-supplied prefix cannot override the first untrusted hop.
    EXPECT_EQ(resolver.resolve("192.0.2.10", "203.0.113.99, 198.51.100.7, 2001:db8::10").value(), "198.51.100.7");
    EXPECT_EQ(resolver.resolve("::ffff:192.0.2.10", "198.51.100.7").value(), "198.51.100.7");
    EXPECT_EQ(resolver.resolve("192.0.2.10").value(), "192.0.2.10");
    EXPECT_EQ(resolver.resolve("198.51.100.7", "203.0.113.99, 192.0.2.10").value(), "198.51.100.7");

    for (const auto* invalid : {"garbage", "198.51.100.7,", ",198.51.100.7", "198.51.100.7,,192.0.2.10",
                                "198.51.100.7:1234", "unknown, 198.51.100.7", " \t"})
    {
        EXPECT_FALSE(resolver.resolve("192.0.2.10", invalid).has_value());
    }
    EXPECT_FALSE(resolver.resolve("192.0.2.10", std::string(4097, 'x')).has_value());
    EXPECT_FALSE(resolver.resolve("192.0.2.10", std::string("198.51.100.7") + '\0' + "suffix").has_value());

    std::string chain = "198.51.100.7";
    for (int i = 0; i < 31; ++i)
    {
        chain += ",192.0.2.10";
    }
    EXPECT_EQ(resolver.resolve("192.0.2.10", chain).value(), "198.51.100.7");
    EXPECT_FALSE(resolver.resolve("192.0.2.10", chain + ",192.0.2.10").has_value());
}

TEST_CASE("rate_limit: validates proxy allowlists and limits")
{
    for (const auto* invalid : {"*", "localhost", "192.0.2.0/24", "192.0.2.10,", ",192.0.2.10", " "})
    {
        EXPECT_THROW_AS(ClientIpResolver{invalid}, std::invalid_argument);
    }
    RateLimitInterceptor::Options options;
    options.maxClients = 0;
    EXPECT_THROW_AS(RateLimitInterceptor{options}, std::invalid_argument);
    options.maxClients = 1;
    options.maxRequests = 0;
    EXPECT_THROW_AS(RateLimitInterceptor{options}, std::invalid_argument);
    options.maxRequests = 1;
    options.window = std::chrono::seconds(0);
    EXPECT_THROW_AS(RateLimitInterceptor{options}, std::invalid_argument);
}

TEST_CASE("rate_limit: reads transport metadata and fails closed without a peer")
{
    RateLimitInterceptor limiter(1);
    oatpp::web::protocol::http::Headers headers;
    headers.put("X-Forwarded-For", "198.51.100.1");
    headers.put("X-Real-IP", "198.51.100.2");
    EXPECT_EQ(limiter.extractClientIp(requestWithPeer("192.0.2.7", headers)).value(), "192.0.2.7");
    EXPECT_THROW_AS(limiter.intercept(requestWithPeer(nullptr, headers)), app::errors::ratelimit::Exceeded);
    EXPECT_THROW_AS(limiter.intercept(requestWithPeer("invalid", headers)), app::errors::ratelimit::Exceeded);
    auto request = requestWithPeer("192.0.2.7", headers);
    auto noConnection = RateLimitInterceptor::IncomingRequest::createShared(nullptr, request->getStartingLine(),
                                                                            headers, nullptr, nullptr);
    EXPECT_THROW_AS(limiter.intercept(noConnection), app::errors::ratelimit::Exceeded);
}

TEST_CASE("rate_limit: rejects ambiguous trusted forwarding and ignores X-Real-IP")
{
    RateLimitInterceptor::Options options;
    options.trustedProxies = "192.0.2.10";
    RateLimitInterceptor limiter(options);
    oatpp::web::protocol::http::Headers headers;
    headers.put("X-Real-IP", "198.51.100.2");
    EXPECT_EQ(limiter.extractClientIp(requestWithPeer("192.0.2.10", headers)).value(), "192.0.2.10");
    headers.put("X-Forwarded-For", "198.51.100.7");
    EXPECT_EQ(limiter.extractClientIp(requestWithPeer("192.0.2.10", headers)).value(), "198.51.100.7");
    headers.put("x-forwarded-for", "203.0.113.9");
    EXPECT_THROW_AS(limiter.intercept(requestWithPeer("192.0.2.10", headers)), app::errors::ratelimit::Exceeded);
    EXPECT_EQ(limiter.extractClientIp(requestWithPeer("192.0.2.7", headers)).value(), "192.0.2.7");
    oatpp::web::protocol::http::Headers emptyHeader;
    emptyHeader.put("X-Forwarded-For", "");
    EXPECT_THROW_AS(limiter.intercept(requestWithPeer("192.0.2.10", emptyHeader)), app::errors::ratelimit::Exceeded);
}

TEST_CASE("rate_limit: caps buckets without resetting active quotas and reclaims expired capacity")
{
    RateLimitInterceptor::Clock::time_point now{};
    RateLimitInterceptor::Options options;
    options.maxRequests = 2;
    options.maxClients = 2;
    options.window = std::chrono::seconds(10);
    RateLimitInterceptor limiter(options,
                                 [&now]
                                 {
                                     return now;
                                 });
    EXPECT_TRUE(limiter.checkAllowed("a"));
    EXPECT_TRUE(limiter.checkAllowed("a"));
    EXPECT_TRUE(limiter.checkAllowed("b"));
    for (int i = 0; i < 10000; ++i)
    {
        EXPECT_FALSE(limiter.checkAllowed("new-" + std::to_string(i)));
    }
    EXPECT_FALSE(limiter.checkAllowed("a"));
    EXPECT_TRUE(limiter.checkAllowed("b"));
    now += std::chrono::seconds(10);
    EXPECT_TRUE(limiter.checkAllowed("c"));
    EXPECT_TRUE(limiter.checkAllowed("a"));
    EXPECT_FALSE(limiter.checkAllowed("b"));
    limiter.reset();
    EXPECT_TRUE(limiter.checkAllowed("b"));
    EXPECT_FALSE(limiter.checkAllowed(std::string(513, 'x')));
}

TEST_CASE("rate_limit: preserves sliding windows and expiry order")
{
    RateLimitInterceptor::Clock::time_point now{};
    RateLimitInterceptor::Options options;
    options.maxRequests = 2;
    options.maxClients = 2;
    options.window = std::chrono::seconds(10);
    RateLimitInterceptor limiter(options,
                                 [&now]
                                 {
                                     return now;
                                 });
    EXPECT_TRUE(limiter.checkAllowed("a"));
    now += std::chrono::seconds(1);
    EXPECT_TRUE(limiter.checkAllowed("b"));
    now += std::chrono::seconds(4);
    EXPECT_TRUE(limiter.checkAllowed("a"));  // Move a behind b in expiry order.
    now += std::chrono::seconds(5);
    EXPECT_TRUE(limiter.checkAllowed("a"));  // Only the first request expired.
    EXPECT_FALSE(limiter.checkAllowed("a"));
    EXPECT_FALSE(limiter.checkAllowed("c"));
    now += std::chrono::seconds(1);
    EXPECT_TRUE(limiter.checkAllowed("c"));  // b expired; a is still protected.
    EXPECT_FALSE(limiter.checkAllowed("a"));
}

TEST_CASE("rate_limit: enforces quotas under concurrent requests")
{
    RateLimitInterceptor limiter(10);
    std::atomic<int> accepted{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back(
            [&]
            {
                for (int j = 0; j < 100; ++j)
                {
                    if (limiter.checkAllowed("same-peer"))
                    {
                        ++accepted;
                    }
                }
            });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
    EXPECT_EQ(accepted.load(), 10);
}
