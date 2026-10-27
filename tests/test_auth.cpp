#include "support/TestRunner.hpp"

#include <chrono>
#include <filesystem>
#include <future>
#include <vector>

#include <jwt-cpp/jwt.h>

#include "support/ScopedEnvironment.hpp"
#include "support/TestDb.hpp"

#include "app/application/ManagedThread.hpp"
#include "app/auth/JwtService.hpp"
#include "app/auth/PasswordHash.hpp"
#include "app/errors/AppException.hpp"
#include "app/infrastructure/database/DatabaseFactory.hpp"
#include "app/repositories/RefreshTokenRepository.hpp"
#include "app/repositories/UserRepository.hpp"
#include "app/services/AuthService.hpp"
#include "app/services/UserService.hpp"

using namespace app;

namespace
{

constexpr const char* kTestSecret = "test-secret-123456789012345678901234";
constexpr const char* kOtherSecret = "other-secret-456789012345678901234";
constexpr const char* kTestAdminPassword = "TestAdmin8391";

struct AuthFixture
{
    std::shared_ptr<infrastructure::database::IDatabase> db;
    std::shared_ptr<repositories::UserRepository> users;
    std::shared_ptr<repositories::RefreshTokenRepository> tokens;
    std::shared_ptr<auth::JwtService> jwt;
    std::shared_ptr<services::AuthService> service;

