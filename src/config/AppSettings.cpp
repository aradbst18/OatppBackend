#include "app/config/AppSettings.hpp"

#include "app/logging/Logger.hpp"

namespace app::config
{

AppSettings AppSettings::defaults()
{
    AppSettings s;
    s.host = "127.0.0.1";
    s.port = 8080;

#ifdef APP_DATABASE_POSTGRESQL
    // Local socket server defaults (see docs/postgresql.md):
    // socket dir data/postgresql, port 5433, db oatpp_backend, role oatpp_tester.
    s.databasePath = "data/postgresql";
    s.databaseKey = "oatpp_test_pw";
    s.databasePort = 5433;
    s.databaseName = "oatpp_backend";
    s.databaseUser = "oatpp_tester";

#else
    s.databasePath = "data/app.db";

#ifdef APP_USER_DATABASE_ENCRYPTED
    s.databaseKey = "dev-sqlcipher-key-change-me";
#endif  // APP_USER_DATABASE_ENCRYPTED
#endif  // APP_DATABASE_POSTGRESQL

    s.jwtSecret = "change-me-in-production-min-32-characters-required";
    s.jwtIssuer = "oatpp-backend";
    s.jwtTtlSeconds = 3600;
    s.logLevel = "INFO";
    s.logPath = "logs";

#ifdef OATPP_SWAGGER_RES_PATH
    s.swaggerResPath = OATPP_SWAGGER_RES_PATH;
#else
    s.swaggerResPath = "/usr/local/include/oatpp-1.3.0/bin/oatpp-swagger/res";
#endif  // OATPP_SWAGGER_RES_PATH

    s.corsAllowedOrigins = "*";
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

#ifdef APP_DATABASE_POSTGRESQL
    // The SQLCipher key is optional (an empty key opens the file plaintext),
    // so only PostgreSQL's scram password is required here.
    if (databaseKey.empty())
    {
        return "databaseKey must not be empty for the POSTGRESQL backend (scram-sha-256 password "
               "required)";
    }
#endif  // APP_DATABASE_POSTGRESQL

#ifdef APP_DATABASE_POSTGRESQL
    if (databasePort == 0)
    {
        return "databasePort must be non-zero for the POSTGRESQL backend";
    }

    if (databaseName.empty())
    {
        return "databaseName must not be empty for the POSTGRESQL backend";
    }

    if (databaseUser.empty())
    {
        return "databaseUser must not be empty for the POSTGRESQL backend";
    }
#endif  // APP_DATABASE_POSTGRESQL

    if (jwtSecret.size() < 32)
    {
        return "jwtSecret must be at least 32 characters";
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
