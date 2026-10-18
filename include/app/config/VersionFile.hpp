#pragma once

#include <string>
#include <vector>

// Exactly one version file format is active by construction (single-valued
// APP_VERSION_FILE_FORMAT). See ConfigStore.hpp for the rationale: multiple
// APP_VERSION_FILE_* defines are an error, and none means the definitions
// were never applied (e.g. a non-CMake build missing the settings).
#if defined(APP_VERSION_FILE_JSON) + defined(APP_VERSION_FILE_TOML) + defined(APP_VERSION_FILE_XML) + \
        defined(APP_VERSION_FILE_YAML) >                                                              \
    1
#error \
    "APP_VERSION_FILE_* defines are mutually exclusive (define exactly one via -DAPP_VERSION_FILE_FORMAT=JSON|TOML|XML|YAML)"
#elif !defined(APP_VERSION_FILE_JSON) && !defined(APP_VERSION_FILE_TOML) && !defined(APP_VERSION_FILE_XML) && \
    !defined(APP_VERSION_FILE_YAML)
#error "No APP_VERSION_FILE_* define set (configure with -DAPP_VERSION_FILE_FORMAT=JSON|TOML|XML|YAML)"
#endif  // APP_VERSION_FILE format selection (JSON / TOML / XML / YAML)

namespace app::config
{

/// Build + startup identity written to the version file on startup.
/// - version: APP_VERSION (compile-time, e.g. "0.0.1").
/// - releaseDate: APP_RELEASE_DATE (configure-time yyyy-mm-dd build stamp).
/// - startupTime: local time the file was written, HH:MM:SS.
struct VersionInfo
{
    std::string version;
    std::string releaseDate;
    std::string startupTime;
};

/// Snapshot the current version info (startupTime = now, local HH:MM:SS).
VersionInfo currentVersionInfo();

/// Default version file path (`configs/version.conf`). The name is fixed like
/// the main config; only the on-disk syntax follows APP_VERSION_FILE_FORMAT.
std::string defaultVersionFilePath();

/// Write `info` to `path` in the compile-time-selected version file format.
/// The matching APP_WITH_*_PARSER define is required (a mismatch fails at
/// configure time and is re-asserted here).
void writeVersionFile(const std::string& path, const VersionInfo& info);

/// Read + validate an existing version file. Absent fields fall back to the
/// current build info and are recorded in `missing` (dotted names:
/// "version", "releaseDate", "startupTime") so the caller can complete the
/// file. Present-but-wrongly-typed fields and invalid values throw
/// errors::config::Invalid — like the main config, they are the operator's
/// to fix. Throws errors::filesystem::NotFound (or the format's parse
/// error) when the file itself cannot be read.
VersionInfo readVersionFile(const std::string& path, std::vector<std::string>* missing = nullptr);

/// Ensure the version file is complete: create it from the current build
/// info when missing, otherwise read it (validating types/values), fill
/// absent fields, and write the merged result back (version/releaseDate
/// always stamp the current build, startupTime the current time). Returns
/// the names completed from defaults (empty when the file was already
/// complete). Invalid content throws; callers log it as critical.
std::vector<std::string> ensureVersionFile(const std::string& path);

}  // namespace app::config
