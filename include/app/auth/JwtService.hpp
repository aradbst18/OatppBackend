#pragma once

#include <chrono>
#include <string>

namespace app::auth
{

struct TokenClaims
{
    std::string subject;  // username
    std::string role;     // canonical role string
    std::string issuer;
    std::chrono::system_clock::time_point issuedAt{};
    std::chrono::system_clock::time_point expiresAt{};
};

/**
 * JWT service wrapping jwt-cpp (HS256). All cryptography stays in this class;
 * controllers and middleware only see create()/verify() and never touch keys.
 *
 * Security rules:
 * - Tokens carry an expiry and are rejected after it (with clock skew of 30s).
 * - Verification checks signature, issuer and expiry together.
 * - Secrets/keys are never logged; error messages never echo token content.
 */
class JwtService
{
public:
    JwtService(std::string secret, std::string issuer, std::chrono::seconds ttl);

    /// Create a signed token for subject/role.
    std::string create(const std::string& subject, const std::string& role) const;

    /// Verify signature + issuer + expiry. Throws Unauthorized on any failure.
    TokenClaims verify(const std::string& token) const;

    /// Parse the Authorization header value ("Bearer <token>"). Throws on
    /// missing/malformed headers without revealing which part failed to callers.
    static std::string extractBearer(const std::string& authorizationHeader);

    const std::string& issuer() const noexcept
    {
        return issuer_;
    }

private:
    std::string secret_;
    std::string issuer_;
    std::chrono::seconds ttl_;
};

}  // namespace app::auth
