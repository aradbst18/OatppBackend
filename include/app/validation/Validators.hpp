#pragma once

#include <string>

namespace app::validation
{

/// Username rule: 4-18 chars from [a-zA-Z0-9@#$]. Mirrors the reference
/// project's convention with a documented, testable helper.
bool isValidUsername(const std::string& value) noexcept;

/// Password rule: 6-48 chars; must contain at least one letter and one digit.
bool isValidPassword(const std::string& value) noexcept;

/// Non-empty after trimming ASCII whitespace.
bool isNonBlank(const std::string& value) noexcept;

/// Validate a required string field. Returns an error message or empty string.
std::string checkRequired(const std::string& fieldName, const std::string& value);

}  // namespace app::validation
