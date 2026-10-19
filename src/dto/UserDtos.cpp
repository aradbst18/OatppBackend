#include "app/dto/UserDtos.hpp"

#include "app/domain/UserRole.hpp"
#include "app/errors/AppException.hpp"
#include "app/validation/Validators.hpp"

namespace app::dto
{

void UserUpsertDto::validate() const
{
    if (!username || username->empty())
    {
        throw errors::request::Missing("username is required");
    }

    if (!password || password->empty())
    {
        throw errors::request::Missing("password is required");
    }

    if (!role || role->empty())
    {
        throw errors::request::Missing("role is required");
    }

    if (!validation::isValidUsername(*username))
    {
        throw errors::validation::InvalidValue(
            "Invalid username: must be 4-18 characters containing alphanumeric or @#$ characters");
    }

    if (!validation::isValidPassword(*password))
    {
        throw errors::validation::InvalidValue(
            "Invalid password: must be 6-48 characters and contain at least one letter and one digit");
    }

    domain::UserRole parsedRole{};
    if (!domain::tryParseUserRole(*role, parsedRole))
    {
        throw errors::validation::InvalidValue("Invalid user role: '" + std::string(*role) +
                                               "'. Allowed roles are 'admin', 'operator', 'viewer'");
    }
}

void PatchUserDto::validate() const
{
    const bool hasUsername = username && !username->empty();
    const bool hasPassword = password && !password->empty();
    const bool hasRole = role && !role->empty();

    if (!hasUsername && !hasPassword && !hasRole)
    {
        throw errors::request::Missing(
            "At least one field (username, password, or role) must be provided for partial update");
    }

    if (hasUsername && !validation::isValidUsername(*username))
    {
        throw errors::validation::InvalidValue(
            "Invalid username: must be 4-18 characters containing alphanumeric or @#$ characters");
    }

    if (hasPassword && !validation::isValidPassword(*password))
    {
        throw errors::validation::InvalidValue(
            "Invalid password: must be 6-48 characters and contain at least one letter and one digit");
    }

    if (hasRole)
    {
        domain::UserRole parsedRole{};
        if (!domain::tryParseUserRole(*role, parsedRole))
        {
            throw errors::validation::InvalidValue("Invalid user role: '" + std::string(*role) +
                                                   "'. Allowed roles are 'admin', 'operator', 'viewer'");
        }
    }
}

#ifdef APP_WITH_SWAGGER

void UserResponseDto::Z__PROPERTY_ADD_INFO_username(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->description = "Unique account username.";
}

void UserResponseDto::Z__PROPERTY_ADD_INFO_role(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->description = "Assigned security and authorization role (`admin`, `operator`, or `viewer`).";
}

void UserUpsertDto::Z__PROPERTY_ADD_INFO_username(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->required = true;
    info->pattern = "^[a-zA-Z0-9@#$]{4,18}$";
    info->description =
        R"(**Account username.**

**Validation Rules:**
- Length: strictly bounded between 4 and 18 characters.
- Allowed characters: ASCII alphanumeric (`[a-zA-Z0-9]`) and `@`, `#`, `$`.
- Case-sensitive.
- For `PUT /users/{username}`, this specifies the new (or retained) username.

Example: `alice01`)";
}

void UserUpsertDto::Z__PROPERTY_ADD_INFO_password(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->required = true;
    info->pattern = "^.{6,48}$";
    info->description =
        R"(**Account plain-text password.**

**Validation Rules:**
- Length: strictly bounded between 6 and 48 characters.
- Complexity: must contain at least one letter and at least one digit.
- Never returned in responses; salted and hashed immediately on arrival.

Example: `Secret1234`)";
}

void UserUpsertDto::Z__PROPERTY_ADD_INFO_role(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->required = true;
    info->description =
        R"(**Assigned role.**

**Allowed Values:**
- `admin`: Full administrative access to user management and server resources.
- `operator`: Standard operational access.
- `viewer`: Read-only access.

Example: `operator`)";
}

void PatchUserDto::Z__PROPERTY_ADD_INFO_username(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->required = false;
    info->pattern = "^[a-zA-Z0-9@#$]{4,18}$";
    info->description =
        R"(**Optional new username.**

- If supplied, renames the account (subject to uniqueness and admin immutability checks).
- If omitted or null, the current username remains unchanged.

Example: `new_alice`)";
}

void PatchUserDto::Z__PROPERTY_ADD_INFO_password(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->required = false;
    info->pattern = "^.{6,48}$";
    info->description =
        R"(**Optional new password.**

- If supplied, updates the account password (must be 6-48 characters with letter + digit).
- If omitted or null, the existing password remains unchanged.

Example: `NewSecret987`)";
}

void PatchUserDto::Z__PROPERTY_ADD_INFO_role(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->required = false;
    info->description =
        R"(**Optional new role.**

- Allowed values: `admin`, `operator`, `viewer`.
- If omitted or null, the existing role remains unchanged.

Example: `admin`)";
}

void DeleteUsersResponseDto::Z__PROPERTY_ADD_INFO_deletedCount(
    oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->description = "Number of non-admin user accounts successfully deleted.";
}

void DeleteUsersResponseDto::Z__PROPERTY_ADD_INFO_message(oatpp::data::mapping::type::BaseObject::Property::Info* info)
{
    info->description = "Human-readable summary confirming deletion and admin retention.";
}

#endif  // APP_WITH_SWAGGER

}  // namespace app::dto
