#include "app/middlewares/RateLimitInterceptor.hpp"

#include <iterator>
#include <stdexcept>
#include <utility>

#include "oatpp/network/tcp/server/ConnectionProvider.hpp"

#include "app/errors/AppException.hpp"

namespace app::middlewares
{

RateLimitInterceptor::RateLimitInterceptor(size_t maxRequests, std::chrono::seconds window)
    : RateLimitInterceptor(Options{maxRequests, window, 10000, {}})
{
}

RateLimitInterceptor::RateLimitInterceptor(const Options& options, Now now)
    : maxRequests_(options.maxRequests),
      window_(options.window),
      maxClients_(options.maxClients),
      clientIpResolver_(options.trustedProxies),
      now_(std::move(now))
{
    if (maxRequests_ == 0 || window_.count() <= 0 || maxClients_ == 0 || !now_)
    {
        throw std::invalid_argument("Rate limit counts, window and clock must be valid and non-zero");
    }
    // By default, protect authentication and token refresh from brute-force attempts
    addProtectedEndpoint("POST /auth/login");
    addProtectedEndpoint("POST /auth/refresh");
}

void RateLimitInterceptor::addProtectedEndpoint(const std::string& methodAndPath)
{
    const auto separator = methodAndPath.find(' ');
    if (separator == std::string::npos || separator == 0 || separator + 1 == methodAndPath.size())
    {
        throw std::invalid_argument("Protected endpoints require a method and path");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    protectedEndpoints_.route(methodAndPath.substr(0, separator), methodAndPath.substr(separator + 1), methodAndPath);
}

std::optional<std::string> RateLimitInterceptor::extractClientIp(const std::shared_ptr<IncomingRequest>& request) const
{
    const auto connection = request->getConnection();
    if (!connection)
    {
        return std::nullopt;
    }
    using ExtendedConnection = oatpp::network::tcp::server::ConnectionProvider::ExtendedConnection;
    const auto peer =
        connection->getInputStreamContext().getProperties().get(ExtendedConnection::PROPERTY_PEER_ADDRESS);
    if (!peer)
    {
        return std::nullopt;
    }

    const auto xff = request->getHeader("X-Forwarded-For");
    std::string_view forwardedFor;
    if (xff)
    {
        forwardedFor = std::string_view(xff->data(), xff->size());
        if (forwardedFor.empty() || request->getHeaders().getAll().count("X-Forwarded-For") > 1)
        {
            // Reject ambiguous/empty forwarding only for trusted peers. Untrusted peers ignore it.
            forwardedFor = "invalid";
        }
    }
    return clientIpResolver_.resolve(std::string_view(peer->data(), peer->size()), forwardedFor);
}

bool RateLimitInterceptor::checkAllowed(const std::string& key)
{
    // Bound the size of stored keys, including callers of this public helper.
    if (key.empty() || key.size() > 512)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = now_();
    const auto cutoff = now - window_;

    // Fixed work budget: expire only the oldest buckets, never scan the map.
    for (size_t cleaned = 0; cleaned < 16 && !expiryOrder_.empty(); ++cleaned)
    {
        const auto& timestamps = expiryOrder_.front().timestamps;
        if (!timestamps.empty() && timestamps.back() > cutoff)
        {
            break;
        }
        clients_.erase(expiryOrder_.front().key);
        expiryOrder_.pop_front();
    }

    auto client = clients_.find(key);
    if (client == clients_.end())
    {
        if (clients_.size() >= maxClients_)
        {
            return false;
        }
        expiryOrder_.push_back(ClientHistory{key, {now}});
        try
        {
            clients_.emplace(key, std::prev(expiryOrder_.end()));
        }
        catch (...)
        {
            expiryOrder_.pop_back();
            throw;
        }
        return true;
    }

    auto history = client->second;
    while (!history->timestamps.empty() && history->timestamps.front() <= cutoff)
    {
        history->timestamps.pop_front();
    }
    if (history->timestamps.size() >= maxRequests_)
    {
        return false;
    }

    history->timestamps.push_back(now);
    expiryOrder_.splice(expiryOrder_.end(), expiryOrder_, history);
    return true;
}

void RateLimitInterceptor::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.clear();
    expiryOrder_.clear();
}

std::shared_ptr<RateLimitInterceptor::OutgoingResponse> RateLimitInterceptor::intercept(
    const std::shared_ptr<IncomingRequest>& request)
{
    std::string endpoint;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto& line = request->getStartingLine();
        auto route = protectedEndpoints_.getRoute(line.method, line.path);
        if (!route)
        {
            return nullptr;
        }
        // Use the registered endpoint so every URL accepted by its route shares one quota.
        endpoint = route.getEndpoint();
    }

    const auto ip = extractClientIp(request);
    if (!ip || !checkAllowed(endpoint + ":" + *ip))
    {
        throw errors::ratelimit::Exceeded("Rate limit exceeded for " + endpoint);
    }

    return nullptr;
}

}  // namespace app::middlewares
