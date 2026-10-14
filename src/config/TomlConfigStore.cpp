#include "app/config/ConfigStore.hpp"

#ifndef APP_WITH_TOML_PARSER
#error "TomlConfigStore.cpp requires APP_WITH_TOML_PARSER (configure with -DAPP_ENABLE_TOML_PARSER=ON)"
#endif  // !APP_WITH_TOML_PARSER

#include <cstdint>

#include "app/errors/AppException.hpp"
#include "app/infrastructure/documents/TomlDocument.hpp"

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
                                          "([server]/[database]/[jwt]/[logging])");
        }
    }
}

const infrastructure::documents::DocumentValue* findGroup(const infrastructure::documents::DocumentValue& root, const std::string& name,
                                                          const std::string& source)
{
    const infrastructure::documents::DocumentValue* group = root.find(name);
    if (group == nullptr)
    {
        return nullptr;
    }

    if (!group->isMap())
    {
        throw errors::config::Invalid(std::string("Invalid [") + name + "] group in " + source);
    }

    return group;
}

AppSettings fromValue(const infrastructure::documents::DocumentValue& root, const std::string& path)
{
    AppSettings s = AppSettings::defaults();
    rejectLegacyFlat(root, path);

    if (const infrastructure::documents::DocumentValue* server = findGroup(root, "server", path))
    {
        if (const infrastructure::documents::DocumentValue* host = server->find("host"))
        {
            s.host = host->asString();
        }

        if (const infrastructure::documents::DocumentValue* port = server->find("port"))
        {
            const int portValue = static_cast<int>(port->asInt());
            if (portValue < 1 || portValue > 65535)
            {
                throw errors::config::Invalid("port out of range in " + path);
            }

            s.port = static_cast<std::uint16_t>(portValue);
        }
    }

    if (const infrastructure::documents::DocumentValue* database = findGroup(root, "database", path))
    {
        if (const infrastructure::documents::DocumentValue* dbPath = database->find("path"))
        {
            s.databasePath = dbPath->asString();
        }

#ifdef APP_DATABASE_SQLCIPHER
        // The key must be explicitly present: falling back to the built-in
        // default would hide a misconfigured (unencrypted-by-accident)
        // deployment.
        if (const infrastructure::documents::DocumentValue* key = database->find("key"))
        {
            s.databaseKey = key->asString();
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

    if (const infrastructure::documents::DocumentValue* jwt = findGroup(root, "jwt", path))
    {
        if (const infrastructure::documents::DocumentValue* secret = jwt->find("secret"))
        {
            s.jwtSecret = secret->asString();
        }

        if (const infrastructure::documents::DocumentValue* issuer = jwt->find("issuer"))
        {
            s.jwtIssuer = issuer->asString();
        }

        if (const infrastructure::documents::DocumentValue* ttl = jwt->find("ttlSeconds"))
        {
            s.jwtTtlSeconds = ttl->asInt();
        }
    }

    if (const infrastructure::documents::DocumentValue* logging = findGroup(root, "logging", path))
    {
        if (const infrastructure::documents::DocumentValue* level = logging->find("level"))
        {
            s.logLevel = level->asString();
        }

        if (const infrastructure::documents::DocumentValue* filePath = logging->find("filePath"))
        {
            s.logFilePath = filePath->asString();
        }
    }

    if (const infrastructure::documents::DocumentValue* swagger = findGroup(root, "swagger", path))
    {
        if (const infrastructure::documents::DocumentValue* resPath = swagger->find("resPath"))
        {
            s.swaggerResPath = resPath->asString();
        }
    }

    s.configSource = path;
    const std::string err = s.validate();

    if (!err.empty())
    {
        throw errors::config::Invalid("Invalid configuration in " + path + ": " + err);
    }

    return s;
}

}  // namespace

AppSettings TomlConfigStore::load(const std::string& path)
{
    const infrastructure::documents::DocumentValue root = infrastructure::documents::loadTomlFile(path);

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
        throw errors::serialization::ParsingFailed("Invalid TOML types in " + path + ": " + ex.what());
    }
}

void TomlConfigStore::save(const std::string& path, const AppSettings& settings)
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

    infrastructure::documents::saveTomlFile(path, root);
}

}  // namespace app::config
