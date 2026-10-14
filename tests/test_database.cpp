#include "support/TestRunner.hpp"

#include "support/TestDb.hpp"

#include "app/auth/PasswordHash.hpp"
#include "app/errors/AppException.hpp"
#include "app/infrastructure/database/DatabaseFactory.hpp"
#include "app/repositories/UserRepository.hpp"

using namespace app;

TEST_CASE("database: sqlite backend name")
{
    auto db = tests::createTestDatabase();
#if defined(APP_DATABASE_SQLCIPHER)
    EXPECT_EQ(db->backendName(), std::string("SQLCIPHER"));
#else
    EXPECT_EQ(db->backendName(), std::string("SQLITE"));
#endif  // APP_DATABASE_SQLCIPHER
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
}

TEST_CASE("database: repository crud")
{
    auto db = tests::createTestDatabase();
    db->open();
    repositories::UserRepository repo(db);
    repo.ensureSchema();
    repo.ensureSchema();  // idempotent
    repo.upsert({"alice", auth::passwordHashHex("Alice1234"), "operator"});
    const auto found = repo.findByUsername("alice");
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->role, std::string("operator"));
    EXPECT_TRUE(!repo.findByUsername("ghost").has_value());
    repo.upsert({"alice", auth::passwordHashHex("Alice1234"), "admin"});
    EXPECT_EQ(repo.findByUsername("alice")->role, std::string("admin"));
    EXPECT_EQ(repo.listAll().size(), 1);
    repo.remove("alice");
    EXPECT_TRUE(!repo.findByUsername("alice").has_value());
}

#if defined(APP_DATABASE_SQLCIPHER)
TEST_CASE("database: sqlcipher requires non-empty key")
{
    auto db = infrastructure::database::DatabaseFactory::create(":memory:", "");
    EXPECT_THROW_AS(db->open(), errors::database::ConnectionFailed);
}
#endif  // APP_DATABASE_SQLCIPHER

TEST_CASE("database: password hash protocol matches the active algorithm")
{
    // Exactly one algorithm is active by construction (single-valued cache var).
    int active = 0;
#if defined(APP_PASSWORD_HASH_SHA256)
    ++active;
    EXPECT_EQ(std::string(auth::passwordHashAlgoName()), std::string("SHA256"));
    EXPECT_EQ(auth::passwordHashHex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    EXPECT_EQ(auth::passwordHashHex("abc").size(), 64);
#endif  // APP_PASSWORD_HASH_SHA256
#if defined(APP_PASSWORD_HASH_SHA384)
    ++active;
    EXPECT_EQ(std::string(auth::passwordHashAlgoName()), std::string("SHA384"));
    EXPECT_EQ(auth::passwordHashHex("abc"), std::string("cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed"
                                                        "8086072ba1e7cc2358baeca134c825a7"));
    EXPECT_EQ(auth::passwordHashHex("abc").size(), 96);
#endif  // APP_PASSWORD_HASH_SHA384
#if defined(APP_PASSWORD_HASH_SHA512)
    ++active;
    EXPECT_EQ(std::string(auth::passwordHashAlgoName()), std::string("SHA512"));
    EXPECT_EQ(auth::passwordHashHex("abc"), std::string("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                                                        "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"));
    EXPECT_EQ(auth::passwordHashHex("abc").size(), 128);
#endif  // APP_PASSWORD_HASH_SHA512
    EXPECT_EQ(active, 1);
    // Deterministic and input-sensitive regardless of algorithm.
    EXPECT_TRUE(auth::passwordHashHex("Alice1234") == auth::passwordHashHex("Alice1234"));
    EXPECT_FALSE(auth::passwordHashHex("Alice1234") == auth::passwordHashHex("alice1234"));
    // Legacy helper keeps its SHA-256 vector.
    EXPECT_EQ(auth::sha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}
