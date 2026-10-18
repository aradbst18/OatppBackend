#include "app/middlewares/RateLimitInterceptor.hpp"

#include "app/errors/AppException.hpp"

namespace app::middlewares
{

RateLimitInterceptor::RateLimitInterceptor(size_t maxRequests, std::chrono::seconds window)
    : maxRequests_(maxRequests), window_(window)
{
    // By default, protect authentication and token refresh from brute-force attempts
    protectedEndpoints_ = {"POST /auth/login", "POST /auth/refresh"};
}

void RateLimitInterceptor::addProtectedEndpoint(const std::string& methodAndPath)
{
    std::lock_guard<std::mutex> lock(mutex_);
    protectedEndpoints_.insert(methodAndPath);
}

std::string RateLimitInterceptor::extractClientIp(const std::shared_ptr<IncomingRequest>& request)
{
    const auto xff = request->getHeader("X-Forwarded-For");
    if (xff && !xff->empty())
    {
        const std::string s = *xff;
        const auto comma = s.find(',');
        return (comma != std::string::npos) ? s.substr(0, comma) : s;
    }

    const auto realIp = request->getHeader("X-Real-IP");
    if (realIp && !realIp->empty())
    {
        return *realIp;
    }

    return "127.0.0.1";
}

bool RateLimitInterceptor::checkAllowed(const std::string& key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    const auto cutoff = now - window_;

    auto& history = clients_[key];
    while (!history.timestamps.empty() && history.timestamps.front() < cutoff)
    {
        history.timestamps.pop_front();
    }

    if (history.timestamps.size() >= maxRequests_)
    {
        return false;
    }

    history.timestamps.push_back(now);

    // Maintenance: occasionally sweep empty records if map grows large
    if (clients_.size() > 1000)
    {
        for (auto it = clients_.begin(); it != clients_.end();)
        {
            while (!it->second.timestamps.empty() && it->second.timestamps.front() < cutoff)
            {
                it->second.timestamps.pop_front();
            }
            if (it->second.timestamps.empty())
            {
                it = clients_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    return true;
}

void RateLimitInterceptor::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.clear();
}

std::shared_ptr<RateLimitInterceptor::OutgoingResponse> RateLimitInterceptor::intercept(
    const std::shared_ptr<IncomingRequest>& request)
{
    const std::string method = request->getStartingLine().method.std_str();
    const std::string path = request->getStartingLine().path.std_str();
    const std::string probe = method + " " + path;

    bool isProtected = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        isProtected = (protectedEndpoints_.find(probe) != protectedEndpoints_.end());
    }

    if (isProtected)
    {
        const std::string ip = extractClientIp(request);
        const std::string key = probe + ":" + ip;

        if (!checkAllowed(key))
        {
            throw errors::ratelimit::Exceeded("Rate limit exceeded for " + probe);
        }
    }

    return nullptr;
}

}  // namespace app::middlewares
