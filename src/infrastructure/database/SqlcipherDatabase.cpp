#include "app/infrastructure/database/SqlcipherDatabase.hpp"

#ifndef APP_DATABASE_SQLCIPHER
#error "SqlcipherDatabase.cpp requires APP_DATABASE_SQLCIPHER (configure with -DAPP_DATABASE_BACKEND=SQLCIPHER)"
#endif  // !APP_DATABASE_SQLCIPHER

#include <sqlite3.h>

#include "app/errors/AppException.hpp"
#include "app/logging/Logger.hpp"

namespace app::infrastructure::database
{

namespace
{

std::string escapeLiteral(const std::string& key)
{
    std::string out;
    out.reserve(key.size());
    for (char c : key)
    {
        if (c == '\'')
        {
            out += "''";
        }
        else
        {
            out += c;
        }
    }

    return out;
}

}  // namespace

SqlcipherDatabase::SqlcipherDatabase(std::string path, std::string key) : SqliteDatabase(std::move(path)), key_(std::move(key))
{
}

void SqlcipherDatabase::open()
{
    SqliteDatabase::open();

    // Apply the key immediately after opening, before any other statement.
    // An empty key is a configuration error, not a fallback to plaintext.
    if (key_.empty())
    {
        close();
        throw errors::database::ConnectionFailed("SQLCipher key must not be empty");
    }

    // NOTE: PRAGMA does not accept bound parameters, so the key is embedded as
    // a quoted literal with ''-escaping. Key material is never logged.
    try
    {
        executeVoid("PRAGMA key = '" + escapeLiteral(key_) + "';");
        const auto rows = execute("PRAGMA cipher_version;");
        if (rows.empty() || rows[0].empty() || rows[0][0].empty())
        {
            throw errors::database::QueryFailed(
                "SQLCipher codec not available (PRAGMA cipher_version is empty); "
                "the linked SQLite has no codec support");
        }
    }
    catch (const errors::database::QueryFailed&)
    {
        close();
        throw;
    }

    logging::logInfo("database", "SQLCipher backend opened (key applied, not logged)");
}

}  // namespace app::infrastructure::database
