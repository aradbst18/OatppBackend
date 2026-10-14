#pragma once

#include <memory>
#include <string>

#include "app/auth/JwtService.hpp"
#include "app/repositories/UserRepository.hpp"

namespace app::services
{

struct LoginResult
{
    std::string accessToken;
    std::string username;
    std::string role;
    long long expiresInSeconds{0};
};

/**
 * Authentication use-cases. Owns credential checking + token issuance so
 * controllers stay thin (DTO in -> service call -> DTO out).
 *
 * Demo policy: users live in UserRepository; the database is seeded with a
 * deterministic admin account (admin / Admin1234) on first boot so the login
 * endpoint works without a separate user-management system.
 */
class AuthService
{
public:
    AuthService(std::shared_ptr<repositories::UserRepository> users, std::shared_ptr<auth::JwtService> jwt);

    void ensureSeeded();

    /// Validate credentials and issue a JWT. Throws Unauthorized/ValidationError.
    LoginResult login(const std::string& username, const std::string& password);

    /// Resolve a verified token back to a display record.
    LoginResult whoAmI(const auth::TokenClaims& claims);

    static constexpr const char* kSeedUsername = "admin";
    static constexpr const char* kSeedPassword = "Admin1234";

private:
    std::shared_ptr<repositories::UserRepository> users_;
    std::shared_ptr<auth::JwtService> jwt_;
};

}  // namespace app::services
