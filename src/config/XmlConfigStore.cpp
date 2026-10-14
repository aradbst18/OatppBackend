#include "app/config/ConfigStore.hpp"

#ifndef APP_WITH_XML_PARSER
#error "XmlConfigStore.cpp requires APP_WITH_XML_PARSER (configure with -DAPP_ENABLE_XML_PARSER=ON)"
#endif  // !APP_WITH_XML_PARSER

#include <cstdint>

#include "app/errors/AppException.hpp"
#include "app/infrastructure/documents/XmlDocument.hpp"

namespace app::config
{

namespace
{

std::string childText(const infrastructure::documents::DocumentValue& parent, const char* name, const std::string& fallback)
{
    const infrastructure::documents::DocumentValue* node = parent.find(name);
    if (node == nullptr || node->isNull())
    {
        return fallback;
    }

    // A present-but-empty element parses as "" and round-trips values like
    // filePath=""; only a missing element falls back.
    return node->asString();
}

// Pre-grouping flat keys (see JsonConfigStore): rejected instead of silently
// loading defaults.
void rejectLegacyFlat(const infrastructure::documents::DocumentValue& root, const std::string& source)
{
    for (const char* key : {"host", "port", "databasePath", "databaseKey", "jwtSecret", "jwtIssuer", "jwtTtlSeconds", "logLevel", "logFilePath"})
    {
        if (root.contains(key))
        {
            throw errors::config::Invalid(std::string("Unsupported flat element <") + key + "> in " + source + ": expected grouped schema " +
                                          "(server/database/jwt/logging)");
        }
    }
}

const infrastructure::documents::DocumentValue* findGroup(const infrastructure::documents::DocumentValue& root, const char* name)
{
    return root.find(name);
}

}  // namespace

AppSettings XmlConfigStore::load(const std::string& path)
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

        if (const infrastructure::documents::DocumentValue* server = findGroup(*root, "server"))
        {
            if (server->isMap())
            {
                s.host = childText(*server, "host", s.host);
                const std::string portText = childText(*server, "port", "");

                if (!portText.empty())
                {
                    try
                    {
                        const int port = std::stoi(portText);
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
        }

        if (const infrastructure::documents::DocumentValue* database = findGroup(*root, "database"))
        {
            if (database->isMap())
            {
                s.databasePath = childText(*database, "path", s.databasePath);

#ifdef APP_DATABASE_SQLCIPHER
                // The key must be explicitly present: falling back to the built-in default
                // would hide a misconfigured (unencrypted-by-accident) deployment.
                if (database->contains("key"))
                {
                    s.databaseKey = childText(*database, "key", "");
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
        }
        else
        {
#ifdef APP_DATABASE_SQLCIPHER
            throw errors::config::Invalid("database.key is required for the SQLCIPHER backend in " + path);
#endif  // APP_DATABASE_SQLCIPHER
        }

        if (const infrastructure::documents::DocumentValue* jwt = findGroup(*root, "jwt"))
        {
            if (jwt->isMap())
            {
                s.jwtSecret = childText(*jwt, "secret", s.jwtSecret);
                s.jwtIssuer = childText(*jwt, "issuer", s.jwtIssuer);
                const std::string ttlText = childText(*jwt, "ttlSeconds", "");

                if (!ttlText.empty())
                {
                    try
                    {
                        s.jwtTtlSeconds = std::stoll(ttlText);
                    }
                    catch (...)
                    {
                        throw errors::config::Invalid("Invalid <ttlSeconds> in " + path);
                    }
                }
            }
        }

        if (const infrastructure::documents::DocumentValue* logging = findGroup(*root, "logging"))
        {
            if (logging->isMap())
            {
                s.logLevel = childText(*logging, "level", s.logLevel);
                s.logFilePath = childText(*logging, "filePath", s.logFilePath);
            }
        }

        if (const infrastructure::documents::DocumentValue* swagger = findGroup(*root, "swagger"))
        {
            if (swagger->isMap())
            {
                s.swaggerResPath = childText(*swagger, "resPath", s.swaggerResPath);
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
    catch (const errors::config::Invalid&)
    {
        throw;
    }
    catch (const errors::serialization::DeserializationFailed& ex)
    {
        throw errors::serialization::ParsingFailed("Invalid XML types in " + path + ": " + ex.what());
    }
}

void XmlConfigStore::save(const std::string& path, const AppSettings& settings)
{
    const std::string err = settings.validate();
    if (!err.empty())
    {
        throw errors::config::Invalid("Refusing to save invalid settings: " + err);
    }

    infrastructure::documents::DocumentValue server = infrastructure::documents::DocumentValue::makeMap();
    server["host"] = settings.host;
    server["port"] = std::to_string(settings.port);

    infrastructure::documents::DocumentValue database = infrastructure::documents::DocumentValue::makeMap();
    database["path"] = settings.databasePath;

#ifdef APP_DATABASE_SQLCIPHER
    database["key"] = settings.databaseKey;
#endif  // APP_DATABASE_SQLCIPHER

    infrastructure::documents::DocumentValue jwt = infrastructure::documents::DocumentValue::makeMap();
    jwt["secret"] = settings.jwtSecret;
    jwt["issuer"] = settings.jwtIssuer;
    jwt["ttlSeconds"] = std::to_string(settings.jwtTtlSeconds);

    infrastructure::documents::DocumentValue logging = infrastructure::documents::DocumentValue::makeMap();
    logging["level"] = settings.logLevel;
    logging["filePath"] = settings.logFilePath;

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
    infrastructure::documents::saveXmlFile(path, document);
}

}  // namespace app::config
