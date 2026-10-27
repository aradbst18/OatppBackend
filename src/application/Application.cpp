#include "app/application/Application.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "oatpp-websocket/ConnectionHandler.hpp"
#include "oatpp/core/base/Environment.hpp"
#include "oatpp/network/Server.hpp"
#include "oatpp/network/tcp/server/ConnectionProvider.hpp"
#include "oatpp/web/protocol/http/incoming/SimpleBodyDecoder.hpp"
#include "oatpp/web/server/HttpConnectionHandler.hpp"
#include "oatpp/web/server/HttpRouter.hpp"

#ifdef APP_WITH_ZLIB
#include "oatpp-zlib/EncoderProvider.hpp"
#endif  // APP_WITH_ZLIB
#ifdef APP_WITH_SWAGGER
#include "app/infrastructure/Swagger.hpp"
#endif  // APP_WITH_SWAGGER

#include "app/auth/PasswordHash.hpp"
#include "app/config/ConfigStore.hpp"
#include "app/config/VersionFile.hpp"
#include "app/controllers/AuthController.hpp"
#include "app/controllers/EchoController.hpp"
#include "app/controllers/UserController.hpp"
#include "app/errors/AppException.hpp"
#include "app/errors/ErrorMapper.hpp"
#include "app/infrastructure/TextFile.hpp"
#include "app/infrastructure/database/DatabaseFactory.hpp"

#ifdef APP_WITH_POSTGRESQL
#include "app/infrastructure/database/PostgresqlLocalServer.hpp"
#endif  // APP_WITH_POSTGRESQL

#include "app/logging/Logger.hpp"
#include "app/logging/OatppLoggerBridge.hpp"
#include "app/middlewares/AuthInterceptor.hpp"
#include "app/middlewares/CorsInterceptor.hpp"
#include "app/middlewares/RateLimitInterceptor.hpp"
#include "app/repositories/UserRepository.hpp"
#include "app/serialization/AppObjectMapper.hpp"
#include "app/websocket/EchoListener.hpp"

