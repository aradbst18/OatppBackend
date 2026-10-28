#include "app/config/ConfigStore.hpp"

#ifndef APP_WITH_XML_PARSER
#error "XmlConfigStore.cpp requires APP_WITH_XML_PARSER (configure with -DAPP_ENABLE_XML_PARSER=ON)"
#endif  // !APP_WITH_XML_PARSER

#include <cstdint>

#include "app/errors/AppException.hpp"
#include "app/infrastructure/TextFile.hpp"
#include "app/infrastructure/documents/XmlDocument.hpp"

namespace app::config
{

namespace
{

std::string childText(const infrastructure::documents::DocumentValue& parent, const char* name,
                      const std::string& fallback)
{
    const infrastructure::documents::DocumentValue* node = parent.find(name);
    if (node == nullptr || node->isNull())
    {
        return fallback;
    }

    // A present-but-empty element parses as "" and round-trips values like
    // path=""; only a missing element falls back.
    return node->asString();
}

// Pre-grouping flat keys (see JsonConfigStore): rejected instead of silently
// loading defaults.
void rejectLegacyFlat(const infrastructure::documents::DocumentValue& root, const std::string& source)
{
    for (const char* key : {"host", "port", "databasePath", "databaseKey", "jwtSecret", "jwtIssuer", "jwtTtlSeconds",
                            "logLevel", "logFilePath"})
    {
        if (root.contains(key))
        {
            throw errors::config::Invalid(std::string("Unsupported flat element <") + key + "> in " + source +
                                          ": expected grouped schema " + "(server/database/jwt/logging)");
        }
    }
}

const infrastructure::documents::DocumentValue* findGroup(const infrastructure::documents::DocumentValue& root,
                                                          const char* name)
{
    return root.find(name);
}

}  // namespace

AppSettings XmlConfigStore::load(const std::string& path, std::vector<std::string>* missing)
{
    const infrastructure::documents::DocumentValue document = infrastructure::documents::loadXmlFile(path);
    const infrastructure::documents::DocumentValue* root = document.find("app");
    if (root == nullptr || !root->isMap())
    {
        throw errors::config::Invalid("XML root must be <app> in " + path);
    }

    try
    {
        rejectLegacyFlat(*root, path);
        AppSettings s = AppSettings::defaults();

        auto noteMissing = [missing](const char* group, const char* leaf)
        {
            if (missing != nullptr)
            {
                missing->push_back(std::string(group) + "." + leaf);
            }
        };

        if (const infrastructure::documents::DocumentValue* server = findGroup(*root, "server"))
        {
            if (!server->isMap())
            {
                throw errors::config::Invalid("Invalid <server> group in " + path);
            }

            if (const auto* origins = server->find("corsAllowedOrigins"))
            {
                if (!origins->isString())
                {
                    throw errors::config::Invalid("server.corsAllowedOrigins must be a string in " + path);
                }
                s.corsAllowedOrigins = origins->asString();
            }
            else
            {
                noteMissing("server", "corsAllowedOrigins");
            }

            if (const auto* proxies = server->find("trustedProxies"))
            {
                if (!proxies->isString())
                {
                    throw errors::config::Invalid("server.trustedProxies must be a string in " + path);
                }
                s.trustedProxies = proxies->asString();
            }

            if (!server->contains("host"))
            {
                noteMissing("server", "host");
            }

            s.host = childText(*server, "host", s.host);
            if (!server->contains("port"))
            {
                noteMissing("server", "port");
            }

            const std::string portText = childText(*server, "port", "");

            if (!portText.empty())
            {
                try
                {
                    std::size_t consumed = 0;
                    const int port = std::stoi(portText, &consumed);
                    if (portText.find_first_not_of(" \t\r\n", consumed) != std::string::npos)
                    {
                        throw errors::config::Invalid("Invalid <port> in " + path);
                    }

                    if (port < 1 || port > 65535)
                    {
                        throw errors::config::Invalid("port out of range in " + path);
                    }

                    s.port = static_cast<std::uint16_t>(port);
                }
                catch (const errors::config::Invalid&)
                {
                    throw;
                }
                catch (...)
                {
                    throw errors::config::Invalid("Invalid <port> in " + path);
                }
            }
        }
        else
        {
            noteMissing("server", "corsAllowedOrigins");
        }

        if (const infrastructure::documents::DocumentValue* database = findGroup(*root, "database"))
        {
            if (!database->isMap())
            {
                throw errors::config::Invalid("Invalid <database> group in " + path);
            }

            if (database->contains("host"))
            {
                throw errors::config::Invalid("database.host was renamed to database.path in " + path +
                                              ": rename the setting while preserving existing database credentials");
            }

            if (database->contains("password"))
            {
                throw errors::config::Invalid("database.password was renamed to database.key in " + path +
                                              ": rename the setting while preserving existing database credentials");
            }

            if (!database->contains("path"))
            {
                noteMissing("database", "path");
            }

            s.databasePath = childText(*database, "path", s.databasePath);
#ifdef APP_DATABASE_POSTGRESQL
            const std::string dbPortText = childText(*database, "port", "");

            if (!dbPortText.empty())
            {
                try
                {
                    std::size_t consumed = 0;
                    const int portValue = std::stoi(dbPortText, &consumed);
                    if (dbPortText.find_first_not_of(" \t\r\n", consumed) != std::string::npos)
                    {
                        throw errors::config::Invalid("Invalid <port> in <database> in " + path);
                    }

                    if (portValue < 1 || portValue > 65535)
                    {
                        throw errors::config::Invalid("database.port out of range in " + path);
                    }

                    s.databasePort = static_cast<std::uint16_t>(portValue);
                }
                catch (const errors::config::Invalid&)
                {
                    throw;
                }
                catch (...)
                {
                    throw errors::config::Invalid("Invalid <port> in <database> in " + path);
                }
            }

            if (!database->contains("port"))
            {
                noteMissing("database", "port");
            }

            if (!database->contains("name"))
            {
                noteMissing("database", "name");
            }

            s.databaseName = childText(*database, "name", s.databaseName);
            if (!database->contains("user"))
            {
                noteMissing("database", "user");
            }

            s.databaseUser = childText(*database, "user", s.databaseUser);
#endif  // APP_DATABASE_POSTGRESQL

#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
            if (database->contains("key"))
            {
                s.databaseKey = childText(*database, "key", "");
            }
            else
            {
                throw errors::config::Invalid("database.key must be explicitly configured in " + path);
            }
#endif  // ENCRYPTED SQLCIPHER || POSTGRESQL
        }
        else
        {
            noteMissing("database", "path");

#ifdef APP_DATABASE_POSTGRESQL
            noteMissing("database", "port");
            noteMissing("database", "name");
            noteMissing("database", "user");
#endif  // APP_DATABASE_POSTGRESQL

#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
            throw errors::config::Invalid("database.key must be explicitly configured in " + path);
#endif  // ENCRYPTED SQLCIPHER || POSTGRESQL
        }

        if (const infrastructure::documents::DocumentValue* jwt = findGroup(*root, "jwt"))
        {
            if (!jwt->isMap())
            {
                throw errors::config::Invalid("Invalid <jwt> group in " + path);
            }

            if (!jwt->contains("secret"))
            {
                noteMissing("jwt", "secret");
            }

            s.jwtSecret = childText(*jwt, "secret", s.jwtSecret);
            if (!jwt->contains("issuer"))
            {
                noteMissing("jwt", "issuer");
            }

            s.jwtIssuer = childText(*jwt, "issuer", s.jwtIssuer);
            if (!jwt->contains("ttlSeconds"))
            {
                noteMissing("jwt", "ttlSeconds");
            }

            const std::string ttlText = childText(*jwt, "ttlSeconds", "");

            if (!ttlText.empty())
            {
                try
                {
                    std::size_t consumed = 0;
                    const auto ttl = std::stoll(ttlText, &consumed);
                    if (ttlText.find_first_not_of(" \t\r\n", consumed) != std::string::npos)
                    {
                        throw errors::config::Invalid("Invalid <ttlSeconds> in " + path);
                    }

                    s.jwtTtlSeconds = ttl;
                }
                catch (...)
                {
                    throw errors::config::Invalid("Invalid <ttlSeconds> in " + path);
                }
            }
        }
        else
        {
            noteMissing("jwt", "secret");
            noteMissing("jwt", "issuer");
            noteMissing("jwt", "ttlSeconds");
        }

        if (const infrastructure::documents::DocumentValue* logging = findGroup(*root, "logging"))
        {
            if (!logging->isMap())
            {
                throw errors::config::Invalid("Invalid <logging> group in " + path);
            }

            if (logging->contains("filePath"))
            {
                throw errors::config::Invalid("logging.filePath was renamed to logging.path in " + path +
                                              ": rename the setting while preserving existing database credentials");
            }

            if (!logging->contains("level"))
            {
                noteMissing("logging", "level");
            }

            s.logLevel = childText(*logging, "level", s.logLevel);
            if (!logging->contains("path"))
            {
                noteMissing("logging", "path");
            }

            s.logPath = childText(*logging, "path", s.logPath);
        }
        else
        {
            noteMissing("logging", "level");
            noteMissing("logging", "path");
        }

        if (const infrastructure::documents::DocumentValue* swagger = findGroup(*root, "swagger"))
        {
            if (!swagger->isMap())
            {
                throw errors::config::Invalid("Invalid <swagger> group in " + path);
            }

            s.swaggerResPath = childText(*swagger, "resPath", s.swaggerResPath);

#ifdef APP_WITH_SWAGGER
            if (!swagger->contains("resPath"))
            {
                noteMissing("swagger", "resPath");
            }
#endif  // APP_WITH_SWAGGER
        }
#ifdef APP_WITH_SWAGGER
        else
        {
            noteMissing("swagger", "resPath");
        }
#endif  // APP_WITH_SWAGGER

        s.configSource = path;

        const std::string err = s.validate();
        if (!err.empty())
        {
            throw errors::config::Invalid("Invalid configuration in " + path + ": " + err);
        }

        return s;
    }
    catch (const errors::config::Invalid&)
    {
        throw;
    }
    catch (const errors::serialization::DeserializationFailed& ex)
    {
        throw errors::serialization::ParsingFailed("Invalid XML types in " + path + ": " + ex.what());
    }
}

void XmlConfigStore::save(const std::string& path, const AppSettings& settings, bool overwrite)
{
    const std::string err = settings.validate();
    if (!err.empty())
    {
        throw errors::config::Invalid("Refusing to save invalid settings: " + err);
    }

    infrastructure::documents::DocumentValue server = infrastructure::documents::DocumentValue::makeMap();
    server["host"] = settings.host;
    server["trustedProxies"] = settings.trustedProxies;
    server["corsAllowedOrigins"] = settings.corsAllowedOrigins;
    server["port"] = std::to_string(settings.port);

    infrastructure::documents::DocumentValue database = infrastructure::documents::DocumentValue::makeMap();
    database["path"] = settings.databasePath;

#ifdef APP_DATABASE_POSTGRESQL
    database["port"] = std::to_string(settings.databasePort);
    database["name"] = settings.databaseName;
    database["user"] = settings.databaseUser;
#endif  // APP_DATABASE_POSTGRESQL

#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
    database["key"] = *settings.databaseKey;
#endif  // ENCRYPTED SQLCIPHER || POSTGRESQL

    infrastructure::documents::DocumentValue jwt = infrastructure::documents::DocumentValue::makeMap();
    jwt["secret"] = settings.jwtSecret;
    jwt["issuer"] = settings.jwtIssuer;
    jwt["ttlSeconds"] = std::to_string(settings.jwtTtlSeconds);

    infrastructure::documents::DocumentValue logging = infrastructure::documents::DocumentValue::makeMap();
    logging["level"] = settings.logLevel;
    logging["path"] = settings.logPath;

    infrastructure::documents::DocumentValue inner = infrastructure::documents::DocumentValue::makeMap();
    inner["server"] = std::move(server);
    inner["database"] = std::move(database);
    inner["jwt"] = std::move(jwt);
    inner["logging"] = std::move(logging);

#ifdef APP_WITH_SWAGGER
    infrastructure::documents::DocumentValue swagger = infrastructure::documents::DocumentValue::makeMap();
    swagger["resPath"] = settings.swaggerResPath;
    inner["swagger"] = std::move(swagger);
#endif  // APP_WITH_SWAGGER

    infrastructure::documents::DocumentValue document = infrastructure::documents::DocumentValue::makeMap();
    document["app"] = std::move(inner);
    infrastructure::writePrivateTextFile(path, infrastructure::documents::serializeXml(document), overwrite);
}

}  // namespace app::config
