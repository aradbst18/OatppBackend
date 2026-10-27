#include "support/TestRunner.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <thread>
#include <vector>

#include <sys/stat.h>

#include "app/application/Application.hpp"
#include "app/config/ConfigStore.hpp"
#include "app/config/VersionFile.hpp"
#include "app/errors/AppException.hpp"
#include "app/infrastructure/TextFile.hpp"
#include "app/infrastructure/documents/JsonDocument.hpp"
#include "app/infrastructure/documents/TomlDocument.hpp"
#include "app/infrastructure/documents/XmlDocument.hpp"
#include "app/infrastructure/documents/YamlDocument.hpp"
#include "app/logging/Logger.hpp"

using namespace app;

namespace
{

config::AppSettings validSettings()
{
    config::AppSettings s = config::AppSettings::defaults();
    s.host = "127.0.0.1";
    s.port = 8080;

#ifdef APP_DATABASE_POSTGRESQL
    s.databasePath = "/tmp/opencode";
    s.databasePort = 5433;
    s.databaseName = "app_test_db";
    s.databaseUser = "app_test_user";
#else
    s.databasePath = ":memory:";
#endif  // APP_DATABASE_POSTGRESQL

#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
    s.databaseKey = "app-test-key";
#endif  // ENCRYPTED SQLCIPHER || POSTGRESQL

    s.jwtSecret = "test-secret-123456789012345678901234";
    s.jwtIssuer = "test-issuer";
    s.jwtTtlSeconds = 60;
    s.logLevel = "WARN";
    s.logPath = "/tmp/opencode/oatpp-backend-logs";
    s.swaggerResPath = "/tmp/opencode/oatpp-backend-swagger-res";

    return s;
}

std::string tmpPath(const char* name)
{
    return std::string("/tmp/opencode/oatpp-backend-") + name;
}

bool hasMissing(const std::vector<std::string>& missing, const std::string& leaf)
{
    return std::find(missing.begin(), missing.end(), leaf) != missing.end();
}

using infrastructure::documents::DocumentValue;

struct ConfigFormat
{
    std::unique_ptr<config::IConfigStore> store;
    std::string (*serialize)(const DocumentValue&);
    bool xml;
};

std::vector<ConfigFormat> configFormats()
{
    std::vector<ConfigFormat> formats;
#ifdef APP_WITH_JSON_PARSER
    formats.push_back({std::make_unique<config::JsonConfigStore>(), infrastructure::documents::serializeJson, false});
#endif
#ifdef APP_WITH_TOML_PARSER
    formats.push_back({std::make_unique<config::TomlConfigStore>(), infrastructure::documents::serializeToml, false});
#endif
#ifdef APP_WITH_XML_PARSER
    formats.push_back({std::make_unique<config::XmlConfigStore>(), infrastructure::documents::serializeXml, true});
#endif
#ifdef APP_WITH_YAML_PARSER
    formats.push_back({std::make_unique<config::YamlConfigStore>(), infrastructure::documents::serializeYaml, false});
#endif
    return formats;
}

DocumentValue minimalConfig()
{
    const auto settings = validSettings();
    auto root = DocumentValue::makeMap();
    root["database"]["path"] = settings.databasePath;
#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
    root["database"]["key"] = *settings.databaseKey;
#endif
    root["jwt"]["secret"] = settings.jwtSecret;
    return root;
}

void writeConfig(const ConfigFormat& format, const std::string& path, const DocumentValue& root)
{
    if (format.xml)
    {
        auto document = DocumentValue::makeMap();
        document["app"] = root;
        infrastructure::writeTextFile(path, format.serialize(document));
    }
    else
    {
        infrastructure::writeTextFile(path, format.serialize(root));
    }
}

void expectConfigRejected(config::IConfigStore& store, const std::string& path, const std::string& field,
                          const std::string& secret = "")
{
    const auto original = infrastructure::readTextFile(path);
    try
    {
        store.load(path);
        tests::fail("insecure config must be rejected", __FILE__, __LINE__);
    }
    catch (const errors::config::Invalid& error)
    {
        const std::string message = error.what();
        EXPECT_TRUE(message.find(field) != std::string::npos);
        if (!secret.empty())
        {
            EXPECT_TRUE(message.find(secret) == std::string::npos);
        }
    }
    EXPECT_EQ(infrastructure::readTextFile(path), original);
}

struct ScopedUmask
{
    mode_t previous;
    explicit ScopedUmask(mode_t value) : previous(::umask(value))
    {
    }
    ~ScopedUmask()
    {
        ::umask(previous);
    }
};

}  // namespace

