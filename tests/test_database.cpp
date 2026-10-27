#include "support/TestRunner.hpp"

#include <cstdio>

#include "support/TestDb.hpp"

#include "app/auth/PasswordHash.hpp"
#include "app/domain/UserRole.hpp"
#include "app/errors/AppException.hpp"
#include "app/infrastructure/database/DatabaseFactory.hpp"
#include "app/infrastructure/database/OrmSupport.hpp"
#include "app/repositories/RefreshTokenRepository.hpp"
#include "app/repositories/UserRepository.hpp"

#ifdef APP_DATABASE_SQLCIPHER
#include "app/infrastructure/database/SqlcipherDatabase.hpp"
#endif  // APP_DATABASE_SQLCIPHER

using namespace app;

TEST_CASE("database: backend name")
{
    auto db = tests::createTestDatabase();

#ifdef APP_DATABASE_POSTGRESQL
    EXPECT_EQ(db->backendName(), std::string("POSTGRESQL"));
#elif defined(APP_USER_DATABASE_ENCRYPTED)
    EXPECT_EQ(db->backendName(), std::string("SQLCIPHER"));
#else
    EXPECT_EQ(db->backendName(), std::string("SQLCIPHER"));
#endif  // APP_DATABASE_POSTGRESQL
}

TEST_CASE("database: open, create, insert, select")
{
    auto db = tests::createTestDatabase();
    db->open();
    EXPECT_TRUE(db->isOpen());

    db->executeVoid("CREATE TABLE t (id TEXT PRIMARY KEY, v TEXT)");
    db->executeVoid("INSERT INTO t (id, v) VALUES (?, ?)", {"a", "1"});

    const auto rows = db->execute("SELECT id, v FROM t WHERE id = ?", {"a"});
    EXPECT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0][0], std::string("a"));
    EXPECT_EQ(rows[0][1], std::string("1"));

    db->close();
    EXPECT_FALSE(db->isOpen());
}

TEST_CASE("database: bound parameters prevent injection")
{
    auto db = tests::createTestDatabase();
    db->open();
    db->executeVoid("CREATE TABLE t (id TEXT PRIMARY KEY, v TEXT)");

    const std::string evil = "x' OR '1'='1";
    db->executeVoid("INSERT INTO t (id, v) VALUES (?, ?)", {evil, "safe"});

    // The injected string must be stored literally, not executed.
    const auto rows = db->execute("SELECT v FROM t WHERE id = ?", {evil});
    EXPECT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0][0], std::string("safe"));

    // A raw-interpolated variant would return rows; the bound variant returns none.
    const auto all = db->execute("SELECT v FROM t");
    EXPECT_EQ(all.size(), 1);
}

TEST_CASE("database: invalid sql throws DatabaseError")
{
    auto db = tests::createTestDatabase();
    db->open();
    EXPECT_THROW_AS(db->execute("SELECT * FROM no_such_table_xyz"), errors::database::QueryFailed);

    // The failure message must name the missing table: the adapter's raw
    // engine error text has to survive the wrapping.
    std::string message;
    try
    {
        db->execute("SELECT * FROM no_such_table_xyz");
    }
    catch (const errors::database::QueryFailed& ex)
    {
        message = ex.what();
    }

    if (message.find("no_such_table_xyz") == std::string::npos)
    {
        ::app::tests::fail("QueryFailed message names the failing table", __FILE__, __LINE__, message);
    }
}

