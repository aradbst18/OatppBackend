#pragma once

#include <string>

namespace app::infrastructure
{

/// Minimal file helpers kept separate from parsing so that format parsers
/// never touch the filesystem directly (testable, mockable).
std::string readTextFile(const std::string& path);
void writeTextFile(const std::string& path, const std::string& content);
bool fileExists(const std::string& path);
void ensureParentDirectory(const std::string& path);

}  // namespace app::infrastructure