TEST_CASE("config: defaults contain no secrets and require explicit configuration")
{
    const auto defaults = config::AppSettings::defaults();
    EXPECT_TRUE(defaults.jwtSecret.empty());
#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
    EXPECT_FALSE(defaults.databaseKey.has_value());
#endif
    EXPECT_FALSE(defaults.validate().empty());
    EXPECT_TRUE(validSettings().validate().empty());
}

TEST_CASE("config: validation rejects bad values")
{
    config::AppSettings s = validSettings();
    s.port = 0;
    EXPECT_FALSE(s.validate().empty());

    s = validSettings();
    s.jwtSecret = "short";
    EXPECT_FALSE(s.validate().empty());

    s = validSettings();
    s.logLevel = "NOPE";
    EXPECT_FALSE(s.validate().empty());
}

TEST_CASE("config: defaults carry the library swagger res path")
{
#ifdef APP_WITH_SWAGGER
    EXPECT_FALSE(config::AppSettings::defaults().swaggerResPath.empty());
#else
    EXPECT_TRUE(true);  // no swagger section in this build; nothing to default
#endif  // APP_WITH_SWAGGER
}

#ifdef APP_WITH_SWAGGER
TEST_CASE("config: empty swagger res path is rejected")
{
    config::AppSettings s = validSettings();
    s.swaggerResPath = "";

    EXPECT_FALSE(s.validate().empty());
}
#endif  // APP_WITH_SWAGGER

#ifdef APP_WITH_JSON_PARSER
TEST_CASE("config: json roundtrip")
{
    const std::string path = tmpPath("cfg.json");
    config::JsonConfigStore store;
    store.save(path, validSettings());

    const config::AppSettings loaded = store.load(path);
    EXPECT_EQ(loaded.host, std::string("127.0.0.1"));
    EXPECT_EQ(loaded.port, 8080);

#ifdef APP_DATABASE_POSTGRESQL
    EXPECT_EQ(loaded.databaseName, std::string("app_test_db"));
    EXPECT_EQ(loaded.databasePort, 5433);
#endif  // APP_DATABASE_POSTGRESQL

    EXPECT_EQ(loaded.jwtIssuer, std::string("test-issuer"));
    EXPECT_EQ(loaded.jwtTtlSeconds, 60);
    EXPECT_EQ(loaded.logPath, std::string("/tmp/opencode/oatpp-backend-logs"));

#ifdef APP_WITH_SWAGGER
    EXPECT_EQ(loaded.swaggerResPath, std::string("/tmp/opencode/oatpp-backend-swagger-res"));
#endif  // APP_WITH_SWAGGER

    std::remove(path.c_str());
}
#endif  // APP_WITH_JSON_PARSER

#ifdef APP_WITH_TOML_PARSER
TEST_CASE("config: toml roundtrip")
{
    const std::string path = tmpPath("cfg.toml");
    config::TomlConfigStore store;
    store.save(path, validSettings());

    const config::AppSettings loaded = store.load(path);
    EXPECT_EQ(loaded.host, std::string("127.0.0.1"));
    EXPECT_EQ(loaded.port, 8080);

#ifdef APP_DATABASE_POSTGRESQL
    EXPECT_EQ(loaded.databaseName, std::string("app_test_db"));
    EXPECT_EQ(loaded.databasePort, 5433);
#endif  // APP_DATABASE_POSTGRESQL

    EXPECT_EQ(loaded.jwtSecret, std::string("test-secret-123456789012345678901234"));
    EXPECT_EQ(loaded.logPath, std::string("/tmp/opencode/oatpp-backend-logs"));

#ifdef APP_WITH_SWAGGER
    EXPECT_EQ(loaded.swaggerResPath, std::string("/tmp/opencode/oatpp-backend-swagger-res"));
#endif  // APP_WITH_SWAGGER

    std::remove(path.c_str());
}
#endif  // APP_WITH_TOML_PARSER

