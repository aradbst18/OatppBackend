#include "app/config/RuntimeFilePath.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "app/errors/AppException.hpp"

namespace app::config
{
namespace
{
std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c)
                   {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}
}  // namespace

std::string resolveRuntimeFilePath(const std::string& path)
{
    const std::filesystem::path requested(path);
    const auto stem = lower(requested.stem().string());
    if (stem != "app" && stem != "version")
    {
        return path;
    }
    const auto ext = lower(requested.extension().string());
    if (ext.empty() || ext == ".")
    {
        return path;
    }
    const auto parent = requested.has_parent_path() ? requested.parent_path() : std::filesystem::path(".");
    const auto filename = lower(requested.filename().string());
    std::error_code ec;
    if (!std::filesystem::is_directory(parent, ec) || ec)
    {
        return path;
    }
    std::string found;
    std::filesystem::directory_iterator it(parent, ec);
    if (ec)
    {
        return path;
    }
    const std::filesystem::directory_iterator end;
    for (; it != end && !ec; it.increment(ec))
    {
        const auto& entry = *it;
        std::error_code entryEc;
        if (!entry.is_regular_file(entryEc) || entryEc)
        {
            continue;
        }
        if (lower(entry.path().filename().string()) == filename)
        {
            if (!found.empty())
            {
                throw errors::config::Invalid("Ambiguous case variants of runtime file: " + path);
            }
            found = requested.has_parent_path() ? entry.path().string() : entry.path().filename().string();
        }
    }
    if (ec)
    {
        return path;
    }
    return found.empty() ? path : found;
}

std::string defaultRuntimeFilePath(const std::string& stem, const std::string& extension, const std::string& directory)
{
    const std::filesystem::path dir(directory.empty() ? "_configs" : directory);
    const auto selected = resolveRuntimeFilePath((dir / (stem + "." + extension)).string());
    std::error_code ec;
    if (std::filesystem::is_regular_file(selected, ec) && !ec)
    {
        return selected;
    }
    const auto legacy = resolveRuntimeFilePath((dir / (stem + ".conf")).string());
    return (std::filesystem::is_regular_file(legacy, ec) && !ec) ? legacy : selected;
}
}  // namespace app::config
