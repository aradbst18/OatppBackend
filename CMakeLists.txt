cmake_minimum_required(VERSION 3.20)

# Application version as a cache variable so -DAPP_VERSION=x.y.z overrides it. Declared before project() so
# PROJECT_VERSION follows it. Validated in cmake/options.cmake (must be numeric major.minor.patch).
set(APP_VERSION
    "0.0.1"
    CACHE STRING "Application version (major.minor.patch)")

project(
    OatppBackend
    VERSION ${APP_VERSION}
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_PREFIX_PATH "/usr/local" "/usr/local/lib/cmake")

include(options)
include(sanitizers)
include(compiler)
include(dependencies)
include(build_summary)
include(install_rules)

add_library(
    app_core STATIC
    src/logging/Logger.cpp
    src/logging/OatppLoggerBridge.cpp
    src/errors/AppException.cpp
    src/errors/ErrorMapper.cpp
    src/validation/Validators.cpp
    src/serialization/AppObjectMapper.cpp
    src/domain/UserRole.cpp
    src/infrastructure/TextFile.cpp
    src/infrastructure/documents/DocumentValue.cpp
    src/config/AppSettings.cpp
    src/config/VersionFile.cpp
    src/config/ConfigFactory.cpp
    src/infrastructure/database/DatabaseFactory.cpp
    src/infrastructure/database/DatabaseExecutor.cpp
    src/infrastructure/database/OrmSupport.cpp
    src/repositories/BaseRepository.cpp
    src/repositories/UserRepository.cpp
    src/repositories/RefreshTokenRepository.cpp
    src/auth/JwtService.cpp
    src/auth/PasswordHash.cpp
    src/services/AuthService.cpp
    src/controllers/AuthController.cpp
    src/controllers/EchoController.cpp
    src/middlewares/AuthInterceptor.cpp
    src/middlewares/CorsInterceptor.cpp
    src/middlewares/RateLimitInterceptor.cpp
    src/websocket/EchoListener.cpp
    src/application/TerminateHandler.cpp
    src/application/ManagedThread.cpp
    src/application/Application.cpp)

# File database support: always SQLCipher-backed (SqlcipherDatabase over the oatpp-sqlite executor, optional key).
# Disabled builds compile and link no SQLCipher.
if(APP_ENABLE_SQLCIPHER)
    target_sources(app_core PRIVATE src/infrastructure/database/SqlcipherDatabase.cpp)
endif()

# File-database inspector: only meaningful with file database support compiled in.
if(APP_ENABLE_SQLCIPHER)
    target_sources(app_core PRIVATE src/infrastructure/documents/SqlcipherDocument.cpp)
endif()

# PostgreSQL support: oatpp-postgresql-backed database (libpq underneath) plus the server inspector. Disabled builds
# compile and link no libpq.
if(APP_ENABLE_POSTGRESQL)
    target_sources(
        app_core
        PRIVATE src/infrastructure/database/PostgresqlDatabase.cpp
                src/infrastructure/database/PostgresqlLocalServer.cpp
                src/infrastructure/documents/PostgresqlDocument.cpp)
endif()

if(APP_ENABLE_ZLIB)
    target_sources(app_core PRIVATE src/infrastructure/Gzip.cpp)
endif()

# Config parsers: a disabled parser's translation unit is never compiled.
if(APP_ENABLE_JSON_PARSER)
    target_sources(app_core PRIVATE src/config/JsonConfigStore.cpp src/infrastructure/documents/JsonDocument.cpp)
endif()

if(APP_ENABLE_TOML_PARSER)
    target_sources(app_core PRIVATE src/config/TomlConfigStore.cpp src/infrastructure/documents/TomlDocument.cpp)
endif()

if(APP_ENABLE_XML_PARSER)
    target_sources(app_core PRIVATE src/config/XmlConfigStore.cpp src/infrastructure/documents/XmlDocument.cpp)
endif()

if(APP_ENABLE_YAML_PARSER)
    target_sources(app_core PRIVATE src/config/YamlConfigStore.cpp src/infrastructure/documents/YamlDocument.cpp)
endif()

if(APP_ENABLE_SWAGGER)
    target_sources(app_core PRIVATE src/infrastructure/Swagger.cpp src/dto/AuthDtos.cpp)
endif()

target_include_directories(app_core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                           $<INSTALL_INTERFACE:include>)

apply_app_definitions(app_core)
apply_app_warnings(app_core)
apply_app_build_type_flags(app_core)
apply_app_hardening(app_core)
app_apply_sanitizers(app_core)
link_app_dependencies(app_core)

add_executable(app_server src/main.cpp)
target_link_libraries(app_server PRIVATE app_core)
apply_app_definitions(app_server)
apply_app_warnings(app_server)
apply_app_build_type_flags(app_server)
apply_app_hardening(app_server)
app_apply_sanitizers(app_server)

set_target_properties(app_server PROPERTIES OUTPUT_NAME "oatpp-backend")

if(APP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

setup_install_rules()
print_build_summary()
