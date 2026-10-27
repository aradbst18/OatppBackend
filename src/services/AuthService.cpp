#include "app/services/AuthService.hpp"

#include <cstdlib>

#include "app/auth/PasswordHash.hpp"
#include "app/domain/UserRole.hpp"
#include "app/errors/AppException.hpp"
#include "app/logging/Logger.hpp"
#include "app/repositories/RefreshTokenDto.hpp"
#include "app/repositories/RefreshTokenRepository.hpp"
#include "app/repositories/UserRepository.hpp"
#include "app/validation/Validators.hpp"

namespace app::services
{

AuthService::AuthService(std::shared_ptr<repositories::UserRepository> users, std::shared_ptr<auth::JwtService> jwt,
                         std::shared_ptr<repositories::RefreshTokenRepository> refreshTokens,
                         std::chrono::seconds refreshTtl)
    : users_(std::move(users)), jwt_(std::move(jwt)), refreshTokens_(std::move(refreshTokens)), refreshTtl_(refreshTtl)
{
    if (!users_ || !jwt_)
    {
        throw errors::internal::OperationFailed("AuthService requires user repository and JWT service");
    }
    if (refreshTokens_ && !users_->sharesDatabaseWith(*refreshTokens_))
    {
        throw errors::internal::OperationFailed("Account and refresh token repositories must share a database");
    }
}

std::string AuthService::ensureSeeded(const std::string& customUsername, const std::string& customPassword)
{
    std::string seedUser = customUsername;
    if (seedUser.empty())
    {
        const char* envUser = std::getenv("APP_SEED_ADMIN_USERNAME");
        seedUser = (envUser != nullptr && envUser[0] != '\0') ? envUser : kDefaultSeedUsername;
    }

    if (!validation::isValidUsername(seedUser))
    {
        throw errors::config::Invalid("Bootstrap admin username must be 4-18 characters from [a-zA-Z0-9@#$]");
    }

    oatpp::Object<repositories::UserDto> existing;
    try
    {
        existing = users_->getUser(seedUser);
    }
    catch (const errors::database::RecordNotFound&)
    {
        // Only a missing account permits initial provisioning.
    }

    if (existing)
    {
        if (!existing->role || std::string(*existing->role) != domain::toString(domain::UserRole::Admin) ||
            !existing->passwordHash || existing->passwordHash->empty())
        {
            throw errors::config::Invalid("Bootstrap account must be an administrator with a stored password hash");
        }

        if (auth::passwordVerify("Admin1234", std::string(*existing->passwordHash)))
        {
            throw errors::config::Invalid(
                "Bootstrap administrator still uses the published sample password; reset its stored password "
                "and invalidate its sessions before restarting. APP_SEED_ADMIN_PASSWORD does not reset existing "
                "accounts");
        }

        logging::logInfo("auth", "Admin account '" + seedUser + "' already present; database ready");
        return seedUser;
    }

    std::string seedPass = customPassword;
    if (seedPass.empty())
    {
        const char* envPass = std::getenv("APP_SEED_ADMIN_PASSWORD");
        if (envPass != nullptr)
        {
            seedPass = envPass;
        }
    }

    if (seedPass.empty())
    {
        throw errors::config::Missing("APP_SEED_ADMIN_PASSWORD is required to create the initial administrator");
    }
    if (seedPass == "Admin1234")
    {
        throw errors::config::Invalid("Bootstrap admin password must not be the published sample password");
    }
    if (!validation::isValidPassword(seedPass) || seedPass.find('\0') != std::string::npos)
    {
        throw errors::config::Invalid(
            "Bootstrap admin password must be 6-48 characters with a letter and a digit, without NUL characters");
    }

    auto user = oatpp::Object<repositories::UserDto>::createShared();
    user->username = seedUser;
    user->passwordHash = auth::passwordHash(seedPass);
    user->role = domain::toString(domain::UserRole::Admin);

    users_->createUser(user);
    logging::logInfo("auth", "Seeded admin account '" + seedUser + "'");
    return seedUser;
}

LoginResult AuthService::login(const std::string& username, const std::string& password)
{
    // Shape validation first (422), then credential check (401) so clients can
    // distinguish malformed input from wrong credentials.
    if (!validation::isValidUsername(username))
    {
        throw errors::validation::InvalidValue("Invalid username: 4-18 chars of [a-zA-Z0-9@#$]");
    }

    if (!validation::isValidPassword(password))
    {
        throw errors::validation::InvalidValue("Invalid password: 6-48 chars with a letter and a digit");
    }

    auto transaction = users_->beginTransaction();
    users_->lockUserByUsername(username);
    oatpp::Object<repositories::UserDto> record;
    try
    {
        record = users_->getUser(username);
    }
    catch (const errors::database::RecordNotFound&)
    {
        // Unknown user and wrong password look identical (see below).
    }

    // Constant-shape failure: an unknown user, a wrong password and an
    // incomplete row all fail the same way.
    if (!record || !record->username || !record->passwordHash || !record->role ||
        !auth::passwordVerify(password, record->passwordHash->c_str()))
    {
        throw errors::authentication::InvalidCredentials("Invalid credentials");
    }

    LoginResult result;
    result.username = *record->username;
    result.role = *record->role;
    result.accessToken = jwt_->create(*record->id, result.role, record->sessionVersion.getValue(-1));
    result.expiresInSeconds = jwt_->ttl().count();

    if (refreshTokens_)
    {
        const std::string refreshToken = generateRefreshToken();
        const auto now = std::chrono::system_clock::now();
        const auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

        auto tokenRecord = repositories::RefreshTokenDto::createShared();
        tokenRecord->token = oatpp::String(refreshToken);
        tokenRecord->userId = record->id;
        tokenRecord->sessionVersion = record->sessionVersion;
        tokenRecord->expiresAt = nowSeconds + refreshTtl_.count();
        tokenRecord->revoked = 0;

        refreshTokens_->createToken(tokenRecord);
        result.refreshToken = refreshToken;
    }

    transaction.commit();
    logging::logInfo("auth", "Login succeeded for user '" + username + "'");
    return result;
}

LoginResult AuthService::refresh(const std::string& refreshToken)
{
    if (refreshToken.empty())
    {
        throw errors::request::Missing("refresh_token is required");
    }

    if (!refreshTokens_)
    {
        throw errors::internal::OperationFailed("Refresh token store is not configured");
    }

    auto transaction = users_->beginTransaction();
    oatpp::Object<repositories::RefreshTokenDto> record;
    try
    {
        record = refreshTokens_->getToken(oatpp::String(refreshToken));
    }
    catch (const errors::database::RecordNotFound&)
    {
        throw errors::authentication::InvalidToken("Invalid refresh token");
    }

    if (!record || !record->userId || !record->sessionVersion || !record->expiresAt)
    {
        throw errors::authentication::InvalidToken("Malformed refresh token record");
    }

    // PostgreSQL locks this account row; SQLCipher starts a write transaction.
    // Re-read after acquiring the lock so concurrent rotations cannot both win.
    users_->lockUserById(record->userId);
    try
    {
        record = refreshTokens_->getToken(oatpp::String(refreshToken));
    }
    catch (const errors::database::RecordNotFound&)
    {
        throw errors::authentication::InvalidToken("Invalid refresh token");
    }

    if (record->revoked && record->revoked.getValue(0) != 0)
    {
        throw errors::authentication::InvalidToken("Refresh token has been revoked");
    }

    const auto now = std::chrono::system_clock::now();
    const auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    if (record->expiresAt.getValue(0) <= nowSeconds)
    {
        throw errors::authentication::TokenExpired("Refresh token has expired");
    }

    // Verify user still exists in database and fetch up-to-date role.
    oatpp::Object<repositories::UserDto> userRecord;
    try
    {
        userRecord = users_->getUserById(record->userId);
    }
    catch (const errors::database::RecordNotFound&)
    {
        throw errors::authentication::InvalidToken("User for refresh token no longer exists");
    }

    if (record->sessionVersion.getValue(-1) != userRecord->sessionVersion.getValue(-2))
    {
        throw errors::authentication::InvalidToken("Account sessions have been invalidated");
    }

    // Both revocation and replacement roll back if any later step fails.
    refreshTokens_->revokeToken(oatpp::String(refreshToken));

    LoginResult result;
    result.username = *userRecord->username;
    result.role = *userRecord->role;
    result.accessToken = jwt_->create(*userRecord->id, result.role, userRecord->sessionVersion.getValue(-1));
    result.expiresInSeconds = jwt_->ttl().count();

    // Issue rotated new refresh token.
    const std::string newRefreshToken = generateRefreshToken();

    auto newRecord = repositories::RefreshTokenDto::createShared();
    newRecord->token = oatpp::String(newRefreshToken);
    newRecord->userId = userRecord->id;
    newRecord->sessionVersion = userRecord->sessionVersion;
    newRecord->expiresAt = nowSeconds + refreshTtl_.count();
    newRecord->revoked = 0;

    refreshTokens_->createToken(newRecord);
    result.refreshToken = newRefreshToken;

    transaction.commit();
    logging::logInfo("auth", "Token refreshed for user '" + result.username + "'");
    return result;
}

std::string AuthService::generateRefreshToken() const
{
    return auth::generateRandomSecret(32);
}

LoginResult AuthService::whoAmI(const auth::TokenClaims& claims)
{
    oatpp::Object<repositories::UserDto> account;
    try
    {
        account = users_->getUserById(claims.subject);
    }
    catch (const errors::database::RecordNotFound&)
    {
        throw errors::authentication::InvalidToken("Account no longer exists");
    }
    if (claims.sessionVersion < 0 || claims.sessionVersion != account->sessionVersion.getValue(-1))
    {
        throw errors::authentication::InvalidToken("Account sessions have been invalidated");
    }
    LoginResult result;
    result.username = *account->username;
    result.role = *account->role;
    result.expiresInSeconds = 0;
    return result;
}

auth::TokenClaims AuthService::authenticate(const std::string& accessToken)
{
    auto claims = jwt_->verify(accessToken);
    // Authorization keeps using the signed role, whose lifetime is bounded by
    // session_version. Every role write increments that version atomically.
    whoAmI(claims);
    return claims;
}

}  // namespace app::services
