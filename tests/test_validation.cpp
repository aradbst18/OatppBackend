#include "support/TestRunner.hpp"

#include "app/domain/UserRole.hpp"
#include "app/validation/NumericBounds.hpp"
#include "app/validation/Validators.hpp"

using namespace app;

TEST_CASE("validation: int8 bounds")
{
    EXPECT_TRUE(validation::fitsInBounds<std::int8_t>(127));
    EXPECT_TRUE(validation::fitsInBounds<std::int8_t>(-128));
    EXPECT_FALSE(validation::fitsInBounds<std::int8_t>(128));
    EXPECT_FALSE(validation::fitsInBounds<std::int8_t>(64654646));
    EXPECT_FALSE(validation::fitsInBounds<std::int8_t>(-129));
}

TEST_CASE("validation: uint8 bounds reject negatives")
{
    EXPECT_TRUE(validation::fitsInBounds<std::uint8_t>(255));
    EXPECT_FALSE(validation::fitsInBounds<std::uint8_t>(256));
    EXPECT_FALSE(validation::fitsInBounds<std::uint8_t>(-1));
}

TEST_CASE("validation: int16/uint16/int32/uint32 bounds")
{
    EXPECT_TRUE(validation::fitsInBounds<std::int16_t>(32767));
    EXPECT_FALSE(validation::fitsInBounds<std::int16_t>(32768));
    EXPECT_TRUE(validation::fitsInBounds<std::uint16_t>(65535));
    EXPECT_FALSE(validation::fitsInBounds<std::uint16_t>(65536));
    EXPECT_TRUE(validation::fitsInBounds<std::int32_t>(2147483647));
    EXPECT_FALSE(validation::fitsInBounds<std::int32_t>(2147483648LL));
    EXPECT_TRUE(validation::fitsUnsignedInBounds<std::uint32_t>(4294967295ULL));
    EXPECT_FALSE(validation::fitsUnsignedInBounds<std::uint32_t>(4294967296ULL));
}

TEST_CASE("validation: username rules")
{
    EXPECT_TRUE(validation::isValidUsername("admin"));
    EXPECT_TRUE(validation::isValidUsername("user@1#"));
    EXPECT_FALSE(validation::isValidUsername("ab"));                    // too short
    EXPECT_FALSE(validation::isValidUsername("01234567890123456789"));  // too long
    EXPECT_FALSE(validation::isValidUsername("bad!name"));              // illegal char
    EXPECT_FALSE(validation::isValidUsername(""));
}

TEST_CASE("validation: password rules")
{
    EXPECT_TRUE(validation::isValidPassword("Admin1234"));
    EXPECT_FALSE(validation::isValidPassword("short"));     // no digit + short
    EXPECT_FALSE(validation::isValidPassword("12345678"));  // no letter
    EXPECT_FALSE(validation::isValidPassword("a1"));        // too short
    EXPECT_FALSE(validation::isValidPassword(""));
}

TEST_CASE("validation: required fields")
{
    EXPECT_TRUE(validation::checkRequired("username", "admin").empty());
    EXPECT_FALSE(validation::checkRequired("username", "   ").empty());
    EXPECT_FALSE(validation::checkRequired("username", "").empty());
}

TEST_CASE("domain: user roles parse and reject")
{
    domain::UserRole role{};
    EXPECT_TRUE(domain::tryParseUserRole("admin", role));
    EXPECT_TRUE(role == domain::UserRole::Admin);
    EXPECT_TRUE(domain::tryParseUserRole("viewer", role));
    EXPECT_TRUE(domain::tryParseUserRole("operator", role));
    EXPECT_FALSE(domain::tryParseUserRole("ADMIN", role));  // canonical lowercase only
    EXPECT_FALSE(domain::tryParseUserRole("", role));
    EXPECT_FALSE(domain::tryParseUserRole("root", role));
    EXPECT_TRUE(domain::isValidUserRoleString("admin"));
    EXPECT_FALSE(domain::isValidUserRoleString("superuser"));
    EXPECT_EQ(std::string(domain::toString(domain::UserRole::Admin)), std::string("admin"));
}