#ifdef APP_WITH_XML_PARSER
TEST_CASE("config: xml roundtrip")
{
    const std::string path = tmpPath("cfg.xml");
    config::XmlConfigStore store;
    store.save(path, validSettings());

    const config::AppSettings loaded = store.load(path);
    EXPECT_EQ(loaded.host, std::string("127.0.0.1"));
    EXPECT_EQ(loaded.port, 8080);

#ifdef APP_DATABASE_POSTGRESQL
    EXPECT_EQ(loaded.databaseName, std::string("app_test_db"));
    EXPECT_EQ(loaded.databasePort, 5433);
#endif  // APP_DATABASE_POSTGRESQL

    EXPECT_EQ(loaded.logLevel, std::string("WARN"));
    EXPECT_EQ(loaded.logPath, std::string("/tmp/opencode/oatpp-backend-logs"));

#ifdef APP_WITH_SWAGGER
    EXPECT_EQ(loaded.swaggerResPath, std::string("/tmp/opencode/oatpp-backend-swagger-res"));
#endif  // APP_WITH_SWAGGER

    std::remove(path.c_str());
}
#endif  // APP_WITH_XML_PARSER

#ifdef APP_WITH_YAML_PARSER
TEST_CASE("config: yaml roundtrip")
{
    const std::string path = tmpPath("cfg.yaml");
    config::YamlConfigStore store;
    store.save(path, validSettings());

    const config::AppSettings loaded = store.load(path);

    EXPECT_EQ(loaded.host, std::string("127.0.0.1"));
    EXPECT_EQ(loaded.port, 8080);

#ifdef APP_DATABASE_POSTGRESQL
    EXPECT_EQ(loaded.databaseName, std::string("app_test_db"));
    EXPECT_EQ(loaded.databasePort, 5433);
#endif  // APP_DATABASE_POSTGRESQL

    EXPECT_EQ(loaded.jwtSecret, std::string("test-secret-123456789012345678901234"));
    EXPECT_EQ(loaded.jwtTtlSeconds, 60);
    EXPECT_EQ(loaded.logLevel, std::string("WARN"));
    EXPECT_EQ(loaded.logPath, std::string("/tmp/opencode/oatpp-backend-logs"));

#ifdef APP_WITH_SWAGGER
    EXPECT_EQ(loaded.swaggerResPath, std::string("/tmp/opencode/oatpp-backend-swagger-res"));
#endif  // APP_WITH_SWAGGER

    std::remove(path.c_str());
}
#endif  // APP_WITH_YAML_PARSER

#ifdef APP_WITH_JSON_PARSER
TEST_CASE("config: invalid json throws ParsingFailed")
{
    const std::string path = tmpPath("bad.json");
    infrastructure::writeTextFile(path, "{ not json");

    config::JsonConfigStore store;

    EXPECT_THROW_AS(store.load(path), errors::serialization::ParsingFailed);

    std::remove(path.c_str());
}

TEST_CASE("config: legacy flat json is rejected")
{
    const std::string path = tmpPath("legacy.json");
    infrastructure::writeTextFile(path, R"({"host": "127.0.0.1", "port": 8080})");

    config::JsonConfigStore store;

    EXPECT_THROW_AS(store.load(path), errors::config::Invalid);

    std::remove(path.c_str());
}
#endif  // APP_WITH_JSON_PARSER

#ifdef APP_WITH_TOML_PARSER
TEST_CASE("config: invalid toml throws ParsingFailed")
{
    const std::string path = tmpPath("bad.toml");
    infrastructure::writeTextFile(path, "port = [unclosed");

    config::TomlConfigStore store;

    EXPECT_THROW_AS(store.load(path), errors::serialization::ParsingFailed);

    std::remove(path.c_str());
}

TEST_CASE("config: legacy flat toml is rejected")
{
    const std::string path = tmpPath("legacy.toml");
    infrastructure::writeTextFile(path, "host = \"127.0.0.1\"\n");

    config::TomlConfigStore store;

    EXPECT_THROW_AS(store.load(path), errors::config::Invalid);

    std::remove(path.c_str());
}
#endif  // APP_WITH_TOML_PARSER

#ifdef APP_WITH_XML_PARSER
TEST_CASE("config: invalid xml throws ParsingFailed")
{
    const std::string path = tmpPath("bad.xml");
    infrastructure::writeTextFile(path, "<app><port>oops");

    config::XmlConfigStore store;

    EXPECT_THROW_AS(store.load(path), errors::serialization::ParsingFailed);

    std::remove(path.c_str());
}

