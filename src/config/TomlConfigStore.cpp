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
    for (const char* key : {"host", "port", "databasePath", "databaseKey", "jwtSecret", "jwtIssuer", "jwtTtlSeconds",
                            "logLevel", "logFilePath"})
    {
        if (root.contains(key))
        {
            throw errors::config::Invalid(std::string("Unsupported flat key '") + key + "' in " + source +
                                          ": expected grouped schema " + "([server]/[database]/[jwt]/[logging])");
        }
    }
}

const infrastructure::documents::DocumentValue* findGroup(const infrastructure::documents::DocumentValue& root,
                                                          const std::string& name, const std::string& source)
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

AppSettings fromValue(const infrastructure::documents::DocumentValue& root, const std::string& path,
                      std::vector<std::string>* missing)
{
    AppSettings s = AppSettings::defaults();
    rejectLegacyFlat(root, path);

    auto noteMissing = [missing](const char* group, const char* leaf)
    {
        if (missing != nullptr)
        {
            missing->push_back(std::string(group) + "." + leaf);
        }
    };

    if (const infrastructure::documents::DocumentValue* server = findGroup(root, "server", path))
    {
        if (const infrastructure::documents::DocumentValue* host = server->find("host"))
        {
            s.host = host->asString();
        }
        else
        {
            noteMissing("server", "host");
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

    if (const infrastructure::documents::DocumentValue* database = findGroup(root, "database", path))
    {
        if (database->find("host") != nullptr)
        {
            throw errors::config::Invalid("database.host was renamed to database.path in " + path +
                                          ": delete the file to regenerate or rename manually");
        }

        if (database->find("password") != nullptr)
        {
            throw errors::config::Invalid("database.password was renamed to database.key in " + path +
                                          ": delete the file to regenerate or rename manually");
        }

        if (const infrastructure::documents::DocumentValue* dbPath = database->find("path"))
        {
            s.databasePath = dbPath->asString();
        }
        else
        {
            noteMissing("database", "path");
        }

#ifdef APP_DATABASE_POSTGRESQL
        if (const infrastructure::documents::DocumentValue* port = database->find("port"))
        {
            const int portValue = static_cast<int>(port->asInt());
            if (portValue < 1 || portValue > 65535)
            {
                throw errors::config::Invalid("database.port out of range in " + path);
            }

            s.databasePort = static_cast<std::uint16_t>(portValue);
        }
        else
        {
            noteMissing("database", "port");
        }

        if (const infrastructure::documents::DocumentValue* name = database->find("name"))
        {
            s.databaseName = name->asString();
        }
        else
        {
            noteMissing("database", "name");
        }

        if (const infrastructure::documents::DocumentValue* user = database->find("user"))
        {
            s.databaseUser = user->asString();
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
            s.databaseKey = key->asString();
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

    if (const infrastructure::documents::DocumentValue* jwt = findGroup(root, "jwt", path))
    {
        if (const infrastructure::documents::DocumentValue* secret = jwt->find("secret"))
        {
            s.jwtSecret = secret->asString();
        }
        else
        {
            noteMissing("jwt", "secret");
        }

        if (const infrastructure::documents::DocumentValue* issuer = jwt->find("issuer"))
        {
            s.jwtIssuer = issuer->asString();
        }
        else
        {
            noteMissing("jwt", "issuer");
        }

        if (const infrastructure::documents::DocumentValue* ttl = jwt->find("ttlSeconds"))
        {
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

    if (const infrastructure::documents::DocumentValue* logging = findGroup(root, "logging", path))
    {
        if (logging->find("filePath") != nullptr)
        {
            throw errors::config::Invalid("logging.filePath was renamed to logging.path in " + path +
                                          ": delete the file to regenerate or rename manually");
        }

        if (const infrastructure::documents::DocumentValue* level = logging->find("level"))
        {
            s.logLevel = level->asString();
        }
        else
        {
            noteMissing("logging", "level");
        }

        if (const infrastructure::documents::DocumentValue* logPath = logging->find("path"))
        {
            s.logPath = logPath->asString();
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

    if (const infrastructure::documents::DocumentValue* swagger = findGroup(root, "swagger", path))
    {
        if (const infrastructure::documents::DocumentValue* resPath = swagger->find("resPath"))
        {
            s.swaggerResPath = resPath->asString();
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

    s.configSource = path;
    const std::string err = s.validate();

    if (!err.empty())
    {
        throw errors::config::Invalid("Invalid configuration in " + path + ": " + err);
    }

    return s;
}

}  // namespace

AppSettings TomlConfigStore::load(const std::string& path, std::vector<std::string>* missing)
{
    const infrastructure::documents::DocumentValue root = infrastructure::documents::loadTomlFile(path);

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

    infrastructure::documents::saveTomlFile(path, root);
}

}  // namespace app::config
