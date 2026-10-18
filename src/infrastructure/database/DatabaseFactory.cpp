#include "app/infrastructure/database/DatabaseFactory.hpp"

#if defined(APP_DATABASE_SQLCIPHER) && defined(APP_DATABASE_POSTGRESQL)
#error "APP_DATABASE_* defines are mutually exclusive (configure with -DAPP_USERS_DATABASE_TYPE=SQLCIPHER|POSTGRESQL)"

#elif defined(APP_DATABASE_SQLCIPHER)
#include "app/infrastructure/database/SqlcipherDatabase.hpp"

#elif defined(APP_DATABASE_POSTGRESQL)
#include "app/infrastructure/database/PostgresqlDatabase.hpp"

#else
#error \
    "No database backend defined (require APP_DATABASE_SQLCIPHER or APP_DATABASE_POSTGRESQL via -DAPP_USERS_DATABASE_TYPE=SQLCIPHER|POSTGRESQL)"
#endif  // APP_DATABASE backend selection (SQLCIPHER / POSTGRESQL)

namespace app::infrastructure::database
{

#if defined(APP_DATABASE_SQLCIPHER)
#ifdef APP_USER_DATABASE_ENCRYPTED
std::shared_ptr<IDatabase> DatabaseFactory::create(const std::string& path, const std::string& key)
{
    return std::make_shared<SqlcipherDatabase>(path, key);
}
#endif  // APP_USER_DATABASE_ENCRYPTED

std::shared_ptr<IDatabase> DatabaseFactory::create(const std::string& path)
{
    return std::make_shared<SqlcipherDatabase>(path);
}

#elif defined(APP_DATABASE_POSTGRESQL)
std::shared_ptr<IDatabase> DatabaseFactory::create(const std::string& path, std::uint16_t port,
                                                   const std::string& dbname, const std::string& user,
                                                   const std::string& key)
{
    return std::make_shared<PostgresqlDatabase>(path, port, dbname, user, key);
}

#else
#error \
    "No database backend defined (require APP_DATABASE_SQLCIPHER or APP_DATABASE_POSTGRESQL via -DAPP_USERS_DATABASE_TYPE=SQLCIPHER|POSTGRESQL)"
#endif  // APP_DATABASE backend selection (SQLCIPHER / POSTGRESQL)

}  // namespace app::infrastructure::database
