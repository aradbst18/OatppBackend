#include "app/config/ConfigStore.hpp"

#ifndef APP_WITH_YAML_PARSER
#error "YamlConfigStore.cpp requires APP_WITH_YAML_PARSER (configure with -DAPP_ENABLE_YAML_PARSER=ON)"
#endif  // !APP_WITH_YAML_PARSER

#include <cstdint>
#include <string>

#include "app/errors/AppException.hpp"
#include "app/infrastructure/documents/YamlDocument.hpp"

namespace app::config
{

namespace
{

// Pre-grouping flat keys (see JsonConfigStore): rejected instead of silently
// loading defaults.
void rejectLegacyFlat(const infrastructure::documents::DocumentValue& root, const std::string& source)
{
    for (const char* key : {"host", "port", "databasePath", "databaseKey", "jwtSecret", "jwtIssuer", "jwtTtlSeconds", "logLevel", "logFilePath"})
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

}  // namespace

AppSettings YamlConfigStore::load(const std::string& path)
{
    const infrastructure::documents::DocumentValue root = infrastructure::documents::loadYamlFile(path);
    if (root.isNull())
    {
        // Empty file: keep defaults, still validated below.
        AppSettings s = AppSettings::defaults();
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

        if (const infrastructure::documents::DocumentValue* server = requireGroup(root, "server", path))
        {
            s.host = infrastructure::documents::stringOr(*server, "host", s.host);
            if (const infrastructure::documents::DocumentValue* port = server->find("port"))
            {
                if (!port->isNull())
                {
                    const int portValue = static_cast<int>(port->asInt());
                    if (portValue < 1 || portValue > 65535)
                    {
                        throw errors::config::Invalid("port out of range in " + path);
                    }

                    s.port = static_cast<std::uint16_t>(portValue);
                }
            }
        }

        if (const infrastructure::documents::DocumentValue* database = requireGroup(root, "database", path))
        {
            s.databasePath = infrastructure::documents::stringOr(*database, "path", s.databasePath);
#ifdef APP_DATABASE_SQLCIPHER
            // The key must be explicitly present: falling back to the built-in
            // default would hide a misconfigured (unencrypted-by-accident)
            // deployment.
            if (database->contains("key"))
            {
                s.databaseKey = infrastructure::documents::stringOr(*database, "key", "");
            }
            else
            {
                throw errors::config::Invalid("database.key is required for the SQLCIPHER backend in " + path);
            }
#endif  // APP_DATABASE_SQLCIPHER
        }
        else
        {
#ifdef APP_DATABASE_SQLCIPHER
            throw errors::config::Invalid("database.key is required for the SQLCIPHER backend in " + path);
#endif  // APP_DATABASE_SQLCIPHER
        }

        if (const infrastructure::documents::DocumentValue* jwt = requireGroup(root, "jwt", path))
        {
            s.jwtSecret = infrastructure::documents::stringOr(*jwt, "secret", s.jwtSecret);
            s.jwtIssuer = infrastructure::documents::stringOr(*jwt, "issuer", s.jwtIssuer);
            if (const infrastructure::documents::DocumentValue* ttl = jwt->find("ttlSeconds"))
            {
                if (!ttl->isNull())
                {
                    s.jwtTtlSeconds = ttl->asInt();
                }
            }
        }

        if (const infrastructure::documents::DocumentValue* logging = requireGroup(root, "logging", path))
        {
            s.logLevel = infrastructure::documents::stringOr(*logging, "level", s.logLevel);
            s.logFilePath = infrastructure::documents::stringOr(*logging, "filePath", s.logFilePath);
        }

        if (const infrastructure::documents::DocumentValue* swagger = requireGroup(root, "swagger", path))
        {
            s.swaggerResPath = infrastructure::documents::stringOr(*swagger, "resPath", s.swaggerResPath);
        }

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

void YamlConfigStore::save(const std::string& path, const AppSettings& settings)
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

    infrastructure::documents::saveYamlFile(path, root);
}

}  // namespace app::config