TEST_CASE("config: legacy flat xml is rejected")
{
    const std::string path = tmpPath("legacy.xml");
    infrastructure::writeTextFile(path, "<app><host>127.0.0.1</host></app>");

    config::XmlConfigStore store;

    EXPECT_THROW_AS(store.load(path), errors::config::Invalid);

    std::remove(path.c_str());
}
#endif  // APP_WITH_XML_PARSER

#ifdef APP_WITH_YAML_PARSER
TEST_CASE("config: invalid yaml throws ParsingFailed")
{
    const std::string path = tmpPath("bad.yaml");
    infrastructure::writeTextFile(path, "host: [unclosed\n  bad indent: :");

    config::YamlConfigStore store;

    EXPECT_THROW_AS(store.load(path), errors::serialization::ParsingFailed);

    std::remove(path.c_str());
}

TEST_CASE("config: legacy flat yaml is rejected")
{
    const std::string path = tmpPath("legacy.yaml");
    infrastructure::writeTextFile(path, "host: \"127.0.0.1\"\n");

    config::YamlConfigStore store;

    EXPECT_THROW_AS(store.load(path), errors::config::Invalid);

    std::remove(path.c_str());
}
#endif  // APP_WITH_YAML_PARSER

TEST_CASE("config: all formats reject missing JWT secrets without modifying the file")
{
    for (auto& format : configFormats())
    {
        const auto path = tmpPath(format.store->formatName()) + "-missing-jwt.conf";
        auto root = minimalConfig();
        root["jwt"].asMap().erase("secret");
        root["jwt"]["issuer"] = "test-issuer";
        writeConfig(format, path, root);
        expectConfigRejected(*format.store, path, "jwt.secret");

        root.asMap().erase("jwt");
        writeConfig(format, path, root);
        expectConfigRejected(*format.store, path, "jwt.secret");
        std::remove(path.c_str());
    }
}

#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
TEST_CASE("config: all formats reject missing database keys without modifying the file")
{
    for (auto& format : configFormats())
    {
        const auto path = tmpPath(format.store->formatName()) + "-missing-key.conf";
        auto root = minimalConfig();
        root["database"].asMap().erase("key");
        writeConfig(format, path, root);
        expectConfigRejected(*format.store, path, "database.key");

        root.asMap().erase("database");
        writeConfig(format, path, root);
        expectConfigRejected(*format.store, path, "database.key");
        std::remove(path.c_str());
    }
}
#endif

#ifdef APP_USER_DATABASE_ENCRYPTED
TEST_CASE("config: all formats preserve explicitly empty SQLCipher keys as plaintext")
{
    for (auto& format : configFormats())
    {
        const auto path = tmpPath(format.store->formatName()) + "-plaintext.conf";
        auto root = minimalConfig();
        root["database"]["key"] = "";
        writeConfig(format, path, root);
        std::vector<std::string> missing;
        const auto loaded = format.store->load(path, &missing);
        EXPECT_TRUE(loaded.databaseKey.has_value());
        EXPECT_TRUE(loaded.databaseKey->empty());
        EXPECT_FALSE(hasMissing(missing, "database.key"));
        format.store->save(path, loaded);
        EXPECT_TRUE(format.store->load(path).databaseKey.value().empty());
        std::remove(path.c_str());
    }
}
#endif

#ifdef APP_DATABASE_POSTGRESQL
TEST_CASE("config: all formats reject empty PostgreSQL passwords")
{
    for (auto& format : configFormats())
    {
        const auto path = tmpPath(format.store->formatName()) + "-empty-password.conf";
        auto root = minimalConfig();
        root["database"]["key"] = "";
        writeConfig(format, path, root);
        expectConfigRejected(*format.store, path, "database.key");
        std::remove(path.c_str());
    }
}
#endif

