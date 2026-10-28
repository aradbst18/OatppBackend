#include "app/config/ConfigStore.hpp"

#ifndef APP_WITH_YAML_PARSER
#error "YamlConfigStore.cpp requires APP_WITH_YAML_PARSER (configure with -DAPP_ENABLE_YAML_PARSER=ON)"
#endif  // !APP_WITH_YAML_PARSER

#include <cstdint>
#include <string>

#include "app/errors/AppException.hpp"
#include "app/infrastructure/TextFile.hpp"
#include "app/infrastructure/documents/YamlDocument.hpp"

namespace app::config
{

namespace
{

// Pre-grouping flat keys (see JsonConfigStore): rejected instead of silently
// loading defaults.
void rejectLegacyFlat(const infrastructure::documents::DocumentValue& root, const std::string& source)
{
    for (const char* key : {"host", "port", "databasePath", "databaseKey", "jwtSecret", "jwtIssuer", "jwtTtlSeconds",
                            "logLevel", "logFilePath"})
    {
        if (root.contains(key))
        {
            throw errors::config::Invalid(std::string("Unsupported flat key '") + key + "' in " + source +
                                          ": expected grouped schema " + "(server/database/jwt/logging)");
        }
    }
}

const infrastructure::documents::DocumentValue* requireGroup(const infrastructure::documents::DocumentValue& root,
                                                             const char* name, const std::string& source)
{
    const infrastructure::documents::DocumentValue* group = root.find(name);
    if (group != nullptr && !group->isMap())
    {
        throw errors::config::Invalid(std::string("Invalid <") + name + "> group in " + source);
    }

    return group;
}

}  // namespace

AppSettings YamlConfigStore::load(const std::string& path, std::vector<std::string>* missing)
{
    const infrastructure::documents::DocumentValue root = infrastructure::documents::loadYamlFile(path);
    if (root.isNull())
    {
        // Empty files lack required secrets and fail validation below.
        AppSettings s = AppSettings::defaults();
        if (missing != nullptr)
        {
            *missing = {"server.host",
                        "server.port",
                        "database.path",
#ifdef APP_DATABASE_POSTGRESQL
                        "database.port",
                        "database.name",
                        "database.user",
#endif  // APP_DATABASE_POSTGRESQL
#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
                        "database.key",
#endif  // ENCRYPTED SQLCIPHER || POSTGRESQL
                        "jwt.secret",
                        "jwt.issuer",
                        "jwt.ttlSeconds",
                        "logging.level",
                        "logging.path"
#ifdef APP_WITH_SWAGGER
                        ,
                        "swagger.resPath"
#endif  // APP_WITH_SWAGGER
            };
        }

        s.configSource = path;
        const std::string err = s.validate();
        if (!err.empty())
        {
            throw errors::config::Invalid("Invalid configuration in " + path + ": " + err);
        }

        return s;
    }

    if (!root.isMap())
    {
        throw errors::config::Invalid("YAML root must be a map in " + path);
    }

    try
    {
        rejectLegacyFlat(root, path);
        AppSettings s = AppSettings::defaults();

        auto noteMissing = [missing](const char* group, const char* leaf)
        {
            if (missing != nullptr)
            {
                missing->push_back(std::string(group) + "." + leaf);
            }
        };

        if (const infrastructure::documents::DocumentValue* server = requireGroup(root, "server", path))
        {
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

            s.host = infrastructure::documents::stringOr(*server, "host", s.host);
            if (const infrastructure::documents::DocumentValue* port = server->find("port"))
            {
                if (!port->isNull())
                {
                    const auto portValue = port->asInt();
                    if (portValue < 1 || portValue > 65535)
                    {
                        throw errors::config::Invalid("port out of range in " + path);
                    }

                    s.port = static_cast<std::uint16_t>(portValue);
                }
            }
            else
            {
                noteMissing("server", "port");
            }
        }
        else
        {
            noteMissing("server", "host");
            noteMissing("server", "port");
        }

        if (const infrastructure::documents::DocumentValue* database = requireGroup(root, "database", path))
        {
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

            s.databasePath = infrastructure::documents::stringOr(*database, "path", s.databasePath);

#ifdef APP_DATABASE_POSTGRESQL
            if (const infrastructure::documents::DocumentValue* dbPort = database->find("port"))
            {
                if (!dbPort->isNull())
                {
                    const auto portValue = dbPort->asInt();
                    if (portValue < 1 || portValue > 65535)
                    {
                        throw errors::config::Invalid("database.port out of range in " + path);
                    }

                    s.databasePort = static_cast<std::uint16_t>(portValue);
                }
            }
            else
            {
                noteMissing("database", "port");
            }

            if (!database->contains("name"))
            {
                noteMissing("database", "name");
            }

            s.databaseName = infrastructure::documents::stringOr(*database, "name", s.databaseName);
            if (!database->contains("user"))
            {
                noteMissing("database", "user");
            }

            s.databaseUser = infrastructure::documents::stringOr(*database, "user", s.databaseUser);
#endif  // APP_DATABASE_POSTGRESQL

#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
            if (database->contains("key"))
            {
                const auto& key = database->at("key");
                if (!key.isString())
                {
                    throw errors::config::Invalid("database.key must be a string in " + path);
                }
                s.databaseKey = key.asString();
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

        if (const infrastructure::documents::DocumentValue* jwt = requireGroup(root, "jwt", path))
        {
            if (!jwt->contains("secret"))
            {
                noteMissing("jwt", "secret");
            }

            s.jwtSecret = infrastructure::documents::stringOr(*jwt, "secret", s.jwtSecret);
            if (!jwt->contains("issuer"))
            {
                noteMissing("jwt", "issuer");
            }

            s.jwtIssuer = infrastructure::documents::stringOr(*jwt, "issuer", s.jwtIssuer);
            if (const infrastructure::documents::DocumentValue* ttl = jwt->find("ttlSeconds"))
            {
                if (!ttl->isNull())
                {
                    s.jwtTtlSeconds = ttl->asInt();
                }
            }
            else
            {
                noteMissing("jwt", "ttlSeconds");
            }
        }
        else
        {
            noteMissing("jwt", "secret");
            noteMissing("jwt", "issuer");
            noteMissing("jwt", "ttlSeconds");
        }

        if (const infrastructure::documents::DocumentValue* logging = requireGroup(root, "logging", path))
        {
            if (logging->contains("filePath"))
            {
                throw errors::config::Invalid("logging.filePath was renamed to logging.path in " + path +
                                              ": rename the setting while preserving existing database credentials");
            }

            if (!logging->contains("level"))
            {
                noteMissing("logging", "level");
            }

            s.logLevel = infrastructure::documents::stringOr(*logging, "level", s.logLevel);
            if (!logging->contains("path"))
            {
                noteMissing("logging", "path");
            }

            s.logPath = infrastructure::documents::stringOr(*logging, "path", s.logPath);
        }
        else
        {
            noteMissing("logging", "level");
            noteMissing("logging", "path");
        }

        if (const infrastructure::documents::DocumentValue* swagger = requireGroup(root, "swagger", path))
        {
            s.swaggerResPath = infrastructure::documents::stringOr(*swagger, "resPath", s.swaggerResPath);

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
        throw errors::serialization::ParsingFailed(std::string("Invalid YAML types in ") + path + ": " + ex.what());
    }
}

void YamlConfigStore::save(const std::string& path, const AppSettings& settings, bool overwrite)
{
    const std::string err = settings.validate();
    if (!err.empty())
    {
        throw errors::config::Invalid("Refusing to save invalid settings: " + err);
    }

    infrastructure::documents::DocumentValue server = infrastructure::documents::DocumentValue::makeMap();
    server["host"] = settings.host;
    server["trustedProxies"] = settings.trustedProxies;
    server["port"] = static_cast<std::int64_t>(settings.port);

    infrastructure::documents::DocumentValue database = infrastructure::documents::DocumentValue::makeMap();
    database["path"] = settings.databasePath;

#ifdef APP_DATABASE_POSTGRESQL
    database["port"] = static_cast<std::int64_t>(settings.databasePort);
    database["name"] = settings.databaseName;
    database["user"] = settings.databaseUser;
#endif  // APP_DATABASE_POSTGRESQL

#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
    database["key"] = *settings.databaseKey;
#endif  // ENCRYPTED SQLCIPHER || POSTGRESQL

    infrastructure::documents::DocumentValue jwt = infrastructure::documents::DocumentValue::makeMap();
    jwt["secret"] = settings.jwtSecret;
    jwt["issuer"] = settings.jwtIssuer;
    jwt["ttlSeconds"] = settings.jwtTtlSeconds;

    infrastructure::documents::DocumentValue logging = infrastructure::documents::DocumentValue::makeMap();
    logging["level"] = settings.logLevel;
    logging["path"] = settings.logPath;

    infrastructure::documents::DocumentValue root = infrastructure::documents::DocumentValue::makeMap();
    root["server"] = std::move(server);
    root["database"] = std::move(database);
    root["jwt"] = std::move(jwt);
    root["logging"] = std::move(logging);

#ifdef APP_WITH_SWAGGER
    infrastructure::documents::DocumentValue swagger = infrastructure::documents::DocumentValue::makeMap();
    swagger["resPath"] = settings.swaggerResPath;
    root["swagger"] = std::move(swagger);
#endif  // APP_WITH_SWAGGER

    infrastructure::writePrivateTextFile(path, infrastructure::documents::serializeYaml(root), overwrite);
}

}  // namespace app::config
