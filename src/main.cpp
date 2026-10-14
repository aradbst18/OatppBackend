#include <csignal>
#include <iostream>
#include <string>

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

    try
    {
        app::config::AppSettings settings = app::application::loadSettingsOrDefaults(configPath);
        app::logging::logInfo("main", std::string("Active config format: ") + app::config::activeFormatName());

        app::application::Application app(std::move(settings));
        app.run();
        return 0;
    }
    catch (const app::errors::AppException& ex)
    {
        std::cerr << "Startup failed [" << app::errors::toString(ex.code()) << "]: " << ex.what() << "\n";
        return 1;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Startup failed: " << ex.what() << "\n";
        return 1;
    }
}
