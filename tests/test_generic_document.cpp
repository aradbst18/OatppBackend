#include "support/TestRunner.hpp"

#include <cstdio>
#include <string>

#include "app/auth/PasswordHash.hpp"
#include "app/errors/AppException.hpp"
#include "app/infrastructure/database/DatabaseFactory.hpp"
#include "app/infrastructure/documents/DatabaseDocument.hpp"
#include "app/infrastructure/documents/DocumentValue.hpp"
#include "app/repositories/UserRepository.hpp"

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

using namespace app;

namespace
{

std::string tmpPath(const char* name)
{
    return std::string("/tmp/opencode/oatpp-backend-") + name;
}

infrastructure::documents::DocumentValue sampleDocument()
{
    infrastructure::documents::DocumentValue address = infrastructure::documents::DocumentValue::makeMap();
    address["host"] = std::string("127.0.0.1");
    address["port"] = static_cast<std::int64_t>(8080);
    address["tls"] = true;
    address["ratio"] = 1.5;

    infrastructure::documents::DocumentValue tags = infrastructure::documents::DocumentValue::makeList();
    tags.asList().push_back(infrastructure::documents::DocumentValue("a"));
    tags.asList().push_back(infrastructure::documents::DocumentValue(static_cast<std::int64_t>(2)));

    infrastructure::documents::DocumentValue root = infrastructure::documents::DocumentValue::makeMap();
    root["address"] = std::move(address);
    root["tags"] = std::move(tags);
    root["empty"] = std::string("");
    return root;
}

}  // namespace

TEST_CASE("generic: value core types and accessors")
{
    infrastructure::documents::DocumentValue null;
    EXPECT_TRUE(null.isNull());
    EXPECT_TRUE(null.empty());

    infrastructure::documents::DocumentValue flag(true);
    EXPECT_TRUE(flag.isBool());
    EXPECT_TRUE(flag.asBool());

    infrastructure::documents::DocumentValue number(static_cast<std::int64_t>(42));
    EXPECT_TRUE(number.isInt());
    EXPECT_TRUE(number.isNumber());
    EXPECT_EQ(number.asInt(), static_cast<std::int64_t>(42));
    EXPECT_EQ(number.asDouble(), 42.0);

    infrastructure::documents::DocumentValue text("hi");
    EXPECT_TRUE(text.isString());
    EXPECT_EQ(text.asString(), std::string("hi"));

    infrastructure::documents::DocumentValue map = infrastructure::documents::DocumentValue::makeMap();
    EXPECT_TRUE(map.isMap());
    EXPECT_TRUE(map.empty());
    map["key"] = std::string("value");
    EXPECT_TRUE(map.contains("key"));
    EXPECT_FALSE(map.contains("missing"));
    EXPECT_EQ(map.at("key").asString(), std::string("value"));
    EXPECT_TRUE(map.find("missing") == nullptr);
    EXPECT_THROW_AS(map.at("missing"), errors::serialization::DeserializationFailed);
    EXPECT_THROW_AS(map.at("key").asInt(), errors::serialization::DeserializationFailed);

    infrastructure::documents::DocumentValue list = infrastructure::documents::DocumentValue::makeList();
    list.asList().push_back(infrastructure::documents::DocumentValue(static_cast<std::int64_t>(1)));
    EXPECT_TRUE(list.isList());
    EXPECT_EQ(list.size(), 1);
}

#ifdef APP_WITH_JSON_PARSER
TEST_CASE("generic: json parses and serializes arbitrary documents")
{
    const infrastructure::documents::DocumentValue root = sampleDocument();
    const std::string text = infrastructure::documents::serializeJson(root);
    const infrastructure::documents::DocumentValue back = infrastructure::documents::parseJson(text, "<test>");
    EXPECT_TRUE(back == root);
    EXPECT_EQ(back.at("address").at("host").asString(), std::string("127.0.0.1"));
    EXPECT_EQ(back.at("address").at("port").asInt(), static_cast<std::int64_t>(8080));
    EXPECT_TRUE(back.at("address").at("tls").asBool());
    EXPECT_EQ(back.at("tags").asList().size(), 2);

    const std::string path = tmpPath("generic.json");
    infrastructure::documents::saveJsonFile(path, root);
    EXPECT_TRUE(infrastructure::documents::loadJsonFile(path) == root);
    std::remove(path.c_str());

    EXPECT_THROW_AS(infrastructure::documents::parseJson("{ not json", "<test>"), errors::serialization::ParsingFailed);
}
#endif  // APP_WITH_JSON_PARSER

#ifdef APP_WITH_TOML_PARSER
TEST_CASE("generic: toml parses and serializes arbitrary documents")
{
    const infrastructure::documents::DocumentValue root = sampleDocument();
    const std::string text = infrastructure::documents::serializeToml(root);
    const infrastructure::documents::DocumentValue back = infrastructure::documents::parseToml(text, "<test>");
    EXPECT_TRUE(back == root);

    const std::string path = tmpPath("generic.toml");
    infrastructure::documents::saveTomlFile(path, root);
    EXPECT_TRUE(infrastructure::documents::loadTomlFile(path) == root);
    std::remove(path.c_str());

    EXPECT_THROW_AS(infrastructure::documents::parseToml("port = [unclosed", "<test>"), errors::serialization::ParsingFailed);
}
#endif  // APP_WITH_TOML_PARSER

