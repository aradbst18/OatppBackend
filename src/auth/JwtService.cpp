#include "app/auth/JwtService.hpp"

#include <jwt-cpp/jwt.h>

#include "app/errors/AppException.hpp"

namespace app::auth
{

JwtService::JwtService(std::string secret, std::string issuer, std::chrono::seconds ttl)
    : secret_(std::move(secret)), issuer_(std::move(issuer)), ttl_(ttl)
{
    if (secret_.size() < 32)
    {
        throw errors::config::Invalid("JWT secret must be at least 32 characters");
    }

    if (issuer_.empty())
    {
        throw errors::config::Invalid("JWT issuer must not be empty");
    }

    if (ttl_.count() <= 0)
    {
        throw errors::config::Invalid("JWT TTL must be positive");
    }
}

std::string JwtService::create(const std::string& subject, const std::string& role) const
{
    if (subject.empty() || role.empty())
    {
        throw errors::validation::InvalidValue("JWT subject and role must not be empty");
    }

    try
    {
        const auto now = std::chrono::system_clock::now();
        return jwt::create()
            .set_issuer(issuer_)
            .set_subject(subject)
            .set_issued_at(now)
            .set_expires_at(now + ttl_)
            .set_payload_claim("role", jwt::claim(role))
            .sign(jwt::algorithm::hs256{secret_});
    }
    catch (const errors::AppException&)
    {
        throw;
    }
    catch (const std::exception& ex)
    {
        throw errors::crypto::TokenGenerationFailed(std::string("failed to sign JWT: ") + ex.what());
    }
}

TokenClaims JwtService::verify(const std::string& token) const
{
    if (token.empty())
    {
        throw errors::authentication::MissingCredentials("Missing token");
    }

    try
    {
        const auto decoded = jwt::decode(token);
        const auto verifier =
            jwt::verify().allow_algorithm(jwt::algorithm::hs256{secret_}).with_issuer(issuer_).leeway(30UL);
        verifier.verify(decoded);

        TokenClaims claims;
        claims.subject = decoded.get_subject();
        claims.issuer = decoded.get_issuer();

        if (decoded.has_payload_claim("role"))
        {
            claims.role = decoded.get_payload_claim("role").as_string();
        }

        claims.issuedAt = decoded.get_issued_at();
        claims.expiresAt = decoded.get_expires_at();

        if (claims.subject.empty())
        {
            throw errors::authentication::InvalidToken("Invalid token: empty subject");
        }

        return claims;
    }
    catch (const errors::AppException&)
    {
        throw;
    }
    catch (const jwt::error::token_verification_exception& ex)
    {
        // jwt-cpp uses one verification exception for bad signature, wrong
        // issuer and expiry. Distinguish expiry when the message says so so
        // clients can tell "re-login (expired)" from "re-login (invalid)".
        const std::string what = ex.what();

        if (what.find("expired") != std::string::npos || what.find("exp") != std::string::npos)
        {
            throw errors::authentication::TokenExpired("Invalid or expired token");
        }

        throw errors::authentication::InvalidToken("Invalid or expired token");
    }
    catch (const std::exception&)
    {
        // Malformed tokens map to 401 without echoing token content.
        throw errors::authentication::InvalidToken("Invalid token");
    }
}

std::string JwtService::extractBearer(const std::string& authorizationHeader)
{
    const std::string prefix = "Bearer ";

    if (authorizationHeader.size() <= prefix.size() || authorizationHeader.compare(0, prefix.size(), prefix) != 0)
    {
        throw errors::authentication::MissingCredentials("Missing or malformed Authorization header");
    }

    std::string token = authorizationHeader.substr(prefix.size());

    if (token.empty())
    {
        throw errors::authentication::MissingCredentials("Missing token");
    }

    return token;
}

}  // namespace app::auth
