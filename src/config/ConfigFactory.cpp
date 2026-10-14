#include "app/config/ConfigStore.hpp"

namespace app::config
{

std::unique_ptr<IConfigStore> createActiveStore()
{
#if defined(APP_CONFIG_JSON)
#ifndef APP_WITH_JSON_PARSER
#error "APP_CONFIG_FORMAT=JSON requires the JSON parser (APP_ENABLE_JSON_PARSER=ON)"
#endif  // !APP_WITH_JSON_PARSER
    return std::make_unique<JsonConfigStore>();

#elif defined(APP_CONFIG_TOML)
#ifndef APP_WITH_TOML_PARSER
#error "APP_CONFIG_FORMAT=TOML requires the TOML parser (APP_ENABLE_TOML_PARSER=ON)"
#endif  // !APP_WITH_TOML_PARSER
    return std::make_unique<TomlConfigStore>();

#elif defined(APP_CONFIG_XML)
#ifndef APP_WITH_XML_PARSER
#error "APP_CONFIG_FORMAT=XML requires the XML parser (APP_ENABLE_XML_PARSER=ON)"
#endif  // !APP_WITH_XML_PARSER
    return std::make_unique<XmlConfigStore>();

#elif defined(APP_CONFIG_YAML)
#ifndef APP_WITH_YAML_PARSER
#error "APP_CONFIG_FORMAT=YAML requires the YAML parser (APP_ENABLE_YAML_PARSER=ON)"
#endif  // !APP_WITH_YAML_PARSER
    return std::make_unique<YamlConfigStore>();

#else
#error "No APP_CONFIG_* define set (cmake/options.cmake)"
#endif  // APP_CONFIG format selection (JSON / TOML / XML / YAML)
}

const char* activeFormatName() noexcept
{
#if defined(APP_CONFIG_JSON)
    return "JSON";
#elif defined(APP_CONFIG_TOML)
    return "TOML";
#elif defined(APP_CONFIG_XML)
    return "XML";
#elif defined(APP_CONFIG_YAML)
    return "YAML";
#else
#error "No APP_CONFIG_* define set (cmake/options.cmake)"
#endif  // APP_CONFIG format selection (JSON / TOML / XML / YAML)
}

std::string defaultConfigPath()
{
    // Deliberately format-independent: every build reads/writes the same
    // `configs/app.conf` name; only the on-disk syntax follows the active
    // format. Missing files are created from defaults on startup.
    return "configs/app.conf";
}

}  // namespace app::config