TEST_CASE("config: all formats reject published sample secrets on load and save")
{
    for (auto& format : configFormats())
    {
        const auto path = tmpPath(format.store->formatName()) + "-sample-secret.conf";
        for (const std::string sample :
             {"change-me-in-production-min-32-characters-required", "change-me-in-production",
              "dev-sqlcipher-key-change-me", "oatpp_test_pw", "REPLACE_ME"})
        {
            auto root = minimalConfig();
            root["jwt"]["secret"] = sample;
            writeConfig(format, path, root);
            expectConfigRejected(*format.store, path, "jwt.secret", sample);
            auto settings = validSettings();
            settings.jwtSecret = sample;
            EXPECT_THROW_AS(format.store->save(path, settings), errors::config::Invalid);
            EXPECT_EQ(settings.validate().find(sample), std::string::npos);

#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
            root = minimalConfig();
            root["database"]["key"] = sample;
            writeConfig(format, path, root);
            expectConfigRejected(*format.store, path, "database.key", sample);
            settings = validSettings();
            settings.databaseKey = sample;
            EXPECT_THROW_AS(format.store->save(path, settings), errors::config::Invalid);
            EXPECT_EQ(settings.validate().find(sample), std::string::npos);
#endif
        }
        std::remove(path.c_str());
    }
}

TEST_CASE("config: directly constructed applications reject insecure settings")
{
    auto settings = validSettings();
    settings.jwtSecret.clear();
    EXPECT_THROW_AS(application::Application{settings}, errors::config::Invalid);
    settings.jwtSecret = "change-me-in-production-min-32-characters-required";
    EXPECT_THROW_AS(application::Application{settings}, errors::config::Invalid);
#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
    settings = validSettings();
    settings.databaseKey = "dev-sqlcipher-key-change-me";
    EXPECT_THROW_AS(application::Application{settings}, errors::config::Invalid);
#endif
}

#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
TEST_CASE("config: directly constructed applications reject an omitted database key")
{
    auto settings = config::AppSettings::defaults();
    settings.jwtSecret = validSettings().jwtSecret;
    EXPECT_THROW_AS(application::Application{settings}, errors::config::Invalid);
}

TEST_CASE("config: all formats refuse to save an omitted programmatic database key")
{
    auto settings = validSettings();
    settings.databaseKey.reset();
    for (auto& format : configFormats())
    {
        const auto path = tmpPath(format.store->formatName()) + "-unconfigured-key.conf";
        format.store->save(path, validSettings());
        const auto original = infrastructure::readTextFile(path);
        EXPECT_THROW_AS(format.store->save(path, settings), errors::config::Invalid);
        EXPECT_EQ(infrastructure::readTextFile(path), original);
        std::remove(path.c_str());
    }
}
#endif  // APP_USER_DATABASE_ENCRYPTED || APP_DATABASE_POSTGRESQL

#ifdef APP_USER_DATABASE_ENCRYPTED
TEST_CASE("config: directly constructed applications accept explicitly selected plaintext")
{
    auto settings = validSettings();
    settings.databaseKey = "";
    application::Application application{settings};
    EXPECT_TRUE(application.settings().databaseKey.has_value());
    EXPECT_TRUE(application.settings().databaseKey->empty());
}
#endif  // APP_USER_DATABASE_ENCRYPTED

#ifdef APP_DATABASE_POSTGRESQL
TEST_CASE("config: directly constructed applications reject an empty PostgreSQL password")
{
    auto settings = validSettings();
    settings.databaseKey = "";
    EXPECT_THROW_AS(application::Application{settings}, errors::config::Invalid);
}
#endif  // APP_DATABASE_POSTGRESQL

TEST_CASE("config: new files have independent persistent secrets and owner-only permissions")
{
    const auto firstPath = tmpPath("first-install.conf");
    const auto secondPath = tmpPath("second-install.conf");
    std::remove(firstPath.c_str());
    std::remove(secondPath.c_str());
    ScopedUmask permissive(0);
    const auto first = application::loadSettingsOrDefaults(firstPath);
    const auto second = application::loadSettingsOrDefaults(secondPath);
    EXPECT_TRUE(first.validate().empty());
    EXPECT_TRUE(second.validate().empty());
    EXPECT_TRUE(first.jwtSecret != second.jwtSecret);
#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
    EXPECT_TRUE(first.databaseKey.has_value());
    EXPECT_TRUE(first.databaseKey->size() >= 32);
    EXPECT_TRUE(first.databaseKey != second.databaseKey);
    EXPECT_TRUE(first.databaseKey != first.jwtSecret);
#endif
    const auto privatePermissions = std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
    EXPECT_TRUE((std::filesystem::status(firstPath).permissions() & std::filesystem::perms::mask) ==
                privatePermissions);
    const auto original = infrastructure::readTextFile(firstPath);
    const auto reloaded = application::loadSettingsOrDefaults(firstPath);
    EXPECT_EQ(reloaded.jwtSecret, first.jwtSecret);
#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
    EXPECT_EQ(reloaded.databaseKey.value(), first.databaseKey.value());
#endif
    EXPECT_EQ(infrastructure::readTextFile(firstPath), original);
    std::remove(firstPath.c_str());
    std::remove(secondPath.c_str());
}

