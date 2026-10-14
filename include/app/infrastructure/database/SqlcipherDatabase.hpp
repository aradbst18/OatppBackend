#pragma once

// This header only exists in SQLCipher builds. Including it in a SQLite-only
// build is a compile error by design, which proves SQLCipher APIs do not leak
// into SQLite-only code (see docs/cmake.md "build matrix" check).
#ifndef APP_DATABASE_SQLCIPHER
#error "SqlcipherDatabase.hpp requires APP_DATABASE_SQLCIPHER (configure with -DAPP_DATABASE_BACKEND=SQLCIPHER)"
#endif  // !APP_DATABASE_SQLCIPHER

#include <string>

#include "app/infrastructure/database/SqliteDatabase.hpp"

namespace app::infrastructure::database
{

/**
 * SQLCipher backend: SQLite with transparent encryption.
 *
 * The key is applied with `PRAGMA key = ...` immediately after opening, before
 * any other statement. Key material is held only in memory and never logged.
 * Use DatabaseFactory::create() instead of instantiating directly so the
 * backend selection stays in one place.
 */
class SqlcipherDatabase : public SqliteDatabase
{
public:
    SqlcipherDatabase(std::string path, std::string key);

    void open() override;
    std::string backendName() const noexcept override
    {
        return "SQLCIPHER";
    }

private:
    std::string key_;
};

}  // namespace app::infrastructure::database
