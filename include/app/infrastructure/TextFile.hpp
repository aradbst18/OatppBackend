#pragma once

#include <string>

namespace app::infrastructure
{

/// Minimal file helpers kept separate from parsing so that format parsers
/// never touch the filesystem directly (testable, mockable).
std::string readTextFile(const std::string& path);
void writeTextFile(const std::string& path, const std::string& content);
/// Atomically replace a file with owner-only permissions (0600), without
/// exposing partial content or following a destination symlink.
/// When overwrite is false, an existing destination is left untouched.
/// Syncs staged contents, the containing directory and newly created parent entries;
/// sync failures throw. Established, unchanged ancestors need only search access.
void writePrivateTextFile(const std::string& path, const std::string& content, bool overwrite = true);
/// Complete pending parent-directory creation and sync the file's containing
/// directory. Existing directories without a trusted marker are the durable
/// boundary; foreign or invalid entries are left alone. Also used when another
/// startup published the file.
void syncParentDirectories(const std::string& path);
bool fileExists(const std::string& path);
void ensureParentDirectory(const std::string& path);

}  // namespace app::infrastructure
