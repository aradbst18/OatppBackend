#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace app::repositories
{
class UserRepository;
}  // namespace app::repositories

namespace app::services
{

struct UserInfo
{
    std::string username;
    std::string role;

    UserInfo() = default;
    UserInfo(UserInfo&&) noexcept = default;
    UserInfo& operator=(UserInfo&&) noexcept = default;
    UserInfo(const UserInfo&) = default;
    UserInfo& operator=(const UserInfo&) = default;
};

struct CreateUserInput
{
    std::string username;
    std::string password;
    std::string role;

    CreateUserInput() = default;
    CreateUserInput(std::string u, std::string p, std::string r)
        : username(std::move(u)), password(std::move(p)), role(std::move(r))
    {
    }
};

struct UpdateUserInput
{
    std::string username;
    std::string password;
    std::string role;

    UpdateUserInput() = default;
    UpdateUserInput(std::string u, std::string p, std::string r)
        : username(std::move(u)), password(std::move(p)), role(std::move(r))
    {
    }
};

struct PatchUserInput
{
    std::optional<std::string> username;
    std::optional<std::string> password;
    std::optional<std::string> role;
};

struct DeleteUsersResult
{
    std::int64_t deletedCount{0};
    std::string message;
};

/**
 * User management business logic service.
 *
 * Owns user retrieval, creation, updating (full & partial), and deletion.
 * Enforces username/password/role validation, password hashing, and ensures
 * the configured bootstrap administrator cannot be deleted, renamed, or demoted.
 */
class UserService
{
public:
    explicit UserService(std::shared_ptr<repositories::UserRepository> users,
                         std::string defaultAdminUsername = "admin");

    /// Retrieve a single user by username.
    /// Throws errors::database::RecordNotFound when user does not exist.
    UserInfo getUser(const std::string& username);

    /// Retrieve all users ordered by username.
    std::vector<UserInfo> listUsers();

    /// Create a single user.
    /// Throws errors::validation::InvalidValue if fields are invalid.
    /// Throws errors::database::Conflict if username already exists.
    UserInfo createUser(const CreateUserInput& input);

    /// Create multiple users atomically, returning them in input order.
    /// Any failure rolls back all users created by this call.
    /// Throws errors::validation::InvalidValue if any user input is invalid.
    /// Throws errors::database::Conflict if any username already exists.
    std::vector<UserInfo> createUsers(const std::vector<CreateUserInput>& inputs);

    /// Fully update an existing user (both password and role are updated).
    /// Throws errors::database::RecordNotFound if user does not exist.
    /// Throws errors::validation::InvalidValue if fields are invalid.
    /// Throws errors::authorization::ResourceAccessDenied if renaming or demoting the bootstrap administrator.
    UserInfo updateUser(const std::string& username, const UpdateUserInput& input);

    /// Partially update an existing user (updates only provided fields: password, role, or both).
    /// Throws errors::database::RecordNotFound if user does not exist.
    /// Throws errors::validation::InvalidValue if fields are invalid or no fields are provided.
    /// Throws errors::authorization::ResourceAccessDenied if renaming or demoting the bootstrap administrator.
    UserInfo patchUser(const std::string& username, const PatchUserInput& input);

    /// Delete a single user.
    /// Throws errors::authorization::AccessDenied if attempting to delete default admin user.
    /// Throws errors::database::RecordNotFound if user does not exist.
    void deleteUser(const std::string& username);

    /// Delete all users except the default admin user.
    /// Returns the count of deleted users and a confirmation message.
    DeleteUsersResult deleteAllUsers();

    const std::string& defaultAdminUsername() const noexcept
    {
        return defaultAdminUsername_;
    }

private:
    void validateCreateInput(const CreateUserInput& input);

    std::shared_ptr<repositories::UserRepository> users_;
    std::string defaultAdminUsername_;
};

}  // namespace app::services
