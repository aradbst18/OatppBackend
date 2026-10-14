#include "app/config/VersionFile.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#ifdef APP_WITH_JSON_PARSER
#include "app/infrastructure/documents/JsonDocument.hpp"
#endif  // APP_WITH_JSON_PARSER
#ifdef APP_WITH_TOML_PARSER
#include "app/infrastructure/documents/TomlDocument.hpp"
#endif  // APP_WITH_TOML_PARSER
#ifdef APP_WITH_XML_PARSER
#include "app/infrastructure/documents/XmlDocument.hpp"
#endif  // APP_WITH_XML_PARSER
#ifdef APP_WITH_YAML_PARSER
#include "app/infrastructure/documents/YamlDocument.hpp"
#endif  // APP_WITH_YAML_PARSER

#include "app/errors/AppException.hpp"
#include "app/infrastructure/documents/DocumentValue.hpp"

namespace app::config
{

namespace
{

std::string currentClockTime()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};

#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif  // _WIN32 || _WIN64

    std::ostringstream out;
    out << std::put_time(&local, "%H:%M:%S");
    return out.str();
}

}  // namespace

VersionInfo currentVersionInfo()
{
    VersionInfo info;
    info.version = APP_VERSION;
    info.releaseDate = APP_RELEASE_DATE;
    info.startupTime = currentClockTime();
    return info;
}

std::string defaultVersionFilePath()
{
    return "configs/version.conf";
}

void writeVersionFile(const std::string& path, const VersionInfo& info)
{
#if defined(APP_VERSION_FILE_JSON)
#ifndef APP_WITH_JSON_PARSER
#error "APP_VERSION_FILE_FORMAT=JSON requires the JSON parser (APP_ENABLE_JSON_PARSER=ON)"
#endif  // !APP_WITH_JSON_PARSER

    infrastructure::documents::DocumentValue root = infrastructure::documents::DocumentValue::makeMap();
    root["version"] = info.version;
    root["releaseDate"] = info.releaseDate;
    root["startupTime"] = info.startupTime;
    infrastructure::documents::saveJsonFile(path, root);

#elif defined(APP_VERSION_FILE_TOML)
#ifndef APP_WITH_TOML_PARSER
#error "APP_VERSION_FILE_FORMAT=TOML requires the TOML parser (APP_ENABLE_TOML_PARSER=ON)"
#endif  // !APP_WITH_TOML_PARSER

    infrastructure::documents::DocumentValue root = infrastructure::documents::DocumentValue::makeMap();
    root["version"] = info.version;
    root["releaseDate"] = info.releaseDate;
    root["startupTime"] = info.startupTime;
    infrastructure::documents::saveTomlFile(path, root);

#elif defined(APP_VERSION_FILE_XML)
#ifndef APP_WITH_XML_PARSER
#error "APP_VERSION_FILE_FORMAT=XML requires the XML parser (APP_ENABLE_XML_PARSER=ON)"
#endif  // !APP_WITH_XML_PARSER

    infrastructure::documents::DocumentValue inner = infrastructure::documents::DocumentValue::makeMap();
    inner["version"] = info.version;
    inner["releaseDate"] = info.releaseDate;
    inner["startupTime"] = info.startupTime;
    infrastructure::documents::DocumentValue document = infrastructure::documents::DocumentValue::makeMap();
    document["appVersion"] = std::move(inner);
    infrastructure::documents::saveXmlFile(path, document);

#elif defined(APP_VERSION_FILE_YAML)
#ifndef APP_WITH_YAML_PARSER
#error "APP_VERSION_FILE_FORMAT=YAML requires the YAML parser (APP_ENABLE_YAML_PARSER=ON)"
#endif  // !APP_WITH_YAML_PARSER

    infrastructure::documents::DocumentValue root = infrastructure::documents::DocumentValue::makeMap();
    root["version"] = info.version;
    root["releaseDate"] = info.releaseDate;
    root["startupTime"] = info.startupTime;
    infrastructure::documents::saveYamlFile(path, root);

#else
#error "No APP_VERSION_FILE_* define set (cmake/options.cmake)"
#endif  // APP_VERSION_FILE format selection (JSON / TOML / XML / YAML)
}

}  // namespace app::config
