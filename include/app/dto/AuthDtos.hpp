#pragma once

#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"

#include OATPP_CODEGEN_BEGIN(DTO)

namespace app::dto
{

/// POST /auth/login request body.
class LoginRequestDto : public oatpp::DTO
{
    DTO_INIT(LoginRequestDto, DTO /* Extends */)

#ifdef APP_WITH_SWAGGER
    DTO_FIELD_INFO(username);
    DTO_FIELD_INFO(password);
    DTO_FIELD_INFO(pinCode);
#endif  // APP_WITH_SWAGGER

    DTO_FIELD(String, username);
    DTO_FIELD(String, password);
    DTO_FIELD(Int8, pinCode, "pinCode");  // optional demo of Int8 range validation
};

/// POST /auth/login response body.
class LoginResponseDto : public oatpp::DTO
{
    DTO_INIT(LoginResponseDto, DTO /* Extends */)

#ifdef APP_WITH_SWAGGER
    DTO_FIELD_INFO(accessToken);
    DTO_FIELD_INFO(refreshToken);
    DTO_FIELD_INFO(tokenType);
    DTO_FIELD_INFO(username);
    DTO_FIELD_INFO(role);
    DTO_FIELD_INFO(expiresIn);
#endif  // APP_WITH_SWAGGER

    DTO_FIELD(String, accessToken, "access_token");
    DTO_FIELD(String, refreshToken, "refresh_token");
    DTO_FIELD(String, tokenType, "token_type");
    DTO_FIELD(String, username);
    DTO_FIELD(String, role);
    DTO_FIELD(Int64, expiresIn, "expires_in");
};

/// POST /auth/refresh request body.
class RefreshRequestDto : public oatpp::DTO
{
    DTO_INIT(RefreshRequestDto, DTO /* Extends */)

#ifdef APP_WITH_SWAGGER
    DTO_FIELD_INFO(refreshToken);
#endif  // APP_WITH_SWAGGER

    DTO_FIELD(String, refreshToken, "refresh_token");
};

/// GET /auth/who-am-i response body.
class WhoAmIResponseDto : public oatpp::DTO
{
    DTO_INIT(WhoAmIResponseDto, DTO /* Extends */)

#ifdef APP_WITH_SWAGGER
    DTO_FIELD_INFO(message);
    DTO_FIELD_INFO(username);
    DTO_FIELD_INFO(role);
#endif  // APP_WITH_SWAGGER

    DTO_FIELD(String, message);
    DTO_FIELD(String, username);
    DTO_FIELD(String, role);
};

/// Verified token claims stashed in request bundle by AuthInterceptor.
class AuthClaimsDto : public oatpp::DTO
{
    DTO_INIT(AuthClaimsDto, DTO /* Extends */)

    DTO_FIELD(String, subject);
    DTO_FIELD(String, role);
    DTO_FIELD(String, issuer);
    DTO_FIELD(Int64, sessionVersion, "session_version");
};

}  // namespace app::dto

#include OATPP_CODEGEN_END(DTO)
