#pragma once

#include <memory>
#include <string>

#include "app/infrastructure/database/IDatabase.hpp"

namespace app::infrastructure::database
{

/**
 * Single place that decides which database implementation is built.
 * - SQLITE builds return SqliteDatabase; there is no key concept, so the
 *   key-taking overload does not exist in these builds by design.
 * - SQLCIPHER builds return SqlcipherDatabase (key required).
 * Callers never #ifdef on the backend themselves.
 */
class DatabaseFactory
{
public:
#if defined(APP_DATABASE_SQLCIPHER) && defined(APP_DATABASE_SQLITE)
#error "APP_DATABASE_SQLCIPHER and APP_DATABASE_SQLITE are mutually exclusive (configure with -DAPP_DATABASE_BACKEND=SQLITE|SQLCIPHER)"
#elif defined(APP_DATABASE_SQLCIPHER)
    static std::shared_ptr<IDatabase> create(const std::string& path, const std::string& key);
#elif defined(APP_DATABASE_SQLITE)
    static std::shared_ptr<IDatabase> create(const std::string& path);
#else
#error "No database backend defined (require APP_DATABASE_SQLITE or APP_DATABASE_SQLCIPHER via -DAPP_DATABASE_BACKEND=SQLITE|SQLCIPHER)"
#endif  // APP_DATABASE backend selection (SQLCIPHER / SQLITE)
};

}  // namespace app::infrastructure::database
