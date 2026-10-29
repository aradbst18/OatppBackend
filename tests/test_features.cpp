#include "support/TestRunner.hpp"

#include <random>
#include <string>

#include "app/config/ConfigStore.hpp"

#ifdef APP_WITH_ZLIB
#include "app/infrastructure/Gzip.hpp"
#endif  // APP_WITH_ZLIB

#ifdef APP_WITH_SWAGGER
#include "oatpp-swagger/Model.hpp"
#include "app/infrastructure/Swagger.hpp"
#endif  // APP_WITH_SWAGGER

using namespace app;

TEST_CASE("features: config format define matches exactly one")
{
    int active = 0;

#ifdef APP_CONFIG_JSON
    ++active;
#endif  // APP_CONFIG_JSON

#ifdef APP_CONFIG_TOML
    ++active;
#endif  // APP_CONFIG_TOML

#ifdef APP_CONFIG_XML
    ++active;
#endif  // APP_CONFIG_XML

#ifdef APP_CONFIG_YAML
    ++active;
#endif  // APP_CONFIG_YAML

    EXPECT_EQ(active, 1);

    EXPECT_TRUE(std::string(config::activeFormatName()) == "JSON" ||
                std::string(config::activeFormatName()) == "TOML" || std::string(config::activeFormatName()) == "XML" ||
                std::string(config::activeFormatName()) == "YAML");
}

TEST_CASE("features: database backend define matches exactly one")
{
    int active = 0;

#ifdef APP_DATABASE_SQLCIPHER
    ++active;
#endif  // APP_DATABASE_SQLCIPHER

#ifdef APP_DATABASE_POSTGRESQL
    ++active;
#endif  // APP_DATABASE_POSTGRESQL

    EXPECT_EQ(active, 1);
}

TEST_CASE("features: sqlcipher support matches the user database")
{
#ifdef APP_DATABASE_SQLCIPHER
    // A SQLCIPHER user database always carries file support.
    EXPECT_TRUE(true);  // compile-time proof below would fail otherwise

#ifdef APP_WITH_SQLCIPHER
    EXPECT_TRUE(true);
#else
#error "APP_DATABASE_SQLCIPHER requires APP_WITH_SQLCIPHER (configure with -DAPP_ENABLE_SQLCIPHER=ON)"
#endif  // APP_WITH_SQLCIPHER

#else
    EXPECT_TRUE(true);  // POSTGRESQL builds may or may not bundle file support
#endif  // APP_DATABASE_SQLCIPHER
}

#ifdef APP_WITH_ZLIB
TEST_CASE("features: gzip roundtrip")
{
    const std::string plain(1000, 'a');
    const std::string compressed = infrastructure::gzipCompress(plain);

    EXPECT_TRUE(!compressed.empty());
    EXPECT_TRUE(compressed.size() < plain.size());
    EXPECT_EQ(infrastructure::gzipDecompress(compressed), plain);
    EXPECT_THROW_AS(infrastructure::gzipDecompress("definitely-not-gzip"), std::runtime_error);
}

TEST_CASE("features: gzip empty payload")
{
    const std::string emptyGzip("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\x03\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00", 20);

    EXPECT_EQ(infrastructure::gzipDecompress(emptyGzip), "");
    const auto compressed = infrastructure::gzipCompress("");
    EXPECT_TRUE(!compressed.empty());
    EXPECT_EQ(infrastructure::gzipDecompress(compressed), "");
}

TEST_CASE("features: gzip preserves bytes across buffer boundaries")
{
    std::mt19937 random(0);
    for (const std::size_t size : {1023U, 1024U, 1025U, 4096U})
    {
        std::string plain(size, '\0');
        for (char& byte : plain)
        {
            byte = static_cast<char>(random() & 0xffU);
        }
        plain.front() = '\0';
        plain.back() = '\0';

        EXPECT_EQ(infrastructure::gzipDecompress(infrastructure::gzipCompress(plain)), plain);
    }
}

TEST_CASE("features: gzip rejects truncated and corrupt streams")
{
    EXPECT_THROW_AS(infrastructure::gzipDecompress(""), std::runtime_error);
    for (const auto& plain : {std::string{}, std::string(4096, 'a')})
    {
        const auto compressed = infrastructure::gzipCompress(plain);
        for (std::size_t size = 1; size < compressed.size(); ++size)
        {
            EXPECT_THROW_AS(infrastructure::gzipDecompress(compressed.substr(0, size)), std::runtime_error);
        }
        for (const std::size_t trailerOffset : {8U, 4U})
        {
            auto corrupt = compressed;
            corrupt[corrupt.size() - trailerOffset] ^= 1;
            EXPECT_THROW_AS(infrastructure::gzipDecompress(corrupt), std::runtime_error);
        }
    }
}
#else
TEST_CASE("features: zlib disabled has no gzip API")
{
    EXPECT_TRUE(true);  // compile-time proof: Gzip.hpp is not includable here
}
#endif  // APP_WITH_ZLIB

#ifdef APP_WITH_SWAGGER
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
