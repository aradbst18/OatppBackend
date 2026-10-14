#pragma once

// Test-only helper: the database key to use for DatabaseFactory::create().
// - SQLITE builds: empty (no encryption).
// - SQLCIPHER builds: non-empty (SqlcipherDatabase refuses empty keys).

#include <memory>
#include <string>

#include "app/infrastructure/database/DatabaseFactory.hpp"
#include "app/infrastructure/database/IDatabase.hpp"

namespace app::tests
{

inline std::string testDatabaseKey()
{
#if defined(APP_DATABASE_SQLCIPHER)
    return "test-key-123";
#else
    return "";
#endif  // APP_DATABASE_SQLCIPHER
}

inline std::shared_ptr<infrastructure::database::IDatabase> createTestDatabase(const std::string& path = ":memory:")
{
#if defined(APP_DATABASE_SQLCIPHER)
    return infrastructure::database::DatabaseFactory::create(path, testDatabaseKey());
#else
    return infrastructure::database::DatabaseFactory::create(path);
#endif  // APP_DATABASE_SQLCIPHER
}

}  // namespace app::tests
