#include "support/TestRunner.hpp"

#include <chrono>

#include <jwt-cpp/jwt.h>

#include "support/TestDb.hpp"

#include "app/auth/JwtService.hpp"
#include "app/errors/AppException.hpp"
#include "app/infrastructure/database/DatabaseFactory.hpp"
#include "app/repositories/UserRepository.hpp"
#include "app/services/AuthService.hpp"

using namespace app;

namespace
{

std::shared_ptr<services::AuthService> makeService(const std::string& secret = "test-secret-123")
{
    auto db = tests::createTestDatabase();
    db->open();
    auto users = std::make_shared<repositories::UserRepository>(db);
    users->ensureSchema();
    auto jwt = std::make_shared<auth::JwtService>(secret, "test-issuer", std::chrono::seconds(3600));
    auto svc = std::make_shared<services::AuthService>(users, jwt);
    svc->ensureSeeded();
    return svc;
}

}  // namespace

TEST_CASE("auth: jwt create and verify")
{
    auth::JwtService jwt("test-secret-123", "test-issuer", std::chrono::seconds(3600));
    const std::string token = jwt.create("admin", "admin");
    EXPECT_TRUE(!token.empty());
    const auth::TokenClaims claims = jwt.verify(token);
    EXPECT_EQ(claims.subject, std::string("admin"));
    EXPECT_EQ(claims.role, std::string("admin"));
    EXPECT_EQ(claims.issuer, std::string("test-issuer"));
}

TEST_CASE("auth: jwt rejects wrong secret")
{
    auth::JwtService a("test-secret-123", "test-issuer", std::chrono::seconds(3600));
    auth::JwtService b("other-secret-456", "test-issuer", std::chrono::seconds(3600));
    const std::string token = a.create("admin", "admin");
    EXPECT_THROW_AS(b.verify(token), errors::authentication::InvalidToken);
}

TEST_CASE("auth: jwt rejects expired token")
{
    // Craft an already-expired token directly (bypasses the positive-TTL guard
    // on JwtService::create). The verifier must reject it despite the 30s
    // clock-skew leeway, since it expired over an hour ago.
    const std::string expired = jwt::create()
                                    .set_issuer("test-issuer")
                                    .set_subject("admin")
                                    .set_issued_at(std::chrono::system_clock::now() - std::chrono::hours(2))
                                    .set_expires_at(std::chrono::system_clock::now() - std::chrono::hours(1))
                                    .set_payload_claim("role", jwt::claim(std::string("admin")))
                                    .sign(jwt::algorithm::hs256{"test-secret-123"});
    auth::JwtService jwt("test-secret-123", "test-issuer", std::chrono::seconds(3600));
    EXPECT_THROW_AS(jwt.verify(expired), errors::AppException);
}

TEST_CASE("auth: jwt rejects malformed and missing tokens")
{
    auth::JwtService jwt("test-secret-123", "test-issuer", std::chrono::seconds(3600));
    EXPECT_THROW_AS(jwt.verify(""), errors::authentication::MissingCredentials);
    EXPECT_THROW_AS(jwt.verify("not.a.token"), errors::authentication::InvalidToken);
    EXPECT_THROW_AS(jwt.verify("a.b.c"), errors::authentication::InvalidToken);
}

TEST_CASE("auth: bearer extraction")
{
    EXPECT_EQ(auth::JwtService::extractBearer("Bearer abc123"), std::string("abc123"));
    EXPECT_THROW_AS(auth::JwtService::extractBearer(""), errors::authentication::MissingCredentials);
    EXPECT_THROW_AS(auth::JwtService::extractBearer("Token abc"), errors::authentication::MissingCredentials);
    EXPECT_THROW_AS(auth::JwtService::extractBearer("Bearer "), errors::authentication::MissingCredentials);
}

TEST_CASE("auth: login succeeds with seed account")
{
    auto svc = makeService();
    const services::LoginResult r = svc->login("admin", "Admin1234");
    EXPECT_EQ(r.username, std::string("admin"));
    EXPECT_TRUE(!r.accessToken.empty());
    EXPECT_EQ(r.role, std::string("admin"));
}

TEST_CASE("auth: login rejects wrong password")
{
    auto svc = makeService();
    EXPECT_THROW_AS(svc->login("admin", "Wrong1234"), errors::authentication::InvalidCredentials);
}

TEST_CASE("auth: login rejects unknown user with 401 (not 404)")
{
    auto svc = makeService();
    try
    {
        svc->login("nobody", "Nope1234");
        EXPECT_TRUE(false);
    }
    catch (const errors::authentication::InvalidCredentials&)
    {
    }
}

TEST_CASE("auth: login rejects malformed input with 422")
{
    auto svc = makeService();
    EXPECT_THROW_AS(svc->login("ab", "Admin1234"), errors::validation::InvalidValue);
    EXPECT_THROW_AS(svc->login("admin", "short"), errors::validation::InvalidValue);
}

TEST_CASE("auth: whoami echoes claims")
{
    auto svc = makeService();
    auth::TokenClaims claims;
    claims.subject = "admin";
    claims.role = "admin";
    claims.issuer = "test-issuer";
    const services::LoginResult r = svc->whoAmI(claims);
    EXPECT_EQ(r.username, std::string("admin"));
    EXPECT_EQ(r.role, std::string("admin"));
}
