#include "app/repositories/UserRepository.hpp"

#include <utility>

#include "app/auth/PasswordHash.hpp"
#include "app/domain/UserRole.hpp"
#include "app/errors/AppException.hpp"

namespace app::repositories
{

namespace
{

constexpr const char* kTableName = "users";

// Error messages include the username; a missing oatpp::String must read as
// empty text instead of crashing on dereference.
std::string textOf(const oatpp::String& value)
{
    return value ? std::string(*value) : std::string();
}

}  // namespace

UserRepository::UserRepository(std::shared_ptr<infrastructure::database::IDatabase> db)
    : BaseRepository(std::move(db), kTableName)
{
}

void UserRepository::checkUser(const oatpp::Object<UserDto>& user)
{
    if (!user)
    {
        throw errors::validation::InvalidValue("user is required");
    }

    if (!user->username || user->username->empty() || !user->passwordHash || user->passwordHash->empty() ||
        !user->role || user->role->empty())
    {
        throw errors::validation::InvalidValue("user fields (username, password_hash, role) must not be empty");
    }

    domain::UserRole role{};
    if (!domain::tryParseUserRole(*user->role, role))
    {
        throw errors::validation::InvalidValue("unknown user role: '" + textOf(user->role) + "'");
    }
}

void UserRepository::ensureSchema()
{
    // Startup migration deliberately discards username-bound sessions: their
    // historical owner cannot be reconstructed safely from today's username.
    if (tableExists() && !columnExists("id"))
    {
        auto transaction = beginTransaction();
        createMigrationTableImp();
        const auto rows = fetch<oatpp::Vector<oatpp::Object<UserDto>>>(legacyUsersImp());
        for (const auto& user : *rows)
        {
            user->id = auth::generateRandomSecret(16);
            user->sessionVersion = v_int64{0};
            insertMigratedUserImp(user);
        }
        executeQuery("DROP TABLE IF EXISTS refresh_tokens;", {});
        executeQuery("DROP TABLE users;", {});
        executeQuery("ALTER TABLE users_session_migration RENAME TO users;", {});
        transaction.commit();
    }
    createTableImp();
    if (!tableExists())
    {
        throw errors::database::QueryFailed(tableName() + " table is missing after CREATE TABLE IF NOT EXISTS");
    }
}

std::string UserRepository::schemaColumns()
{
    return " (id TEXT PRIMARY KEY NOT NULL, username TEXT NOT NULL UNIQUE, password_hash TEXT NOT NULL, "
           "role TEXT NOT NULL, session_version BIGINT NOT NULL DEFAULT 0 CHECK (session_version >= 0))";
}

void UserRepository::createUser(const oatpp::Object<UserDto>& user)
{
    checkUser(user);

    if (isUserExists(user->username))
    {
        throw errors::database::Conflict("username(" + textOf(user->username) + ") already exists");
    }

    // Identities are generated here, never accepted from callers or reused.
    user->id = auth::generateRandomSecret(16);
    user->sessionVersion = v_int64{0};
    insertUserImp(user);
}

void UserRepository::updateUser(const oatpp::Object<UserDto>& user)
{
    checkUser(user);

    const auto rows = fetch<oatpp::Vector<oatpp::Fields<oatpp::String>>>(updateUserImp(user));
    if (rows->empty())
    {
        throw errors::database::RecordNotFound("username(" + textOf(user->username) + ") does not exist");
    }
}

oatpp::Object<UserDto> UserRepository::getUser(const oatpp::String& username)
{
    const auto result = findUserImp(username);
    const auto rows = fetch<oatpp::Vector<oatpp::Object<UserDto>>>(result);

    if (rows->empty())
    {
        throw errors::database::RecordNotFound("username(" + textOf(username) + ") does not exist");
    }

    if (rows->size() > 1)
    {
        throw errors::database::Conflict("found duplicate rows for username(" + textOf(username) + ")");
    }

    return rows->front();
}

bool UserRepository::isUserExists(const oatpp::String& username)
{
    const auto result = userExistsImp(username);
    const auto rows = fetch<oatpp::Vector<oatpp::Fields<oatpp::Int64>>>(result);
    return !rows->empty() && rows->front()["count"].getValue(0) > 0;
}

oatpp::Object<UserDto> UserRepository::getUserById(const oatpp::String& id)
{
    const auto rows = fetch<oatpp::Vector<oatpp::Object<UserDto>>>(findUserByIdImp(id));
    if (rows->empty())
    {
        throw errors::database::RecordNotFound("Account no longer exists");
    }
    return rows->front();
}

void UserRepository::lockUserByUsername(const oatpp::String& username)
{
    lockUserByUsernameImp(username);
}

void UserRepository::lockUserById(const oatpp::String& id)
{
    lockUserByIdImp(id);
}

oatpp::Vector<oatpp::Object<UserDto>> UserRepository::listUsers()
{
    const auto result = listUsersImp();
    return fetch<oatpp::Vector<oatpp::Object<UserDto>>>(result);
}

void UserRepository::deleteUser(const oatpp::String& username)
{
    deleteUserImp(username);
}

void UserRepository::updatePassword(const oatpp::String& username, const oatpp::String& passwordHash)
{
    if (!username || username->empty())
    {
        throw errors::validation::InvalidValue("username is required");
    }

    if (!passwordHash || passwordHash->empty())
    {
        throw errors::validation::InvalidValue("password_hash is required");
    }

    if (!isUserExists(username))
    {
        throw errors::database::RecordNotFound("username(" + textOf(username) + ") does not exist");
    }

    updatePasswordImp(username, passwordHash);
}

void UserRepository::updateRole(const oatpp::String& username, const oatpp::String& role)
{
    if (!username || username->empty())
    {
        throw errors::validation::InvalidValue("username is required");
    }

    if (!role || role->empty())
    {
        throw errors::validation::InvalidValue("role is required");
    }

    domain::UserRole parsed{};
    if (!domain::tryParseUserRole(*role, parsed))
    {
        throw errors::validation::InvalidValue("unknown user role: '" + textOf(role) + "'");
    }

    if (!isUserExists(username))
    {
        throw errors::database::RecordNotFound("username(" + textOf(username) + ") does not exist");
    }

    updateRoleImp(username, role);
}

void UserRepository::updateUsername(const oatpp::String& currentUsername, const oatpp::String& newUsername)
{
    if (!currentUsername || currentUsername->empty())
    {
        throw errors::validation::InvalidValue("current_username is required");
    }

    if (!newUsername || newUsername->empty())
    {
        throw errors::validation::InvalidValue("new_username is required");
    }

    if (!isUserExists(currentUsername))
    {
        throw errors::database::RecordNotFound("username(" + textOf(currentUsername) + ") does not exist");
    }

    updateUsernameImp(currentUsername, newUsername);
}

void UserRepository::updateUserWithNewUsername(const oatpp::String& currentUsername, const oatpp::String& newUsername,
                                               const oatpp::String& passwordHash, const oatpp::String& role)
{
    if (!currentUsername || currentUsername->empty())
    {
        throw errors::validation::InvalidValue("current_username is required");
    }

    if (!newUsername || newUsername->empty())
    {
        throw errors::validation::InvalidValue("new_username is required");
    }

    if (!passwordHash || passwordHash->empty())
    {
        throw errors::validation::InvalidValue("password_hash is required");
    }

    if (!role || role->empty())
    {
        throw errors::validation::InvalidValue("role is required");
    }

    if (!isUserExists(currentUsername))
    {
        throw errors::database::RecordNotFound("username(" + textOf(currentUsername) + ") does not exist");
    }

    updateUserWithNewUsernameImp(currentUsername, newUsername, passwordHash, role);
}

std::int64_t UserRepository::deleteUsersExcept(const oatpp::String& excludeUsername)
{
    std::int64_t count = 0;
    {
        const auto countResult = countUsersExceptImp(excludeUsername);
        const auto rows = fetch<oatpp::Vector<oatpp::Fields<oatpp::Int64>>>(countResult);
        if (!rows->empty())
        {
            count = rows->front()["count"].getValue(0);
        }
    }

    deleteUsersExceptImp(excludeUsername);

    return count;
}

}  // namespace app::repositories