TEST_CASE("config: all stores save and replace files with owner-only permissions")
{
    ScopedUmask permissive(0);
    for (auto& format : configFormats())
    {
        const auto path = tmpPath(format.store->formatName()) + "-private.conf";
        std::remove(path.c_str());
        format.store->save(path, validSettings());
        const auto privatePermissions = std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
        EXPECT_TRUE((std::filesystem::status(path).permissions() & std::filesystem::perms::mask) == privatePermissions);
        std::filesystem::permissions(path, std::filesystem::perms::all);
        format.store->save(path, validSettings());
        EXPECT_TRUE((std::filesystem::status(path).permissions() & std::filesystem::perms::mask) == privatePermissions);
        EXPECT_EQ(format.store->load(path).jwtSecret, validSettings().jwtSecret);
        std::remove(path.c_str());
    }
}

TEST_CASE("config: concurrent first starts share one persisted configuration")
{
    const auto path = tmpPath("concurrent-startup.conf");
    std::remove(path.c_str());
    std::vector<config::AppSettings> loaded(4);
    std::vector<std::exception_ptr> errors(loaded.size());
    std::vector<std::thread> workers;
    for (std::size_t index = 0; index < loaded.size(); ++index)
    {
        workers.emplace_back(
            [&, index]()
            {
                try
                {
                    loaded[index] = application::loadSettingsOrDefaults(path);
                }
                catch (...)
                {
                    errors[index] = std::current_exception();
                }
            });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }
    const auto persisted = application::loadSettings(path);
    for (std::size_t index = 0; index < loaded.size(); ++index)
    {
        if (errors[index])
        {
            std::rethrow_exception(errors[index]);
        }
        EXPECT_EQ(loaded[index].jwtSecret, persisted.jwtSecret);
#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
        EXPECT_EQ(loaded[index].databaseKey.value(), persisted.databaseKey.value());
#endif
    }
    std::remove(path.c_str());
}

TEST_CASE("config: exclusive creation preserves existing configurations in all formats")
{
    for (auto& format : configFormats())
    {
        const auto path = tmpPath(format.store->formatName()) + "-exclusive.conf";
        std::remove(path.c_str());
        format.store->save(path, validSettings(), false);
        const auto original = infrastructure::readTextFile(path);
        auto replacement = validSettings();
        replacement.jwtSecret = "another-private-test-secret-1234567890";
        format.store->save(path, replacement, false);
        EXPECT_EQ(infrastructure::readTextFile(path), original);
        EXPECT_EQ(format.store->load(path).jwtSecret, validSettings().jwtSecret);
        std::remove(path.c_str());
    }
}

TEST_CASE("config: invalid existing configurations are never regenerated")
{
    const auto path = tmpPath("existing-insecure.conf");
    auto formatList = configFormats();
    for (auto& format : formatList)
    {
        if (std::string(format.store->formatName()) != config::activeFormatName())
        {
            continue;
        }
        auto root = minimalConfig();
        root["jwt"]["secret"] = "change-me-in-production-min-32-characters-required";
        writeConfig(format, path, root);
        const auto original = infrastructure::readTextFile(path);
        application::ensureConfigExists(path);
        EXPECT_THROW_AS(application::loadSettingsOrDefaults(path), errors::config::Invalid);
        EXPECT_EQ(infrastructure::readTextFile(path), original);
    }
    std::remove(path.c_str());
}

#if defined(APP_WITH_YAML_PARSER) && (defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL))
TEST_CASE("config: a null YAML key cannot silently select plaintext")
{
    const auto path = tmpPath("null-key.yaml");
    infrastructure::writeTextFile(path,
                                  "database:\n  path: ':memory:'\n  key: null\n"
                                  "jwt:\n  secret: 'test-secret-123456789012345678901234'\n");
    config::YamlConfigStore store;
    expectConfigRejected(store, path, "database.key");
    std::remove(path.c_str());
}
#endif

