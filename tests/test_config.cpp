#include "support/TestRunner.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "app/application/Application.hpp"
#include "app/config/ConfigStore.hpp"
#include "app/config/VersionFile.hpp"
#include "app/errors/AppException.hpp"
#include "app/infrastructure/TextFile.hpp"
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

#ifdef APP_USER_DATABASE_ENCRYPTED
TEST_CASE("config: encrypted sqlcipher completes a missing database key")
{
    // Absent key falls back to defaults like any other missing parameter.
    const std::string path = tmpPath("nokey.conf");
    std::string content;

#ifdef APP_CONFIG_JSON
    content =
        R"({"server": {"host": "127.0.0.1", "port": 8080}, "database": {"path": ":memory:"}, )"
        R"("jwt": {"secret": "test-secret-123456789012345678901234", "issuer": "test-issuer", "ttlSeconds": 60}, )"
        R"("logging": {"level": "WARN", "path": ""}})";
#elif defined(APP_CONFIG_TOML)
    content =
        "[server]\nhost = \"127.0.0.1\"\nport = 8080\n"
        "[database]\npath = \":memory:\"\n"
        "[jwt]\nsecret = \"test-secret-123456789012345678901234\"\nissuer = \"test-issuer\"\nttlSeconds = 60\n"
        "[logging]\nlevel = \"WARN\"\npath = \"\"\n";
#elif defined(APP_CONFIG_XML)
    content =
        "<app><server><host>127.0.0.1</host><port>8080</port></server>"
        "<database><path>:memory:</path></database>"
        "<jwt><secret>test-secret-123456789012345678901234</secret><issuer>test-issuer</issuer>"
        "<ttlSeconds>60</ttlSeconds></jwt>"
        "<logging><level>WARN</level><path></path></logging></app>";
#elif defined(APP_CONFIG_YAML)
    content =
        "server:\n  host: \"127.0.0.1\"\n  port: 8080\n"
        "database:\n  path: \":memory:\"\n"
        "jwt:\n  secret: \"test-secret-123456789012345678901234\"\n  issuer: \"test-issuer\"\n  ttlSeconds: 60\n"
        "logging:\n  level: \"WARN\"\n  path: \"\"\n";
#else
#error "No APP_CONFIG_* define set"
#endif  // APP_CONFIG format selection (JSON / TOML / XML / YAML)

    infrastructure::writeTextFile(path, content);

    auto store = config::createActiveStore();
    std::vector<std::string> missing;
    const config::AppSettings loaded = store->load(path, &missing);

    EXPECT_TRUE(hasMissing(missing, "database.key"));
    EXPECT_EQ(loaded.databaseKey, config::AppSettings::defaults().databaseKey);

    store->save(path, loaded);

    std::vector<std::string> missingAfter;
    store->load(path, &missingAfter);

    EXPECT_TRUE(missingAfter.empty());

    // Present-but-empty key is valid: the SQLCipher key is optional, so it
    // loads with the empty key intact (plaintext mode) and is not a gap.
    const std::string emptyPath = tmpPath("emptykey.conf");
    std::string emptyContent;

#ifdef APP_CONFIG_JSON
    emptyContent =
        R"({"server": {"host": "127.0.0.1", "port": 8080}, "database": {"path": ":memory:", "key": ""}, )"
        R"("jwt": {"secret": "test-secret-123456789012345678901234", "issuer": "test-issuer", "ttlSeconds": 60}, )"
        R"("logging": {"level": "WARN", "path": ""}})";
#elif defined(APP_CONFIG_TOML)
    emptyContent =
        "[server]\nhost = \"127.0.0.1\"\nport = 8080\n"
        "[database]\npath = \":memory:\"\nkey = \"\"\n"
        "[jwt]\nsecret = \"test-secret-123456789012345678901234\"\nissuer = \"test-issuer\"\nttlSeconds = 60\n"
        "[logging]\nlevel = \"WARN\"\npath = \"\"\n";
#elif defined(APP_CONFIG_XML)
    emptyContent =
        "<app><server><host>127.0.0.1</host><port>8080</port></server>"
        "<database><path>:memory:</path><key></key></database>"
        "<jwt><secret>test-secret-123456789012345678901234</secret><issuer>test-issuer</issuer>"
        "<ttlSeconds>60</ttlSeconds></jwt>"
        "<logging><level>WARN</level><path></path></logging></app>";
#elif defined(APP_CONFIG_YAML)
    emptyContent =
        "server:\n  host: \"127.0.0.1\"\n  port: 8080\n"
        "database:\n  path: \":memory:\"\n  key: \"\"\n"
        "jwt:\n  secret: \"test-secret-123456789012345678901234\"\n  issuer: \"test-issuer\"\n  ttlSeconds: 60\n"
        "logging:\n  level: \"WARN\"\n  path: \"\"\n";
#else
#error "No APP_CONFIG_* define set"
#endif  // APP_CONFIG format selection (JSON / TOML / XML / YAML)

    infrastructure::writeTextFile(emptyPath, emptyContent);

    std::vector<std::string> plainMissing;
    const config::AppSettings plain = store->load(emptyPath, &plainMissing);

    EXPECT_TRUE(plain.databaseKey.empty());
    EXPECT_TRUE(!hasMissing(plainMissing, "database.key"));

    std::remove(path.c_str());
    std::remove(emptyPath.c_str());
}
#endif  // APP_USER_DATABASE_ENCRYPTED

