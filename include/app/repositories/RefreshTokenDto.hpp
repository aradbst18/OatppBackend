#pragma once

#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"

#include OATPP_CODEGEN_BEGIN(DTO)

namespace app::repositories
{

/**
 * Row of the `refresh_tokens` table.
 */
class RefreshTokenDto : public oatpp::DTO
{
    DTO_INIT(RefreshTokenDto, DTO /* Extends */)

    DTO_FIELD(oatpp::String, token);
    DTO_FIELD(oatpp::String, userId, "user_id");
    DTO_FIELD(oatpp::Int64, sessionVersion, "session_version");
    DTO_FIELD(oatpp::Int64, expiresAt, "expires_at");
    DTO_FIELD(oatpp::Int32, revoked);
};

}  // namespace app::repositories

#include OATPP_CODEGEN_END(DTO)
