#include "app/validation/Validators.hpp"

#include <cctype>

namespace app::validation
{

namespace
{

bool inSet(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '@' || c == '#' ||
           c == '$';
}

bool isBlankChar(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

}  // namespace

bool isValidUsername(const std::string& value) noexcept
{
    if (value.size() < 4 || value.size() > 18)
    {
        return false;
    }

    for (char c : value)
    {
        if (!inSet(c))
        {
            return false;
        }
    }

    return true;
}

bool isValidPassword(const std::string& value) noexcept
{
    if (value.size() < 6 || value.size() > 48)
    {
        return false;
    }

    bool hasLetter = false;
    bool hasDigit = false;

    for (char c : value)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
        {
            hasLetter = true;
        }

        if (c >= '0' && c <= '9')
        {
            hasDigit = true;
        }
    }

    return hasLetter && hasDigit;
}

bool isNonBlank(const std::string& value) noexcept
{
    for (char c : value)
    {
        if (!isBlankChar(c))
        {
            return true;
        }
    }

    return false;
}

std::string checkRequired(const std::string& fieldName, const std::string& value)
{
    if (!isNonBlank(value))
    {
        return fieldName + " is required";
    }

    return "";
}

}  // namespace app::validation
