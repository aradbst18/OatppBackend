#pragma once

#include <cstdint>
#include <optional>
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

    /// Comma-separated literal proxy IPs allowed to supply X-Forwarded-For. Empty trusts none.
    std::string trustedProxies;

    /// Unified database location:
    /// - SQLCIPHER: filesystem path of the database file (e.g. "data/app.db", ":memory:" in tests).
    /// - POSTGRESQL: Unix socket directory of the local server (e.g. "data/postgresql", see docs/postgresql.md).
    std::string databasePath = "data/app.db";

#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
    // Unified database secret:
    // - SQLCIPHER: file encryption key, applied through SQLCipher's sqlite3_key() at open.
    //   Assigning "" explicitly selects plaintext; nullopt means unconfigured and is rejected.
    // - POSTGRESQL: role password for scram-sha-256 authentication. Must be present and non-empty.
    // Absent in unencrypted SQLCIPHER builds by design (no secret concept there).
    std::optional<std::string> databaseKey;
#endif  // APP_USER_DATABASE_ENCRYPTED || APP_DATABASE_POSTGRESQL

    /// PostgreSQL connection parameters (used only when the user database is
    /// POSTGRESQL). Compiled out in SQLCIPHER builds so no PG-only
    /// field leaks into other backends. Secrets live in databaseKey, never
    /// here, and are never logged.
#ifdef APP_DATABASE_POSTGRESQL
    std::uint16_t databasePort = 5432;
    std::string databaseName = "oatpp_backend";
    std::string databaseUser = "oatpp";
#endif  // APP_DATABASE_POSTGRESQL

    std::string jwtSecret;
    std::string jwtIssuer = "oatpp-backend";
    std::int64_t jwtTtlSeconds = 3600;
    std::string logLevel = "INFO";

    /// Directory holding daily log files (`<dir>/yyyy-mm-dd.log`). Empty
    /// disables file logging (console/stderr only). Created on demand.
    std::string logPath = "logs";

    /// Directory with the Swagger UI assets (index.html, *.js, *.css) served
    /// at `/swagger/ui`. Default is the oatpp-swagger library's own res dir
    /// (see OATPP_SWAGGER_RES_PATH); override it here to serve a custom or
    /// relocated copy (e.g. the installed `share/swagger-res`). Only used
    /// when built with APP_WITH_SWAGGER.
    std::string swaggerResPath;

    /// "*" or comma-separated exact HTTP(S) origins; explicit empty values are invalid.
    std::string corsAllowedOrigins = "*";
    std::string configSource;  // filled with the file the settings came from

    /// Non-secret defaults. Secrets must be supplied before validation;
    /// first startup generates and persists them in the new config file.
    static AppSettings defaults();

    /// Returns an error description, or empty when valid.
    std::string validate() const;

    AppSettings() = default;
    AppSettings(AppSettings&&) noexcept = default;
    AppSettings& operator=(AppSettings&&) noexcept = default;
    AppSettings(const AppSettings&) = default;
    AppSettings& operator=(const AppSettings&) = default;
};

}  // namespace app::config
