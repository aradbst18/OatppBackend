# Configuration & 4-Store Parity Rules for Agents

This document defines the rules for modifying, adding, and validating configuration settings across the OatppBackend repository.

---

## 1. Supported Formats & Compile-Time Selection

The application supports four configuration formats:
- **JSON** (`JsonConfigStore`) via `jsoncpp`
- **TOML** (`TomlConfigStore`) via `toml11`
- **XML** (`XmlConfigStore`) via `pugixml`
- **YAML** (`YamlConfigStore`) via `yaml-cpp`

One format is selected at compile time using `APP_CONFIG_FORMAT` (`JSON`, `TOML`, `XML`, or `YAML`), defining `APP_CONFIG_<FORMAT>`.

---

## 2. The 4-Store Parity Rule

When any new setting or field is added to `app::config::AppSettings`:

1. **Update `AppSettings.hpp`:** Add the member variable with its appropriate C++ type.
2. **Update `AppSettings.cpp`:**
   - Define its default value in `AppSettings::defaults()`.
   - Add range/format validation logic to `AppSettings::validate()`.
3. **Update All 4 Config Stores:**
   - `src/config/JsonConfigStore.cpp`
   - `src/config/TomlConfigStore.cpp`
   - `src/config/XmlConfigStore.cpp`
   - `src/config/YamlConfigStore.cpp`
   Each store must serialize and deserialize the field using the identical leaf key path.
4. **Update Sample Configuration Files:**
   - `configs/app.json`
   - `configs/app.toml`
   - `configs/app.xml`
   - `configs/app.yaml`
5. **Update Test Cases:**
   - Add roundtrip verification in `tests/test_config.cpp` across all four parser test suites.

---

## 3. Fallback & Auto-Completion Semantics

- **Missing Configuration File:** If `configs/app.conf` is missing on application startup, the application creates the file populated with values from `AppSettings::defaults()`.
- **Missing Leaf Completion:** If an existing configuration file is missing one or more optional fields, the loader must:
  1. Record the missing keys in the `missingLeaves` output list.
  2. Populate the setting with its compiled default value.
  3. Resave the completed configuration file back to disk so future loads are clean.
- **Legacy Key Rejection:** Renamed or deprecated keys (e.g., legacy `filePath` in logging) must be rejected with `errors::config::Invalid` to prevent ambiguous behavior.
