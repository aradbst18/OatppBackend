#include "app/config/AppSettings.hpp"

#include "app/logging/Logger.hpp"

namespace app::config
{

AppSettings AppSettings::defaults()
{
    AppSettings s;
    s.host = "127.0.0.1";
    s.port = 8080;
    s.databasePath = "data/app.db";

#ifdef APP_DATABASE_SQLCIPHER
    s.databaseKey = "dev-sqlcipher-key-change-me";
#endif  // APP_DATABASE_SQLCIPHER

    s.jwtSecret = "change-me-in-production";
    s.jwtIssuer = "oatpp-backend";
    s.jwtTtlSeconds = 3600;
    s.logLevel = "INFO";
    s.logFilePath = "logs";

#ifdef OATPP_SWAGGER_RES_PATH
    s.swaggerResPath = OATPP_SWAGGER_RES_PATH;
#else
    s.swaggerResPath = "/usr/local/include/oatpp-1.3.0/bin/oatpp-swagger/res";
#endif  // OATPP_SWAGGER_RES_PATH

    return s;
}

std::string AppSettings::validate() const
{
    if (host.empty())
    {
        return "host must not be empty";
    }

    if (port == 0)
    {
        return "port must be non-zero";
    }

    if (databasePath.empty())
    {
        return "databasePath must not be empty";
    }

#ifdef APP_DATABASE_SQLCIPHER
    if (databaseKey.empty())
    {
        return "databaseKey must not be empty for the SQLCIPHER backend";
    }
#endif  // APP_DATABASE_SQLCIPHER

    if (jwtSecret.size() < 8)
    {
        return "jwtSecret must be at least 8 characters";
    }

    if (jwtIssuer.empty())
    {
        return "jwtIssuer must not be empty";
    }

    if (jwtTtlSeconds <= 0)
    {
        return "jwtTtlSeconds must be positive";
    }

    logging::Level level{};
    if (!logging::tryParseLevel(logLevel, level))
    {
        return "logLevel must be one of TRACE/DEBUG/INFO/WARN/ERROR/CRITICAL";
    }

#ifdef APP_WITH_SWAGGER
    if (swaggerResPath.empty())
    {
        return "swaggerResPath must not be empty";
    }
#endif  // APP_WITH_SWAGGER

    return "";
}

}  // namespace app::config