TEST_CASE("database: placeholder translation")
{
    auto db = tests::createTestDatabase();
    db->open();

    // A `?` inside quoted text is data, not a placeholder (mirrors the
    // adapters, which skip quoted strings when scanning query templates).
    const auto literal = db->execute("SELECT 'a?b' AS v");
    EXPECT_EQ(literal.size(), 1);
    EXPECT_EQ(literal[0][0], std::string("a?b"));

    // A `:` inside single-quoted text is data too.
    const auto colon = db->execute("SELECT 'a:b' AS v");
    EXPECT_EQ(colon.size(), 1);
    EXPECT_EQ(colon[0][0], std::string("a:b"));

    // Doubled quotes inside a literal keep it open (SQL escaping).
    const auto escaped = db->execute("SELECT 'it''s ?' AS v");
    EXPECT_EQ(escaped.size(), 1);
    EXPECT_EQ(escaped[0][0], std::string("it's ?"));

    // Placeholder/parameter count mismatches are rejected before execution.
    EXPECT_THROW_AS(db->execute("SELECT ?"), errors::database::QueryFailed);
    EXPECT_THROW_AS(db->execute("SELECT 1", {"extra"}), errors::database::QueryFailed);
}

TEST_CASE("database: orm query translation")
{
    const auto translated = infrastructure::database::toOrmQuery("UPDATE t SET a = ?, b = ? WHERE id = ?");
    EXPECT_EQ(translated.text, std::string("UPDATE t SET a = :p0, b = :p1 WHERE id = :p2"));
    EXPECT_EQ(translated.paramNames.size(), 3);
    EXPECT_EQ(translated.paramNames[0], std::string("p0"));
    EXPECT_EQ(translated.paramNames[2], std::string("p2"));

    // Quoted text, quoted identifiers and comments are never rewritten.
    const auto untouched = infrastructure::database::toOrmQuery("SELECT 'a?b', \"c?d\" /* e?f */ -- g?h");
    EXPECT_EQ(untouched.text, std::string("SELECT 'a?b', \"c?d\" /* e?f */ -- g?h"));
    EXPECT_EQ(untouched.paramNames.size(), 0);

    // A bare ':' (a PostgreSQL '::' cast) is rejected with a clear error.
    EXPECT_THROW_AS(infrastructure::database::toOrmQuery("SELECT 1::text"), errors::database::QueryFailed);
}

TEST_CASE("database: colon outside single quotes is rejected")
{
    auto db = tests::createTestDatabase();
    db->open();

    // Both adapters reserve ':' for their named template parameters, so a
    // bare colon (a PostgreSQL '::' cast, or a colon in a comment) must fail
    // with a clear error instead of silently corrupting the statement.
    EXPECT_THROW_AS(db->execute("SELECT 1::text"), errors::database::QueryFailed);
    EXPECT_THROW_AS(db->execute("SELECT 1 -- note: comment"), errors::database::QueryFailed);
}

TEST_CASE("database: repository crud")
{
    auto db = tests::createTestDatabase();
    db->open();

    repositories::UserRepository repo(db);
    repo.ensureSchema();
    repo.ensureSchema();  // idempotent

    tests::seedUser(repo, "alice", domain::toString(domain::UserRole::Operator));

    const auto found = repo.getUser("alice");
    EXPECT_EQ(*found->username, std::string("alice"));
    EXPECT_TRUE(auth::passwordVerify("Alice1234", found->passwordHash->c_str()));
    EXPECT_EQ(*found->role, std::string("operator"));
    EXPECT_TRUE(repo.isUserExists("alice"));
    EXPECT_TRUE(!repo.isUserExists("ghost"));
    EXPECT_THROW_AS(repo.getUser("ghost"), errors::database::RecordNotFound);
    EXPECT_THROW_AS(tests::seedUser(repo, "alice", "viewer", "Other1234"), errors::database::Conflict);

    found->role = domain::toString(domain::UserRole::Admin);
    repo.updateUser(found);
    EXPECT_EQ(*repo.getUser("alice")->role, std::string("admin"));

    auto missing = oatpp::Object<repositories::UserDto>::createShared();
    missing->username = "ghost";
    missing->passwordHash = auth::passwordHash("Ghost1234");
    missing->role = domain::toString(domain::UserRole::Viewer);
    EXPECT_THROW_AS(repo.updateUser(missing), errors::database::RecordNotFound);

    auto invalid = oatpp::Object<repositories::UserDto>::createShared();
    invalid->username = "bob";
    invalid->passwordHash = auth::passwordHash("Bob12345");
    invalid->role = "superuser";
    EXPECT_THROW_AS(repo.createUser(invalid), errors::validation::InvalidValue);

    EXPECT_EQ(repo.listUsers()->size(), 1);

    repo.deleteUser("alice");
    repo.deleteUser("alice");  // a missing username is a no-op
    EXPECT_TRUE(!repo.isUserExists("alice"));
    EXPECT_TRUE(repo.listUsers()->empty());
}

