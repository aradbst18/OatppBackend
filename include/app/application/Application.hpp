#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "app/auth/JwtService.hpp"
#include "app/config/AppSettings.hpp"
#include "app/infrastructure/database/IDatabase.hpp"
#include "app/repositories/UserRepository.hpp"
#include "app/services/AuthService.hpp"

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
    std::shared_ptr<auth::JwtService> jwtService();
    std::shared_ptr<infrastructure::database::IDatabase> database();

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
    std::shared_ptr<auth::JwtService> jwt_;
    std::shared_ptr<services::AuthService> auth_;
};

/// Load settings from file-or-defaults, throwing ConfigurationError with a
/// clear message when the file is missing/invalid.
config::AppSettings loadSettingsOrDefaults(const std::string& configPath);

}  // namespace app::application
