# Dependency discovery + linking for OatppBackend. All find_package calls are REQUIRED so a missing dependency fails with a clear message instead of
# an obscure compile error. Optional features (zlib/swagger/ sqlcipher/parsers) are only searched when the corresponding feature flag is enabled,
# which guarantees disabled dependencies are never linked.
#
# Static linking policy: static archives are preferred for every dependency (.a sorts before .so below). Two exceptions keep their shared libraries:
# OpenSSL (Ubuntu's static libcrypto.a references libjitterentropy, which is not installed here — and unused by the shared library — so static OpenSSL
# cannot link on this machine) and SQLCipher (ships shared-only). Shared fallbacks also cover any dependency without a static archive, so configure
# never breaks — the verbose build summary reports the actual per-library link type.

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

list(PREPEND CMAKE_FIND_LIBRARY_SUFFIXES ".a")
if(APP_ENABLE_ZLIB)
    find_package(ZLIB REQUIRED)
endif()

find_package(oatpp 1.3.0 EXACT REQUIRED)
find_package(oatpp-sqlite 1.3.0 EXACT REQUIRED)
find_package(oatpp-websocket 1.3.0 EXACT REQUIRED)

if(APP_ENABLE_ZLIB)
    find_package(oatpp-zlib 1.3.0 EXACT REQUIRED)
endif()

if(APP_ENABLE_SWAGGER)
    find_package(oatpp-swagger 1.3.0 EXACT REQUIRED)
endif()

# Config parsers are only searched when enabled, so a disabled parser's dependency is never required, built, or linked.
if(APP_ENABLE_JSON_PARSER)
    find_package(jsoncpp 1.9.6 EXACT REQUIRED)
endif()

if(APP_ENABLE_TOML_PARSER)
    find_package(toml11 4.4.0 EXACT REQUIRED)
endif()

if(APP_ENABLE_XML_PARSER)
    find_package(pugixml 1.15 EXACT REQUIRED)
endif()

if(APP_ENABLE_YAML_PARSER)
    find_package(yaml-cpp 0.9.0 EXACT REQUIRED)
endif()

find_package(jwt-cpp 0.7.2 EXACT REQUIRED)

if(APP_DATABASE_BACKEND STREQUAL "SQLITE")
    find_package(SQLite3 REQUIRED)

    # The static sqlite archive is built with zstd support, which the shared library pulls transitively but the archive needs explicitly.
    if(SQLite3_LIBRARY MATCHES "\\.a$")
        find_library(ZSTD_LIBRARY NAMES zstd REQUIRED)
        if(NOT TARGET ZSTD::ZSTD)
            add_library(ZSTD::ZSTD UNKNOWN IMPORTED)
            set_target_properties(ZSTD::ZSTD PROPERTIES IMPORTED_LOCATION "${ZSTD_LIBRARY}")
        endif()
    endif()
elseif(APP_DATABASE_BACKEND STREQUAL "SQLCIPHER")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SQLCIPHER REQUIRED IMPORTED_TARGET sqlcipher)

    # oatpp-sqlite's imported target unconditionally references SQLite::SQLite3 in its INTERFACE_LINK_LIBRARIES. In SQLCipher builds we satisfy that
    # name with an alias to sqlcipher so the codec implementation wins at link time (never the plaintext system sqlite) and no extra
    # find_package(SQLite3) is needed.
    if(NOT TARGET SQLite::SQLite3)
        add_library(SQLite::SQLite3 INTERFACE IMPORTED)
        set_target_properties(SQLite::SQLite3 PROPERTIES INTERFACE_LINK_LIBRARIES PkgConfig::SQLCIPHER)
    endif()
else()
    message(FATAL_ERROR "APP_DATABASE_BACKEND must be SQLITE or SQLCIPHER (got '${APP_DATABASE_BACKEND}')")
endif()

function(link_app_dependencies target)
    target_link_libraries(
        ${target}
        PUBLIC oatpp::oatpp
               oatpp::oatpp-sqlite
               oatpp::oatpp-websocket
               jwt-cpp::jwt-cpp
               OpenSSL::SSL
               OpenSSL::Crypto
               Threads::Threads)

    if(APP_ENABLE_JSON_PARSER)
        target_link_libraries(${target} PUBLIC jsoncpp_static)
    endif()

    if(APP_ENABLE_TOML_PARSER)
        target_link_libraries(${target} PUBLIC toml11::toml11)
    endif()

    if(APP_ENABLE_XML_PARSER)
        target_link_libraries(${target} PUBLIC pugixml::static)
    endif()

    if(APP_ENABLE_YAML_PARSER)
        target_link_libraries(${target} PUBLIC yaml-cpp::yaml-cpp)
    endif()

    if(APP_ENABLE_ZLIB)
        target_link_libraries(${target} PUBLIC oatpp::oatpp-zlib)
    endif()

    if(APP_ENABLE_SWAGGER)
        target_link_libraries(${target} PUBLIC oatpp::oatpp-swagger)
    endif()

    if(APP_DATABASE_BACKEND STREQUAL "SQLITE")
        target_link_libraries(${target} PUBLIC SQLite::SQLite3)
        if(TARGET ZSTD::ZSTD)
            target_link_libraries(${target} PUBLIC ZSTD::ZSTD)
        endif()
    elseif(APP_DATABASE_BACKEND STREQUAL "SQLCIPHER")
        target_link_libraries(${target} PUBLIC PkgConfig::SQLCIPHER)
    else()
        message(FATAL_ERROR "APP_DATABASE_BACKEND must be SQLITE or SQLCIPHER (got '${APP_DATABASE_BACKEND}')")
    endif()
endfunction()
