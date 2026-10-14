#include "app/domain/UserRole.hpp"

namespace app::domain
{

const char* toString(UserRole role) noexcept
{
    switch (role)
    {
        case UserRole::Viewer:
            return "viewer";
        case UserRole::Operator:
            return "operator";
        case UserRole::Admin:
            return "admin";
    }
    return "unknown";
}

bool tryParseUserRole(const std::string& text, UserRole& out) noexcept
{
    if (text == "viewer")
    {
        out = UserRole::Viewer;
        return true;
    }

    if (text == "operator")
    {
        out = UserRole::Operator;
        return true;
    }

    if (text == "admin")
    {
        out = UserRole::Admin;
        return true;
    }

    return false;
}

bool isValidUserRoleString(const std::string& text) noexcept
{
    UserRole tmp{};
    return tryParseUserRole(text, tmp);
}

}  // namespace app::domain