TEST_CASE("config: private saves replace symlinks without modifying their targets")
{
    const auto path = tmpPath("symlink-config.conf");
    const auto target = tmpPath("symlink-target.conf");
    std::remove(path.c_str());
    infrastructure::writeTextFile(target, "unrelated contents");
    std::filesystem::create_symlink(target, path);
    auto store = config::createActiveStore();
    store->save(path, validSettings());
    EXPECT_FALSE(std::filesystem::is_symlink(path));
    EXPECT_EQ(infrastructure::readTextFile(target), std::string("unrelated contents"));
    EXPECT_EQ(store->load(path).jwtSecret, validSettings().jwtSecret);
    std::remove(path.c_str());
    std::remove(target.c_str());
}

TEST_CASE("config: failed private saves preserve existing contents and clean up staging files")
{
    const auto directory = tmpPath("failed-save");
    const auto destination = directory + "/app.conf";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(destination);
    infrastructure::writeTextFile(destination + "/sentinel", "preserve this");
    auto store = config::createActiveStore();
    EXPECT_THROW_AS(store->save(destination, validSettings()), errors::filesystem::WriteFailed);
    EXPECT_EQ(infrastructure::readTextFile(destination + "/sentinel"), std::string("preserve this"));
    std::size_t entries = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        EXPECT_EQ(entry.path().filename().string(), std::string("app.conf"));
        ++entries;
    }
    EXPECT_EQ(entries, 1U);
    std::filesystem::remove_all(directory);
}

TEST_CASE("config: missing file throws NotFound")
{
    auto store = config::createActiveStore();

    // Active-format-agnostic: TOML reports ParsingFailed (toml::parse opens the
    // file itself), file-based stores report filesystem::NotFound. Both derive
    // from AppException.
    EXPECT_THROW_AS(store->load("/tmp/opencode/oatpp-backend-does-not-exist.conf"), errors::AppException);
}

TEST_CASE("config: empty logPath disables file logging")
{
    config::AppSettings s = validSettings();
    s.logPath = "";

    EXPECT_TRUE(s.validate().empty());

    const std::string path = tmpPath("cfg-nolog.conf");
    auto store = config::createActiveStore();
    store->save(path, s);

    EXPECT_EQ(store->load(path).logPath, std::string(""));

    std::remove(path.c_str());
}

TEST_CASE("config: save refuses invalid settings")
{
    auto store = config::createActiveStore();
    config::AppSettings bad = validSettings();
    bad.port = 0;

    EXPECT_THROW_AS(store->save(tmpPath("refuse.conf"), bad), errors::config::Invalid);
}

TEST_CASE("config: active store matches compile-time format")
{
    auto store = config::createActiveStore();

    EXPECT_EQ(std::string(store->formatName()), std::string(config::activeFormatName()));
    EXPECT_EQ(std::string(config::defaultConfigPath()), std::string("configs/app.conf"));
}

TEST_CASE("config: missing non-secret leaves are recorded and completed in all formats")
{
    for (auto& format : configFormats())
    {
        const auto path = tmpPath(format.store->formatName()) + "-partial.conf";
        auto root = minimalConfig();
        root["server"]["port"] = 8080;
        writeConfig(format, path, root);
        std::vector<std::string> missing;
        const auto loaded = format.store->load(path, &missing);
        EXPECT_TRUE(hasMissing(missing, "server.host"));
        EXPECT_TRUE(hasMissing(missing, "jwt.issuer"));
        EXPECT_TRUE(hasMissing(missing, "jwt.ttlSeconds"));
        EXPECT_TRUE(hasMissing(missing, "logging.level"));
        EXPECT_TRUE(hasMissing(missing, "logging.path"));
        EXPECT_FALSE(hasMissing(missing, "jwt.secret"));
        EXPECT_FALSE(hasMissing(missing, "database.path"));
        EXPECT_EQ(loaded.jwtSecret, validSettings().jwtSecret);
        format.store->save(path, loaded);
        std::vector<std::string> missingAfter;
        const auto reloaded = format.store->load(path, &missingAfter);
        EXPECT_TRUE(missingAfter.empty());
        EXPECT_EQ(reloaded.jwtSecret, loaded.jwtSecret);
        EXPECT_EQ(reloaded.logPath, loaded.logPath);
        std::remove(path.c_str());
    }
}

