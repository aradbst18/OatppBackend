#include "app/validation/Validators.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string_view>

#include "app/errors/AppException.hpp"

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

bool hasValidPasswordLengthAndEncoding(const std::string& value) noexcept
{
    if (value.size() < 15 || value.size() > 128 * 4)
    {
        return false;
    }

    std::size_t count = 0;
    for (std::size_t i = 0; i < value.size(); ++count)
    {
        const auto lead = static_cast<unsigned char>(value[i++]);
        std::uint32_t point = lead;
        unsigned int continuation = 0;
        std::uint32_t minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf)
        {
            point = lead & 0x1fu;
            continuation = 1;
            minimum = 0x80;
        }
        else if (lead >= 0xe0 && lead <= 0xef)
        {
            point = lead & 0x0fu;
            continuation = 2;
            minimum = 0x800;
        }
        else if (lead >= 0xf0 && lead <= 0xf4)
        {
            point = lead & 0x07u;
            continuation = 3;
            minimum = 0x10000;
        }
        else if (lead >= 0x80)
        {
            return false;
        }

        for (unsigned int j = 0; j < continuation; ++j)
        {
            if (i == value.size())
            {
                return false;
            }
            const auto next = static_cast<unsigned char>(value[i++]);
            if ((next & 0xc0u) != 0x80u)
            {
                return false;
            }
            point = (point << 6) | (next & 0x3fu);
        }
        if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff) || point < 0x20 ||
            (point >= 0x7f && point <= 0x9f))
        {
            return false;
        }
    }
    return count >= 15 && count <= 128;
}

bool isCommonPassword(const std::string& value) noexcept
{
    // shortcut: offline snapshot misses newer breaches; refresh it when threat information changes.
    static constexpr std::string_view passwords[] = {
#include "passwords/CommonPasswords.inc"
    };
    return std::binary_search(std::begin(passwords), std::end(passwords), std::string_view(value));
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
    return hasValidPasswordLengthAndEncoding(value) && !isCommonPassword(value);
}

void validatePassword(const std::string& value)
{
    if (!hasValidPasswordLengthAndEncoding(value))
    {
        throw errors::validation::PasswordFormat();
    }
    if (isCommonPassword(value))
    {
        throw errors::validation::PasswordTooCommon();
    }
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
