#include "app/infrastructure/Swagger.hpp"

#ifndef APP_WITH_SWAGGER
#error "Swagger.cpp requires APP_WITH_SWAGGER (configure with -DAPP_ENABLE_SWAGGER=ON)"
#endif  // !APP_WITH_SWAGGER

#include "oatpp-swagger/Controller.hpp"
#include "oatpp-swagger/Generator.hpp"
#include "oatpp-swagger/Resources.hpp"
#include "oatpp/parser/json/mapping/ObjectMapper.hpp"
#include "oatpp/web/server/HttpRouter.hpp"

#include <cstdlib>
#include <filesystem>

#ifndef OATPP_SWAGGER_RES_PATH
#define OATPP_SWAGGER_RES_PATH "/usr/local/include/oatpp-1.3.0/bin/oatpp-swagger/res"
#endif  // !OATPP_SWAGGER_RES_PATH

namespace app::infrastructure
{

namespace
{

// Resolution order: configured swaggerResPath when it exists,
// OATPP_SWAGGER_RES_PATH env var when it exists, exe-relative
// ../share/swagger-res copy (installed runs: out/bin/oatpp-backend), then the
// configured path as-is so failures report the configured location. Dev runs
// (binary in build/<preset>/) fall back through the compile-time
// OATPP_SWAGGER_RES_PATH default baked into the configuration.
std::string resolveSwaggerResPath(const std::string& configured)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!configured.empty() && fs::is_directory(configured, ec))
    {
        return configured;
    }

    if (const char* env = std::getenv("OATPP_SWAGGER_RES_PATH"))
    {
        std::error_code ec2;
        if (fs::is_directory(env, ec2))
        {
            return env;
        }
    }

    std::error_code ec3;
    const fs::path exe = fs::canonical("/proc/self/exe", ec3);
    if (!ec3)
    {
        const fs::path installed = exe.parent_path() / "../share/swagger-res";
        std::error_code ec4;
        if (fs::is_directory(installed, ec4))
        {
            return installed.lexically_normal().string();
        }
    }

    if (!configured.empty())
    {
        return configured;
    }

    return OATPP_SWAGGER_RES_PATH;
}

}  // namespace

std::shared_ptr<oatpp::swagger::DocumentInfo> createDocumentInfo()
{
    oatpp::swagger::DocumentInfo::Builder builder;
    builder.setTitle("OatppBackend API")
        .setDescription(
            "Modern C++17 backend foundation: JWT auth, validated DTOs, echo WebSocket.\n"
            "\n"
            "## Quickstart for frontend developers\n"
            "\n"
            "1. `POST /auth/login` with `{\"username\": \"admin\", \"password\": \"Admin1234\"}` "
            "to obtain `access_token`.\n"
            "2. Send the token on every protected call as `Authorization: Bearer <access_token>`.\n"
            "3. `GET /auth/who-am-i` verifies your wiring and returns `{username, role}`.\n"
            "4. Open `GET /ws/echo` as a WebSocket for live echo testing.\n"
            "\n"
            "All error responses are `HTTP Status + text/plain message` via oatpp's "
            "`createResponse` / `AppErrorHandler` — no numeric envelope. Success payloads "
            "are JSON DTOs via `createDtoResponse`. Domain failures are typed "
            "`app::errors::{request,authentication,validation,filesystem,database,"
            "serialization,network,config,crypto,websocket,external_service,ratelimit,"
            "runtime,internal}` exceptions mapped to status; `message` is human-readable "
            "and never contains secrets.\n"
            "\n"
            "Click **Authorize** below (bearer_auth) to try protected endpoints directly from this "
            "UI.")
        .setVersion(APP_VERSION)
        .addServer("http://localhost:8080", "Local development server")
        .addSecurityScheme("bearer_auth",
                           oatpp::swagger::DocumentInfo::SecuritySchemeBuilder()
                               .setType("http")
                               .setScheme("bearer")
                               .setBearerFormat("JWT")
                               .setDescription("JWT access token from POST /auth/login. "
                                               "Click Authorize and enter `Bearer <token>` (or just the raw "
                                               "token — Swagger UI adds the scheme). Sent as "
                                               "`Authorization: Bearer <token>`.")
                               .build());

    return builder.build();
}

void registerSwaggerWithEndpoints(const std::shared_ptr<oatpp::web::server::HttpRouter>& router,
                                  const oatpp::web::server::api::Endpoints& endpoints, const std::string& resPath)
{
    auto docInfo = createDocumentInfo();
    auto resources = oatpp::swagger::Resources::loadResources(resolveSwaggerResPath(resPath));

    oatpp::swagger::Generator generator(std::make_shared<oatpp::swagger::Generator::Config>());
    auto document = generator.generateDocument(docInfo, endpoints);

    // IMPORTANT: the swagger Controller serializes the generated OpenAPI document
    // with this mapper. The default ObjectMapper has `includeNullFields = true`,
    // which emits `"get": null, "post": null, ...` for every unused HTTP method
    // in each PathItem. Swagger UI renders those null operations as phantom
    // (empty) GET/POST/... entries under the "default" group. Match upstream
    // `oatpp::swagger::Controller::createShared()` and omit null fields so only
    // the real operations appear.
    auto serializerConfig = oatpp::parser::json::mapping::Serializer::Config::createShared();
    serializerConfig->includeNullFields = false;

    auto deserializerConfig = oatpp::parser::json::mapping::Deserializer::Config::createShared();
    deserializerConfig->allowUnknownFields = false;

    auto objectMapper = oatpp::parser::json::mapping::ObjectMapper::createShared(serializerConfig, deserializerConfig);
    auto controller = std::make_shared<oatpp::swagger::Controller>(objectMapper, document, resources,
                                                                   oatpp::swagger::ControllerPaths{});
    router->addController(controller);
}

}  // namespace app::infrastructure
