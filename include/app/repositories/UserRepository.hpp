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

    /// Creates or upgrades `users` at startup. Legacy sessions are discarded.
    void ensureSchema();

    /// Inserts a new user. Throws Conflict when the username already exists.
    void createUser(const oatpp::Object<UserDto>& user);

    /// Updates password hash and role of an existing user.
    /// Throws RecordNotFound when the username does not exist.
    void updateUser(const oatpp::Object<UserDto>& user);

    /// The user with `username`. Throws RecordNotFound when missing.
    oatpp::Object<UserDto> getUser(const oatpp::String& username);

    /// Resolve immutable identity, independent of username changes or reuse.
    oatpp::Object<UserDto> getUserById(const oatpp::String& id);

    /// Serialize session issuance with account writes. Caller owns a transaction.
    void lockUserByUsername(const oatpp::String& username);
    void lockUserById(const oatpp::String& id);

    /// True when the username exists (existence probe, no row fetch).
    bool isUserExists(const oatpp::String& username);

    /// All users, ordered by username.
    oatpp::Vector<oatpp::Object<UserDto>> listUsers();

    /// Updates only the password hash of an existing user.
    /// Throws RecordNotFound when the username does not exist.
    void updatePassword(const oatpp::String& username, const oatpp::String& passwordHash);

    /// Updates only the role of an existing user.
    /// Throws RecordNotFound when the username does not exist.
    void updateRole(const oatpp::String& username, const oatpp::String& role);

    /// Updates only the username of an existing user.
    /// Throws RecordNotFound when currentUsername does not exist.
    void updateUsername(const oatpp::String& currentUsername, const oatpp::String& newUsername);

    /// Updates username, password hash, and role of an existing user.
    /// Throws RecordNotFound when currentUsername does not exist.
    void updateUserWithNewUsername(const oatpp::String& currentUsername, const oatpp::String& newUsername,
                                   const oatpp::String& passwordHash, const oatpp::String& role);

    /// Deletes the user; a missing username is a no-op (idempotent).
    void deleteUser(const oatpp::String& username);

    /// Deletes all users except `excludeUsername`. Returns count of deleted records.
    std::int64_t deleteUsersExcept(const oatpp::String& excludeUsername);

private:
    /// Rejects a missing DTO and empty or non-canonical fields before SQL.
    static void checkUser(const oatpp::Object<UserDto>& user);

    static std::string schemaColumns();

    QUERY(createTableImp, "CREATE TABLE IF NOT EXISTS " + tableName() + schemaColumns())

    QUERY(createMigrationTableImp, "CREATE TABLE users_session_migration " + schemaColumns())
    QUERY(legacyUsersImp, "SELECT username, password_hash, role FROM users;")
    QUERY(insertMigratedUserImp,
          "INSERT INTO users_session_migration (id, username, password_hash, role, session_version) "
          "VALUES (:user.id, :user.username, :user.password_hash, :user.role, :user.session_version);",
          PARAM(oatpp::Object<UserDto>, user))

    QUERY(insertUserImp,
          "INSERT INTO " + tableName() +
              " (id, username, password_hash, role, session_version) VALUES (:user.id, :user.username, "
              ":user.password_hash, :user.role, :user.session_version);",
          PARAM(oatpp::Object<UserDto>, user))

    QUERY(updateUserImp,
          "UPDATE " + tableName() +
              " SET password_hash = :user.password_hash, role = :user.role, session_version = session_version + 1 "
              "WHERE username = "
              ":user.username RETURNING id;",
          PARAM(oatpp::Object<UserDto>, user))

    QUERY(updatePasswordImp,
          "UPDATE " + tableName() +
              " SET password_hash = :password_hash, session_version = session_version + 1 WHERE username = :username;",
          PARAM(oatpp::String, username), PARAM(oatpp::String, password_hash))

    QUERY(updateRoleImp,
          "UPDATE " + tableName() +
              " SET role = :role, session_version = session_version + 1 WHERE username = :username;",
          PARAM(oatpp::String, username), PARAM(oatpp::String, role))

    QUERY(updateUsernameImp,
          "UPDATE " + tableName() +
              " SET username = :newUsername, session_version = session_version + 1 WHERE username = :currentUsername;",
          PARAM(oatpp::String, currentUsername), PARAM(oatpp::String, newUsername))

    QUERY(updateUserWithNewUsernameImp,
          "UPDATE " + tableName() +
              " SET username = :newUsername, password_hash = :passwordHash, role = :role, session_version = "
              "session_version + 1 WHERE username = "
              ":currentUsername;",
          PARAM(oatpp::String, currentUsername), PARAM(oatpp::String, newUsername), PARAM(oatpp::String, passwordHash),
          PARAM(oatpp::String, role))

    QUERY(findUserImp,
          "SELECT id, username, password_hash, role, session_version FROM " + tableName() +
              " WHERE username = :username;",
          PARAM(oatpp::String, username))

    QUERY(findUserByIdImp, "SELECT id, username, password_hash, role, session_version FROM users WHERE id = :id;",
          PARAM(oatpp::String, id))
    QUERY(lockUserByUsernameImp, "UPDATE users SET session_version = session_version WHERE username = :username;",
          PARAM(oatpp::String, username))
    QUERY(lockUserByIdImp, "UPDATE users SET session_version = session_version WHERE id = :id;",
          PARAM(oatpp::String, id))

    QUERY(listUsersImp,
          "SELECT id, username, password_hash, role, session_version FROM " + tableName() + " ORDER BY username;")

    QUERY(userExistsImp, "SELECT count(*) AS count FROM " + tableName() + " WHERE username = :username;",
          PARAM(oatpp::String, username))

    QUERY(deleteUserImp, "DELETE FROM " + tableName() + " WHERE username = :username;", PARAM(oatpp::String, username))

    QUERY(countUsersExceptImp, "SELECT count(*) AS count FROM " + tableName() + " WHERE username != :excludeUsername;",
          PARAM(oatpp::String, excludeUsername))

    QUERY(deleteUsersExceptImp, "DELETE FROM " + tableName() + " WHERE username != :excludeUsername;",
          PARAM(oatpp::String, excludeUsername))
};

}  // namespace app::repositories

#include OATPP_CODEGEN_END(DbClient)
