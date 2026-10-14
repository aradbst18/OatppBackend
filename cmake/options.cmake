# Feature flags for OatppBackend.
#
# Exposes: APP_ENABLE_ZLIB      ON/OFF   - oatpp-zlib gzip/deflate support APP_ENABLE_SWAGGER ON/OFF - oatpp-swagger OpenAPI documentation
# APP_BUILD_TESTS      ON/OFF   - build the test suite (skip to save time) VERBOSE_BUILD_SUMMARY ON/OFF - extra per-dependency details in summary
# ENABLE_SANITIZERS  ON/OFF   - master sanitizer switch (the build configuration selects the set; see cmake/sanitizers.cmake) APP_DATABASE_BACKEND
# SQLITE/SQLCIPHER - database backend selection APP_PASSWORD_HASH_ALGO SHA256/SHA384/SHA512 - user password hash algorithm APP_CONFIG_FORMAT
# JSON/TOML/XML/YAML - primary application config format APP_VERSION x.y.z    - application version (sets PROJECT_VERSION) APP_RELEASE_DATE yyyy-mm-dd
# (empty = configure date) - release date APP_VERSION_FILE_FORMAT JSON/TOML/XML/YAML - version file format APP_ENABLE_JSON_PARSER ON/OFF - compile +
# link the JSON config parser APP_ENABLE_TOML_PARSER ON/OFF - compile + link the TOML config parser APP_ENABLE_XML_PARSER  ON/OFF - compile + link the
# XML config parser APP_ENABLE_YAML_PARSER ON/OFF - compile + link the YAML config parser
#
# And helper: apply_app_definitions(<target>)

option(APP_ENABLE_ZLIB "Enable oatpp-zlib compression support" ON)
option(APP_ENABLE_SWAGGER "Enable oatpp-swagger OpenAPI documentation" ON)
option(APP_BUILD_TESTS "Build the test suite (app_tests)" ON)
option(VERBOSE_BUILD_SUMMARY "Print per-dependency paths, versions and link types in the build summary" OFF)
option(ENABLE_SANITIZERS "Master sanitizer switch; the selected build configuration determines the sanitizer set (see cmake/sanitizers.cmake)" ON)

option(APP_ENABLE_JSON_PARSER "Enable the JSON config parser (jsoncpp)" ON)
option(APP_ENABLE_TOML_PARSER "Enable the TOML config parser (toml11)" ON)
option(APP_ENABLE_XML_PARSER "Enable the XML config parser (pugixml)" ON)
option(APP_ENABLE_YAML_PARSER "Enable the YAML config parser (yaml-cpp)" ON)

set(APP_DATABASE_BACKEND
    "SQLITE"
    CACHE STRING "Database backend (SQLITE or SQLCIPHER)")
set_property(CACHE APP_DATABASE_BACKEND PROPERTY STRINGS SQLITE SQLCIPHER)

set(APP_PASSWORD_HASH_ALGO
    "SHA256"
    CACHE STRING "User password hash algorithm (SHA256, SHA384 or SHA512)")
set_property(CACHE APP_PASSWORD_HASH_ALGO PROPERTY STRINGS SHA256 SHA384 SHA512)

set(APP_CONFIG_FORMAT
    "JSON"
    CACHE STRING "Primary config format (JSON, TOML, XML or YAML)")
set_property(CACHE APP_CONFIG_FORMAT PROPERTY STRINGS JSON TOML XML YAML)

# Release date (yyyy-mm-dd): empty means "stamp at configure time" (i.e. the build date). A provided value must already be a valid yyyy-mm-dd date.
set(APP_RELEASE_DATE
    ""
    CACHE STRING "Release date (yyyy-mm-dd, empty = configure date)")
if(APP_RELEASE_DATE STREQUAL "")
    string(TIMESTAMP APP_RELEASE_DATE "%Y-%m-%d")
    set(APP_RELEASE_DATE
        "${APP_RELEASE_DATE}"
        CACHE STRING "Release date (yyyy-mm-dd)" FORCE)
endif()

set(APP_VERSION_FILE_FORMAT
    "JSON"
    CACHE STRING "Version file format (JSON, TOML, XML or YAML)")
