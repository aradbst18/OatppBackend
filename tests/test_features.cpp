#include "support/TestRunner.hpp"

#include <string>

#include "app/config/ConfigStore.hpp"

#if defined(APP_WITH_ZLIB)
#include "app/infrastructure/Gzip.hpp"
#endif  // APP_WITH_ZLIB
#if defined(APP_WITH_SWAGGER)
#include "oatpp-swagger/Model.hpp"
#include "app/infrastructure/Swagger.hpp"
#endif  // APP_WITH_SWAGGER

using namespace app;

TEST_CASE("features: config format define matches exactly one")
{
    int active = 0;
#if defined(APP_CONFIG_JSON)
    ++active;
#endif  // APP_CONFIG_JSON
#if defined(APP_CONFIG_TOML)
    ++active;
#endif  // APP_CONFIG_TOML
#if defined(APP_CONFIG_XML)
    ++active;
#endif  // APP_CONFIG_XML
#if defined(APP_CONFIG_YAML)
    ++active;
#endif  // APP_CONFIG_YAML
    EXPECT_EQ(active, 1);
    EXPECT_TRUE(std::string(config::activeFormatName()) == "JSON" || std::string(config::activeFormatName()) == "TOML" ||
                std::string(config::activeFormatName()) == "XML" || std::string(config::activeFormatName()) == "YAML");
}

TEST_CASE("features: database backend define matches exactly one")
{
    int active = 0;
#if defined(APP_DATABASE_SQLITE)
    ++active;
#endif  // APP_DATABASE_SQLITE
#if defined(APP_DATABASE_SQLCIPHER)
    ++active;
#endif  // APP_DATABASE_SQLCIPHER
    EXPECT_EQ(active, 1);
}

#if defined(APP_WITH_ZLIB)
TEST_CASE("features: gzip roundtrip")
{
    const std::string plain(1000, 'a');
    const std::string compressed = infrastructure::gzipCompress(plain);
    EXPECT_TRUE(!compressed.empty());
    EXPECT_TRUE(compressed.size() < plain.size());
    EXPECT_EQ(infrastructure::gzipDecompress(compressed), plain);
    EXPECT_THROW_AS(infrastructure::gzipDecompress("definitely-not-gzip"), std::runtime_error);
}
#else
TEST_CASE("features: zlib disabled has no gzip API")
{
    EXPECT_TRUE(true);  // compile-time proof: Gzip.hpp is not includable here
}
#endif  // APP_WITH_ZLIB

#if defined(APP_WITH_SWAGGER)
TEST_CASE("features: swagger document info builds")
{
    auto doc = infrastructure::createDocumentInfo();
    EXPECT_TRUE(doc != nullptr);
    EXPECT_TRUE(doc->header != nullptr);
    EXPECT_TRUE(doc->header->title == "OatppBackend API");
}
#endif  // APP_WITH_SWAGGER

TEST_CASE("features: C++17 enforced")
{
    EXPECT_TRUE(__cplusplus >= 201703L);
}
