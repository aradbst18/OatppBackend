#include "app/dto/AuthDtos.hpp"

// Swagger property documentation: DTO_FIELD_INFO declarations
// live in the header (guarded by APP_WITH_SWAGGER); the Z__PROPERTY_ADD_INFO
// bodies live here so no swagger text remains in headers. This TU is only
// compiled when APP_ENABLE_SWAGGER=ON (see CMakeLists), so the docs are
// completely absent from non-swagger builds.

#ifdef APP_WITH_SWAGGER

namespace app::dto
{

void LoginRequestDto::Z__PROPERTY_ADD_INFO_username(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->required = true;
    info->pattern = "^[a-zA-Z0-9@#$]{4,18}$";
    info->description =
        R"(**Account username.**

**Validation Rules:**
- Length: strictly bounded between 4 and 18 characters.
- Allowed characters: English letters (A-Z, a-z), digits (0-9), and `@`, `#`, `$`.
- Case-sensitive.

Example: `admin01`)";
}

void LoginRequestDto::Z__PROPERTY_ADD_INFO_password(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->required = true;
    info->pattern = "^.{6,48}$";
    info->description =
        R"(**Account password.**

**Validation Rules:**
- Length: strictly bounded between 6 and 48 characters.
- Must contain at least one letter and one digit.
- Sent as JSON, then verified against the stored password hash on the backend.

Example: `Alice1234`)";
}

void LoginRequestDto::Z__PROPERTY_ADD_INFO_pinCode(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->required = false;
    info->description =
        "Optional demo field exercising Int8 range validation. "
        "Out-of-range values fail deserialization with 400 before the controller runs.";
}

void LoginResponseDto::Z__PROPERTY_ADD_INFO_accessToken(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->description = "JWT access token to send as `Authorization: Bearer <token>`.";
}

void LoginResponseDto::Z__PROPERTY_ADD_INFO_refreshToken(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->description = "Opaque refresh token used to obtain a new access token.";
}

void LoginResponseDto::Z__PROPERTY_ADD_INFO_tokenType(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->description = "Token type, always `Bearer`.";
}

void LoginResponseDto::Z__PROPERTY_ADD_INFO_username(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->description = "Authenticated username.";
}

void LoginResponseDto::Z__PROPERTY_ADD_INFO_role(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->description = "Role embedded in the token (`admin` or `operator`).";
}

void LoginResponseDto::Z__PROPERTY_ADD_INFO_expiresIn(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->description = "Lifetime of the access token in seconds.";
}

void RefreshRequestDto::Z__PROPERTY_ADD_INFO_refreshToken(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->required = true;
    info->description = "Opaque refresh token previously returned by login or refresh.";
}

void WhoAmIResponseDto::Z__PROPERTY_ADD_INFO_message(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->description = "Greeting confirming the caller was authenticated.";
}

void WhoAmIResponseDto::Z__PROPERTY_ADD_INFO_username(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->description = "Username from the verified JWT.";
}

void WhoAmIResponseDto::Z__PROPERTY_ADD_INFO_role(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->description = "Role from the verified JWT.";
}

}  // namespace app::dto

#endif  // APP_WITH_SWAGGER
