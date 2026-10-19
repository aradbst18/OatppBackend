#pragma once

#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"

#include OATPP_CODEGEN_BEGIN(DTO)

namespace app::dto
{

/// User account summary response body.
/// Exposes public account identity and authorization role without leaking credentials.
class UserResponseDto : public oatpp::DTO
{
    DTO_INIT(UserResponseDto, DTO /* Extends */)

#ifdef APP_WITH_SWAGGER
    DTO_FIELD_INFO(username);
    DTO_FIELD_INFO(role);
#endif  // APP_WITH_SWAGGER

    DTO_FIELD(String, username);
    DTO_FIELD(String, role);
};

/// Shared request body for creating (POST /users) or fully updating (PUT /users/{username}) an account.
/// All fields (username, password, and role) are mandatory and strictly validated.
class UserUpsertDto : public oatpp::DTO
{
    DTO_INIT(UserUpsertDto, DTO /* Extends */)

#ifdef APP_WITH_SWAGGER
    DTO_FIELD_INFO(username);
    DTO_FIELD_INFO(password);
    DTO_FIELD_INFO(role);
#endif  // APP_WITH_SWAGGER

    DTO_FIELD(String, username);
    DTO_FIELD(String, password);
    DTO_FIELD(String, role);

    /// Validates that all fields are non-empty and satisfy domain constraints.
    /// Throws app::errors::request::Missing if any field is absent or empty.
    /// Throws app::errors::validation::InvalidValue if any field value fails charset/length/policy rules.
    void validate() const;
};

// Aliases for clear semantic usage in service and controller layers.
using CreateUserDto = UserUpsertDto;
using UpdateUserDto = UserUpsertDto;

/// Request body for partial updates (PATCH /users/{username}).
/// All fields are optional; any omitted or null field remains unchanged in the database.
/// Any provided field must satisfy strict domain validation rules.
class PatchUserDto : public oatpp::DTO
{
    DTO_INIT(PatchUserDto, DTO /* Extends */)

#ifdef APP_WITH_SWAGGER
    DTO_FIELD_INFO(username);
    DTO_FIELD_INFO(password);
    DTO_FIELD_INFO(role);
#endif  // APP_WITH_SWAGGER

    DTO_FIELD(String, username);
    DTO_FIELD(String, password);
    DTO_FIELD(String, role);

    /// Validates provided fields against domain constraints and ensures at least one field is provided.
    /// Throws app::errors::request::Missing if all fields are null/empty.
    /// Throws app::errors::validation::InvalidValue if any provided field fails validation.
    void validate() const;
};

/// Response body returned after deleting multiple users (DELETE /users).
class DeleteUsersResponseDto : public oatpp::DTO
{
    DTO_INIT(DeleteUsersResponseDto, DTO /* Extends */)

#ifdef APP_WITH_SWAGGER
    DTO_FIELD_INFO(deletedCount);
    DTO_FIELD_INFO(message);
#endif  // APP_WITH_SWAGGER

    DTO_FIELD(Int64, deletedCount, "deleted_count");
    DTO_FIELD(String, message);
};

}  // namespace app::dto

#include OATPP_CODEGEN_END(DTO)