namespace app::application
{

namespace
{

std::atomic<bool> g_envInitialized{false};

void ensureOatppEnvironment()
{
    bool expected = false;
    if (g_envInitialized.compare_exchange_strong(expected, true))
    {
        oatpp::base::Environment::init(std::make_shared<logging::OatppLoggerBridge>());
        std::atexit(
            []()
            {
                try
                {
                    oatpp::base::Environment::destroy();
                }
                catch (...)
                {
                }
            });
    }
}

}  // namespace

Application::Application(config::AppSettings settings) : settings_(std::move(settings))
{
    const std::string error = settings_.validate();
    if (!error.empty())
    {
        throw errors::config::Invalid("Invalid application settings: " + error);
    }
}

Application::~Application()
{
    stop();
}

void Application::initialize()
{
    bool expected = false;
    if (!initialized_.compare_exchange_strong(expected, true))
    {
        return;
    }

    ensureOatppEnvironment();
    initInfrastructure();
    initDatabase();
    initServices();
    initRouting();
}

void Application::initInfrastructure()
{
    logging::Level level = logging::Level::Info;
    logging::tryParseLevel(settings_.logLevel, level);
    auto& logger = logging::Logger::instance();
    logger.setMinLevel(level);
    logger.setLogFileDirectory(settings_.logPath);

    if (!settings_.logPath.empty())
    {
        logging::logInfo("app", "File logging to " + logging::Logger::resolveLogFilePath(settings_.logPath));
    }

    try
    {
        // Same completeness contract as the main config: create when
        // missing, complete absent fields from the current build info,
        // reject wrong types and invalid values.
        const std::string versionPath = config::defaultVersionFilePath();
        const std::vector<std::string> completed = config::ensureVersionFile(versionPath);
        const config::VersionInfo version = config::currentVersionInfo();
        if (completed.empty())
        {
            logging::logInfo("app",
                             "Version " + version.version + " (" + version.releaseDate + ") written to " + versionPath);
        }
        else
        {
            std::string joined;
            for (const std::string& name : completed)
            {
                joined += (joined.empty() ? "" : ", ") + name;
            }

            logging::logInfo("app", "Version " + version.version + " (" + version.releaseDate + ") written to " +
                                        versionPath + " (completed missing parameters: " + joined + ")");
        }
    }
    catch (const std::exception& ex)
    {
        // Invalid version content is the operator's to fix: record it as
        // critical in the configured log and abort startup.
        logging::logCritical("app", std::string("Invalid version file: ") + ex.what());
        throw;
    }
    catch (...)
    {
        logging::logCritical("app", "Invalid version file: unknown error");
        throw;
    }

    logging::logInfo("app", "Initializing infrastructure");
}

void Application::initDatabase()
{
#if defined(APP_DATABASE_SQLCIPHER) && defined(APP_DATABASE_POSTGRESQL)
#error "APP_DATABASE_* defines are mutually exclusive (configure with -DAPP_USERS_DATABASE_TYPE=SQLCIPHER|POSTGRESQL)"

#elif defined(APP_DATABASE_POSTGRESQL)
    // Step-by-step verification for PostgreSQL:
    // missing server -> create + start + role/db, down -> start,
    // running -> verify role/db + scram, then open + schema + admin.
    // Test databases (oatpp_test_*) are provisioned externally by
    // tests/support/TestDb.hpp; skip server management there so tests
    // stay hermetic and never restart the shared server mid-run.
    const bool isTestDb = settings_.databaseName.rfind("oatpp_test_", 0) == 0;

    if (!isTestDb)
    {
        infrastructure::database::PostgresqlLocalServer server(settings_.databasePath, settings_.databasePort,
                                                               settings_.databaseName, settings_.databaseUser,
                                                               *settings_.databaseKey);
        server.ensureProvisioned();
    }

    database_ = infrastructure::database::DatabaseFactory::create(settings_.databasePath, settings_.databasePort,
                                                                  settings_.databaseName, settings_.databaseUser,
                                                                  *settings_.databaseKey);

#elif defined(APP_DATABASE_SQLCIPHER)
    // File backend (always SQLCipher-backed; encrypted when a key is set):
    // missing app.db -> create on open; existing file -> verify schema +
    // admin below. Explicit existence log keeps the verification steps visible.
    const bool missing = settings_.databasePath != ":memory:" && !infrastructure::fileExists(settings_.databasePath);
    if (missing)
    {
        logging::logInfo("app", "User database file missing at '" + settings_.databasePath + "'; creating");
    }
    else
    {
        logging::logInfo("app", "User database file exists at '" + settings_.databasePath + "'; verifying admin user");
    }

#ifdef APP_USER_DATABASE_ENCRYPTED
    if (settings_.databaseKey->empty())
    {
        logging::logWarn("app", "User database key is empty; opening the file without encryption (plaintext)");
    }

    database_ = infrastructure::database::DatabaseFactory::create(settings_.databasePath, *settings_.databaseKey);
#else
    database_ = infrastructure::database::DatabaseFactory::create(settings_.databasePath);
#endif  // APP_USER_DATABASE_ENCRYPTED

#else
#error \
    "No database backend defined (require APP_DATABASE_SQLCIPHER or APP_DATABASE_POSTGRESQL via -DAPP_USERS_DATABASE_TYPE=SQLCIPHER|POSTGRESQL)"
#endif  // APP_DATABASE backend selection (SQLCIPHER / POSTGRESQL)

    database_->open();

    users_ = std::make_shared<repositories::UserRepository>(database_);
    users_->ensureSchema();

    refreshTokens_ = std::make_shared<repositories::RefreshTokenRepository>(database_);
    refreshTokens_->ensureSchema();

    logging::logInfo("app", "Database opened (" + database_->backendName() + ")");
}

void Application::initServices()
{
    jwt_ = std::make_shared<auth::JwtService>(settings_.jwtSecret, settings_.jwtIssuer,
                                              std::chrono::seconds(settings_.jwtTtlSeconds));
    auth_ = std::make_shared<services::AuthService>(users_, jwt_, refreshTokens_);
    const std::string adminUsername = auth_->ensureSeeded();

    userService_ = std::make_shared<services::UserService>(users_, adminUsername);
    logging::logInfo("app", "Services initialized");
}

void Application::initRouting()
{
    auto objectMapper = serialization::createAppObjectMapper();
    router_ = oatpp::web::server::HttpRouter::createShared();

    // WebSocket handler for /ws/echo upgrades (created first, injected).
    auto wsHandler = oatpp::websocket::ConnectionHandler::createShared();
    wsHandler->setSocketInstanceListener(std::make_shared<websocket::EchoInstanceListener>());
    websocketHandler_ = wsHandler;

    auto authController = std::make_shared<controllers::AuthController>(objectMapper, auth_, jwt_);
    auto userController = std::make_shared<controllers::UserController>(objectMapper, userService_, jwt_);
    auto echoController = std::make_shared<controllers::EchoController>(objectMapper, websocketHandler_);

    router_->addController(authController);
    router_->addController(userController);
    router_->addController(echoController);

#ifdef APP_WITH_SWAGGER
    // Swagger needs the merged endpoint list; collect from our controllers.
    oatpp::web::server::api::Endpoints merged;
    merged.append(authController->getEndpoints());
    merged.append(userController->getEndpoints());
    merged.append(echoController->getEndpoints());
    infrastructure::registerSwaggerWithEndpoints(router_, merged, settings_.swaggerResPath);
#endif  // APP_WITH_SWAGGER

    // HTTP handler with central error mapping, auth interception and optional
    // content encoding.
    auto components = std::make_shared<oatpp::web::server::HttpProcessor::Components>(router_);

#ifdef APP_WITH_ZLIB
    auto encoders = std::make_shared<oatpp::web::protocol::http::encoding::ProviderCollection>();
    encoders->add(std::make_shared<oatpp::zlib::DeflateEncoderProvider>());
    encoders->add(std::make_shared<oatpp::zlib::GzipEncoderProvider>());
    components->contentEncodingProviders = encoders;

    auto decoders = std::make_shared<oatpp::web::protocol::http::encoding::ProviderCollection>();
    decoders->add(std::make_shared<oatpp::zlib::DeflateDecoderProvider>());
    decoders->add(std::make_shared<oatpp::zlib::GzipDecoderProvider>());
    components->bodyDecoder = std::make_shared<oatpp::web::protocol::http::incoming::SimpleBodyDecoder>(decoders);
#endif  // APP_WITH_ZLIB

    httpHandler_ = std::make_shared<oatpp::web::server::HttpConnectionHandler>(components);
    httpHandler_->setErrorHandler(errors::AppErrorHandler::createShared());

    auto cors = std::make_shared<middlewares::CorsInterceptor>(settings_.corsAllowedOrigins);
    httpHandler_->addRequestInterceptor(cors);
    httpHandler_->addResponseInterceptor(cors);

    httpHandler_->addRequestInterceptor(std::make_shared<middlewares::RateLimitInterceptor>());
    httpHandler_->addRequestInterceptor(std::make_shared<middlewares::AuthInterceptor>(jwt_));

    // Connection provider is created here but the blocking Server runs in run().
    connectionProvider_ = oatpp::network::tcp::server::ConnectionProvider::createShared(
        {settings_.host.c_str(), static_cast<v_uint16>(settings_.port), oatpp::network::Address::IP_4});

    server_ = std::make_shared<oatpp::network::Server>(connectionProvider_, httpHandler_);

    logging::logInfo("app", "Routing initialized on " + settings_.host + ":" + std::to_string(settings_.port));
}

void Application::run()
{
    initialize();
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true))
    {
        return;
    }

    logging::logInfo("app", "Server running");

    // NOTE: websocket upgrades are dispatched through the EchoController using
    // the shared websocketHandler_ component; both handlers serve the same
    // connection provider via the HTTP upgrade path.
    server_->run();
    running_.store(false);
}

