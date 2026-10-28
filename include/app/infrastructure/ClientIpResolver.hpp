#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace app::infrastructure
{

/// Resolves rate-limit identities from transport metadata and an opt-in proxy allowlist.
/// Only literal IPs are accepted; IPv4-mapped IPv6 addresses normalize to IPv4.
class ClientIpResolver
{
public:
    /// Comma-separated proxy IPs. Empty trusts no proxies; invalid entries throw invalid_argument.
    explicit ClientIpResolver(std::string_view trustedProxies = {});

    /// Untrusted peers ignore forwarding data. Invalid peers or trusted-proxy chains return nullopt.
    /// X-Forwarded-For is limited to 4096 bytes and 32 hops and traversed right to left.
    std::optional<std::string> resolve(std::string_view peer, std::string_view forwardedFor = {}) const;

private:
    std::unordered_set<std::string> trustedProxies_;
};

}  // namespace app::infrastructure
