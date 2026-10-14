#include "support/TestRunner.hpp"

#include <cstdio>

#include "app/config/ConfigStore.hpp"
#include "app/errors/AppException.hpp"
#include "app/infrastructure/TextFile.hpp"

using namespace app;

namespace
{

config::AppSettings validSettings()
{
    config::AppSettings s = config::AppSettings::defaults();
    s.host = "127.0.0.1";
    s.port = 8080;
    s.databasePath = ":memory:";
    s.jwtSecret = "test-secret-123";
    s.jwtIssuer = "test-issuer";
    s.jwtTtlSeconds = 60;
    s.logLevel = "WARN";
    s.logFilePath = "/tmp/opencode/oatpp-backend-logs";
    s.swaggerResPath = "/tmp/opencode/oatpp-backend-swagger-res";
    return s;
}

std::string tmpPath(const char* name)
{
    return std::string("/tmp/opencode/oatpp-backend-") + name;
}

}  // namespace

TEST_CASE("config: defaults validate")
{
    EXPECT_TRUE(config::AppSettings::defaults().validate().empty());
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
#if defined(APP_WITH_SWAGGER)
    EXPECT_FALSE(config::AppSettings::defaults().swaggerResPath.empty());
#else
    EXPECT_TRUE(true);  // no swagger section in this build; nothing to default
#endif  // APP_WITH_SWAGGER
}

#if defined(APP_WITH_SWAGGER)
TEST_CASE("config: empty swagger res path is rejected")
{
    config::AppSettings s = validSettings();
    s.swaggerResPath = "";
    EXPECT_FALSE(s.validate().empty());
}
#endif  // APP_WITH_SWAGGER

#if defined(APP_WITH_JSON_PARSER)
TEST_CASE("config: json roundtrip")
{
    const std::string path = tmpPath("cfg.json");
    config::JsonConfigStore store;
    store.save(path, validSettings());
    const config::AppSettings loaded = store.load(path);
    EXPECT_EQ(loaded.host, std::string("127.0.0.1"));
    EXPECT_EQ(loaded.port, 8080);
    EXPECT_EQ(loaded.jwtIssuer, std::string("test-issuer"));
    EXPECT_EQ(loaded.jwtTtlSeconds, 60);
    EXPECT_EQ(loaded.logFilePath, std::string("/tmp/opencode/oatpp-backend-logs"));
#if defined(APP_WITH_SWAGGER)
    EXPECT_EQ(loaded.swaggerResPath, std::string("/tmp/opencode/oatpp-backend-swagger-res"));
#endif  // APP_WITH_SWAGGER
    std::remove(path.c_str());
}
#endif  // APP_WITH_JSON_PARSER

#if defined(APP_WITH_TOML_PARSER)
TEST_CASE("config: toml roundtrip")
{
    const std::string path = tmpPath("cfg.toml");
    config::TomlConfigStore store;
    store.save(path, validSettings());
    const config::AppSettings loaded = store.load(path);
    EXPECT_EQ(loaded.host, std::string("127.0.0.1"));
    EXPECT_EQ(loaded.port, 8080);
    EXPECT_EQ(loaded.jwtSecret, std::string("test-secret-123"));
    EXPECT_EQ(loaded.logFilePath, std::string("/tmp/opencode/oatpp-backend-logs"));
#if defined(APP_WITH_SWAGGER)
    EXPECT_EQ(loaded.swaggerResPath, std::string("/tmp/opencode/oatpp-backend-swagger-res"));
#endif  // APP_WITH_SWAGGER
    std::remove(path.c_str());
}
#endif  // APP_WITH_TOML_PARSER

#if defined(APP_WITH_XML_PARSER)
TEST_CASE("config: xml roundtrip")
{
    const std::string path = tmpPath("cfg.xml");
    config::XmlConfigStore store;
    store.save(path, validSettings());
    const config::AppSettings loaded = store.load(path);
    EXPECT_EQ(loaded.host, std::string("127.0.0.1"));
    EXPECT_EQ(loaded.port, 8080);
    EXPECT_EQ(loaded.logLevel, std::string("WARN"));
    EXPECT_EQ(loaded.logFilePath, std::string("/tmp/opencode/oatpp-backend-logs"));
#if defined(APP_WITH_SWAGGER)
    EXPECT_EQ(loaded.swaggerResPath, std::string("/tmp/opencode/oatpp-backend-swagger-res"));
#endif  // APP_WITH_SWAGGER
    std::remove(path.c_str());
}
#endif  // APP_WITH_XML_PARSER

