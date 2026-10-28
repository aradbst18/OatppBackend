#include "app/infrastructure/ClientIpResolver.hpp"

#include <arpa/inet.h>

#include <cstring>
#include <stdexcept>
#include <vector>

namespace app::infrastructure
{
namespace
{

std::string_view trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string_view::npos)
    {
        return {};
    }
    return value.substr(first, value.find_last_not_of(" \t") - first + 1);
}

std::optional<std::string> normalizeIp(std::string_view value)
{
    value = trim(value);
    if (value.empty() || value.size() >= INET6_ADDRSTRLEN || value.find('\0') != std::string_view::npos)
    {
        return std::nullopt;
    }

    const std::string text(value);
    in_addr ipv4{};
    in6_addr ipv6{};
    char buffer[INET6_ADDRSTRLEN]{};
    if (::inet_pton(AF_INET, text.c_str(), &ipv4) == 1)
    {
        ::inet_ntop(AF_INET, &ipv4, buffer, sizeof(buffer));
    }
    else if (::inet_pton(AF_INET6, text.c_str(), &ipv6) == 1)
    {
        if (IN6_IS_ADDR_V4MAPPED(&ipv6))
        {
            std::memcpy(&ipv4, &ipv6.s6_addr[12], sizeof(ipv4));
            ::inet_ntop(AF_INET, &ipv4, buffer, sizeof(buffer));
        }
        else
        {
            ::inet_ntop(AF_INET6, &ipv6, buffer, sizeof(buffer));
        }
    }
    else
    {
        return std::nullopt;
    }
    return std::string(buffer);
}

}  // namespace

ClientIpResolver::ClientIpResolver(std::string_view trustedProxies)
{
    if (trustedProxies.empty())
    {
        return;
    }
    while (true)
    {
        const auto comma = trustedProxies.find(',');
        const auto ip = normalizeIp(trustedProxies.substr(0, comma));
        if (!ip)
        {
            throw std::invalid_argument("server.trustedProxies must contain comma-separated literal IP addresses");
        }
        trustedProxies_.insert(*ip);
        if (comma == std::string_view::npos)
        {
            break;
        }
        trustedProxies.remove_prefix(comma + 1);
    }
}

std::optional<std::string> ClientIpResolver::resolve(std::string_view peer, std::string_view forwardedFor) const
{
    auto client = normalizeIp(peer);
    if (!client || trustedProxies_.count(*client) == 0 || forwardedFor.empty())
    {
        return client;
    }
    if (forwardedFor.size() > 4096)
    {
        return std::nullopt;
    }

    std::vector<std::string> hops;
    while (true)
    {
        const auto comma = forwardedFor.find(',');
        const auto ip = normalizeIp(forwardedFor.substr(0, comma));
        if (!ip || hops.size() == 32)
        {
            return std::nullopt;
        }
        hops.push_back(*ip);
        if (comma == std::string_view::npos)
        {
            break;
        }
        forwardedFor.remove_prefix(comma + 1);
    }

    for (auto it = hops.rbegin(); it != hops.rend() && trustedProxies_.count(*client) != 0; ++it)
    {
        client = *it;
    }
    return client;
}

}  // namespace app::infrastructure
