#include "app/services/AuthService.hpp"

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
}

void AuthService::ensureSeeded(const std::string& customUsername, const std::string& customPassword)
{
    std::string seedUser = customUsername;
    if (seedUser.empty())
    {
        const char* envUser = std::getenv("APP_SEED_ADMIN_USERNAME");
        seedUser = (envUser != nullptr && envUser[0] != '\0') ? envUser : kDefaultSeedUsername;
    }

    std::string seedPass = customPassword;
    if (seedPass.empty())
    {
        const char* envPass = std::getenv("APP_SEED_ADMIN_PASSWORD");
        seedPass = (envPass != nullptr && envPass[0] != '\0') ? envPass : kDefaultSeedPassword;
    }

    if (!users_->isUserExists(seedUser))
    {
        if (seedPass == kDefaultSeedPassword)
        {
            logging::logWarn("auth",
                             "Seeding admin account with DEFAULT password ('Admin1234')! Set APP_SEED_ADMIN_PASSWORD "
                             "in production.");
        }

        auto user = oatpp::Object<repositories::UserDto>::createShared();
        user->username = seedUser;
        user->passwordHash = auth::passwordHash(seedPass);
        user->role = domain::toString(domain::UserRole::Admin);

        users_->createUser(user);
        logging::logInfo("auth", "Seeded admin account '" + seedUser + "'");
    }
    else
    {
        logging::logInfo("auth", "Admin account '" + seedUser + "' already present; database ready");
    }
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
    result.accessToken = jwt_->create(result.username, result.role);
    result.expiresInSeconds = jwt_->ttl().count();

    if (refreshTokens_)
    {
        const std::string refreshToken = generateRefreshToken();
        const auto now = std::chrono::system_clock::now();
        const auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

        auto tokenRecord = repositories::RefreshTokenDto::createShared();
        tokenRecord->token = oatpp::String(refreshToken);
        tokenRecord->username = record->username;
        tokenRecord->expiresAt = nowSeconds + refreshTtl_.count();
        tokenRecord->revoked = 0;

        refreshTokens_->createToken(tokenRecord);
        result.refreshToken = refreshToken;
    }

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

    oatpp::Object<repositories::RefreshTokenDto> record;
    try
    {
        record = refreshTokens_->getToken(oatpp::String(refreshToken));
    }
    catch (const errors::database::RecordNotFound&)
    {
        throw errors::authentication::InvalidToken("Invalid refresh token");
    }

    if (!record || !record->username || !record->expiresAt)
    {
        throw errors::authentication::InvalidToken("Malformed refresh token record");
    }

    if (record->revoked && record->revoked.getValue(0) != 0)
    {
        throw errors::authentication::InvalidToken("Refresh token has been revoked");
    }

    const auto now = std::chrono::system_clock::now();
    const auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    if (record->expiresAt.getValue(0) < nowSeconds)
    {
        throw errors::authentication::TokenExpired("Refresh token has expired");
    }

    // Refresh token rotation: revoke old token immediately.
    refreshTokens_->revokeToken(oatpp::String(refreshToken));

    // Verify user still exists in database and fetch up-to-date role.
    oatpp::Object<repositories::UserDto> userRecord;
    try
    {
        userRecord = users_->getUser(record->username);
    }
    catch (const errors::database::RecordNotFound&)
    {
        throw errors::authentication::InvalidToken("User for refresh token no longer exists");
    }

    LoginResult result;
    result.username = *userRecord->username;
    result.role = *userRecord->role;
    result.accessToken = jwt_->create(result.username, result.role);
    result.expiresInSeconds = jwt_->ttl().count();

    // Issue rotated new refresh token.
    const std::string newRefreshToken = generateRefreshToken();

    auto newRecord = repositories::RefreshTokenDto::createShared();
    newRecord->token = oatpp::String(newRefreshToken);
    newRecord->username = userRecord->username;
    newRecord->expiresAt = nowSeconds + refreshTtl_.count();
    newRecord->revoked = 0;

    refreshTokens_->createToken(newRecord);
    result.refreshToken = newRefreshToken;

    logging::logInfo("auth", "Token refreshed for user '" + result.username + "'");
    return result;
}

std::string AuthService::generateRefreshToken() const
{
    return auth::generateRandomSecret(32);
}

LoginResult AuthService::whoAmI(const auth::TokenClaims& claims)
{
    LoginResult result;
    result.username = claims.subject;
    result.role = claims.role.empty() ? domain::toString(domain::UserRole::Viewer) : claims.role;
    result.expiresInSeconds = 0;
    return result;
}

}  // namespace app::services