    explicit AuthFixture(const std::string& secret = kTestSecret, const std::string& path = ":memory:")
    {
        db = tests::createTestDatabase(path);
        db->open();
        users = std::make_shared<repositories::UserRepository>(db);
        users->ensureSchema();
        tokens = std::make_shared<repositories::RefreshTokenRepository>(db);
        tokens->ensureSchema();
        jwt = std::make_shared<auth::JwtService>(secret, "test-issuer", std::chrono::seconds(3600));
        service = std::make_shared<services::AuthService>(users, jwt, tokens);
    }
};

struct TemporaryDatabaseFile
{
    std::string path = "/tmp/oatpp-cpp004-" + auth::sha256Hex(auth::generateRandomSecret(16)) + ".db";
    ~TemporaryDatabaseFile()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
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

TEST_CASE("auth: whoami resolves the current account by immutable identity")
{
    auto svc = makeService();

    const auto login = svc->login("admin", kTestAdminPassword);
    const auto claims = svc->authenticate(login.accessToken);

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

TEST_CASE("auth: password changes invalidate existing refresh tokens")
{
    auto db = tests::createTestDatabase();
    db->open();
    auto users = std::make_shared<repositories::UserRepository>(db);
    users->ensureSchema();
    auto tokens = std::make_shared<repositories::RefreshTokenRepository>(db);
    tokens->ensureSchema();
    auto jwt = std::make_shared<auth::JwtService>(kTestSecret, "test-issuer", std::chrono::seconds(3600));
    services::AuthService authService(users, jwt, tokens);
    services::UserService userService(users);
    userService.createUser({"alice", "AlicePass123", "viewer"});
    const auto login = authService.login("alice", "AlicePass123");

    services::PatchUserInput patch;
    patch.password = "ChangedPass456";
    userService.patchUser("alice", patch);

    EXPECT_THROW_AS(authService.refresh(login.refreshToken), errors::authentication::InvalidToken);
    EXPECT_THROW_AS(authService.authenticate(login.accessToken), errors::authentication::InvalidToken);
    EXPECT_FALSE(authService.login("alice", "ChangedPass456").refreshToken.empty());
}

TEST_CASE("auth: full password updates invalidate all sessions and preserve account identity")
{
    AuthFixture fixture;
    services::UserService users(fixture.users);
    users.createUser({"alice", "AlicePass123", "viewer"});
    const auto first = fixture.service->login("alice", "AlicePass123");
    const auto second = fixture.service->login("alice", "AlicePass123");
    const auto before = fixture.users->getUser("alice");
    users.updateUser("alice", {"alice", "ChangedPass456", "viewer"});

    EXPECT_EQ(std::string(*fixture.users->getUser("alice")->id), std::string(*before->id));
    for (const auto& login : {first, second})
    {
        EXPECT_THROW_AS(fixture.service->refresh(login.refreshToken), errors::authentication::InvalidToken);
        EXPECT_THROW_AS(fixture.service->authenticate(login.accessToken), errors::authentication::InvalidToken);
    }
    EXPECT_THROW_AS(fixture.service->login("alice", "AlicePass123"), errors::authentication::InvalidCredentials);
    const auto fresh = fixture.service->login("alice", "ChangedPass456");
    EXPECT_EQ(fixture.service->whoAmI(fixture.service->authenticate(fresh.accessToken)).username, std::string("alice"));
}

TEST_CASE("auth: deleting and recreating a username cannot transfer its previous sessions")
{
    AuthFixture fixture;
    services::UserService users(fixture.users);
    users.createUser({"alice", "AlicePass123", "viewer"});
    const auto old = fixture.service->login("alice", "AlicePass123");
    const std::string oldId = *fixture.users->getUser("alice")->id;
    users.deleteUser("alice");
    EXPECT_THROW_AS(fixture.tokens->getToken(old.refreshToken), errors::database::RecordNotFound);
    EXPECT_THROW_AS(fixture.service->authenticate(old.accessToken), errors::authentication::InvalidToken);
    users.createUser({"alice", "Replacement456", "admin"});

    EXPECT_TRUE(oldId != std::string(*fixture.users->getUser("alice")->id));
    EXPECT_THROW_AS(fixture.service->refresh(old.refreshToken), errors::authentication::InvalidToken);
    EXPECT_THROW_AS(fixture.service->authenticate(old.accessToken), errors::authentication::InvalidToken);
    EXPECT_EQ(fixture.service->login("alice", "Replacement456").role, std::string("admin"));
}

TEST_CASE("auth: renaming and reusing a username cannot transfer its sessions")
{
    AuthFixture fixture;
    services::UserService users(fixture.users);
    users.createUser({"alice", "AlicePass123", "viewer"});
    const auto old = fixture.service->login("alice", "AlicePass123");
    const std::string id = *fixture.users->getUser("alice")->id;
    services::PatchUserInput patch;
    patch.username = "alicia";
    users.patchUser("alice", patch);
    users.createUser({"alice", "Replacement456", "admin"});

    EXPECT_EQ(std::string(*fixture.users->getUser("alicia")->id), id);
    EXPECT_THROW_AS(fixture.service->refresh(old.refreshToken), errors::authentication::InvalidToken);
    EXPECT_THROW_AS(fixture.service->authenticate(old.accessToken), errors::authentication::InvalidToken);
    const auto fresh = fixture.service->login("alicia", "AlicePass123");
    EXPECT_EQ(fixture.service->whoAmI(fixture.service->authenticate(fresh.accessToken)).username,
              std::string("alicia"));
}

TEST_CASE("auth: role changes invalidate old access and refresh tokens without affecting other users")
{
    AuthFixture fixture;
    services::UserService users(fixture.users);
    users.createUser({"alice", "AlicePass123", "admin"});
    users.createUser({"bobby", "BobPass123", "viewer"});
    const auto old = fixture.service->login("alice", "AlicePass123");
    const auto other = fixture.service->login("bobby", "BobPass123");
    services::PatchUserInput patch;
    patch.role = "viewer";
    users.patchUser("alice", patch);

    EXPECT_THROW_AS(fixture.service->authenticate(old.accessToken), errors::authentication::InvalidToken);
    EXPECT_THROW_AS(fixture.service->refresh(old.refreshToken), errors::authentication::InvalidToken);
    EXPECT_EQ(fixture.service->authenticate(other.accessToken).role, std::string("viewer"));
    EXPECT_FALSE(fixture.service->refresh(other.refreshToken).accessToken.empty());
    EXPECT_EQ(fixture.service->login("alice", "AlicePass123").role, std::string("viewer"));
}

TEST_CASE("auth: bulk deletion removes sessions while preserving the administrator session")
{
    AuthFixture fixture;
    fixture.service->ensureSeeded("admin", kTestAdminPassword);
    services::UserService users(fixture.users);
    users.createUser({"alice", "AlicePass123", "viewer"});
    const auto admin = fixture.service->login("admin", kTestAdminPassword);
    const auto alice = fixture.service->login("alice", "AlicePass123");
    EXPECT_EQ(users.deleteAllUsers().deletedCount, 1);

    EXPECT_THROW_AS(fixture.tokens->getToken(alice.refreshToken), errors::database::RecordNotFound);
    EXPECT_THROW_AS(fixture.service->refresh(alice.refreshToken), errors::authentication::InvalidToken);
    EXPECT_THROW_AS(fixture.service->authenticate(alice.accessToken), errors::authentication::InvalidToken);
    EXPECT_EQ(fixture.service->authenticate(admin.accessToken).role, std::string("admin"));
    EXPECT_FALSE(fixture.service->refresh(admin.refreshToken).accessToken.empty());
}

TEST_CASE("auth: invalid and rolled back account changes preserve existing sessions")
{
    AuthFixture fixture;
    services::UserService users(fixture.users);
    users.createUser({"alice", "AlicePass123", "viewer"});
    const auto login = fixture.service->login("alice", "AlicePass123");
    services::PatchUserInput patch;
    patch.username = "alicia";
    patch.role = "invalid";
    EXPECT_THROW_AS(users.patchUser("alice", patch), errors::validation::InvalidValue);
    EXPECT_TRUE(fixture.users->isUserExists("alice"));
    EXPECT_FALSE(fixture.users->isUserExists("alicia"));

    {
        auto transaction = fixture.users->beginTransaction();
        fixture.users->updatePassword("alice", auth::passwordHash("ChangedPass456"));
        transaction.rollback();
    }
    EXPECT_EQ(fixture.service->authenticate(login.accessToken).role, std::string("viewer"));
    EXPECT_FALSE(fixture.service->refresh(login.refreshToken).accessToken.empty());
}

TEST_CASE("auth: legacy access tokens without a session version are rejected")
{
    AuthFixture fixture;
    fixture.service->ensureSeeded("admin", kTestAdminPassword);
    const auto now = std::chrono::system_clock::now();
    const auto token = jwt::create()
                           .set_issuer("test-issuer")
                           .set_subject("admin")
                           .set_issued_at(now)
                           .set_expires_at(now + std::chrono::hours(1))
                           .set_payload_claim("role", jwt::claim(std::string("admin")))
                           .sign(jwt::algorithm::hs256{kTestSecret});
    EXPECT_THROW_AS(fixture.service->authenticate(token), errors::authentication::InvalidToken);
}

TEST_CASE("auth: concurrent refresh requests consume the token exactly once")
{
    TemporaryDatabaseFile file;
    AuthFixture fixture(kTestSecret, file.path);
    tests::seedUser(*fixture.users, "alice", "viewer", "AlicePass123");
    const auto login = fixture.service->login("alice", "AlicePass123");
    std::promise<void> start;
    const auto ready = start.get_future().share();
    std::atomic<int> succeeded{0};
    std::atomic<int> rejected{0};
    std::exception_ptr firstError;
    std::exception_ptr secondError;
    auto work = [&](std::exception_ptr& error)
    {
        ready.wait();
        try
        {
            fixture.service->refresh(login.refreshToken);
            ++succeeded;
        }
        catch (const errors::authentication::InvalidToken&)
        {
            ++rejected;
        }
        catch (...)
        {
            error = std::current_exception();
        }
    };
    application::ManagedThread first("refresh-first",
                                     [&](const std::atomic<bool>&)
                                     {
                                         work(firstError);
                                     });
    application::ManagedThread second("refresh-second",
                                      [&](const std::atomic<bool>&)
                                      {
                                          work(secondError);
                                      });
    first.start();
    second.start();
    start.set_value();
    first.join();
    second.join();
    if (firstError)
        std::rethrow_exception(firstError);
    if (secondError)
        std::rethrow_exception(secondError);
    EXPECT_EQ(succeeded.load(), 1);
    EXPECT_EQ(rejected.load(), 1);
}

TEST_CASE("auth: failed refresh replacement rolls back consumption of the original token")
{
    AuthFixture fixture;
    tests::seedUser(*fixture.users, "alice", "viewer", "AlicePass123");
    const auto login = fixture.service->login("alice", "AlicePass123");
    if (fixture.db->backendName() == "POSTGRESQL")
    {
        fixture.db->executeVoid(
            "CREATE FUNCTION reject_refresh_insert() RETURNS trigger LANGUAGE plpgsql "
            "AS 'BEGIN RAISE EXCEPTION ''forced insert failure''; END';");
        fixture.db->executeVoid(
            "CREATE TRIGGER reject_refresh_insert BEFORE INSERT ON refresh_tokens "
            "FOR EACH ROW EXECUTE FUNCTION reject_refresh_insert();");
    }
    else
    {
        fixture.db->executeVoid(
            "CREATE TRIGGER reject_refresh_insert BEFORE INSERT ON refresh_tokens "
            "BEGIN SELECT RAISE(ABORT, 'forced insert failure'); END;");
    }
    EXPECT_THROW_AS(fixture.service->refresh(login.refreshToken), errors::database::Conflict);
    EXPECT_EQ(fixture.tokens->getToken(login.refreshToken)->revoked.getValue(-1), 0);
    fixture.db->executeVoid(fixture.db->backendName() == "POSTGRESQL"
                                ? "DROP TRIGGER reject_refresh_insert ON refresh_tokens;"
                                : "DROP TRIGGER reject_refresh_insert;");
    EXPECT_FALSE(fixture.service->refresh(login.refreshToken).accessToken.empty());
}

TEST_CASE("auth: sessions issued concurrently with a password change cannot survive it")
{
    TemporaryDatabaseFile file;
    AuthFixture fixture(kTestSecret, file.path);
    tests::seedUser(*fixture.users, "alice", "viewer", "AlicePass123");
    services::UserService users(fixture.users);
    const auto original = fixture.service->login("alice", "AlicePass123");
    std::promise<void> start;
    const auto ready = start.get_future().share();
    services::LoginResult rotated;
    services::LoginResult loggedIn;
    std::exception_ptr refreshError;
    std::exception_ptr loginError;
    application::ManagedThread refresh("refresh-race",
                                       [&](const std::atomic<bool>&)
                                       {
                                           ready.wait();
                                           try
                                           {
                                               rotated = fixture.service->refresh(original.refreshToken);
                                           }
                                           catch (const errors::authentication::InvalidToken&)
                                           {
                                           }
                                           catch (...)
                                           {
                                               refreshError = std::current_exception();
                                           }
                                       });
    application::ManagedThread login("login-race",
                                     [&](const std::atomic<bool>&)
                                     {
                                         ready.wait();
                                         try
                                         {
                                             loggedIn = fixture.service->login("alice", "AlicePass123");
                                         }
                                         catch (const errors::authentication::InvalidCredentials&)
                                         {
                                         }
                                         catch (...)
                                         {
                                             loginError = std::current_exception();
                                         }
                                     });
    refresh.start();
    login.start();
    start.set_value();
    services::PatchUserInput patch;
    patch.password = "ChangedPass456";
    users.patchUser("alice", patch);
    refresh.join();
    login.join();
    if (refreshError)
        std::rethrow_exception(refreshError);
    if (loginError)
        std::rethrow_exception(loginError);
    for (const auto& issued : {original, rotated, loggedIn})
    {
        if (!issued.accessToken.empty())
        {
            EXPECT_THROW_AS(fixture.service->authenticate(issued.accessToken), errors::authentication::InvalidToken);
            EXPECT_THROW_AS(fixture.service->refresh(issued.refreshToken), errors::authentication::InvalidToken);
        }
    }
    EXPECT_FALSE(fixture.service->login("alice", "ChangedPass456").accessToken.empty());
}
