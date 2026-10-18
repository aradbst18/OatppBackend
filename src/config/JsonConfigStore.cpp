#include "app/config/ConfigStore.hpp"

#ifndef APP_WITH_JSON_PARSER
#error "JsonConfigStore.cpp requires APP_WITH_JSON_PARSER (configure with -DAPP_ENABLE_JSON_PARSER=ON)"
#endif  // !APP_WITH_JSON_PARSER

#include <cstdint>

#include "app/errors/AppException.hpp"
#include "app/infrastructure/documents/JsonDocument.hpp"

namespace app::config
{

namespace
{

// Pre-grouping flat keys. The grouped schema (server/database/jwt/logging)
// replaced them; a file still using them is rejected instead of silently
// loading defaults.
constexpr const char* kLegacyFlatKeys[] = {
    "host", "port", "databasePath", "databaseKey", "jwtSecret", "jwtIssuer", "jwtTtlSeconds", "logLevel", "logFilePath",
};

void rejectLegacyFlat(const infrastructure::documents::DocumentValue& root, const std::string& source)
{
    for (const char* key : kLegacyFlatKeys)
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

AppSettings fromValue(const infrastructure::documents::DocumentValue& root, const std::string& source,
                      std::vector<std::string>* missing)
{
    AppSettings s = AppSettings::defaults();
    rejectLegacyFlat(root, source);

    auto noteMissing = [missing](const char* group, const char* leaf)
    {
        if (missing != nullptr)
        {
            missing->push_back(std::string(group) + "." + leaf);
        }
    };

    if (const infrastructure::documents::DocumentValue* server = requireGroup(root, "server", source))
    {
        if (const infrastructure::documents::DocumentValue* host = server->find("host"))
        {
            if (host->isString())
            {
                s.host = host->asString();
            }
            else
            {
                throw errors::config::Invalid("Invalid <host> type in " + source + ": expected a string");
            }
        }
        else
        {
            noteMissing("server", "host");
        }

        if (const infrastructure::documents::DocumentValue* port = server->find("port"))
        {
            if (!port->isNumber())
            {
                throw errors::config::Invalid("Invalid <port> type in " + source + ": expected a number");
            }

            const int portValue = static_cast<int>(port->asInt());
            if (portValue < 1 || portValue > 65535)
            {
                throw errors::config::Invalid("port out of range in " + source);
            }

            s.port = static_cast<std::uint16_t>(portValue);
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

    if (const infrastructure::documents::DocumentValue* database = requireGroup(root, "database", source))
    {
        if (database->find("host") != nullptr)
        {
            throw errors::config::Invalid("database.host was renamed to database.path in " + source +
                                          ": delete the file to regenerate or rename manually");
        }

        if (database->find("password") != nullptr)
        {
            throw errors::config::Invalid("database.password was renamed to database.key in " + source +
                                          ": delete the file to regenerate or rename manually");
        }

        if (const infrastructure::documents::DocumentValue* path = database->find("path"))
        {
            if (path->isString())
            {
                s.databasePath = path->asString();
            }
            else
            {
                throw errors::config::Invalid("Invalid <path> type in " + source + ": expected a string");
            }
        }
        else
        {
            noteMissing("database", "path");
        }

#ifdef APP_DATABASE_POSTGRESQL
        if (const infrastructure::documents::DocumentValue* port = database->find("port"))
        {
            if (!port->isNumber())
            {
                throw errors::config::Invalid("Invalid <port> type in " + source + ": expected a number");
            }

            const int portValue = static_cast<int>(port->asInt());
            if (portValue < 1 || portValue > 65535)
            {
                throw errors::config::Invalid("database.port out of range in " + source);
            }

            s.databasePort = static_cast<std::uint16_t>(portValue);
        }
        else
        {
            noteMissing("database", "port");
        }

        if (const infrastructure::documents::DocumentValue* name = database->find("name"))
        {
            if (name->isString())
            {
                s.databaseName = name->asString();
            }
            else
            {
                throw errors::config::Invalid("Invalid <name> type in " + source + ": expected a string");
            }
        }
        else
        {
            noteMissing("database", "name");
        }

        if (const infrastructure::documents::DocumentValue* user = database->find("user"))
        {
            if (user->isString())
            {
                s.databaseUser = user->asString();
            }
            else
            {
                throw errors::config::Invalid("Invalid <user> type in " + source + ": expected a string");
            }
        }
        else
        {
            noteMissing("database", "user");
        }
#endif  // APP_DATABASE_POSTGRESQL

#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
        // Absent key falls back to the built-in default like any other
        // missing parameter; a present-but-wrongly-typed key throws below.
        // If the default key cannot open the database, that connection
        // failure is the operator's to fix.
        if (const infrastructure::documents::DocumentValue* key = database->find("key"))
        {
            if (key->isString())
            {
                s.databaseKey = key->asString();
            }
            else
            {
                throw errors::config::Invalid("Invalid <key> type in " + source + ": expected a string");
            }
        }
        else
        {
            noteMissing("database", "key");
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
        noteMissing("database", "key");
#endif  // ENCRYPTED SQLCIPHER || POSTGRESQL
    }

    if (const infrastructure::documents::DocumentValue* jwt = requireGroup(root, "jwt", source))
    {
        if (const infrastructure::documents::DocumentValue* secret = jwt->find("secret"))
        {
            if (secret->isString())
            {
                s.jwtSecret = secret->asString();
            }
            else
            {
                throw errors::config::Invalid("Invalid <secret> type in " + source + ": expected a string");
            }
        }
        else
        {
            noteMissing("jwt", "secret");
        }

        if (const infrastructure::documents::DocumentValue* issuer = jwt->find("issuer"))
        {
            if (issuer->isString())
            {
                s.jwtIssuer = issuer->asString();
            }
            else
            {
                throw errors::config::Invalid("Invalid <issuer> type in " + source + ": expected a string");
            }
        }
        else
        {
            noteMissing("jwt", "issuer");
        }

        if (const infrastructure::documents::DocumentValue* ttl = jwt->find("ttlSeconds"))
        {
            if (!ttl->isNumber())
            {
                throw errors::config::Invalid("Invalid <ttlSeconds> type in " + source + ": expected a number");
            }

            s.jwtTtlSeconds = ttl->asInt();
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

    if (const infrastructure::documents::DocumentValue* logging = requireGroup(root, "logging", source))
    {
        if (logging->find("filePath") != nullptr)
        {
            throw errors::config::Invalid("logging.filePath was renamed to logging.path in " + source +
                                          ": delete the file to regenerate or rename manually");
        }

        if (const infrastructure::documents::DocumentValue* level = logging->find("level"))
        {
            if (level->isString())
            {
                s.logLevel = level->asString();
            }
            else
            {
                throw errors::config::Invalid("Invalid <level> type in " + source + ": expected a string");
            }
        }
        else
        {
            noteMissing("logging", "level");
        }

        if (const infrastructure::documents::DocumentValue* logPath = logging->find("path"))
        {
            if (logPath->isString())
            {
                s.logPath = logPath->asString();
            }
            else
            {
                throw errors::config::Invalid("Invalid <path> type in " + source + ": expected a string");
            }
        }
        else
        {
            noteMissing("logging", "path");
        }
    }
    else
    {
        noteMissing("logging", "level");
        noteMissing("logging", "path");
    }

    if (const infrastructure::documents::DocumentValue* swagger = requireGroup(root, "swagger", source))
    {
        if (const infrastructure::documents::DocumentValue* resPath = swagger->find("resPath"))
        {
            if (resPath->isString())
            {
                s.swaggerResPath = resPath->asString();
            }
            else
            {
                throw errors::config::Invalid("Invalid <resPath> type in " + source + ": expected a string");
            }
        }
#ifdef APP_WITH_SWAGGER
        else
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

    s.configSource = source;
    const std::string err = s.validate();
    if (!err.empty())
    {
        throw errors::config::Invalid("Invalid configuration in " + source + ": " + err);
    }

    return s;
}

}  // namespace

AppSettings JsonConfigStore::load(const std::string& path, std::vector<std::string>* missing)
{
    const infrastructure::documents::DocumentValue root = infrastructure::documents::loadJsonFile(path);
    if (!root.isMap())
    {
        throw errors::config::Invalid("JSON root must be an object in " + path);
    }

    try
    {
        return fromValue(root, path, missing);
    }
    catch (const errors::config::Invalid&)
    {
        throw;
    }
    catch (const errors::serialization::DeserializationFailed& ex)
    {
        throw errors::serialization::ParsingFailed("Invalid JSON types in " + path + ": " + ex.what());
    }
}

void JsonConfigStore::save(const std::string& path, const AppSettings& settings)
{
    const std::string err = settings.validate();
    if (!err.empty())
    {
        throw errors::config::Invalid("Refusing to save invalid settings: " + err);
    }

    infrastructure::documents::DocumentValue server = infrastructure::documents::DocumentValue::makeMap();
    server["host"] = settings.host;
    server["port"] = static_cast<std::int64_t>(settings.port);

    infrastructure::documents::DocumentValue database = infrastructure::documents::DocumentValue::makeMap();
    database["path"] = settings.databasePath;

#ifdef APP_DATABASE_POSTGRESQL
    database["port"] = static_cast<std::int64_t>(settings.databasePort);
    database["name"] = settings.databaseName;
    database["user"] = settings.databaseUser;
#endif  // APP_DATABASE_POSTGRESQL

#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
    database["key"] = settings.databaseKey;
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

    infrastructure::documents::saveJsonFile(path, root);
}

}  // namespace app::config
