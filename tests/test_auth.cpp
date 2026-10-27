#include "support/TestRunner.hpp"

#include <chrono>
#include <vector>

#include <jwt-cpp/jwt.h>

#include "support/ScopedEnvironment.hpp"
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
constexpr const char* kTestAdminPassword = "TestAdmin8391";

struct AuthFixture
{
    std::shared_ptr<repositories::UserRepository> users;
    std::shared_ptr<services::AuthService> service;

    explicit AuthFixture(const std::string& secret = kTestSecret)
    {
        auto db = tests::createTestDatabase();
        db->open();
        users = std::make_shared<repositories::UserRepository>(db);
        users->ensureSchema();
        auto jwt = std::make_shared<auth::JwtService>(secret, "test-issuer", std::chrono::seconds(3600));
        service = std::make_shared<services::AuthService>(users, jwt);
    }
};

std::shared_ptr<services::AuthService> makeService(const std::string& secret = kTestSecret)
{
    AuthFixture fixture(secret);
    fixture.service->ensureSeeded("admin", kTestAdminPassword);

    return fixture.service;
}

}  // namespace

TEST_CASE("auth: bootstrap requires a password and leaves the users table empty on failure")
{
    const tests::ScopedEnvironment username("APP_SEED_ADMIN_USERNAME", nullptr);
    AuthFixture fixture;
    {
        const tests::ScopedEnvironment password("APP_SEED_ADMIN_PASSWORD", nullptr);
        EXPECT_THROW_AS(fixture.service->ensureSeeded(), errors::config::Missing);
    }
    {
        const tests::ScopedEnvironment password("APP_SEED_ADMIN_PASSWORD", "");
        EXPECT_THROW_AS(fixture.service->ensureSeeded(), errors::config::Missing);
    }
    EXPECT_TRUE(fixture.users->listUsers()->empty());
}

TEST_CASE("auth: bootstrap rejects the published password through arguments and environment")
{
    const tests::ScopedEnvironment username("APP_SEED_ADMIN_USERNAME", nullptr);
    const tests::ScopedEnvironment password("APP_SEED_ADMIN_PASSWORD", "Admin1234");
    AuthFixture fixture;
    EXPECT_THROW_AS(fixture.service->ensureSeeded(), errors::config::Invalid);
    EXPECT_THROW_AS(fixture.service->ensureSeeded("customAdmin", "Admin1234"), errors::config::Invalid);
    EXPECT_TRUE(fixture.users->listUsers()->empty());
}

TEST_CASE("auth: bootstrap validates credentials before creating an administrator")
{
    AuthFixture fixture;
    EXPECT_THROW_AS(fixture.service->ensureSeeded("bad name", kTestAdminPassword), errors::config::Invalid);
    const std::vector<std::string> invalidPasswords = {"short", "lettersOnly", "12345678", std::string(49, 'a') + "1",
                                                       std::string("Valid123\0tail", 13)};
    for (const auto& password : invalidPasswords)
    {
        EXPECT_THROW_AS(fixture.service->ensureSeeded("admin", password), errors::config::Invalid);
    }
    EXPECT_TRUE(fixture.users->listUsers()->empty());
}

TEST_CASE("auth: bootstrap resolves environment credentials and arguments take precedence")
{
    const tests::ScopedEnvironment username("APP_SEED_ADMIN_USERNAME", "envAdmin");
    const tests::ScopedEnvironment password("APP_SEED_ADMIN_PASSWORD", "EnvAdmin8391");
    AuthFixture fixture;
    EXPECT_EQ(fixture.service->ensureSeeded(), std::string("envAdmin"));
    EXPECT_EQ(fixture.service->login("envAdmin", "EnvAdmin8391").role, std::string("admin"));
    EXPECT_THROW_AS(fixture.service->login("envAdmin", "Admin1234"), errors::authentication::InvalidCredentials);
    EXPECT_EQ(fixture.service->ensureSeeded("otherAdmin", kTestAdminPassword), std::string("otherAdmin"));
    EXPECT_EQ(fixture.service->login("otherAdmin", kTestAdminPassword).role, std::string("admin"));
    EXPECT_FALSE(fixture.users->isUserExists("admin"));
}

