#pragma once

#include <memory>
#include <string>

#include "app/config/AppSettings.hpp"

// Exactly one config format is active by construction (single-valued
// APP_CONFIG_FORMAT). More than one APP_CONFIG_* define (e.g. hand-passed
// -D flags in a non-CMake build) is a configuration error, not a silent
// first-match win; none at all means apply_app_definitions() was missed.
#if defined(APP_CONFIG_JSON) + defined(APP_CONFIG_TOML) + defined(APP_CONFIG_XML) + defined(APP_CONFIG_YAML) > 1
#error "APP_CONFIG_* defines are mutually exclusive (define exactly one via -DAPP_CONFIG_FORMAT=JSON|TOML|XML|YAML)"
#elif !defined(APP_CONFIG_JSON) && !defined(APP_CONFIG_TOML) && !defined(APP_CONFIG_XML) && !defined(APP_CONFIG_YAML)
#error "No APP_CONFIG_* define set (configure with -DAPP_CONFIG_FORMAT=JSON|TOML|XML|YAML)"
#endif  // APP_CONFIG format selection (JSON / TOML / XML / YAML)

namespace app::config
{

/**
 * Format parser interface. Each implementation (JSON/TOML/XML/YAML) converts
 * between AppSettings and its on-disk representation. Adding a new format
 * means adding one new class, no changes to callers (see ConfigFactory).
 *
 * Each store class only exists when its parser is enabled
 * (APP_WITH_*_PARSER): referencing a disabled store is a compile error by
 * design, which proves disabled parsers do not leak into the build.
 */
class IConfigStore
{
public:
    virtual ~IConfigStore() = default;
    virtual AppSettings load(const std::string& path) = 0;
    virtual void save(const std::string& path, const AppSettings& settings) = 0;
    virtual const char* formatName() const noexcept = 0;
};

#ifdef APP_WITH_JSON_PARSER
class JsonConfigStore : public IConfigStore
{
public:
    AppSettings load(const std::string& path) override;
    void save(const std::string& path, const AppSettings& settings) override;
    const char* formatName() const noexcept override
    {
        return "JSON";
    }
};
#endif  // APP_WITH_JSON_PARSER

#ifdef APP_WITH_TOML_PARSER
class TomlConfigStore : public IConfigStore
{
public:
    AppSettings load(const std::string& path) override;
    void save(const std::string& path, const AppSettings& settings) override;
    const char* formatName() const noexcept override
    {
        return "TOML";
    }
};
#endif  // APP_WITH_TOML_PARSER

#ifdef APP_WITH_XML_PARSER
class XmlConfigStore : public IConfigStore
{
public:
    AppSettings load(const std::string& path) override;
    void save(const std::string& path, const AppSettings& settings) override;
    const char* formatName() const noexcept override
    {
        return "XML";
    }
};
#endif  // APP_WITH_XML_PARSER

#ifdef APP_WITH_YAML_PARSER
class YamlConfigStore : public IConfigStore
{
public:
    AppSettings load(const std::string& path) override;
    void save(const std::string& path, const AppSettings& settings) override;
    const char* formatName() const noexcept override
    {
        return "YAML";
    }
};
#endif  // APP_WITH_YAML_PARSER

/// Create the compile-time-selected primary store.
/// The selection comes from APP_CONFIG_JSON/_TOML/_XML/_YAML defines (see
/// cmake/options.cmake), each requiring its APP_WITH_*_PARSER define (a
/// mismatch fails at configure time and is re-asserted here). Only the active
/// alias is used by Application, so there is no runtime branching between
/// formats in production code.
std::unique_ptr<IConfigStore> createActiveStore();

/// Human-readable name of the active format ("JSON", "TOML", "XML" or "YAML").
const char* activeFormatName() noexcept;

/// Default config file for the active format. The name is format-independent
/// (`configs/app.conf`); only the on-disk syntax follows the active format.
/// The file is created from defaults on first startup when missing.
std::string defaultConfigPath();

}  // namespace app::config
