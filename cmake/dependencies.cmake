# Dependency discovery + linking for OatppBackend. All find_package calls are REQUIRED so a missing dependency fails
# with a clear message instead of an obscure compile error. Optional features (zlib/swagger/postgresql/parsers,
# SQLCipher file support) are only searched when the corresponding feature flag is enabled, which guarantees disabled
# dependencies are never linked.
#
# Static linking policy: static archives are preferred for every dependency (.a sorts before .so below). One exception
# keeps its shared library: OpenSSL (Ubuntu's static libcrypto.a needs the uninstalled libjitterentropy). Shared
# fallbacks also cover any dependency without a static archive, so configure never breaks — the verbose build summary
# reports the actual per-library link type.
#
# User databases run through the oatpp ORM executors, which plug into the existing IDatabase::execute(sql, bound params)
# contract so repositories work unchanged on both user databases: oatpp-sqlite executes file statements (SQLCipher
# supplies the codec; the adapter's exported SQLite::SQLite3 reference is satisfied by SQLCipher below) and
# oatpp-postgresql executes PostgreSQL statements (libpq underneath).

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

list(PREPEND CMAKE_FIND_LIBRARY_SUFFIXES ".a")
if(APP_ENABLE_ZLIB)
    find_package(ZLIB REQUIRED)
endif()

find_package(oatpp 1.3.0 EXACT REQUIRED)
include(oatpp_websocket)

if(APP_ENABLE_ZLIB)
    find_package(oatpp-zlib 1.3.0 EXACT REQUIRED)
endif()

if(APP_ENABLE_SWAGGER)
    find_package(oatpp-swagger 1.3.0 EXACT REQUIRED)
endif()

# File database support is always SQLCipher-backed: the SQLCipher library is the only file-database library searched or
# linked (it opens both plaintext and encrypted files; see docs/roadmap/postgresql_encryption.md for the PostgreSQL
# side). The standard SQLite library is never searched.

if(APP_ENABLE_SQLCIPHER)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SQLCIPHER REQUIRED IMPORTED_TARGET sqlcipher)
    find_package(oatpp-sqlite 1.3.0 EXACT REQUIRED)

    # oatpp-sqlite's static archive exports SQLite::SQLite3 (its object code calls the sqlite3_* C API). Define that
    # target as SQLCipher: SQLCipher implements the same API, so the installed adapter works unchanged and the standard
    # SQLite library is never searched or linked. The codec compile definition makes SQLCipher's sqlite3.h declare
    # sqlite3_key()/sqlite3_rekey() for every consumer of this target (app_core links it through oatpp-sqlite).
    if(NOT TARGET SQLite::SQLite3)
        add_library(SQLite::SQLite3 INTERFACE IMPORTED)
        set_target_properties(SQLite::SQLite3 PROPERTIES INTERFACE_LINK_LIBRARIES PkgConfig::SQLCIPHER
                                                         INTERFACE_COMPILE_DEFINITIONS "SQLITE_HAS_CODEC=1")
    endif()
endif()

if(APP_ENABLE_POSTGRESQL)
    find_package(oatpp-postgresql 1.3.0 EXACT REQUIRED)
endif()

# Config parsers are only searched when enabled, so a disabled parser's dependency is never required, built, or linked.
if(APP_ENABLE_JSON_PARSER)
    find_package(jsoncpp 1.9.8 EXACT REQUIRED)
endif()

if(APP_ENABLE_TOML_PARSER)
    find_package(toml11 4.4.0 EXACT REQUIRED)
endif()

if(APP_ENABLE_XML_PARSER)
    find_package(pugixml 1.16 EXACT REQUIRED)
endif()

if(APP_ENABLE_YAML_PARSER)
    find_package(yaml-cpp 0.9.0 EXACT REQUIRED)
endif()

find_package(jwt-cpp 0.7.2 EXACT REQUIRED)

function(link_app_dependencies target)
    target_link_libraries(${target} PUBLIC oatpp::oatpp oatpp::oatpp-websocket jwt-cpp::jwt-cpp OpenSSL::SSL
                                           OpenSSL::Crypto Threads::Threads)

    if(APP_ENABLE_SQLCIPHER)
        target_link_libraries(${target} PUBLIC PkgConfig::SQLCIPHER oatpp::oatpp-sqlite)
    endif()

    if(APP_ENABLE_POSTGRESQL)
        target_link_libraries(${target} PUBLIC oatpp::oatpp-postgresql)
    endif()

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
endfunction()