set_property(CACHE APP_VERSION_FILE_FORMAT PROPERTY STRINGS JSON TOML XML YAML)

# Case-insensitive string options: normalize to uppercase so any casing ("JSOn", "SQLite", ...) configures the same build. Written back to the cache
# only when normalization changes the value, preserving each variable's help string, so the summary shows the canonical value.
foreach(_var IN ITEMS APP_DATABASE_BACKEND APP_PASSWORD_HASH_ALGO APP_CONFIG_FORMAT APP_VERSION_FILE_FORMAT)
    string(TOUPPER "${${_var}}" _normalized)
    if(NOT "${${_var}}" STREQUAL "${_normalized}")
        get_property(
            _doc
            CACHE ${_var}
            PROPERTY HELPSTRING)
        set(${_var}
            "${_normalized}"
            CACHE STRING "${_doc}" FORCE)
    endif()
endforeach()
unset(_var)
unset(_normalized)
unset(_doc)

# Boolean canonicalization: accept ON/OFF (any case), TRUE/FALSE, YES/NO, Y/N and 1/0 (false = OFF = 0, true = ON = 1) and store canonical ON/OFF back
# to the cache, so the summary and later configures show one spelling. Anything outside that vocabulary fails fast instead of silently evaluating
# truthy the way a bare if() would.
foreach(
    _var IN
    ITEMS CMAKE_EXPORT_COMPILE_COMMANDS
          APP_ENABLE_ZLIB
          APP_ENABLE_SWAGGER
          APP_BUILD_TESTS
          VERBOSE_BUILD_SUMMARY
          ENABLE_SANITIZERS
          APP_ENABLE_JSON_PARSER
          APP_ENABLE_TOML_PARSER
          APP_ENABLE_XML_PARSER
          APP_ENABLE_YAML_PARSER)
    if(DEFINED ${_var} AND DEFINED CACHE{${_var}})
        string(TOUPPER "${${_var}}" _upper)
        if(_upper STREQUAL "ON"
           OR _upper STREQUAL "TRUE"
           OR _upper STREQUAL "YES"
           OR _upper STREQUAL "Y"
           OR _upper STREQUAL "1")
            set(_canonical "ON")
        elseif(
            _upper STREQUAL "OFF"
            OR _upper STREQUAL "FALSE"
            OR _upper STREQUAL "NO"
            OR _upper STREQUAL "N"
            OR _upper STREQUAL "0"
            OR _upper STREQUAL "")
            set(_canonical "OFF")
        else()
            message(FATAL_ERROR "${_var} must be a boolean (ON/OFF, TRUE/FALSE, YES/NO, Y/N or 1/0, any case; got '${${_var}}')")
        endif()
        if(NOT "${${_var}}" STREQUAL "${_canonical}")
            get_property(
                _doc
                CACHE ${_var}
                PROPERTY HELPSTRING)
            get_property(
                _type
                CACHE ${_var}
                PROPERTY TYPE)
            set(${_var}
                "${_canonical}"
                CACHE ${_type} "${_doc}" FORCE)
        endif()
    endif()
endforeach()
unset(_var)
unset(_upper)
unset(_canonical)
unset(_doc)
unset(_type)

# --- Validation (fail fast on invalid combinations) ---
# Explicit SQLITE branch (plaintext SQLite backend) or Explicit SQLCIPHER branch (encrypted SQLCipher backend).
if(NOT APP_DATABASE_BACKEND STREQUAL "SQLITE" AND NOT APP_DATABASE_BACKEND STREQUAL "SQLCIPHER")
    message(FATAL_ERROR "APP_DATABASE_BACKEND must be SQLITE or SQLCIPHER (got '${APP_DATABASE_BACKEND}')")
endif()

if(NOT APP_PASSWORD_HASH_ALGO STREQUAL "SHA256"
   AND NOT APP_PASSWORD_HASH_ALGO STREQUAL "SHA384"
   AND NOT APP_PASSWORD_HASH_ALGO STREQUAL "SHA512")
    message(FATAL_ERROR "APP_PASSWORD_HASH_ALGO must be SHA256, SHA384 or SHA512 (got '${APP_PASSWORD_HASH_ALGO}')")
endif()

