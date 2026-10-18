#pragma once

#include <memory>
#include <string>

#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"

#include "app/infrastructure/database/IDatabase.hpp"
#include "app/repositories/BaseRepository.hpp"
#include "app/repositories/RefreshTokenDto.hpp"

#include OATPP_CODEGEN_BEGIN(DbClient)

namespace app::repositories
{

/**
 * Repository for OAuth2 refresh tokens in the `refresh_tokens` table.
 */
class RefreshTokenRepository : public BaseRepository
{
public:
    explicit RefreshTokenRepository(std::shared_ptr<infrastructure::database::IDatabase> db);

    /// Creates the `refresh_tokens` table if missing.
    void ensureSchema();

    /// Stores a new refresh token. Throws Conflict if token already exists.
    void createToken(const oatpp::Object<RefreshTokenDto>& token);

    /// Looks up a token by string. Throws RecordNotFound if missing.
    oatpp::Object<RefreshTokenDto> getToken(const oatpp::String& token);

    /// Marks a specific refresh token as revoked. Missing token is a no-op.
    void revokeToken(const oatpp::String& token);

    /// Revokes all active refresh tokens for a user.
    void revokeAllForUser(const oatpp::String& username);

    /// Deletes all expired tokens prior to `nowTimestamp`.
    void deleteExpired(oatpp::Int64 nowTimestamp);

private:
    static void checkToken(const oatpp::Object<RefreshTokenDto>& token);

    QUERY(createTableImp,
          "CREATE TABLE IF NOT EXISTS " + tableName() +
              " (token TEXT PRIMARY KEY, username TEXT NOT NULL, expires_at BIGINT NOT NULL, revoked INTEGER NOT NULL)")

    QUERY(insertTokenImp,
          "INSERT INTO " + tableName() +
              " (token, username, expires_at, revoked) VALUES (:token.token, :token.username, "
              ":token.expires_at, :token.revoked);",
          PARAM(oatpp::Object<RefreshTokenDto>, token))

    QUERY(findTokenImp, "SELECT token, username, expires_at, revoked FROM " + tableName() + " WHERE token = :token;",
          PARAM(oatpp::String, token))

    QUERY(revokeTokenImp, "UPDATE " + tableName() + " SET revoked = 1 WHERE token = :token;",
          PARAM(oatpp::String, token))

    QUERY(revokeAllForUserImp, "UPDATE " + tableName() + " SET revoked = 1 WHERE username = :username;",
          PARAM(oatpp::String, username))

    QUERY(deleteExpiredImp, "DELETE FROM " + tableName() + " WHERE expires_at < :now;", PARAM(oatpp::Int64, now))
};

}  // namespace app::repositories

#include OATPP_CODEGEN_END(DbClient)