#ifdef APP_DATABASE_POSTGRESQL
TEST_CASE("config: postgresql completes a missing database key")
{
    // Absent key falls back to defaults like any other missing parameter.
    const std::string path = tmpPath("nokey-pg.conf");
    std::string content;

#ifdef APP_CONFIG_JSON
    content =
        R"({"server": {"host": "127.0.0.1", "port": 8080}, "database": {"path": "data/postgresql", "port": 5433, "name": "x", "user": "y"}, )"
        R"("jwt": {"secret": "test-secret-123456789012345678901234", "issuer": "test-issuer", "ttlSeconds": 60}, )"
        R"("logging": {"level": "WARN", "path": ""}})";
#elif defined(APP_CONFIG_TOML)
    content =
        "[server]\nhost = \"127.0.0.1\"\nport = 8080\n"
        "[database]\npath = \"data/postgresql\"\nport = 5433\nname = \"x\"\nuser = \"y\"\n"
        "[jwt]\nsecret = \"test-secret-123456789012345678901234\"\nissuer = \"test-issuer\"\nttlSeconds = 60\n"
        "[logging]\nlevel = \"WARN\"\npath = \"\"\n";
#elif defined(APP_CONFIG_XML)
    content =
        "<app><server><host>127.0.0.1</host><port>8080</port></server>"
        "<database><path>data/postgresql</path><port>5433</port><name>x</name><user>y</user></"
        "database>"
        "<jwt><secret>test-secret-123456789012345678901234</secret><issuer>test-issuer</issuer>"
        "<ttlSeconds>60</ttlSeconds></jwt>"
        "<logging><level>WARN</level><path></path></logging></app>";
#elif defined(APP_CONFIG_YAML)
    content =
        "server:\n  host: \"127.0.0.1\"\n  port: 8080\n"
        "database:\n  path: \"data/postgresql\"\n  port: 5433\n  name: \"x\"\n  user: \"y\"\n"
        "jwt:\n  secret: \"test-secret-123456789012345678901234\"\n  issuer: \"test-issuer\"\n  ttlSeconds: 60\n"
        "logging:\n  level: \"WARN\"\n  path: \"\"\n";
#else
#error "No APP_CONFIG_* define set"
#endif  // APP_CONFIG format selection (JSON / TOML / XML / YAML)

    infrastructure::writeTextFile(path, content);

    auto store = config::createActiveStore();
    std::vector<std::string> missing;
    const config::AppSettings loaded = store->load(path, &missing);

    EXPECT_TRUE(hasMissing(missing, "database.key"));
    EXPECT_EQ(loaded.databaseKey, config::AppSettings::defaults().databaseKey);

    store->save(path, loaded);

    std::vector<std::string> missingAfter;
    store->load(path, &missingAfter);

    EXPECT_TRUE(missingAfter.empty());

    std::remove(path.c_str());
}
#endif  // APP_DATABASE_POSTGRESQL

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

