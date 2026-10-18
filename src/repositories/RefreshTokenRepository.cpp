#include "app/repositories/RefreshTokenRepository.hpp"

#include <utility>

#include "app/errors/AppException.hpp"

namespace app::repositories
{

namespace
{

constexpr const char* kTableName = "refresh_tokens";

}  // namespace

RefreshTokenRepository::RefreshTokenRepository(std::shared_ptr<infrastructure::database::IDatabase> db)
    : BaseRepository(std::move(db), kTableName)
{
}

void RefreshTokenRepository::checkToken(const oatpp::Object<RefreshTokenDto>& token)
{
    if (!token)
    {
        throw errors::validation::InvalidValue("refresh token is required");
    }

    if (!token->token || token->token->empty() || !token->username || token->username->empty())
    {
        throw errors::validation::InvalidValue("refresh token fields (token, username) must not be empty");
    }

    if (!token->expiresAt || token->expiresAt.getValue(0) <= 0)
    {
        throw errors::validation::InvalidValue("refresh token expires_at must be positive");
    }
}

void RefreshTokenRepository::ensureSchema()
{
    createTableImp();
    if (!tableExists())
    {
        throw errors::database::QueryFailed(tableName() + " table is missing after CREATE TABLE IF NOT EXISTS");
    }
}

void RefreshTokenRepository::createToken(const oatpp::Object<RefreshTokenDto>& token)
{
    checkToken(token);

    try
    {
        insertTokenImp(token);
    }
    catch (const errors::database::QueryFailed& ex)
    {
        throw errors::database::Conflict("token already exists or insert failed: " + std::string(ex.what()));
    }
}

oatpp::Object<RefreshTokenDto> RefreshTokenRepository::getToken(const oatpp::String& token)
{
    if (!token || token->empty())
    {
        throw errors::database::RecordNotFound("token is empty");
    }

    const auto result = findTokenImp(token);
    const auto rows = fetch<oatpp::Vector<oatpp::Object<RefreshTokenDto>>>(result);

    if (rows->empty())
    {
        throw errors::database::RecordNotFound("refresh token not found");
    }

    return rows->front();
}

void RefreshTokenRepository::revokeToken(const oatpp::String& token)
{
    if (token && !token->empty())
    {
        revokeTokenImp(token);
    }
}

void RefreshTokenRepository::revokeAllForUser(const oatpp::String& username)
{
    if (username && !username->empty())
    {
        revokeAllForUserImp(username);
    }
}

void RefreshTokenRepository::deleteExpired(oatpp::Int64 nowTimestamp)
{
    deleteExpiredImp(nowTimestamp);
}

}  // namespace app::repositories
