#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "app/infrastructure/database/IDatabase.hpp"

namespace app::infrastructure::database
{

/**
 * Single place that decides which user-database implementation is built
 * (see APP_USERS_DATABASE_TYPE).
 * - SQLCIPHER builds return SqlcipherDatabase, always backed by the SQLCipher
 *   library (the standard SQLite library is never linked). The key-taking
 *   overload exists only when user-database encryption is enabled
 *   (APP_USER_DATABASE_ENCRYPTED); the key itself is optional (an empty key
 *   opens the file plaintext). Without encryption support there is no key
 *   concept, so the overload does not exist in those builds by design.
 * - POSTGRESQL builds return PostgresqlDatabase (path = Unix socket dir,
 *   port/dbname/user/key required; key is the scram-sha-256 password,
 *   requires APP_WITH_POSTGRESQL, enforced at configure
 *   time and re-asserted by PostgresqlDatabase.hpp).
 * Callers never #ifdef on the backend themselves.
 *
 * Naming is unified: database.path is the file path (SQLCIPHER) or the socket
 * directory (POSTGRESQL); database.key is the optional file encryption key
 * (SQLCIPHER; empty = plaintext) or the PostgreSQL password. The old
 * database.host/password names were removed (loading them fails with a
 * migration error).
 */
class DatabaseFactory
{
public:
#if defined(APP_DATABASE_SQLCIPHER) && defined(APP_DATABASE_POSTGRESQL)
#error "APP_DATABASE_* defines are mutually exclusive (configure with -DAPP_USERS_DATABASE_TYPE=SQLCIPHER|POSTGRESQL)"

#elif defined(APP_DATABASE_SQLCIPHER)
#ifdef APP_USER_DATABASE_ENCRYPTED
    static std::shared_ptr<IDatabase> create(const std::string& path, const std::string& key);
#endif  // APP_USER_DATABASE_ENCRYPTED
    static std::shared_ptr<IDatabase> create(const std::string& path);

#elif defined(APP_DATABASE_POSTGRESQL)
    static std::shared_ptr<IDatabase> create(const std::string& path, std::uint16_t port, const std::string& dbname,
                                             const std::string& user, const std::string& key);

#else
#error \
    "No database backend defined (require APP_DATABASE_SQLCIPHER or APP_DATABASE_POSTGRESQL via -DAPP_USERS_DATABASE_TYPE=SQLCIPHER|POSTGRESQL)"
#endif  // APP_DATABASE backend selection (SQLCIPHER / POSTGRESQL)
};

}  // namespace app::infrastructure::database
