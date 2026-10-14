#pragma once

#include <cstdint>
#include <string>

namespace app::config
{

/// Runtime application settings populated from JSON/TOML/XML/YAML files.
/// Field names are identical across formats so switching APP_CONFIG_FORMAT
/// never changes application code, only the parser selected at compile time.
struct AppSettings
{
    std::string host = "127.0.0.1";
    std::uint16_t port = 8080;
    std::string databasePath = "data/app.db";

#ifdef APP_DATABASE_SQLCIPHER
    // SQLCipher only: the encryption password, applied via PRAGMA key at open.
    // Must be non-empty (see validate()). Absent in SQLITE builds by design.
    std::string databaseKey;
#endif  // APP_DATABASE_SQLCIPHER

    std::string jwtSecret = "change-me-in-production";
    std::string jwtIssuer = "oatpp-backend";
    std::int64_t jwtTtlSeconds = 3600;
    std::string logLevel = "INFO";
    /// Directory holding daily log files (`<dir>/yyyy-mm-dd.log`). Empty
    /// disables file logging (console/stderr only). Created on demand.
    std::string logFilePath = "logs";
    /// Directory with the Swagger UI assets (index.html, *.js, *.css) served
    /// at `/swagger/ui`. Default is the oatpp-swagger library's own res dir
    /// (see OATPP_SWAGGER_RES_PATH); override it here to serve a custom or
    /// relocated copy (e.g. the installed `share/swagger-res`). Only used
    /// when built with APP_WITH_SWAGGER.
    std::string swaggerResPath;
    std::string configSource;  // filled with the file the settings came from

    /// Defaults used when no config file exists (tests, first boot).
    static AppSettings defaults();

    /// Returns an error description, or empty when valid.
    std::string validate() const;
};

}  // namespace app::config
