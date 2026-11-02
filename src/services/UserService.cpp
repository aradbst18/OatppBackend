#include "app/services/UserService.hpp"

#include <utility>

#include "app/auth/PasswordHash.hpp"
#include "app/domain/UserRole.hpp"
#include "app/errors/AppException.hpp"
#include "app/logging/Logger.hpp"
#include "app/repositories/UserDto.hpp"
#include "app/repositories/UserRepository.hpp"
#include "app/validation/Validators.hpp"

namespace app::services
{

UserService::UserService(std::shared_ptr<repositories::UserRepository> users, std::string defaultAdminUsername)
    : users_(std::move(users)), defaultAdminUsername_(std::move(defaultAdminUsername))
{
    if (!users_)
    {
        throw errors::internal::OperationFailed("UserService requires UserRepository");
    }

    if (defaultAdminUsername_.empty())
    {
        defaultAdminUsername_ = "admin";
    }
}

void UserService::validateCreateInput(const CreateUserInput& input)
{
    if (!validation::isValidUsername(input.username))
    {
        throw errors::validation::InvalidValue(
            "Invalid username: must be 4-18 characters containing alphanumeric or @#$ characters");
    }

    validation::validatePassword(input.password);

    domain::UserRole role{};
    if (!domain::tryParseUserRole(input.role, role))
    {
        throw errors::validation::InvalidValue("Invalid user role: '" + input.role + "'");
    }
}

UserInfo UserService::getUser(const std::string& username)
{
    APP_LOG_TRACE("users", "Looking up user '" + username + "' in repository");
    if (username.empty())
    {
        throw errors::validation::InvalidValue("Username is required");
    }

    const auto row = users_->getUser(username);

    UserInfo info;
    info.username = row->username ? std::string(*row->username) : std::string();
    info.role = row->role ? std::string(*row->role) : std::string();

    return info;
}

std::vector<UserInfo> UserService::listUsers()
{
    APP_LOG_TRACE("users", "Listing all users from repository");
    const auto rows = users_->listUsers();

    std::vector<UserInfo> result;
    result.reserve(rows->size());

    for (const auto& row : *rows)
    {
        if (row)
        {
            UserInfo info;
            info.username = row->username ? std::string(*row->username) : std::string();
            info.role = row->role ? std::string(*row->role) : std::string();
            result.push_back(std::move(info));
        }
    }

    APP_LOG_DEBUG("users", "Listed " + std::to_string(result.size()) + " users from repository");
    return result;
}

UserInfo UserService::createUser(const CreateUserInput& input)
{
    APP_LOG_TRACE("users", "Validating new user creation input for '" + input.username + "'");
    validateCreateInput(input);

    if (users_->isUserExists(input.username))
    {
        throw errors::database::Conflict("User '" + input.username + "' already exists");
    }

    APP_LOG_TRACE("users", "Hashing password and creating user record in repository");
    auto userDto = oatpp::Object<repositories::UserDto>::createShared();
    userDto->username = input.username;
    userDto->passwordHash = auth::passwordHash(input.password);
    userDto->role = input.role;

    users_->createUser(userDto);

    UserInfo info;
    info.username = input.username;
    info.role = input.role;

    logging::logInfo("users", "Created user '" + input.username + "' with role '" + input.role + "'");
    return info;
}

std::vector<UserInfo> UserService::createUsers(const std::vector<CreateUserInput>& inputs)
{
    APP_LOG_TRACE("users", "Batch creating " + std::to_string(inputs.size()) + " users in transaction");
    if (inputs.empty())
    {
        throw errors::validation::InvalidValue("User list must not be empty");
    }

    for (const auto& input : inputs)
    {
        validateCreateInput(input);
    }

    std::vector<UserInfo> created;
    created.reserve(inputs.size());

    auto transaction = users_->beginTransaction();
    for (const auto& input : inputs)
    {
        created.push_back(createUser(input));
    }

    transaction.commit();
    logging::logInfo("users", "Batch created " + std::to_string(created.size()) + " users");
    return created;
}

UserInfo UserService::updateUser(const std::string& currentUsername, const UpdateUserInput& input)
{
    APP_LOG_TRACE("users", "Updating user '" + currentUsername + "' in transaction");
    if (currentUsername.empty())
    {
        throw errors::request::Missing("Current username is required");
    }

    if (!validation::isValidUsername(input.username))
    {
        throw errors::validation::InvalidValue(
            "Invalid username: must be 4-18 characters containing alphanumeric or @#$ characters");
    }

    validation::validatePassword(input.password);

    domain::UserRole role{};
    if (!domain::tryParseUserRole(input.role, role))
    {
        throw errors::validation::InvalidValue("Invalid user role: '" + input.role + "'");
    }

    auto transaction = users_->beginTransaction();
    users_->lockUserByUsername(currentUsername);
    if (!users_->isUserExists(currentUsername))
    {
        throw errors::database::RecordNotFound("User '" + currentUsername + "' does not exist");
    }

    if (currentUsername == defaultAdminUsername_ && input.username != defaultAdminUsername_)
    {
        throw errors::authorization::ResourceAccessDenied("Default admin username cannot be changed");
    }

    if (currentUsername == defaultAdminUsername_ && role != domain::UserRole::Admin)
    {
        throw errors::authorization::ResourceAccessDenied("Bootstrap administrator cannot be demoted");
    }

    if (input.username != currentUsername && users_->isUserExists(input.username))
    {
        throw errors::database::Conflict("Username '" + input.username + "' already exists");
    }

    const std::string newHash = auth::passwordHash(input.password);

    if (input.username != currentUsername)
    {
        users_->updateUserWithNewUsername(currentUsername, input.username, newHash, input.role);
    }
    else
    {
        auto userDto = oatpp::Object<repositories::UserDto>::createShared();
        userDto->username = input.username;
        userDto->passwordHash = newHash;
        userDto->role = input.role;
        users_->updateUser(userDto);
    }

    const auto result = getUser(input.username);
    transaction.commit();
    logging::logInfo("users", "Updated user '" + currentUsername + "' (new username: '" + input.username + "')");
    return result;
}

UserInfo UserService::patchUser(const std::string& currentUsername, const PatchUserInput& input)
{
    APP_LOG_TRACE("users", "Patching user '" + currentUsername + "' in transaction");
    if (currentUsername.empty())
    {
        throw errors::request::Missing("Current username is required");
    }

    if (!input.username.has_value() && !input.password.has_value() && !input.role.has_value())
    {
        throw errors::request::Missing(
            "At least one field (username, password, or role) must be provided for partial update");
    }

    if (input.username.has_value() && *input.username != currentUsername)
    {
        if (currentUsername == defaultAdminUsername_)
        {
            throw errors::authorization::ResourceAccessDenied("Default admin username cannot be changed");
        }

        if (!validation::isValidUsername(*input.username))
        {
            throw errors::validation::InvalidValue(
                "Invalid username: must be 4-18 characters containing alphanumeric or @#$ characters");
        }
    }

    std::optional<std::string> passwordHash;
    if (input.password.has_value())
    {
        validation::validatePassword(*input.password);

        passwordHash = auth::passwordHash(*input.password);
    }

    if (input.role.has_value())
    {
        domain::UserRole role{};
        if (!domain::tryParseUserRole(*input.role, role))
        {
            throw errors::validation::InvalidValue("Invalid user role: '" + *input.role + "'");
        }

        if (currentUsername == defaultAdminUsername_ && role != domain::UserRole::Admin)
        {
            throw errors::authorization::ResourceAccessDenied("Bootstrap administrator cannot be demoted");
        }
    }

    // Validate everything first, then write all fields and invalidate sessions
    // in one statement. A failed PATCH cannot rename an account partially.
    auto transaction = users_->beginTransaction();
    users_->lockUserByUsername(currentUsername);
    const auto current = users_->getUser(currentUsername);
    const std::string targetUsername = input.username.value_or(currentUsername);
    if (targetUsername != currentUsername && users_->isUserExists(targetUsername))
    {
        throw errors::database::Conflict("Username '" + targetUsername + "' already exists");
    }
    users_->updateUserWithNewUsername(currentUsername, targetUsername,
                                      passwordHash ? oatpp::String(*passwordHash) : current->passwordHash,
                                      input.role ? oatpp::String(*input.role) : current->role);
    const auto result = getUser(targetUsername);
    transaction.commit();
    logging::logInfo("users", "Patched user '" + currentUsername + "' successfully");
    return result;
}

void UserService::deleteUser(const std::string& username)
{
    APP_LOG_TRACE("users", "Deleting user '" + username + "' from repository");
    if (username.empty())
    {
        throw errors::validation::InvalidValue("Username is required");
    }

    if (username == defaultAdminUsername_)
    {
        throw errors::authorization::ResourceAccessDenied("Default admin user cannot be deleted");
    }

    if (!users_->isUserExists(username))
    {
        throw errors::database::RecordNotFound("User '" + username + "' does not exist");
    }

    users_->deleteUser(username);
    logging::logInfo("users", "Deleted user '" + username + "' successfully");
}

DeleteUsersResult UserService::deleteAllUsers()
{
    APP_LOG_WARN("users", "Deleting all non-admin users except '" + defaultAdminUsername_ + "'");
    const std::int64_t count = users_->deleteUsersExcept(defaultAdminUsername_);

    DeleteUsersResult result;
    result.deletedCount = count;
    result.message = "All users deleted except default admin user ('" + defaultAdminUsername_ + "')";

    logging::logInfo("users", "Deleted " + std::to_string(count) + " non-admin users");
    return result;
}

}  // namespace app::services
