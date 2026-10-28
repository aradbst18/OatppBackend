#include "app/config/AppSettings.hpp"

#include <arpa/inet.h>

#include <charconv>
#include <stdexcept>
#include <string_view>

#include "app/infrastructure/ClientIpResolver.hpp"
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

bool isHttpOrigin(std::string_view origin)
{
    if (origin.substr(0, 7) == "http://")
    {
        origin.remove_prefix(7);
    }
    else if (origin.substr(0, 8) == "https://")
    {
        origin.remove_prefix(8);
    }
    else
    {
        return false;
    }
    if (origin.empty() || origin.find('\0') != std::string_view::npos)
    {
        return false;
    }

    auto hostEnd = origin.find(':');
    if (origin.front() == '[')
    {
        hostEnd = origin.find(']');
        if (hostEnd == std::string_view::npos)
        {
            return false;
        }
        in6_addr address{};
        const std::string host(origin.substr(1, hostEnd - 1));
        if (::inet_pton(AF_INET6, host.c_str(), &address) != 1)
        {
            return false;
        }
        ++hostEnd;
    }
    else
    {
        auto host = origin.substr(0, hostEnd);
        if (host.empty() || host.size() > 253)
        {
            return false;
        }
        if (host.find_first_not_of("0123456789.") == std::string_view::npos)
        {
            in_addr address{};
            if (::inet_pton(AF_INET, std::string(host).c_str(), &address) != 1)
            {
                return false;
            }
        }
        while (!host.empty())
        {
            const auto dot = host.find('.');
            const auto label = host.substr(0, dot);
            if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-' ||
                label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
                    std::string_view::npos)
            {
                return false;
            }
            if (dot == std::string_view::npos)
            {
                break;
            }
            host.remove_prefix(dot + 1);
        }
    }

    if (hostEnd == std::string_view::npos || hostEnd == origin.size())
    {
        return true;
    }
    if (origin[hostEnd] != ':')
    {
        return false;
    }
    const auto port = origin.substr(hostEnd + 1);
    unsigned int number = 0;
    const auto parsed = std::from_chars(port.data(), port.data() + port.size(), number);
    return parsed.ec == std::errc{} && parsed.ptr == port.data() + port.size() && number <= 65535;
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
    try
    {
        infrastructure::ClientIpResolver resolver(trustedProxies);
    }
    catch (const std::invalid_argument& error)
    {
        return error.what();
    }

    if (corsAllowedOrigins != "*")
    {
        std::string_view remaining(corsAllowedOrigins);
        while (true)
        {
            const auto comma = remaining.find(',');
            auto origin = remaining.substr(0, comma);
            const auto first = origin.find_first_not_of(" \t");
            if (first == std::string_view::npos)
            {
                return "server.corsAllowedOrigins must be '*' or comma-separated HTTP(S) origins";
            }
            origin = origin.substr(first, origin.find_last_not_of(" \t") - first + 1);
            if (!isHttpOrigin(origin))
            {
                return "server.corsAllowedOrigins must be '*' or comma-separated HTTP(S) origins";
            }
            if (comma == std::string_view::npos)
            {
                break;
            }
            remaining.remove_prefix(comma + 1);
        }
    }

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