TEST_CASE("database: repository transactions commit and roll back")
{
    // DbClient transactions run through the same seam: BEGIN/COMMIT/ROLLBACK
    // execute on the database's open handle (see DatabaseExecutor), so
    // repository statements between them are part of the transaction.
    auto db = tests::createTestDatabase();
    db->open();

    repositories::UserRepository repo(db);
    repo.ensureSchema();

    {
        auto transaction = repo.beginTransaction();
        tests::seedUser(repo, "alice", "viewer");
        tests::seedUser(repo, "bob", "viewer");
        transaction.commit();
    }
    EXPECT_EQ(repo.listUsers()->size(), 2);

    {
        auto transaction = repo.beginTransaction();
        tests::seedUser(repo, "carol", "viewer");
        transaction.rollback();
    }
    EXPECT_EQ(repo.listUsers()->size(), 2);
    EXPECT_TRUE(!repo.isUserExists("carol"));
}

TEST_CASE("database: legacy account migration preserves credentials and discards unsafe sessions")
{
    auto db = tests::createTestDatabase();
    db->open();
    db->executeVoid("CREATE TABLE users (username TEXT PRIMARY KEY, password_hash TEXT NOT NULL, role TEXT NOT NULL);");
    const std::string hash = auth::passwordHash("AlicePass123");
    db->executeVoid("INSERT INTO users (username, password_hash, role) VALUES (?, ?, ?);", {"alice", hash, "admin"});
    db->executeVoid(
        "CREATE TABLE refresh_tokens (token TEXT PRIMARY KEY, username TEXT NOT NULL, "
        "expires_at BIGINT NOT NULL, revoked INTEGER NOT NULL);");
    db->executeVoid("INSERT INTO refresh_tokens VALUES ('legacy-token', 'alice', 4102444800, 0);");
    repositories::UserRepository users(db);
    repositories::RefreshTokenRepository tokens(db);
    users.ensureSchema();
    tokens.ensureSchema();
    const auto migrated = users.getUser("alice");
    EXPECT_FALSE(migrated->id->empty());
    EXPECT_EQ(std::string(*migrated->passwordHash), hash);
    EXPECT_EQ(std::string(*migrated->role), std::string("admin"));
    EXPECT_EQ(migrated->sessionVersion.getValue(-1), 0);
    EXPECT_THROW_AS(tokens.getToken("legacy-token"), errors::database::RecordNotFound);

    auto token = repositories::RefreshTokenDto::createShared();
    token->token = "new-token";
    token->userId = migrated->id;
    token->sessionVersion = migrated->sessionVersion;
    token->expiresAt = v_int64{4102444800};
    token->revoked = 0;
    tokens.createToken(token);
    users.ensureSchema();
    tokens.ensureSchema();
    EXPECT_EQ(std::string(*users.getUser("alice")->id), std::string(*migrated->id));
    EXPECT_EQ(std::string(*tokens.getToken("new-token")->userId), std::string(*migrated->id));
    users.deleteUser("alice");
    EXPECT_THROW_AS(tokens.getToken("new-token"), errors::database::RecordNotFound);
}

