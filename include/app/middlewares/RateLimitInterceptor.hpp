#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "oatpp/web/server/HttpRouter.hpp"
#include "oatpp/web/server/interceptor/RequestInterceptor.hpp"

#include "app/infrastructure/ClientIpResolver.hpp"

namespace app::middlewares
{

/**
 * Sliding-window rate limiter interceptor for sensitive endpoints (e.g. login).
 *
 * Tracks request counts per client IP over a sliding time window.
 * Exceeding the threshold throws errors::ratelimit::Exceeded (HTTP 429).
 */
class RateLimitInterceptor : public oatpp::web::server::interceptor::RequestInterceptor
{
public:
    using Clock = std::chrono::steady_clock;
    using Now = std::function<Clock::time_point()>;

    struct Options
    {
        size_t maxRequests = 10;
        std::chrono::seconds window{60};
        size_t maxClients = 10000;
        std::string trustedProxies;
    };

    RateLimitInterceptor(size_t maxRequests = 10, std::chrono::seconds window = std::chrono::seconds(60));
    explicit RateLimitInterceptor(const Options& options, Now now = Clock::now);

    std::shared_ptr<OutgoingResponse> intercept(const std::shared_ptr<IncomingRequest>& request) override;

    /// Add an endpoint method+path (e.g. "POST /auth/login") to be rate limited.
    void addProtectedEndpoint(const std::string& methodAndPath);

    /// Consume one request in an endpoint/IP bucket. New buckets are rejected at capacity.
    bool checkAllowed(const std::string& key);

    /// Clear all rate-limit history (useful in tests).
    void reset();

    std::optional<std::string> extractClientIp(const std::shared_ptr<IncomingRequest>& request) const;

private:
    struct ClientHistory
    {
        std::string key;
        std::deque<Clock::time_point> timestamps;
    };

    size_t maxRequests_;
    std::chrono::seconds window_;
    size_t maxClients_;
    infrastructure::ClientIpResolver clientIpResolver_;
    Now now_;
    oatpp::web::server::HttpRouterTemplate<std::string> protectedEndpoints_;

    std::mutex mutex_;
    // Ordered by last accepted request; rejection never extends an entry's lifetime.
    std::list<ClientHistory> expiryOrder_;
    std::unordered_map<std::string, std::list<ClientHistory>::iterator> clients_;
};

}  // namespace app::middlewares
