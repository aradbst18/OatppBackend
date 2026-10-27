#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "app/auth/JwtService.hpp"
#include "app/config/AppSettings.hpp"
#include "app/infrastructure/database/IDatabase.hpp"
#include "app/repositories/RefreshTokenRepository.hpp"
#include "app/repositories/UserRepository.hpp"
#include "app/services/AuthService.hpp"
#include "app/services/UserService.hpp"

namespace oatpp::web::server
{

class HttpRouter;
class HttpConnectionHandler;

}  // namespace oatpp::web::server

namespace oatpp::network
{

class Server;
class ConnectionHandler;

}  // namespace oatpp::network

namespace oatpp::network::tcp::server
{

class ConnectionProvider;

}

namespace app::application
{

/**
 * Explicit application lifecycle:
 *
 *   configure() -> initInfrastructure() -> initDatabase() -> initServices()
 *     -> initRouting() -> run() -> stop()
 *
 * Each phase is a separate method so tests can drive the lifecycle without
 * binding sockets (configure + initDatabase + initServices only). main()
 * only parses argv and calls run(); no business logic lives there.
 *
 * Threading: the HTTP server runs on the caller's thread via run(); an
 * optional background worker (ManagedThread) demonstrates owned-thread
 * semantics. Shutdown is cooperative: stop() halts the server connection
 * provider and joins the worker.
 */
class Application
{
public:
    explicit Application(config::AppSettings settings);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    const config::AppSettings& settings() const noexcept
    {
        return settings_;
    }

    /// Full startup without blocking (used by tests + run()).
    void initialize();

    /// Block serving HTTP until stop() is called.
    void run();

    /// Cooperative shutdown; idempotent and safe to call twice.
    void stop();

    bool isRunning() const noexcept
    {
        return running_.load();
    }

    std::shared_ptr<oatpp::web::server::HttpRouter> router();

    // Exposed for integration tests.
    std::shared_ptr<services::AuthService> authService();
    std::shared_ptr<services::UserService> userService();
    std::shared_ptr<auth::JwtService> jwtService();
    std::shared_ptr<infrastructure::database::IDatabase> database();
    std::shared_ptr<repositories::RefreshTokenRepository> refreshTokenRepository();

private:
    void initInfrastructure();
    void initDatabase();
    void initServices();
    void initRouting();

    config::AppSettings settings_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};

    std::shared_ptr<oatpp::web::server::HttpRouter> router_;
    std::shared_ptr<oatpp::web::server::HttpConnectionHandler> httpHandler_;
    std::shared_ptr<oatpp::network::tcp::server::ConnectionProvider> connectionProvider_;
    std::shared_ptr<oatpp::network::ConnectionHandler> websocketHandler_;
    std::shared_ptr<oatpp::network::Server> server_;

    std::shared_ptr<infrastructure::database::IDatabase> database_;
    std::shared_ptr<repositories::UserRepository> users_;
    std::shared_ptr<repositories::RefreshTokenRepository> refreshTokens_;
    std::shared_ptr<auth::JwtService> jwt_;
    std::shared_ptr<services::AuthService> auth_;
    std::shared_ptr<services::UserService> userService_;
};

/// Operational startup sequence (see docs/configuration.md):
/// 1) ensureConfigExists: create the config file from per-type defaults and
///    fresh private secrets when missing (SQLCIPHER: path, or path+key; POSTGRESQL:
///    path/key/user/name/port). Existing contents are preserved; directory
///    entries are synced before proceeding. Each backend reads only its own fields.
/// 2) loadSettings: parse + validate the file.
/// Steps 1-2 run before logging exists; failures are reported via
/// writeCriticalLog() (<exeDir>/logs/yyyy-mm-dd.log next to the executable)
/// and throw.
/// 3) logging, 4) database, 5) REST/Oatpp, 6) server follow in initialize().
/// Load settings from file-or-defaults, throwing ConfigurationError with a
/// clear message when the file is missing/invalid.
config::AppSettings loadSettingsOrDefaults(const std::string& configPath);

/// Step 1: create the config file with unique secrets when missing; sync its
/// containing directory and pending parent entries before returning, including
/// when the file already exists. Established ancestors need only search access.
void ensureConfigExists(const std::string& configPath);

/// Step 2: load + validate an existing config file. Absent leaves fall back to
/// non-secret defaults; missing secrets are rejected. When `missing` is not
/// null each absent non-secret leaf is recorded for file completion.
config::AppSettings loadSettings(const std::string& configPath, std::vector<std::string>* missing = nullptr);

/// Pre-logging failure sink: append one standard-format CRITICAL line to
/// `<exeDir>/logs/yyyy-mm-dd.log` (the `logs` directory is created next to
/// the executable when needed). Best effort, never throws. Used only before
/// the logging system knows its configured destination; afterwards failures
/// go to the configured log file instead.
void writeCriticalLog(const std::string& exeDir, const std::string& tag, const std::string& message) noexcept;

/// Directory containing the running executable (parent of argv[0], or "." when
/// argv[0] carries no directory). Used for the critical log location.
std::string executableDir(const std::string& argv0);

}  // namespace app::application
