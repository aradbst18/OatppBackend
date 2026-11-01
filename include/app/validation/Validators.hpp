#pragma once

#include <string>

namespace app::validation
{

/// Username rule: 4-18 chars from [a-zA-Z0-9@#$]. Mirrors the reference
/// project's convention with a documented, testable helper.
bool isValidUsername(const std::string& value) noexcept;

/// New passwords: 15-128 UTF-8 code points, no controls or common passwords.
bool isValidPassword(const std::string& value) noexcept;

/// Enforce the new-password policy with a public-safe, specific rejection reason.
void validatePassword(const std::string& value);

/// Non-empty after trimming ASCII whitespace.
bool isNonBlank(const std::string& value) noexcept;

/// Validate a required string field. Returns an error message or empty string.
std::string checkRequired(const std::string& fieldName, const std::string& value);

}  // namespace app::validation
