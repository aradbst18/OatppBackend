#pragma once

#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"

#include OATPP_CODEGEN_BEGIN(DTO)

namespace app::repositories
{

/**
 * Row of the `users` table. Due to the two DTO_FIELD forms, the first field
 * keeps the project's camelCase member name while the column stays
 * `password_hash`: the second macro argument (the property name) is what both
 * adapters match result columns against and what `QUERY` parameter paths use.
 *
 * Carries data only: no validation, no defaults with side effects (the
 * repository checks before it writes, see docs/guides/adding-repository.md).
 */
class UserDto : public oatpp::DTO
{
    DTO_INIT(UserDto, DTO /* Extends */)

    DTO_FIELD(oatpp::String, id);
    DTO_FIELD(oatpp::String, username);
    DTO_FIELD(oatpp::String, passwordHash, "password_hash");
    DTO_FIELD(oatpp::String, role);
    DTO_FIELD(oatpp::Int64, sessionVersion, "session_version");
};

}  // namespace app::repositories

#include OATPP_CODEGEN_END(DTO)
