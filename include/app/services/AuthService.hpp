#pragma once

#include <memory>
#include <string>

#include "app/auth/JwtService.hpp"

namespace app::repositories
{
class UserRepository;
class RefreshTokenRepository;
}  // namespace app::repositories

namespace app::services
{

struct LoginResult
{
    std::string accessToken;
    std::string refreshToken;
    std::string username;
    std::string role;
    long long expiresInSeconds{0};

    LoginResult() = default;
    LoginResult(LoginResult&&) noexcept = default;
    LoginResult& operator=(LoginResult&&) noexcept = default;
    LoginResult(const LoginResult&) = default;
    LoginResult& operator=(const LoginResult&) = default;
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
    AuthService(std::shared_ptr<repositories::UserRepository> users, std::shared_ptr<auth::JwtService> jwt,
                std::shared_ptr<repositories::RefreshTokenRepository> refreshTokens = nullptr,
                std::chrono::seconds refreshTtl = std::chrono::hours(24 * 7));

    /// Seed an initial admin user if not already present.
    /// Uses username/password arguments if provided; otherwise checks environment
    /// variables APP_SEED_ADMIN_USERNAME / APP_SEED_ADMIN_PASSWORD, falling back
    /// to defaults for local development.
    void ensureSeeded(const std::string& username = "", const std::string& password = "");

    /// Validate credentials and issue a JWT (+ refresh token if configured).
    LoginResult login(const std::string& username, const std::string& password);

    /// Exchange a valid refresh token for a fresh access token and rotated refresh token.
    LoginResult refresh(const std::string& refreshToken);

    /// Resolve a verified token back to a display record.
    LoginResult whoAmI(const auth::TokenClaims& claims);

    static constexpr const char* kDefaultSeedUsername = "admin";
    static constexpr const char* kDefaultSeedPassword = "Admin1234";

    // Legacy aliases kept for compatibility
    static constexpr const char* kSeedUsername = kDefaultSeedUsername;
    static constexpr const char* kSeedPassword = kDefaultSeedPassword;

private:
    std::string generateRefreshToken() const;

    std::shared_ptr<repositories::UserRepository> users_;
    std::shared_ptr<auth::JwtService> jwt_;
    std::shared_ptr<repositories::RefreshTokenRepository> refreshTokens_;
    std::chrono::seconds refreshTtl_;
};

}  // namespace app::services