# At least one config parser must be enabled: with all four OFF there is no way to load or save any config file, so fail at configure time (not at
# build).
if(NOT APP_ENABLE_JSON_PARSER
   AND NOT APP_ENABLE_TOML_PARSER
   AND NOT APP_ENABLE_XML_PARSER
   AND NOT APP_ENABLE_YAML_PARSER)
    message(FATAL_ERROR "At least one config parser must be enabled (APP_ENABLE_JSON/TOML/XML/YAML_PARSER); all four are OFF")
endif()

if(NOT APP_CONFIG_FORMAT STREQUAL "JSON"
   AND NOT APP_CONFIG_FORMAT STREQUAL "TOML"
   AND NOT APP_CONFIG_FORMAT STREQUAL "XML"
   AND NOT APP_CONFIG_FORMAT STREQUAL "YAML")
    message(FATAL_ERROR "APP_CONFIG_FORMAT must be JSON, TOML, XML or YAML (got '${APP_CONFIG_FORMAT}')")
endif()

# The active format's parser must be enabled: a disabled parser means its dependency is never searched or linked, so selecting its format is a
# configuration error (fail here, not at some obscure compile error).
if(APP_CONFIG_FORMAT STREQUAL "JSON" AND NOT APP_ENABLE_JSON_PARSER)
    message(FATAL_ERROR "APP_CONFIG_FORMAT=JSON requires APP_ENABLE_JSON_PARSER=ON")
endif()

if(APP_CONFIG_FORMAT STREQUAL "TOML" AND NOT APP_ENABLE_TOML_PARSER)
    message(FATAL_ERROR "APP_CONFIG_FORMAT=TOML requires APP_ENABLE_TOML_PARSER=ON")
endif()

if(APP_CONFIG_FORMAT STREQUAL "XML" AND NOT APP_ENABLE_XML_PARSER)
    message(FATAL_ERROR "APP_CONFIG_FORMAT=XML requires APP_ENABLE_XML_PARSER=ON")
endif()

if(APP_CONFIG_FORMAT STREQUAL "YAML" AND NOT APP_ENABLE_YAML_PARSER)
    message(FATAL_ERROR "APP_CONFIG_FORMAT=YAML requires APP_ENABLE_YAML_PARSER=ON")
endif()

if(NOT APP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "APP_VERSION must be numeric major.minor.patch (got '${APP_VERSION}')")
endif()

if(NOT APP_RELEASE_DATE MATCHES "^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]$")
    message(FATAL_ERROR "APP_RELEASE_DATE must be yyyy-mm-dd (got '${APP_RELEASE_DATE}')")
endif()

if(NOT APP_VERSION_FILE_FORMAT STREQUAL "JSON"
   AND NOT APP_VERSION_FILE_FORMAT STREQUAL "TOML"
   AND NOT APP_VERSION_FILE_FORMAT STREQUAL "XML"
   AND NOT APP_VERSION_FILE_FORMAT STREQUAL "YAML")
    message(FATAL_ERROR "APP_VERSION_FILE_FORMAT must be JSON, TOML, XML or YAML (got '${APP_VERSION_FILE_FORMAT}')")
endif()

# The version file is written with a real parser, so its format's parser must be enabled too.
if(APP_VERSION_FILE_FORMAT STREQUAL "JSON" AND NOT APP_ENABLE_JSON_PARSER)
    message(FATAL_ERROR "APP_VERSION_FILE_FORMAT=JSON requires APP_ENABLE_JSON_PARSER=ON")
endif()

if(APP_VERSION_FILE_FORMAT STREQUAL "TOML" AND NOT APP_ENABLE_TOML_PARSER)
    message(FATAL_ERROR "APP_VERSION_FILE_FORMAT=TOML requires APP_ENABLE_TOML_PARSER=ON")
endif()

if(APP_VERSION_FILE_FORMAT STREQUAL "XML" AND NOT APP_ENABLE_XML_PARSER)
    message(FATAL_ERROR "APP_VERSION_FILE_FORMAT=XML requires APP_ENABLE_XML_PARSER=ON")
endif()

if(APP_VERSION_FILE_FORMAT STREQUAL "YAML" AND NOT APP_ENABLE_YAML_PARSER)
    message(FATAL_ERROR "APP_VERSION_FILE_FORMAT=YAML requires APP_ENABLE_YAML_PARSER=ON")
