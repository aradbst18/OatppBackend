#include "support/TestRunner.hpp"

#include <cstdio>
#include <regex>
#include <string>

#include "app/config/VersionFile.hpp"
#include "app/infrastructure/TextFile.hpp"

using namespace app;

namespace
{

std::string tmpPath(const char* name)
{
    return std::string("/tmp/opencode/oatpp-backend-") + name;
}

}  // namespace

TEST_CASE("version: current info carries build version and dates")
{
    const config::VersionInfo info = config::currentVersionInfo();
    EXPECT_EQ(info.version, std::string(APP_VERSION));
    EXPECT_EQ(info.releaseDate, std::string(APP_RELEASE_DATE));
    EXPECT_TRUE(std::regex_match(info.version, std::regex(R"(^\d+\.\d+\.\d+$)")));
    EXPECT_TRUE(std::regex_match(info.releaseDate, std::regex(R"(^\d{4}-\d{2}-\d{2}$)")));
    EXPECT_TRUE(std::regex_match(info.startupTime, std::regex(R"(^\d{2}:\d{2}:\d{2}$)")));
}

TEST_CASE("version: default path is configs/version.conf")
{
    EXPECT_EQ(config::defaultVersionFilePath(), std::string("configs/version.conf"));
}

TEST_CASE("version: version file format define matches exactly one")
{
    int active = 0;
#if defined(APP_VERSION_FILE_JSON)
    ++active;
#endif  // APP_VERSION_FILE_JSON
#if defined(APP_VERSION_FILE_TOML)
    ++active;
#endif  // APP_VERSION_FILE_TOML
#if defined(APP_VERSION_FILE_XML)
    ++active;
#endif  // APP_VERSION_FILE_XML
#if defined(APP_VERSION_FILE_YAML)
    ++active;
#endif  // APP_VERSION_FILE_YAML
    EXPECT_EQ(active, 1);
}

TEST_CASE("version: file contains version, release date and startup time")
{
    const std::string path = tmpPath("version-check.conf");
    const config::VersionInfo info = config::currentVersionInfo();
    config::writeVersionFile(path, info);
    const std::string content = infrastructure::readTextFile(path);
    EXPECT_TRUE(content.find(info.version) != std::string::npos);
    EXPECT_TRUE(content.find(info.releaseDate) != std::string::npos);
    EXPECT_TRUE(content.find(info.startupTime) != std::string::npos);
    std::remove(path.c_str());
}
