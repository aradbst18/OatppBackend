#include "support/TestRunner.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

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

TEST_CASE("version: default path follows the selected format")
{
#if defined(APP_VERSION_FILE_JSON)
    const std::string extension = ".json";
#elif defined(APP_VERSION_FILE_TOML)
    const std::string extension = ".toml";
#elif defined(APP_VERSION_FILE_XML)
    const std::string extension = ".xml";
#elif defined(APP_VERSION_FILE_YAML)
    const std::string extension = ".yaml";
#endif  // APP_VERSION_FILE format selection (JSON / TOML / XML / YAML)
    const std::filesystem::path path(config::defaultVersionFilePath());
    EXPECT_EQ(path.parent_path().string(), std::string("_configs"));
    EXPECT_TRUE(path.extension() == extension || path.extension() == ".conf");

    const std::filesystem::path customDir = "/tmp/opencode/custom-version-dir";
    const std::filesystem::path customPath(config::defaultVersionFilePath(customDir.string()));
    EXPECT_EQ(customPath.parent_path().string(), customDir.string());
    EXPECT_TRUE(customPath.extension() == extension || customPath.extension() == ".conf");
}

TEST_CASE("version: existing case variants are validated and updated in place")
{
    const std::filesystem::path directory = "/tmp/opencode/runtime-version-case";
    std::filesystem::remove_all(directory);
    const auto upper = (directory / "VERSION.CONF").string();
    const auto lower = (directory / "version.conf").string();
    config::writeVersionFile(upper, {"1.2.3", "2026-01-02", "01:02:03"});
    EXPECT_TRUE(std::filesystem::exists(upper));
    EXPECT_FALSE(std::filesystem::exists(lower));
    EXPECT_EQ(config::readVersionFile(lower).version, std::string("1.2.3"));
    EXPECT_TRUE(config::ensureVersionFile(lower).empty());
    EXPECT_EQ(config::readVersionFile(upper).version, std::string(APP_VERSION));
    EXPECT_FALSE(std::filesystem::exists(lower));
    std::filesystem::remove_all(directory);
}

TEST_CASE("version: version file format define matches exactly one")
{
    int active = 0;

#ifdef APP_VERSION_FILE_JSON
    ++active;
#endif  // APP_VERSION_FILE_JSON

#ifdef APP_VERSION_FILE_TOML
    ++active;
#endif  // APP_VERSION_FILE_TOML

#ifdef APP_VERSION_FILE_XML
    ++active;
#endif  // APP_VERSION_FILE_XML

#ifdef APP_VERSION_FILE_YAML
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

TEST_CASE("version: replacement preserves an already-open reader's snapshot")
{
    const std::string path = tmpPath("version-snapshot.conf");
    const config::VersionInfo previous{"1.2.3", "2026-01-02", "01:02:03"};
    const config::VersionInfo replacement{"12.34.56", "2026-03-04", "04:05:06"};
    config::writeVersionFile(path, previous);
    const std::string original = infrastructure::readTextFile(path);
    std::ifstream reader(path, std::ios::binary);
    EXPECT_TRUE(reader.is_open());

    config::writeVersionFile(path, replacement);

    std::ostringstream snapshot;
    snapshot << reader.rdbuf();
    const config::VersionInfo current = config::readVersionFile(path);
    std::remove(path.c_str());

    EXPECT_EQ(snapshot.str(), original);
    EXPECT_EQ(current.version, replacement.version);
    EXPECT_EQ(current.releaseDate, replacement.releaseDate);
    EXPECT_EQ(current.startupTime, replacement.startupTime);
}

TEST_CASE("version: concurrent startup publishes complete files")
{
    for (const bool exists : {false, true})
    {
        const std::string path = tmpPath(exists ? "version-existing.conf" : "version-missing.conf");
        std::remove(path.c_str());
        if (exists)
        {
            config::writeVersionFile(path, config::currentVersionInfo());
        }

        std::promise<void> start;
        const auto ready = start.get_future().share();
        std::vector<std::future<void>> callers;
        for (int i = 0; i < 12; ++i)
        {
            callers.push_back(std::async(std::launch::async,
                                         [&, ready]()
                                         {
                                             ready.wait();
                                             for (int call = 0; call < 50; ++call)
                                             {
                                                 EXPECT_TRUE(config::ensureVersionFile(path).empty());
                                             }
                                         }));
        }
        start.set_value();
        for (auto& caller : callers)
        {
            caller.get();
        }

        std::vector<std::string> missing;
        const config::VersionInfo current = config::readVersionFile(path, &missing);
        std::remove(path.c_str());
        EXPECT_TRUE(missing.empty());
        EXPECT_EQ(current.version, std::string(APP_VERSION));
        EXPECT_EQ(current.releaseDate, std::string(APP_RELEASE_DATE));
    }
}
