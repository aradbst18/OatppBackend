#include "app/infrastructure/database/DatabaseFactory.hpp"

#if defined(APP_DATABASE_SQLCIPHER) && defined(APP_DATABASE_SQLITE)
#error "APP_DATABASE_SQLCIPHER and APP_DATABASE_SQLITE are mutually exclusive (configure with -DAPP_DATABASE_BACKEND=SQLITE|SQLCIPHER)"
#elif defined(APP_DATABASE_SQLCIPHER)
#include "app/infrastructure/database/SqlcipherDatabase.hpp"
#elif defined(APP_DATABASE_SQLITE)
#include "app/infrastructure/database/SqliteDatabase.hpp"
#else
#error "No database backend defined (require APP_DATABASE_SQLITE or APP_DATABASE_SQLCIPHER via -DAPP_DATABASE_BACKEND=SQLITE|SQLCIPHER)"
#endif  // APP_DATABASE backend selection (SQLCIPHER / SQLITE)

namespace app::infrastructure::database
{

#if defined(APP_DATABASE_SQLCIPHER)
std::shared_ptr<IDatabase> DatabaseFactory::create(const std::string& path, const std::string& key)
{
    return std::make_shared<SqlcipherDatabase>(path, key);
}
#elif defined(APP_DATABASE_SQLITE)
std::shared_ptr<IDatabase> DatabaseFactory::create(const std::string& path)
{
    return std::make_shared<SqliteDatabase>(path);
}
#else
#error "No database backend defined (require APP_DATABASE_SQLITE or APP_DATABASE_SQLCIPHER via -DAPP_DATABASE_BACKEND=SQLITE|SQLCIPHER)"
#endif  // APP_DATABASE backend selection (SQLCIPHER / SQLITE)

}  // namespace app::infrastructure::database