#if defined(APP_WITH_YAML_PARSER)
TEST_CASE("config: yaml roundtrip")
{
    const std::string path = tmpPath("cfg.yaml");
    config::YamlConfigStore store;
    store.save(path, validSettings());
    const config::AppSettings loaded = store.load(path);
    EXPECT_EQ(loaded.host, std::string("127.0.0.1"));
    EXPECT_EQ(loaded.port, 8080);
    EXPECT_EQ(loaded.jwtSecret, std::string("test-secret-123"));
    EXPECT_EQ(loaded.jwtTtlSeconds, 60);
    EXPECT_EQ(loaded.logLevel, std::string("WARN"));
    EXPECT_EQ(loaded.logFilePath, std::string("/tmp/opencode/oatpp-backend-logs"));
#if defined(APP_WITH_SWAGGER)
    EXPECT_EQ(loaded.swaggerResPath, std::string("/tmp/opencode/oatpp-backend-swagger-res"));
#endif  // APP_WITH_SWAGGER
    std::remove(path.c_str());
}
#endif  // APP_WITH_YAML_PARSER

#if defined(APP_WITH_JSON_PARSER)
TEST_CASE("config: invalid json throws ParsingFailed")
{
    const std::string path = tmpPath("bad.json");
    infrastructure::writeTextFile(path, "{ not json");
    config::JsonConfigStore store;
    EXPECT_THROW_AS(store.load(path), errors::serialization::ParsingFailed);
    std::remove(path.c_str());
}
#endif  // APP_WITH_JSON_PARSER

#if defined(APP_WITH_JSON_PARSER)
TEST_CASE("config: legacy flat json is rejected")
{
    const std::string path = tmpPath("legacy.json");
    infrastructure::writeTextFile(path, R"({"host": "127.0.0.1", "port": 8080})");
    config::JsonConfigStore store;
    EXPECT_THROW_AS(store.load(path), errors::config::Invalid);
    std::remove(path.c_str());
}
#endif  // APP_WITH_JSON_PARSER

#if defined(APP_WITH_TOML_PARSER)
TEST_CASE("config: invalid toml throws ParsingFailed")
{
    const std::string path = tmpPath("bad.toml");
    infrastructure::writeTextFile(path, "port = [unclosed");
    config::TomlConfigStore store;
    EXPECT_THROW_AS(store.load(path), errors::serialization::ParsingFailed);
    std::remove(path.c_str());
}
#endif  // APP_WITH_TOML_PARSER

#if defined(APP_WITH_TOML_PARSER)
TEST_CASE("config: legacy flat toml is rejected")
{
    const std::string path = tmpPath("legacy.toml");
    infrastructure::writeTextFile(path, "host = \"127.0.0.1\"\n");
    config::TomlConfigStore store;
    EXPECT_THROW_AS(store.load(path), errors::config::Invalid);
    std::remove(path.c_str());
}
#endif  // APP_WITH_TOML_PARSER

#if defined(APP_WITH_XML_PARSER)
TEST_CASE("config: invalid xml throws ParsingFailed")
{
    const std::string path = tmpPath("bad.xml");
    infrastructure::writeTextFile(path, "<app><port>oops");
    config::XmlConfigStore store;
    EXPECT_THROW_AS(store.load(path), errors::serialization::ParsingFailed);
    std::remove(path.c_str());
}
#endif  // APP_WITH_XML_PARSER

#if defined(APP_WITH_XML_PARSER)
TEST_CASE("config: legacy flat xml is rejected")
{
    const std::string path = tmpPath("legacy.xml");
    infrastructure::writeTextFile(path, "<app><host>127.0.0.1</host></app>");
    config::XmlConfigStore store;
    EXPECT_THROW_AS(store.load(path), errors::config::Invalid);
    std::remove(path.c_str());
}
#endif  // APP_WITH_XML_PARSER

