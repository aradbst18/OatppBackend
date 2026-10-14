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
            throw errors::config::Invalid(std::string("Unsupported flat key '") + key + "' in " + source + ": expected grouped schema " +
                                          "(server/database/jwt/logging)");
        }
    }
}

const infrastructure::documents::DocumentValue* requireGroup(const infrastructure::documents::DocumentValue& root, const char* name,
                                                             const std::string& source)
{
    const infrastructure::documents::DocumentValue* group = root.find(name);
    if (group != nullptr && !group->isMap())
    {
        throw errors::config::Invalid(std::string("Invalid <") + name + "> group in " + source);
    }

    return group;
}

AppSettings fromValue(const infrastructure::documents::DocumentValue& root, const std::string& source)
{
    AppSettings s = AppSettings::defaults();
    rejectLegacyFlat(root, source);

    if (const infrastructure::documents::DocumentValue* server = requireGroup(root, "server", source))
    {
        if (const infrastructure::documents::DocumentValue* host = server->find("host"))
        {
            if (host->isString())
            {
                s.host = host->asString();
            }
        }

        if (const infrastructure::documents::DocumentValue* port = server->find("port"))
        {
            if (port->isNumber())
            {
                const int portValue = static_cast<int>(port->asInt());
                if (portValue < 1 || portValue > 65535)
                {
                    throw errors::config::Invalid("port out of range in " + source);
                }

                s.port = static_cast<std::uint16_t>(portValue);
            }
        }
    }

    if (const infrastructure::documents::DocumentValue* database = requireGroup(root, "database", source))
    {
        if (const infrastructure::documents::DocumentValue* path = database->find("path"))
        {
            if (path->isString())
            {
                s.databasePath = path->asString();
            }
        }

#ifdef APP_DATABASE_SQLCIPHER
        // The key must be explicitly present: falling back to the built-in default
        // would hide a misconfigured (unencrypted-by-accident) deployment.
        if (const infrastructure::documents::DocumentValue* key = database->find("key"))
        {
            if (key->isString())
            {
                s.databaseKey = key->asString();
            }
            else
            {
                throw errors::config::Invalid("database.key is required for the SQLCIPHER backend in " + source);
            }
        }
        else
        {
            throw errors::config::Invalid("database.key is required for the SQLCIPHER backend in " + source);
        }
#endif  // APP_DATABASE_SQLCIPHER
    }
    else
    {
#ifdef APP_DATABASE_SQLCIPHER
        throw errors::config::Invalid("database.key is required for the SQLCIPHER backend in " + source);
#endif  // APP_DATABASE_SQLCIPHER
    }

    if (const infrastructure::documents::DocumentValue* jwt = requireGroup(root, "jwt", source))
    {
        if (const infrastructure::documents::DocumentValue* secret = jwt->find("secret"))
        {
            if (secret->isString())
            {
                s.jwtSecret = secret->asString();
            }
        }

        if (const infrastructure::documents::DocumentValue* issuer = jwt->find("issuer"))
        {
            if (issuer->isString())
            {
                s.jwtIssuer = issuer->asString();
            }
        }

        if (const infrastructure::documents::DocumentValue* ttl = jwt->find("ttlSeconds"))
        {
            if (ttl->isNumber())
            {
                s.jwtTtlSeconds = ttl->asInt();
            }
        }
    }

    if (const infrastructure::documents::DocumentValue* logging = requireGroup(root, "logging", source))
    {
        if (const infrastructure::documents::DocumentValue* level = logging->find("level"))
        {
            if (level->isString())
            {
                s.logLevel = level->asString();
            }
        }

        if (const infrastructure::documents::DocumentValue* filePath = logging->find("filePath"))
        {
            if (filePath->isString())
            {
                s.logFilePath = filePath->asString();
            }
        }
    }

    if (const infrastructure::documents::DocumentValue* swagger = requireGroup(root, "swagger", source))
    {
        if (const infrastructure::documents::DocumentValue* resPath = swagger->find("resPath"))
        {
            if (resPath->isString())
            {
                s.swaggerResPath = resPath->asString();
            }
        }
    }

    s.configSource = source;
    const std::string err = s.validate();
    if (!err.empty())
    {
        throw errors::config::Invalid("Invalid configuration in " + source + ": " + err);
    }

    return s;
}

}  // namespace

AppSettings JsonConfigStore::load(const std::string& path)
{
    const infrastructure::documents::DocumentValue root = infrastructure::documents::loadJsonFile(path);
    if (!root.isMap())
    {
        throw errors::config::Invalid("JSON root must be an object in " + path);
    }

    try
    {
        return fromValue(root, path);
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

#ifdef APP_DATABASE_SQLCIPHER
    database["key"] = settings.databaseKey;
#endif  // APP_DATABASE_SQLCIPHER

    infrastructure::documents::DocumentValue jwt = infrastructure::documents::DocumentValue::makeMap();
    jwt["secret"] = settings.jwtSecret;
    jwt["issuer"] = settings.jwtIssuer;
    jwt["ttlSeconds"] = settings.jwtTtlSeconds;

    infrastructure::documents::DocumentValue logging = infrastructure::documents::DocumentValue::makeMap();
    logging["level"] = settings.logLevel;
    logging["filePath"] = settings.logFilePath;

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
