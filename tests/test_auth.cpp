#include "support/TestRunner.hpp"

#include <chrono>

#include <jwt-cpp/jwt.h>

#include "support/TestDb.hpp"

#include "app/auth/JwtService.hpp"
#include "app/errors/AppException.hpp"
#include "app/infrastructure/database/DatabaseFactory.hpp"
#include "app/repositories/RefreshTokenRepository.hpp"
#include "app/repositories/UserRepository.hpp"
#include "app/services/AuthService.hpp"

using namespace app;

namespace
{

constexpr const char* kTestSecret = "test-secret-123456789012345678901234";
constexpr const char* kOtherSecret = "other-secret-456789012345678901234";

std::shared_ptr<services::AuthService> makeService(const std::string& secret = kTestSecret)
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
    auth::JwtService jwt(kTestSecret, "test-issuer", std::chrono::seconds(3600));

    const std::string token = jwt.create("admin", "admin");
    EXPECT_TRUE(!token.empty());

    const auth::TokenClaims claims = jwt.verify(token);
    EXPECT_EQ(claims.subject, std::string("admin"));
    EXPECT_EQ(claims.role, std::string("admin"));
    EXPECT_EQ(claims.issuer, std::string("test-issuer"));
}

TEST_CASE("auth: jwt rejects wrong secret")
{
    auth::JwtService a(kTestSecret, "test-issuer", std::chrono::seconds(3600));
    auth::JwtService b(kOtherSecret, "test-issuer", std::chrono::seconds(3600));

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
                                    .sign(jwt::algorithm::hs256{kTestSecret});

    auth::JwtService jwt(kTestSecret, "test-issuer", std::chrono::seconds(3600));
    EXPECT_THROW_AS(jwt.verify(expired), errors::AppException);
}

TEST_CASE("auth: jwt rejects malformed and missing tokens")
{
    auth::JwtService jwt(kTestSecret, "test-issuer", std::chrono::seconds(3600));

    EXPECT_THROW_AS(jwt.verify(""), errors::authentication::MissingCredentials);
    EXPECT_THROW_AS(jwt.verify("not.a.token"), errors::authentication::InvalidToken);
    EXPECT_THROW_AS(jwt.verify("a.b.c"), errors::authentication::InvalidToken);
}

TEST_CASE("auth: jwt rejects short secret and generates random secrets")
{
    EXPECT_THROW_AS(auth::JwtService("short-secret-under-32-chars", "test-issuer", std::chrono::seconds(3600)),
                    errors::config::Invalid);

    const std::string sec1 = auth::generateRandomSecret(32);
    const std::string sec2 = auth::generateRandomSecret(32);

    EXPECT_TRUE(sec1.size() >= 32);
    EXPECT_TRUE(sec2.size() >= 32);
    EXPECT_TRUE(sec1 != sec2);
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

TEST_CASE("auth: seed credentials can be customized")
{
    auto db = tests::createTestDatabase();
    db->open();

    auto users = std::make_shared<repositories::UserRepository>(db);
    users->ensureSchema();

    auto jwt = std::make_shared<auth::JwtService>(kTestSecret, "test-issuer", std::chrono::seconds(3600));

    auto svc = std::make_shared<services::AuthService>(users, jwt);
    svc->ensureSeeded("customAdmin", "CustomPass123");

    const services::LoginResult r = svc->login("customAdmin", "CustomPass123");
    EXPECT_EQ(r.username, std::string("customAdmin"));
    EXPECT_EQ(r.role, std::string("admin"));
    EXPECT_TRUE(!r.accessToken.empty());
    EXPECT_EQ(r.expiresInSeconds, 3600);
}

TEST_CASE("auth: expiresInSeconds syncs with JwtService TTL")
{
    auto db = tests::createTestDatabase();
    db->open();

    auto users = std::make_shared<repositories::UserRepository>(db);
    users->ensureSchema();

    auto jwt = std::make_shared<auth::JwtService>(kTestSecret, "test-issuer", std::chrono::seconds(7200));
    EXPECT_EQ(jwt->ttl().count(), 7200);

    auto svc = std::make_shared<services::AuthService>(users, jwt);
    svc->ensureSeeded("ttlUser", "Pass1234");

    const services::LoginResult r = svc->login("ttlUser", "Pass1234");
    EXPECT_EQ(r.expiresInSeconds, 7200);
}

TEST_CASE("auth: refresh token creation, rotation, and revocation")
{
    auto db = tests::createTestDatabase();
    db->open();

    auto users = std::make_shared<repositories::UserRepository>(db);
    users->ensureSchema();

    auto refreshTokens = std::make_shared<repositories::RefreshTokenRepository>(db);
    refreshTokens->ensureSchema();

    auto jwt = std::make_shared<auth::JwtService>(kTestSecret, "test-issuer", std::chrono::seconds(3600));
    auto svc = std::make_shared<services::AuthService>(users, jwt, refreshTokens, std::chrono::seconds(3600));
    svc->ensureSeeded("alice", "AlicePass123");

    // 1. Login issues a refresh token
    const services::LoginResult loginRes = svc->login("alice", "AlicePass123");
    EXPECT_FALSE(loginRes.accessToken.empty());
    EXPECT_FALSE(loginRes.refreshToken.empty());
    const std::string originalRefreshToken = loginRes.refreshToken;

    // 2. Refresh with valid token succeeds and issues rotated new token
    const services::LoginResult refreshRes = svc->refresh(originalRefreshToken);
    EXPECT_FALSE(refreshRes.accessToken.empty());
    EXPECT_FALSE(refreshRes.refreshToken.empty());
    EXPECT_TRUE(refreshRes.refreshToken != originalRefreshToken);
    EXPECT_EQ(refreshRes.username, std::string("alice"));

    // 3. Re-using the old refresh token fails (revoked)
    EXPECT_THROW_AS(svc->refresh(originalRefreshToken), errors::authentication::InvalidToken);

    // 4. Refreshing with rotated new token succeeds
    const services::LoginResult refreshRes2 = svc->refresh(refreshRes.refreshToken);
    EXPECT_FALSE(refreshRes2.accessToken.empty());
    EXPECT_TRUE(refreshRes2.refreshToken != refreshRes.refreshToken);

    // 5. Non-existent refresh token fails
    EXPECT_THROW_AS(svc->refresh("non-existent-token-xyz"), errors::authentication::InvalidToken);

    // 6. Empty refresh token fails
    EXPECT_THROW_AS(svc->refresh(""), errors::request::Missing);
}

TEST_CASE("auth: expired refresh token is rejected")
{
    auto db = tests::createTestDatabase();
    db->open();

    auto users = std::make_shared<repositories::UserRepository>(db);
    users->ensureSchema();

    auto refreshTokens = std::make_shared<repositories::RefreshTokenRepository>(db);
    refreshTokens->ensureSchema();

    auto jwt = std::make_shared<auth::JwtService>(kTestSecret, "test-issuer", std::chrono::seconds(3600));

    // Negative TTL so it expires immediately
    auto svc = std::make_shared<services::AuthService>(users, jwt, refreshTokens, std::chrono::seconds(-10));
    svc->ensureSeeded("bobby", "BobPass123");

    const services::LoginResult loginRes = svc->login("bobby", "BobPass123");
    EXPECT_FALSE(loginRes.refreshToken.empty());

    EXPECT_THROW_AS(svc->refresh(loginRes.refreshToken), errors::authentication::TokenExpired);
}
