#pragma once

#include <memory>
#include <string>

#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"

#include "app/infrastructure/database/IDatabase.hpp"
#include "app/repositories/BaseRepository.hpp"
#include "app/repositories/UserDto.hpp"

#include OATPP_CODEGEN_BEGIN(DbClient)

namespace app::repositories
{

/**
 * The user store (table `users`) used by AuthService, written in the
 * repository model (see docs/guides/adding-repository.md): SQL lives in `QUERY`
 * templates with named parameters, rows travel as UserDto objects, and the
 * methods speak in database domain errors instead of optionals.
 */
class UserRepository : public BaseRepository
{
public:
    explicit UserRepository(std::shared_ptr<infrastructure::database::IDatabase> db);

    /// Creates the `users` table when missing (idempotent; called at startup).
    void ensureSchema();

    /// Inserts a new user. Throws Conflict when the username already exists.
    void createUser(const oatpp::Object<UserDto>& user);

    /// Updates password hash and role of an existing user.
    /// Throws RecordNotFound when the username does not exist.
    void updateUser(const oatpp::Object<UserDto>& user);

    /// The user with `username`. Throws RecordNotFound when missing.
    oatpp::Object<UserDto> getUser(const oatpp::String& username);

    /// True when the username exists (existence probe, no row fetch).
    bool isUserExists(const oatpp::String& username);

    /// All users, ordered by username.
    oatpp::Vector<oatpp::Object<UserDto>> listUsers();

    /// Deletes the user; a missing username is a no-op (idempotent).
    void deleteUser(const oatpp::String& username);

private:
    /// Rejects a missing DTO and empty or non-canonical fields before SQL.
    static void checkUser(const oatpp::Object<UserDto>& user);

    QUERY(createTableImp, "CREATE TABLE IF NOT EXISTS " + tableName() +
                              " (username TEXT PRIMARY KEY, password_hash TEXT NOT NULL, role TEXT NOT NULL)")

    QUERY(insertUserImp,
          "INSERT INTO " + tableName() +
              " (username, password_hash, role) VALUES (:user.username, :user.password_hash, "
              ":user.role);",
          PARAM(oatpp::Object<UserDto>, user))

    QUERY(updateUserImp,
          "UPDATE " + tableName() +
              " SET password_hash = :user.password_hash, role = :user.role WHERE username = "
              ":user.username;",
          PARAM(oatpp::Object<UserDto>, user))

    QUERY(findUserImp, "SELECT username, password_hash, role FROM " + tableName() + " WHERE username = :username;",
          PARAM(oatpp::String, username))

    QUERY(listUsersImp, "SELECT username, password_hash, role FROM " + tableName() + " ORDER BY username;")

    QUERY(userExistsImp, "SELECT count(*) AS count FROM " + tableName() + " WHERE username = :username;",
          PARAM(oatpp::String, username))

    QUERY(deleteUserImp, "DELETE FROM " + tableName() + " WHERE username = :username;", PARAM(oatpp::String, username))
};

}  // namespace app::repositories

#include OATPP_CODEGEN_END(DbClient)