void Application::stop()
{
    if (server_)
    {
        try
        {
            server_->stop();
        }
        catch (...)
        {
        }
    }

    if (connectionProvider_)
    {
        try
        {
            connectionProvider_->stop();
        }
        catch (...)
        {
        }
    }

    if (database_)
    {
        try
        {
            database_->close();
        }
        catch (...)
        {
        }
    }

    running_.store(false);
}

std::shared_ptr<oatpp::web::server::HttpRouter> Application::router()
{
    initialize();
    return router_;
}

std::shared_ptr<services::AuthService> Application::authService()
{
    initialize();
    return auth_;
}

std::shared_ptr<services::UserService> Application::userService()
{
    initialize();
    return userService_;
}

std::shared_ptr<auth::JwtService> Application::jwtService()
{
    initialize();
    return jwt_;
}

std::shared_ptr<infrastructure::database::IDatabase> Application::database()
{
    initialize();
    return database_;
}

std::shared_ptr<repositories::RefreshTokenRepository> Application::refreshTokenRepository()
{
    initialize();
    return refreshTokens_;
}

config::AppSettings loadSettingsOrDefaults(const std::string& configPath)
{
    std::string path = configPath.empty() ? config::defaultConfigPath() : configPath;
    const bool missingBefore = !infrastructure::fileExists(path);
    ensureConfigExists(path);
    config::AppSettings loaded = loadSettings(path);

    if (missingBefore)
    {
        loaded.configSource = path + " (created)";
    }

    return loaded;
}