TEST_CASE("database: transactions are isolated by owning database and reject nesting")
{
    auto firstDb = tests::createTestDatabase();
    firstDb->open();
    repositories::UserRepository first(firstDb);
    first.ensureSchema();
    auto secondDb = tests::createTestDatabase();
    secondDb->open();
    repositories::UserRepository second(secondDb);
    second.ensureSchema();
    {
        auto firstTx = first.beginTransaction();
        tests::seedUser(first, "alice", "viewer");
        tests::seedUser(second, "bobby", "viewer");
        EXPECT_THROW_AS(first.beginTransaction(), errors::database::TransactionFailed);
        auto secondTx = second.beginTransaction();
        tests::seedUser(second, "carol", "viewer");
        firstTx.rollback();
        secondTx.commit();
    }
    EXPECT_TRUE(first.listUsers()->empty());
    EXPECT_EQ(second.listUsers()->size(), 2);
    EXPECT_FALSE(first.isUserExists("bobby"));
    EXPECT_TRUE(second.isUserExists("carol"));
}

TEST_CASE("database: failed legacy migration rolls back users and refresh tokens together")
{
    auto db = tests::createTestDatabase();
    db->open();
    db->executeVoid("CREATE TABLE users (username TEXT PRIMARY KEY, password_hash TEXT NOT NULL, role TEXT NOT NULL);");
    const auto hash = auth::passwordHash("AlicePass123");
    db->executeVoid("INSERT INTO users VALUES (?, ?, ?);", {"alice", hash, "viewer"});
    db->executeVoid(
        "CREATE TABLE refresh_tokens (token TEXT PRIMARY KEY, username TEXT NOT NULL, "
        "expires_at BIGINT NOT NULL, revoked INTEGER NOT NULL);");
    db->executeVoid("INSERT INTO refresh_tokens VALUES ('legacy-token', 'alice', 4102444800, 0);");
    // An unknown external dependency prevents replacing the legacy table. The
    // already executed refresh-table drop must be rolled back as well.
    db->executeVoid("CREATE TABLE legacy_dependency (username TEXT REFERENCES users(username));");
    db->executeVoid("INSERT INTO legacy_dependency VALUES ('alice');");
    repositories::UserRepository users(db);
    EXPECT_THROW_AS(users.ensureSchema(), errors::database::QueryFailed);
    EXPECT_EQ(db->execute("SELECT password_hash FROM users WHERE username = 'alice';")[0][0], hash);
    EXPECT_EQ(db->execute("SELECT token FROM refresh_tokens;")[0][0], std::string("legacy-token"));

    db->executeVoid("DROP TABLE legacy_dependency;");
    users.ensureSchema();
    repositories::RefreshTokenRepository tokens(db);
    tokens.ensureSchema();
    EXPECT_FALSE(users.getUser("alice")->id->empty());
    EXPECT_THROW_AS(tokens.getToken("legacy-token"), errors::database::RecordNotFound);
}

TEST_CASE("database: repository requires a database")
{
    EXPECT_THROW_AS(repositories::UserRepository(nullptr), errors::database::ConnectionFailed);
}

TEST_CASE("database: repository on a closed database fails fast")
{
    // Statements already fail at the database boundary, before any SQL runs.
    auto db = tests::createTestDatabase();
    repositories::UserRepository repo(db);

    EXPECT_THROW_AS(repo.ensureSchema(), errors::database::ConnectionFailed);
}

#ifdef APP_DATABASE_SQLCIPHER
TEST_CASE("database: empty key opens plaintext")
{
    // The file backend is a single SQLCipher-backed class: an empty key
    // means plaintext (standard-SQLite behavior), never an error.
    auto db = infrastructure::database::DatabaseFactory::create(":memory:");
    db->open();
    EXPECT_TRUE(db->isOpen());
    EXPECT_EQ(db->backendName(), std::string("SQLCIPHER"));

    db->close();
}

