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

    /// Creates the account-bound token schema, discarding legacy username tokens.
    void ensureSchema();

    /// Stores a new refresh token. Throws Conflict if token already exists.
    void createToken(const oatpp::Object<RefreshTokenDto>& token);

    /// Looks up a token by string. Throws RecordNotFound if missing.
    oatpp::Object<RefreshTokenDto> getToken(const oatpp::String& token);

    /// Marks a specific refresh token as revoked. Missing token is a no-op.
    void revokeToken(const oatpp::String& token);

    /// Revokes all refresh tokens for an immutable account ID.
    void revokeAllForUser(const oatpp::String& userId);

    /// Deletes at most 1000 oldest tokens expiring at or before `nowTimestamp`.
    void deleteExpired(oatpp::Int64 nowTimestamp);

private:
    static void checkToken(const oatpp::Object<RefreshTokenDto>& token);

    QUERY(createTableImp, "CREATE TABLE IF NOT EXISTS " + tableName() +
                              " (token TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE, "
                              "session_version BIGINT NOT NULL CHECK (session_version >= 0), "
                              "expires_at BIGINT NOT NULL, revoked INTEGER NOT NULL)")

    QUERY(createUserIndexImp, "CREATE INDEX IF NOT EXISTS refresh_tokens_user_id_idx ON refresh_tokens(user_id);")

    QUERY(createExpiryIndexImp,
          "CREATE INDEX IF NOT EXISTS refresh_tokens_expires_at_idx ON refresh_tokens(expires_at);")

    QUERY(insertTokenImp,
          "INSERT INTO " + tableName() +
              " (token, user_id, session_version, expires_at, revoked) VALUES (:token.token, :token.user_id, "
              ":token.session_version, :token.expires_at, :token.revoked);",
          PARAM(oatpp::Object<RefreshTokenDto>, token))

    QUERY(findTokenImp,
          "SELECT token, user_id, session_version, expires_at, revoked FROM " + tableName() + " WHERE token = :token;",
          PARAM(oatpp::String, token))

    QUERY(revokeTokenImp, "UPDATE " + tableName() + " SET revoked = 1 WHERE token = :token;",
          PARAM(oatpp::String, token))

    QUERY(revokeAllForUserImp, "UPDATE " + tableName() + " SET revoked = 1 WHERE user_id = :userId;",
          PARAM(oatpp::String, userId))

    QUERY(deleteExpiredImp,
          "DELETE FROM " + tableName() + " WHERE token IN (SELECT token FROM " + tableName() +
              " WHERE expires_at <= :now ORDER BY expires_at LIMIT 1000);",
          PARAM(oatpp::Int64, now))
};

}  // namespace app::repositories

#include OATPP_CODEGEN_END(DbClient)
