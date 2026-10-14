#include "app/services/AuthService.hpp"

#include "app/auth/PasswordHash.hpp"
#include "app/domain/UserRole.hpp"
#include "app/errors/AppException.hpp"
#include "app/logging/Logger.hpp"
#include "app/validation/Validators.hpp"

namespace app::services
{

AuthService::AuthService(std::shared_ptr<repositories::UserRepository> users, std::shared_ptr<auth::JwtService> jwt)
    : users_(std::move(users)), jwt_(std::move(jwt))
{
    if (!users_ || !jwt_)
    {
        throw errors::internal::OperationFailed("AuthService requires user repository and JWT service");
    }
}

void AuthService::ensureSeeded()
{
    if (!users_->findByUsername(kSeedUsername).has_value())
    {
        users_->upsert(repositories::UserRecord{kSeedUsername, auth::passwordHashHex(kSeedPassword), domain::toString(domain::UserRole::Admin)});

        logging::logInfo("auth", "Seeded demo admin account");
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

    const auto record = users_->findByUsername(username);
    // Constant-shape failure: unknown user and wrong password look identical.
    if (!record.has_value() || record->passwordHash != auth::passwordHashHex(password))
    {
        throw errors::authentication::InvalidCredentials("Invalid credentials");
    }

    LoginResult result;
    result.username = record->username;
    result.role = record->role;
    result.accessToken = jwt_->create(record->username, record->role);
    result.expiresInSeconds = 3600;

    logging::logInfo("auth", "Login succeeded for user '" + username + "'");
    return result;
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