TEST_CASE("database: wrong SQLCipher key fails at open")
{
    // Key validation runs inside the connection provider (a real
    // sqlite_master read), so a wrong key fails open() instead of the first
    // query, and a correctly keyed file reopens cleanly afterwards.
    const std::string path = "/tmp/opencode/oatpp-backend-wrong-key.db";
    std::remove(path.c_str());

    {
        infrastructure::database::SqlcipherDatabase db(path, "key-a");
        db.open();
        db.executeVoid("CREATE TABLE t (id TEXT PRIMARY KEY, v TEXT)");
        db.executeVoid("INSERT INTO t (id, v) VALUES (?, ?)", {"a", "1"});
    }

    {
        infrastructure::database::SqlcipherDatabase db(path, "key-b");
        EXPECT_THROW_AS(db.open(), errors::database::ConnectionFailed);
        EXPECT_FALSE(db.isOpen());
    }

    {
        infrastructure::database::SqlcipherDatabase db(path, "key-a");
        db.open();
        const auto rows = db.execute("SELECT v FROM t WHERE id = ?", {"a"});
        EXPECT_EQ(rows.size(), 1);
        EXPECT_EQ(rows[0][0], std::string("1"));
    }

    std::remove(path.c_str());
}
#endif  // APP_DATABASE_SQLCIPHER

TEST_CASE("database: password hash protocol matches the active algorithm")
{
    // Exactly one algorithm is active by construction (single-valued cache var).
    int active = 0;

#ifdef APP_PASSWORD_HASH_SHA256
    ++active;
    EXPECT_EQ(std::string(auth::passwordHashAlgoName()), std::string("SHA256"));
    // Legacy unsalted digest stays deterministic.
    EXPECT_EQ(auth::passwordHashHex("abc"),
              std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    EXPECT_EQ(auth::passwordHashHex("abc").size(), 64);
#endif  // APP_PASSWORD_HASH_SHA256

#ifdef APP_PASSWORD_HASH_SHA384
    ++active;
    EXPECT_EQ(std::string(auth::passwordHashAlgoName()), std::string("SHA384"));
    EXPECT_EQ(auth::passwordHashHex("abc"),
              std::string("cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed"
                          "8086072ba1e7cc2358baeca134c825a7"));
    EXPECT_EQ(auth::passwordHashHex("abc").size(), 96);
#endif  // APP_PASSWORD_HASH_SHA384

#ifdef APP_PASSWORD_HASH_SHA512
    ++active;
    EXPECT_EQ(std::string(auth::passwordHashAlgoName()), std::string("SHA512"));
    EXPECT_EQ(auth::passwordHashHex("abc"),
              std::string("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                          "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"));
    EXPECT_EQ(auth::passwordHashHex("abc").size(), 128);
#endif  // APP_PASSWORD_HASH_SHA512

    EXPECT_EQ(active, 1);

    // Legacy unsalted helper is still deterministic and input-sensitive.
    EXPECT_TRUE(auth::passwordHashHex("Alice1234") == auth::passwordHashHex("Alice1234"));
    EXPECT_FALSE(auth::passwordHashHex("Alice1234") == auth::passwordHashHex("alice1234"));

    // Legacy helper keeps its SHA-256 vector.
    EXPECT_EQ(auth::sha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

TEST_CASE("database: salted password hash produces unique outputs and verifies")
{
    // Each call to passwordHash produces a different output (different salt).
    const std::string h1 = auth::passwordHash("Alice1234");
    const std::string h2 = auth::passwordHash("Alice1234");
    EXPECT_TRUE(h1 != h2);  // different salts

    // The format is $<algorithm>$<iterations>$<base64_salt>$<base64_hash>
    EXPECT_TRUE(h1[0] == '$');
    EXPECT_TRUE(h1.find("pbkdf2-sha") != std::string::npos);

    // Verification succeeds for the correct password.
    EXPECT_TRUE(auth::passwordVerify("Alice1234", h1));
    EXPECT_TRUE(auth::passwordVerify("Alice1234", h2));

    // Verification fails for the wrong password.
    EXPECT_FALSE(auth::passwordVerify("alice1234", h1));
    EXPECT_FALSE(auth::passwordVerify("Wrong1234", h1));

    // Case sensitive.
    EXPECT_FALSE(auth::passwordVerify("ALICE1234", h1));
}
