#pragma once

#include <string>

namespace app::config
{
/// Resolve case variants of app/version filenames only; reject ambiguous matches.
std::string resolveRuntimeFilePath(const std::string& path);
/// Prefer the selected extension, falling back to an existing legacy .conf file in directory (default _configs).
std::string defaultRuntimeFilePath(const std::string& stem, const std::string& extension,
                                   const std::string& directory = "_configs");
}  // namespace app::config
