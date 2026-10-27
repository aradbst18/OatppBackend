#include "app/config/AppSettings.hpp"

#include "app/logging/Logger.hpp"

namespace app::config
{

namespace
{

bool isSampleSecret(const std::string& value)
{
    return value == "change-me-in-production-min-32-characters-required" || value == "change-me-in-production" ||
           value == "dev-sqlcipher-key-change-me" || value == "oatpp_test_pw" || value == "REPLACE_ME";
}

}  // namespace

AppSettings AppSettings::defaults()
{
    AppSettings s;
    s.host = "127.0.0.1";
    s.port = 8080;

#ifdef APP_DATABASE_POSTGRESQL
    // Local socket server defaults (see docs/postgresql.md):
    // socket dir data/postgresql, port 5433, db oatpp_backend, role oatpp_tester.
    s.databasePath = "data/postgresql";
    s.databasePort = 5433;
    s.databaseName = "oatpp_backend";
    s.databaseUser = "oatpp_tester";

#else
    s.databasePath = "data/app.db";

#endif  // APP_DATABASE_POSTGRESQL

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

#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
    if (!databaseKey.has_value())
    {
        return "database.key must be explicitly configured";
    }
    if (isSampleSecret(*databaseKey))
    {
        return "database.key must not use a published sample value; migrate the existing database credentials "
               "before updating this setting";
    }
#endif  // ENCRYPTED SQLCIPHER || POSTGRESQL

#ifdef APP_DATABASE_POSTGRESQL
    // SQLCipher permits explicitly selected plaintext; PostgreSQL requires a password.
    if (databaseKey->empty())
    {
        return "database.key must not be empty for the POSTGRESQL backend (scram-sha-256 password "
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

    if (isSampleSecret(jwtSecret))
    {
        return "jwt.secret must not use a published sample value; configure a unique random secret";
    }

    if (jwtSecret.size() < 32)
    {
        return "jwt.secret must be explicitly configured with at least 32 characters";
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
