#pragma once

#include <cstdint>
#include <string>

namespace app::domain
{

/**
 * Application user roles.
 *
 * Rules:
 * - Declared as `enum class` (never plain enum, never magic numbers).
 * - Serialized on the wire as canonical lowercase strings ("admin", ...).
 * - Unknown strings are rejected (see tryParse); no silent fallback.
 * - The oatpp DTO layer uses its own string field and converts through
 *   toString/tryParse so Swagger shows readable values.
 */
enum class UserRole : std::int32_t
{
    Viewer = 0,
    Operator = 1,
    Admin = 2
};

const char* toString(UserRole role) noexcept;

/// Parse a canonical role string. Returns false for unknown values.
bool tryParseUserRole(const std::string& text, UserRole& out) noexcept;

bool isValidUserRoleString(const std::string& text) noexcept;

}  // namespace app::domain
