#include "app/repositories/UserRepository.hpp"

#include <utility>

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
    createTableImp();
    if (!tableExists())
    {
        throw errors::database::QueryFailed(tableName() + " table is missing after CREATE TABLE IF NOT EXISTS");
    }
}

void UserRepository::createUser(const oatpp::Object<UserDto>& user)
{
    checkUser(user);

    if (isUserExists(user->username))
    {
        throw errors::database::Conflict("username(" + textOf(user->username) + ") already exists");
    }

    insertUserImp(user);
}

void UserRepository::updateUser(const oatpp::Object<UserDto>& user)
{
    checkUser(user);

    updateUserImp(user);
    if (!isUserExists(user->username))
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

oatpp::Vector<oatpp::Object<UserDto>> UserRepository::listUsers()
{
    const auto result = listUsersImp();
    return fetch<oatpp::Vector<oatpp::Object<UserDto>>>(result);
}

void UserRepository::deleteUser(const oatpp::String& username)
{
    deleteUserImp(username);
}

}  // namespace app::repositories
