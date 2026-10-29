#include "app/config/VersionFile.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <regex>
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
#include "app/infrastructure/TextFile.hpp"
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

namespace
{

bool isDottedVersion(const std::string& value)
{
    static const std::regex pattern(R"(^[0-9]+\.[0-9]+\.[0-9]+$)");
    return std::regex_match(value, pattern);
}

bool isIsoDate(const std::string& value)
{
    static const std::regex pattern(R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}$)");
    return std::regex_match(value, pattern);
}

bool isClockTime(const std::string& value)
{
    static const std::regex pattern(R"(^[0-9]{2}:[0-9]{2}:[0-9]{2}$)");
    return std::regex_match(value, pattern);
}

void noteMissing(std::vector<std::string>* missing, const char* leaf)
{
    if (missing != nullptr)
    {
        missing->push_back(leaf);
    }
}

// Read one version field: absent -> default + recorded; present non-string
// -> throw (wrong type); present string with wrong shape -> throw (invalid).
std::string readField(const infrastructure::documents::DocumentValue& group, const char* leaf,
                      const std::string& fallback, bool (*valid)(const std::string&), const std::string& path,
                      std::vector<std::string>* missing)
{
    const infrastructure::documents::DocumentValue* node = group.find(leaf);
    if (node == nullptr || node->isNull())
    {
        noteMissing(missing, leaf);
        return fallback;
    }

    if (!node->isString())
    {
        throw errors::config::Invalid(std::string("Invalid <") + leaf + "> type in " + path + ": expected a string");
    }

    const std::string value = node->asString();
    if (!valid(value))
    {
        throw errors::config::Invalid(std::string("Invalid <") + leaf + "> value in " + path + ": '" + value + "'");
    }

    return value;
}

}  // namespace

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
    infrastructure::writePrivateTextFile(path, infrastructure::documents::serializeJson(root));

#elif defined(APP_VERSION_FILE_TOML)
#ifndef APP_WITH_TOML_PARSER
#error "APP_VERSION_FILE_FORMAT=TOML requires the TOML parser (APP_ENABLE_TOML_PARSER=ON)"
#endif  // !APP_WITH_TOML_PARSER

    infrastructure::documents::DocumentValue root = infrastructure::documents::DocumentValue::makeMap();
    root["version"] = info.version;
    root["releaseDate"] = info.releaseDate;
    root["startupTime"] = info.startupTime;
    infrastructure::writePrivateTextFile(path, infrastructure::documents::serializeToml(root));

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
    infrastructure::writePrivateTextFile(path, infrastructure::documents::serializeXml(document));

#elif defined(APP_VERSION_FILE_YAML)
#ifndef APP_WITH_YAML_PARSER
#error "APP_VERSION_FILE_FORMAT=YAML requires the YAML parser (APP_ENABLE_YAML_PARSER=ON)"
#endif  // !APP_WITH_YAML_PARSER

    infrastructure::documents::DocumentValue root = infrastructure::documents::DocumentValue::makeMap();
    root["version"] = info.version;
    root["releaseDate"] = info.releaseDate;
    root["startupTime"] = info.startupTime;
    infrastructure::writePrivateTextFile(path, infrastructure::documents::serializeYaml(root));

#else
#error "No APP_VERSION_FILE_* define set (cmake/options.cmake)"
#endif  // APP_VERSION_FILE format selection (JSON / TOML / XML / YAML)
}

VersionInfo readVersionFile(const std::string& path, std::vector<std::string>* missing)
{
    const VersionInfo current = currentVersionInfo();

#if defined(APP_VERSION_FILE_JSON)
#ifndef APP_WITH_JSON_PARSER
#error "APP_VERSION_FILE_FORMAT=JSON requires the JSON parser (APP_ENABLE_JSON_PARSER=ON)"
#endif  // !APP_WITH_JSON_PARSER
    const infrastructure::documents::DocumentValue root = infrastructure::documents::loadJsonFile(path);
    if (!root.isMap())
    {
        throw errors::config::Invalid("Version file root must be an object in " + path);
    }

    const infrastructure::documents::DocumentValue* group = &root;
#elif defined(APP_VERSION_FILE_TOML)
#ifndef APP_WITH_TOML_PARSER
#error "APP_VERSION_FILE_FORMAT=TOML requires the TOML parser (APP_ENABLE_TOML_PARSER=ON)"
#endif  // !APP_WITH_TOML_PARSER
    const infrastructure::documents::DocumentValue root = infrastructure::documents::loadTomlFile(path);
    if (!root.isMap())
    {
        throw errors::config::Invalid("Version file root must be a map in " + path);
    }

    const infrastructure::documents::DocumentValue* group = &root;
#elif defined(APP_VERSION_FILE_XML)
#ifndef APP_WITH_XML_PARSER
#error "APP_VERSION_FILE_FORMAT=XML requires the XML parser (APP_ENABLE_XML_PARSER=ON)"
#endif  // !APP_WITH_XML_PARSER
    const infrastructure::documents::DocumentValue document = infrastructure::documents::loadXmlFile(path);
    const infrastructure::documents::DocumentValue* group = document.find("appVersion");
    if (group == nullptr || !group->isMap())
    {
        throw errors::config::Invalid("Version file root must be <appVersion> in " + path);
    }
#elif defined(APP_VERSION_FILE_YAML)
#ifndef APP_WITH_YAML_PARSER
#error "APP_VERSION_FILE_FORMAT=YAML requires the YAML parser (APP_ENABLE_YAML_PARSER=ON)"
#endif  // !APP_WITH_YAML_PARSER
    const infrastructure::documents::DocumentValue root = infrastructure::documents::loadYamlFile(path);
    if (root.isNull())
    {
        noteMissing(missing, "version");
        noteMissing(missing, "releaseDate");
        noteMissing(missing, "startupTime");
        return current;
    }

    if (!root.isMap())
    {
        throw errors::config::Invalid("Version file root must be a map in " + path);
    }

    const infrastructure::documents::DocumentValue* group = &root;
#else
#error "No APP_VERSION_FILE_* define set (cmake/options.cmake)"
#endif  // APP_VERSION_FILE format selection (JSON / TOML / XML / YAML)

    try
    {
        VersionInfo info;
        info.version = readField(*group, "version", current.version, isDottedVersion, path, missing);
        info.releaseDate = readField(*group, "releaseDate", current.releaseDate, isIsoDate, path, missing);
        info.startupTime = readField(*group, "startupTime", current.startupTime, isClockTime, path, missing);
        return info;
    }
    catch (const errors::config::Invalid&)
    {
        throw;
    }
    catch (const errors::serialization::DeserializationFailed& ex)
    {
        throw errors::serialization::ParsingFailed("Invalid version file types in " + path + ": " + ex.what());
    }
}

std::vector<std::string> ensureVersionFile(const std::string& path)
{
    if (!infrastructure::fileExists(path))
    {
        writeVersionFile(path, currentVersionInfo());
        return {};
    }

    std::vector<std::string> missing;
    readVersionFile(path, &missing);

    // version/releaseDate always stamp the current build, startupTime the
    // current boot; the read above already validated whatever was present.
    writeVersionFile(path, currentVersionInfo());
    return missing;
}

}  // namespace app::config