TEST_CASE("auth: bootstrap defaults the username but requires the environment password")
{
    const tests::ScopedEnvironment username("APP_SEED_ADMIN_USERNAME", "");
    const tests::ScopedEnvironment password("APP_SEED_ADMIN_PASSWORD", kTestAdminPassword);
    AuthFixture fixture;
    EXPECT_EQ(fixture.service->ensureSeeded(), std::string("admin"));
    EXPECT_EQ(fixture.service->login("admin", kTestAdminPassword).role, std::string("admin"));
    EXPECT_THROW_AS(fixture.service->login("admin", "Admin1234"), errors::authentication::InvalidCredentials);
}

TEST_CASE("auth: bootstrap preserves a changed password without a bootstrap secret on restart")
{
    const tests::ScopedEnvironment username("APP_SEED_ADMIN_USERNAME", nullptr);
    const tests::ScopedEnvironment password("APP_SEED_ADMIN_PASSWORD", nullptr);
    AuthFixture fixture;
    fixture.service->ensureSeeded("admin", kTestAdminPassword);
    const std::string changedHash = auth::passwordHash("ChangedAdmin8391");
    fixture.users->updatePassword("admin", changedHash);

    auto jwt = std::make_shared<auth::JwtService>(kTestSecret, "test-issuer", std::chrono::seconds(3600));
    services::AuthService restarted(fixture.users, jwt);
    EXPECT_EQ(restarted.ensureSeeded(), std::string("admin"));
    {
        const tests::ScopedEnvironment stalePassword("APP_SEED_ADMIN_PASSWORD", "Admin1234");
        restarted.ensureSeeded();
        restarted.ensureSeeded("admin", "Replacement8391");
    }
    EXPECT_EQ(std::string(*fixture.users->getUser("admin")->passwordHash), changedHash);
    EXPECT_EQ(restarted.login("admin", "ChangedAdmin8391").role, std::string("admin"));
    EXPECT_THROW_AS(restarted.login("admin", kTestAdminPassword), errors::authentication::InvalidCredentials);
}

TEST_CASE("auth: bootstrap rejects an existing published-password account without resetting it")
{
    AuthFixture fixture;
    tests::seedUser(*fixture.users, "admin", "admin", "Admin1234");
    const std::string originalHash = *fixture.users->getUser("admin")->passwordHash;
    EXPECT_THROW_AS(fixture.service->ensureSeeded("admin", kTestAdminPassword), errors::config::Invalid);
    EXPECT_EQ(std::string(*fixture.users->getUser("admin")->passwordHash), originalHash);
    EXPECT_EQ(fixture.users->listUsers()->size(), 1U);

    fixture.users->updatePassword("admin", auth::passwordHash(kTestAdminPassword));
    EXPECT_EQ(fixture.service->ensureSeeded("admin", "IgnoredPassword8391"), std::string("admin"));
    EXPECT_EQ(fixture.service->login("admin", kTestAdminPassword).role, std::string("admin"));
    EXPECT_THROW_AS(fixture.service->login("admin", "Admin1234"), errors::authentication::InvalidCredentials);
}

TEST_CASE("auth: bootstrap refuses to promote an existing non-administrator")
{
    AuthFixture fixture;
    tests::seedUser(*fixture.users, "admin", "viewer", kTestAdminPassword);
    EXPECT_THROW_AS(fixture.service->ensureSeeded("admin", kTestAdminPassword), errors::config::Invalid);
    EXPECT_EQ(std::string(*fixture.users->getUser("admin")->role), std::string("viewer"));
}

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

    const services::LoginResult r = svc->login("admin", kTestAdminPassword);
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

    EXPECT_THROW_AS(svc->login("ab", kTestAdminPassword), errors::validation::InvalidValue);
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