#if defined(APP_WITH_YAML_PARSER)
TEST_CASE("config: invalid yaml throws ParsingFailed")
{
    const std::string path = tmpPath("bad.yaml");
    infrastructure::writeTextFile(path, "host: [unclosed\n  bad indent: :");
    config::YamlConfigStore store;
    EXPECT_THROW_AS(store.load(path), errors::serialization::ParsingFailed);
    std::remove(path.c_str());
}
#endif  // APP_WITH_YAML_PARSER

#if defined(APP_WITH_YAML_PARSER)
TEST_CASE("config: legacy flat yaml is rejected")
{
    const std::string path = tmpPath("legacy.yaml");
    infrastructure::writeTextFile(path, "host: \"127.0.0.1\"\n");
    config::YamlConfigStore store;
    EXPECT_THROW_AS(store.load(path), errors::config::Invalid);
    std::remove(path.c_str());
}
#endif  // APP_WITH_YAML_PARSER

#if defined(APP_DATABASE_SQLCIPHER)
TEST_CASE("config: sqlcipher requires explicit database key")
{
    // Grouped schema, valid in every other respect, but no database.key.
    const std::string path = tmpPath("nokey.conf");
    std::string content;
#if defined(APP_CONFIG_JSON)
    content = R"({"server": {"host": "127.0.0.1", "port": 8080}, "database": {"path": ":memory:"}, )"
              R"("jwt": {"secret": "test-secret-123", "issuer": "test-issuer", "ttlSeconds": 60}, )"
              R"("logging": {"level": "WARN", "filePath": ""}})";
#elif defined(APP_CONFIG_TOML)
    content =
        "[server]\nhost = \"127.0.0.1\"\nport = 8080\n"
        "[database]\npath = \":memory:\"\n"
        "[jwt]\nsecret = \"test-secret-123\"\nissuer = \"test-issuer\"\nttlSeconds = 60\n"
        "[logging]\nlevel = \"WARN\"\nfilePath = \"\"\n";
#elif defined(APP_CONFIG_XML)
    content =
        "<app><server><host>127.0.0.1</host><port>8080</port></server>"
        "<database><path>:memory:</path></database>"
        "<jwt><secret>test-secret-123</secret><issuer>test-issuer</issuer>"
        "<ttlSeconds>60</ttlSeconds></jwt>"
        "<logging><level>WARN</level><filePath></filePath></logging></app>";
#elif defined(APP_CONFIG_YAML)
    content =
        "server:\n  host: \"127.0.0.1\"\n  port: 8080\n"
        "database:\n  path: \":memory:\"\n"
        "jwt:\n  secret: \"test-secret-123\"\n  issuer: \"test-issuer\"\n  ttlSeconds: 60\n"
        "logging:\n  level: \"WARN\"\n  filePath: \"\"\n";
#else
#error "No APP_CONFIG_* define set"
#endif  // APP_CONFIG format selection (JSON / TOML / XML / YAML)
    infrastructure::writeTextFile(path, content);

    auto store = config::createActiveStore();

    EXPECT_THROW_AS(store->load(path), errors::config::Invalid);

    std::remove(path.c_str());
}
#endif  // APP_DATABASE_SQLCIPHER

TEST_CASE("config: missing file throws NotFound")
{
    auto store = config::createActiveStore();
    // Active-format-agnostic: TOML reports ParsingFailed (toml::parse opens the
    // file itself), file-based stores report filesystem::NotFound. Both derive
    // from AppException.
    EXPECT_THROW_AS(store->load("/tmp/opencode/oatpp-backend-does-not-exist.conf"), errors::AppException);
}

TEST_CASE("config: empty logFilePath disables file logging")
{
    config::AppSettings s = validSettings();
    s.logFilePath = "";
    EXPECT_TRUE(s.validate().empty());
    const std::string path = tmpPath("cfg-nolog.conf");
    auto store = config::createActiveStore();
    store->save(path, s);
    EXPECT_EQ(store->load(path).logFilePath, std::string(""));
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