endif()

# Exactly one config format is active by construction (single-valued cache var). The active format is reflected in compile definitions below.

function(apply_app_definitions target)
    if(APP_ENABLE_ZLIB)
        target_compile_definitions(${target} PUBLIC APP_WITH_ZLIB=1)
    endif()

    if(APP_ENABLE_SWAGGER)
        target_compile_definitions(${target} PUBLIC APP_WITH_SWAGGER=1)
    endif()

    if(APP_ENABLE_JSON_PARSER)
        target_compile_definitions(${target} PUBLIC APP_WITH_JSON_PARSER=1)
    endif()

    if(APP_ENABLE_TOML_PARSER)
        target_compile_definitions(${target} PUBLIC APP_WITH_TOML_PARSER=1)
    endif()

    if(APP_ENABLE_XML_PARSER)
        target_compile_definitions(${target} PUBLIC APP_WITH_XML_PARSER=1)
    endif()

    if(APP_ENABLE_YAML_PARSER)
        target_compile_definitions(${target} PUBLIC APP_WITH_YAML_PARSER=1)
    endif()

    if(APP_DATABASE_BACKEND STREQUAL "SQLITE")
        target_compile_definitions(${target} PUBLIC APP_DATABASE_SQLITE=1)
    elseif(APP_DATABASE_BACKEND STREQUAL "SQLCIPHER")
        target_compile_definitions(${target} PUBLIC APP_DATABASE_SQLCIPHER=1 SQLITE_HAS_CODEC=1)
    else()
        message(FATAL_ERROR "APP_DATABASE_BACKEND must be SQLITE or SQLCIPHER (got '${APP_DATABASE_BACKEND}')")
    endif()

    if(APP_PASSWORD_HASH_ALGO STREQUAL "SHA256")
        target_compile_definitions(${target} PUBLIC APP_PASSWORD_HASH_SHA256=1)
    elseif(APP_PASSWORD_HASH_ALGO STREQUAL "SHA384")
        target_compile_definitions(${target} PUBLIC APP_PASSWORD_HASH_SHA384=1)
    elseif(APP_PASSWORD_HASH_ALGO STREQUAL "SHA512")
        target_compile_definitions(${target} PUBLIC APP_PASSWORD_HASH_SHA512=1)
    else()
        message(FATAL_ERROR "APP_PASSWORD_HASH_ALGO must be SHA256, SHA384 or SHA512 (got '${APP_PASSWORD_HASH_ALGO}')")
    endif()

    if(APP_CONFIG_FORMAT STREQUAL "JSON")
        target_compile_definitions(${target} PUBLIC APP_CONFIG_JSON=1)
    elseif(APP_CONFIG_FORMAT STREQUAL "TOML")
        target_compile_definitions(${target} PUBLIC APP_CONFIG_TOML=1)
    elseif(APP_CONFIG_FORMAT STREQUAL "XML")
        target_compile_definitions(${target} PUBLIC APP_CONFIG_XML=1)
    elseif(APP_CONFIG_FORMAT STREQUAL "YAML")
        target_compile_definitions(${target} PUBLIC APP_CONFIG_YAML=1)
    endif()

    if(APP_VERSION_FILE_FORMAT STREQUAL "JSON")
        target_compile_definitions(${target} PUBLIC APP_VERSION_FILE_JSON=1)
    elseif(APP_VERSION_FILE_FORMAT STREQUAL "TOML")
        target_compile_definitions(${target} PUBLIC APP_VERSION_FILE_TOML=1)
    elseif(APP_VERSION_FILE_FORMAT STREQUAL "XML")
        target_compile_definitions(${target} PUBLIC APP_VERSION_FILE_XML=1)
    elseif(APP_VERSION_FILE_FORMAT STREQUAL "YAML")
        target_compile_definitions(${target} PUBLIC APP_VERSION_FILE_YAML=1)
    endif()

    target_compile_definitions(${target} PUBLIC APP_VERSION="${PROJECT_VERSION}" APP_RELEASE_DATE="${APP_RELEASE_DATE}" APP_NAME="OatppBackend")
endfunction()
