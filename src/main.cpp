#include <csignal>
#include <iostream>
#include <string>
#include <vector>

#include "app/application/Application.hpp"
#include "app/application/TerminateHandler.hpp"
#include "app/config/ConfigStore.hpp"
#include "app/errors/AppException.hpp"
#include "app/logging/Logger.hpp"

namespace
{

void printUsage(const char* argv0)
{
    std::cout << "Usage: " << argv0 << " [--config <path>] [--help]\n"
              << "\n"
              << "Options:\n"
              << "  --config <path>  Config file (default: " << app::config::defaultConfigPath() << " for the active "
              << app::config::activeFormatName() << " format)\n"
              << "  --help           Show this help\n";
}

}  // namespace

int main(int argc, char** argv)
{
    app::application::installTerminateHandler();

    std::string configPath;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            printUsage(argv[0]);
            return 0;
        }

        if (arg == "--config" && i + 1 < argc)
        {
            configPath = argv[++i];
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    const std::string exeDir = app::application::executableDir(argv[0]);

    // Steps 1-2 run before logging has a destination: any failure is written
    // to <exeDir>/logs/yyyy-mm-dd.log and the application terminates.
    app::config::AppSettings settings;
    std::vector<std::string> missing;
    try
    {
        // 1) Create the configuration file when missing (per-type defaults:
        // SQLCIPHER path (+key when set), POSTGRESQL path/key/user/name/port).
        app::application::ensureConfigExists(configPath);
        // 2) Extract + validate; absent leaves fall back to defaults and are
        // recorded for completion. Wrong types and invalid values throw.
        settings = app::application::loadSettings(configPath, &missing);
    }
    catch (const app::errors::AppException& ex)
    {
        const std::string msg = std::string("Startup failed [") + app::errors::toString(ex.code()) + "]: " + ex.what();
        app::application::writeCriticalLog(exeDir, "config", msg);
        std::cerr << msg << "\n";
        return 1;
    }
    catch (const std::exception& ex)
    {
        const std::string msg = std::string("Startup failed: ") + ex.what();
        app::application::writeCriticalLog(exeDir, "config", msg);
        std::cerr << msg << "\n";
        return 1;
    }

    // 3) The logging destination (logging.path, default-filled above) is now
    // established: initialize the logging system. Everything from here on,
    // including step 2b, is logged to the configured file in normal format.
    app::logging::Level level = app::logging::Level::Info;
    if (!app::logging::tryParseLevel(settings.logLevel, level))
    {
        level = app::logging::Level::Info;
    }

    auto& logger = app::logging::Logger::instance();
    logger.setMinLevel(level);
    logger.setLogFileDirectory(settings.logPath);

    try
    {
        app::logging::logInfo("main", std::string("Active config format: ") + app::config::activeFormatName());

        if (!missing.empty())
        {
            // 2b) Complete the file with the defaulted parameters.
            const std::string path = configPath.empty() ? app::config::defaultConfigPath() : configPath;
            auto store = app::config::createActiveStore();
            store->save(path, settings);

            std::string joined;
            for (const std::string& name : missing)
            {
                joined += (joined.empty() ? "" : ", ") + name;
            }

            app::logging::logInfo("main", "Completed missing configuration parameters with defaults: " + joined);
        }

        // 4)-7) Database, REST/Oatpp, server; failures use the normal log.
        app::application::Application app(std::move(settings));
        app.run();
        return 0;
    }
    catch (const app::errors::AppException& ex)
    {
        const std::string msg = std::string("Startup failed [") + app::errors::toString(ex.code()) + "]: " + ex.what();
        app::logging::logCritical("main", msg);
        std::cerr << msg << "\n";
        return 1;
    }
    catch (const std::exception& ex)
    {
        const std::string msg = std::string("Startup failed: ") + ex.what();
        app::logging::logCritical("main", msg);
        std::cerr << msg << "\n";
        return 1;
    }
}