TEST_CASE("config: missing leaves are recorded and completed")
{
    // server + database only: jwt/logging (/swagger) fall back to defaults.
    const std::string path = tmpPath("partial.conf");
    std::string content;

#ifdef APP_CONFIG_JSON
#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
    content =
        R"({"server": {"host": "127.0.0.1", "port": 8080}, "database": {"path": ":memory:", "key": "test-key-123"}})";
#else
    content = R"({"server": {"host": "127.0.0.1", "port": 8080}, "database": {"path": ":memory:"}})";
#endif  // backend key requirement
#elif defined(APP_CONFIG_TOML)
    content = "[server]\nhost = \"127.0.0.1\"\nport = 8080\n[database]\npath = \":memory:\"\n";
#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
    content += "key = \"test-key-123\"\n";
#endif  // backend key requirement
#elif defined(APP_CONFIG_XML)
    content =
        "<app><server><host>127.0.0.1</host><port>8080</port></server><database><path>:memory:</"
        "path>";
#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
    content += "<key>test-key-123</key>";
#endif  // backend key requirement
    content += "</database></app>";
#elif defined(APP_CONFIG_YAML)
    content = "server:\n  host: \"127.0.0.1\"\n  port: 8080\ndatabase:\n  path: \":memory:\"\n";
#if defined(APP_USER_DATABASE_ENCRYPTED) || defined(APP_DATABASE_POSTGRESQL)
    content += "  key: \"test-key-123\"\n";
#endif  // backend key requirement
#else
#error "No APP_CONFIG_* define set"
#endif  // APP_CONFIG format selection (JSON / TOML / XML / YAML)

    infrastructure::writeTextFile(path, content);

    auto store = config::createActiveStore();
    std::vector<std::string> missing;
    const config::AppSettings loaded = store->load(path, &missing);

    EXPECT_TRUE(hasMissing(missing, "jwt.secret"));
    EXPECT_TRUE(hasMissing(missing, "jwt.issuer"));
    EXPECT_TRUE(hasMissing(missing, "jwt.ttlSeconds"));
    EXPECT_TRUE(hasMissing(missing, "logging.level"));
    EXPECT_TRUE(hasMissing(missing, "logging.path"));
    EXPECT_TRUE(!hasMissing(missing, "server.host"));
    EXPECT_TRUE(!hasMissing(missing, "database.path"));

#ifdef APP_DATABASE_POSTGRESQL
    EXPECT_TRUE(hasMissing(missing, "database.port"));
    EXPECT_TRUE(hasMissing(missing, "database.name"));
    EXPECT_TRUE(hasMissing(missing, "database.user"));
#endif  // APP_DATABASE_POSTGRESQL

#ifdef APP_WITH_SWAGGER
    EXPECT_TRUE(hasMissing(missing, "swagger.resPath"));
#endif  // APP_WITH_SWAGGER

    EXPECT_EQ(loaded.jwtSecret, config::AppSettings::defaults().jwtSecret);

    // Completing the file clears the report and keeps the values.
    store->save(path, loaded);

    std::vector<std::string> missingAfter;
    const config::AppSettings reloaded = store->load(path, &missingAfter);

    EXPECT_TRUE(missingAfter.empty());
    EXPECT_EQ(reloaded.jwtSecret, loaded.jwtSecret);
    EXPECT_EQ(reloaded.logPath, loaded.logPath);

    std::remove(path.c_str());
}

TEST_CASE("config: legacy logging.filePath is rejected")
{
    const std::string path = tmpPath("oldlog.conf");
    std::string content;

#ifdef APP_CONFIG_JSON
    content =
        R"({"server": {"host": "127.0.0.1", "port": 8080}, "database": {"path": ":memory:"}, )"
        R"("jwt": {"secret": "test-secret-123456789012345678901234", "issuer": "test-issuer", "ttlSeconds": 60}, )"
        R"("logging": {"level": "WARN", "filePath": ""}})";
#elif defined(APP_CONFIG_TOML)
    content =
        "[server]\nhost = \"127.0.0.1\"\nport = 8080\n"
        "[database]\npath = \":memory:\"\n"
        "[jwt]\nsecret = \"test-secret-123456789012345678901234\"\nissuer = \"test-issuer\"\nttlSeconds = 60\n"
        "[logging]\nlevel = \"WARN\"\nfilePath = \"\"\n";
#elif defined(APP_CONFIG_XML)
    content =
        "<app><server><host>127.0.0.1</host><port>8080</port></server>"
        "<database><path>:memory:</path></database>"
        "<jwt><secret>test-secret-123456789012345678901234</secret><issuer>test-issuer</issuer>"
        "<ttlSeconds>60</ttlSeconds></jwt>"
        "<logging><level>WARN</level><filePath></filePath></logging></app>";
#elif defined(APP_CONFIG_YAML)
    content =
        "server:\n  host: \"127.0.0.1\"\n  port: 8080\n"
        "database:\n  path: \":memory:\"\n"
        "jwt:\n  secret: \"test-secret-123456789012345678901234\"\n  issuer: \"test-issuer\"\n  ttlSeconds: 60\n"
        "logging:\n  level: \"WARN\"\n  filePath: \"\"\n";
#else
#error "No APP_CONFIG_* define set"
#endif  // APP_CONFIG format selection (JSON / TOML / XML / YAML)

    infrastructure::writeTextFile(path, content);

    auto store = config::createActiveStore();

    EXPECT_THROW_AS(store->load(path), errors::config::Invalid);

    std::remove(path.c_str());
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