TEST_CASE("config: legacy logging.filePath is rejected in all formats")
{
    for (auto& format : configFormats())
    {
        const auto path = tmpPath(format.store->formatName()) + "-legacy-log.conf";
        auto root = minimalConfig();
        root["logging"]["filePath"] = "legacy.log";
        writeConfig(format, path, root);
        expectConfigRejected(*format.store, path, "logging.filePath");
        std::remove(path.c_str());
    }
}

TEST_CASE("startup: pre-logging failures land beside the executable")
{
    const std::string dir = "/tmp/opencode/oatpp-backend-earlylog";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    application::writeCriticalLog(dir, "config", "boom-probe");

    const std::string file = dir + "/logs/" + logging::Logger::currentDateString() + ".log";
    EXPECT_TRUE(infrastructure::fileExists(file));

    const std::string body = infrastructure::readTextFile(file);
    EXPECT_TRUE(body.find("[CRITICAL]") != std::string::npos);
    EXPECT_TRUE(body.find("boom-probe") != std::string::npos);

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("version: missing file is created")
{
    const std::string path = tmpPath("ver-missing.conf");
    std::remove(path.c_str());

    const std::vector<std::string> completed = config::ensureVersionFile(path);

    EXPECT_TRUE(completed.empty());
    EXPECT_TRUE(infrastructure::fileExists(path));

    std::vector<std::string> missingAfter;
    config::readVersionFile(path, &missingAfter);

    EXPECT_TRUE(missingAfter.empty());

    std::remove(path.c_str());
}

TEST_CASE("version: partial file is completed")
{
    const std::string path = tmpPath("ver-partial.conf");
    std::string content;

#ifdef APP_VERSION_FILE_JSON
    content = R"({"version": "0.0.1", "releaseDate": "2026-01-02"})";
#elif defined(APP_VERSION_FILE_TOML)
    content = "version = \"0.0.1\"\nreleaseDate = \"2026-01-02\"\n";
#elif defined(APP_VERSION_FILE_XML)
    content = "<appVersion><version>0.0.1</version><releaseDate>2026-01-02</releaseDate></appVersion>";
#elif defined(APP_VERSION_FILE_YAML)
    content = "version: \"0.0.1\"\nreleaseDate: \"2026-01-02\"\n";
#else
#error "No APP_VERSION_FILE_* define set"
#endif  // APP_VERSION_FILE format selection (JSON / TOML / XML / YAML)

    infrastructure::writeTextFile(path, content);

    std::vector<std::string> missing;
    const config::VersionInfo info = config::readVersionFile(path, &missing);

    EXPECT_TRUE(missing.size() == 1 && missing[0] == "startupTime");
    EXPECT_EQ(info.version, std::string("0.0.1"));
    EXPECT_EQ(info.releaseDate, std::string("2026-01-02"));
    EXPECT_TRUE(info.startupTime.size() == 8);

    const std::vector<std::string> completed = config::ensureVersionFile(path);

    EXPECT_TRUE(completed.size() == 1 && completed[0] == "startupTime");

    std::vector<std::string> missingAfter;
    config::readVersionFile(path, &missingAfter);

    EXPECT_TRUE(missingAfter.empty());

    std::remove(path.c_str());
}

TEST_CASE("version: invalid values throw")
{
    const std::string path = tmpPath("ver-bad.conf");
    std::string content;

#ifdef APP_VERSION_FILE_JSON
    content = R"({"version": "nope", "releaseDate": "2026-01-02", "startupTime": "11:22:33"})";
#elif defined(APP_VERSION_FILE_TOML)
    content = "version = \"nope\"\nreleaseDate = \"2026-01-02\"\nstartupTime = \"11:22:33\"\n";
#elif defined(APP_VERSION_FILE_XML)
    content =
        "<appVersion><version>nope</version><releaseDate>2026-01-02</"
        "releaseDate><startupTime>11:22:33</startupTime></appVersion>";
#elif defined(APP_VERSION_FILE_YAML)
    content = "version: \"nope\"\nreleaseDate: \"2026-01-02\"\nstartupTime: \"11:22:33\"\n";
#else
#error "No APP_VERSION_FILE_* define set"
#endif  // APP_VERSION_FILE format selection (JSON / TOML / XML / YAML)

    infrastructure::writeTextFile(path, content);

    EXPECT_THROW_AS(config::readVersionFile(path), errors::config::Invalid);

    std::remove(path.c_str());
}