#ifdef APP_WITH_XML_PARSER
TEST_CASE("generic: xml parses and serializes arbitrary documents")
{
    infrastructure::documents::DocumentValue inner = infrastructure::documents::DocumentValue::makeMap();
    inner["host"] = std::string("127.0.0.1");
    inner["port"] = std::string("8080");
    infrastructure::documents::DocumentValue document = infrastructure::documents::DocumentValue::makeMap();
    document["server"] = std::move(inner);

    const std::string text = infrastructure::documents::serializeXml(document);
    const infrastructure::documents::DocumentValue back = infrastructure::documents::parseXml(text, "<test>");
    EXPECT_TRUE(back == document);
    EXPECT_EQ(back.at("server").at("host").asString(), std::string("127.0.0.1"));

    const std::string path = tmpPath("generic.xml");
    infrastructure::documents::saveXmlFile(path, document);
    EXPECT_TRUE(infrastructure::documents::loadXmlFile(path) == document);
    std::remove(path.c_str());

    EXPECT_THROW_AS(infrastructure::documents::parseXml("<app><port>oops", "<test>"), errors::serialization::ParsingFailed);
}
#endif  // APP_WITH_XML_PARSER

#ifdef APP_WITH_YAML_PARSER
TEST_CASE("generic: yaml parses and serializes arbitrary documents")
{
    const infrastructure::documents::DocumentValue root = sampleDocument();
    const std::string text = infrastructure::documents::serializeYaml(root);
    const infrastructure::documents::DocumentValue back = infrastructure::documents::parseYaml(text, "<test>");
    EXPECT_TRUE(back == root);
    EXPECT_EQ(back.at("empty").asString(), std::string(""));

    const std::string path = tmpPath("generic.yaml");
    infrastructure::documents::saveYamlFile(path, root);
    EXPECT_TRUE(infrastructure::documents::loadYamlFile(path) == root);
    std::remove(path.c_str());

    EXPECT_THROW_AS(infrastructure::documents::parseYaml("host: [unclosed\n  bad indent: :", "<test>"), errors::serialization::ParsingFailed);
}
#endif  // APP_WITH_YAML_PARSER

TEST_CASE("generic: database inspector dumps arbitrary database files")
{
    const std::string path = tmpPath("generic-inspect.db");
    std::remove(path.c_str());

#if defined(APP_DATABASE_SQLCIPHER)
    const std::string key = "inspect-key-123";
#else
    const std::string key = "";
#endif  // APP_DATABASE_SQLCIPHER

    {
#if defined(APP_DATABASE_SQLCIPHER)
        auto db = infrastructure::database::DatabaseFactory::create(path, key);
#else
        auto db = infrastructure::database::DatabaseFactory::create(path);
#endif  // APP_DATABASE_SQLCIPHER
        db->open();
        repositories::UserRepository repo(db);
        repo.ensureSchema();
        repo.upsert({"alice", auth::passwordHashHex("Alice1234"), "operator"});
        db->close();
    }

    const auto tables = infrastructure::documents::listDatabaseTables(path, key);
    EXPECT_EQ(tables.size(), 1);
    EXPECT_EQ(tables[0], std::string("users"));

    const std::string schema = infrastructure::documents::databaseTableSchema(path, "users", key);
    EXPECT_TRUE(schema.find("CREATE TABLE") != std::string::npos);

    const infrastructure::documents::DocumentValue table = infrastructure::documents::dumpDatabaseTable(path, "users", key);
    EXPECT_EQ(table.at("rows").asList().size(), 1);
    EXPECT_EQ(table.at("rows").asList()[0].at("username").asString(), std::string("alice"));
    EXPECT_EQ(table.at("rows").asList()[0].at("role").asString(), std::string("operator"));

    const infrastructure::documents::DocumentValue full = infrastructure::documents::dumpDatabase(path, key);
    EXPECT_TRUE(full.at("tables").contains("users"));
    EXPECT_EQ(full.at("tables").at("users").at("rows").asList().size(), 1);

#ifdef APP_WITH_JSON_PARSER
    // Value interop: a database snapshot serializes like any other document.
    const std::string snap = infrastructure::documents::serializeJson(full);
    EXPECT_TRUE(snap.find("users") != std::string::npos);
    EXPECT_TRUE(snap.find("alice") != std::string::npos);
#endif  // APP_WITH_JSON_PARSER

    EXPECT_THROW_AS(infrastructure::documents::dumpDatabaseTable(path, "no_such_table", key), errors::database::QueryFailed);
    EXPECT_THROW_AS(infrastructure::documents::databaseTableSchema(path, "no_such_table", key), errors::database::QueryFailed);

#if defined(APP_DATABASE_SQLCIPHER)
    EXPECT_THROW_AS(infrastructure::documents::listDatabaseTables(path, ""), errors::database::ConnectionFailed);
#else
    EXPECT_THROW_AS(infrastructure::documents::listDatabaseTables(path, "unexpected-key"), errors::config::Invalid);
#endif  // APP_DATABASE_SQLCIPHER

    std::remove(path.c_str());
}
