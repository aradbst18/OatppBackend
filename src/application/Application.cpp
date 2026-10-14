#include "app/application/Application.hpp"

#include <atomic>
#include <chrono>

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

#include "app/config/ConfigStore.hpp"
#include "app/config/VersionFile.hpp"
#include "app/controllers/AuthController.hpp"
#include "app/controllers/EchoController.hpp"
#include "app/errors/AppException.hpp"
#include "app/errors/ErrorMapper.hpp"
#include "app/infrastructure/TextFile.hpp"
#include "app/infrastructure/database/DatabaseFactory.hpp"
#include "app/logging/Logger.hpp"
#include "app/logging/OatppLoggerBridge.hpp"
#include "app/middlewares/AuthInterceptor.hpp"
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
    logger.setLogFileDirectory(settings_.logFilePath);

    if (!settings_.logFilePath.empty())
    {
        logging::logInfo("app", "File logging to " + logging::Logger::resolveLogFilePath(settings_.logFilePath));
    }

    try
    {
        const config::VersionInfo version = config::currentVersionInfo();
        const std::string versionPath = config::defaultVersionFilePath();
        config::writeVersionFile(versionPath, version);
        logging::logInfo("app", "Version " + version.version + " (" + version.releaseDate + ") written to " + versionPath);
    }
    catch (const std::exception& ex)
    {
        // Informational file only: never fail startup because of it.
        logging::logWarn("app", std::string("Cannot write version file: ") + ex.what());
    }
    catch (...)
    {
        logging::logWarn("app", "Cannot write version file: unknown error");
    }

    logging::logInfo("app", "Initializing infrastructure");
}

void Application::initDatabase()
{
#ifdef APP_DATABASE_SQLCIPHER
    database_ = infrastructure::database::DatabaseFactory::create(settings_.databasePath, settings_.databaseKey);
#else
    database_ = infrastructure::database::DatabaseFactory::create(settings_.databasePath);
#endif  // APP_DATABASE_SQLCIPHER

    database_->open();
    users_ = std::make_shared<repositories::UserRepository>(database_);
    users_->ensureSchema();
    logging::logInfo("app", "Database opened (" + database_->backendName() + ")");
}

void Application::initServices()
{
    jwt_ = std::make_shared<auth::JwtService>(settings_.jwtSecret, settings_.jwtIssuer, std::chrono::seconds(settings_.jwtTtlSeconds));
    auth_ = std::make_shared<services::AuthService>(users_, jwt_);
    auth_->ensureSeeded();
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
    auto echoController = std::make_shared<controllers::EchoController>(objectMapper, websocketHandler_);
    router_->addController(authController);
    router_->addController(echoController);

#ifdef APP_WITH_SWAGGER
    // Swagger needs the merged endpoint list; collect from our controllers.
    oatpp::web::server::api::Endpoints merged;
    merged.append(authController->getEndpoints());
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

config::AppSettings loadSettingsOrDefaults(const std::string& configPath)
{
    std::string path = configPath.empty() ? config::defaultConfigPath() : configPath;
    auto store = config::createActiveStore();
    if (!infrastructure::fileExists(path))
    {
        // First startup (or a fresh --config path): materialize the active
        // `configs/app.conf` from defaults so the file always exists going
        // forward, then proceed with those values. save() validates first and
        // creates parent directories as needed.
        auto created = config::AppSettings::defaults();
        store->save(path, created);
        created.configSource = path + " (created)";
        return created;
    }

    return store->load(path);
}

}  // namespace app::application