void ensureConfigExists(const std::string& configPath)
{
    std::string path = configPath.empty() ? config::defaultConfigPath() : configPath;
    if (infrastructure::fileExists(path))
    {
        // Another startup may have published the file but not yet synced its
        // directory entries. Persist them before any database can use its key.
        infrastructure::syncParentDirectories(path);
        return;
    }

    // First startup (or a fresh --config path): materialize the file from
    // per-type defaults and fresh independent secrets. Existing configurations
    // are never repaired by inventing replacement secrets.
    auto created = config::AppSettings::defaults();
    created.jwtSecret = auth::generateRandomSecret(32);
#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
    created.databaseKey = auth::generateRandomSecret(32);
#endif  // ENCRYPTED SQLCIPHER || POSTGRESQL
    auto store = config::createActiveStore();
    store->save(path, created, false);
}

config::AppSettings loadSettings(const std::string& configPath, std::vector<std::string>* missing)
{
    std::string path = configPath.empty() ? config::defaultConfigPath() : configPath;
    auto store = config::createActiveStore();
    return store->load(path, missing);
}

std::string executableDir(const std::string& argv0)
{
    const auto sep = argv0.find_last_of("/\\");
    if (sep == std::string::npos || sep == 0)
    {
        return ".";
    }

    return argv0.substr(0, sep);
}

void writeCriticalLog(const std::string& exeDir, const std::string& tag, const std::string& message) noexcept
{
    try
    {
        // Same daily file layout as the main log, rooted next to the
        // executable: <exeDir>/logs/yyyy-mm-dd.log.
        const std::string base = exeDir.empty() ? "." : exeDir;
        const std::string dir = base + "/logs";
        const std::string path = dir + "/" + logging::Logger::currentDateString() + ".log";
        const std::string line =
            logging::Logger::formatLine(logging::Logger::currentTimeString(), logging::Level::Critical, tag, message) +
            "\n";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        (void)ec;

        std::ofstream out(path, std::ios::app);
        if (out)
        {
            out << line;
        }
    }
    catch (...)
    {
    }
}

}  // namespace app::application
