#pragma once

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "oatpp/web/server/interceptor/RequestInterceptor.hpp"

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
    RateLimitInterceptor(size_t maxRequests = 10, std::chrono::seconds window = std::chrono::seconds(60));

    std::shared_ptr<OutgoingResponse> intercept(const std::shared_ptr<IncomingRequest>& request) override;

    /// Add an endpoint method+path (e.g. "POST /auth/login") to be rate limited.
    void addProtectedEndpoint(const std::string& methodAndPath);

    /// Helper to test or inspect whether an IP is currently rate-limited on an endpoint.
    bool checkAllowed(const std::string& key);

    /// Clear all rate-limit history (useful in tests).
    void reset();

    static std::string extractClientIp(const std::shared_ptr<IncomingRequest>& request);

private:
    struct ClientHistory
    {
        std::deque<std::chrono::steady_clock::time_point> timestamps;
    };

    size_t maxRequests_;
    std::chrono::seconds window_;
    std::unordered_set<std::string> protectedEndpoints_;

    std::mutex mutex_;
    std::unordered_map<std::string, ClientHistory> clients_;
};

}  // namespace app::middlewares
