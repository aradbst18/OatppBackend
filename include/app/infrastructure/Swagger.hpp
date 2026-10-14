#pragma once

#if !defined(APP_WITH_SWAGGER)
#error "Swagger.hpp requires APP_WITH_SWAGGER (configure with -DAPP_ENABLE_SWAGGER=ON)"
#endif  // !APP_WITH_SWAGGER

#include <memory>
#include <string>

// NOTE: this header deliberately includes no oatpp-swagger headers. All
// Swagger documentation implementation (DocumentInfo builder, generator,
// UI resources) lives in src/infrastructure/Swagger.cpp, so translation
// units that include this header never depend on oatpp-swagger.

namespace oatpp::web::server
{

class HttpRouter;
namespace api
{

struct Endpoints;

}  // namespace api

}  // namespace oatpp::web::server

namespace oatpp::swagger
{

class DocumentInfo;

}  // namespace oatpp::swagger

namespace app::infrastructure
{

/// Build the shared Swagger DocumentInfo via the Builder API.
std::shared_ptr<oatpp::swagger::DocumentInfo> createDocumentInfo();

/// Generate the OpenAPI document from already-registered endpoints and mount
/// the Swagger UI controllers (/api-docs/oas-3.0.0.json, /swagger/ui, ...).
/// `resPath` is the configured swaggerResPath; when it does not exist the
/// exe-relative installed copy (`../share/swagger-res`) and the
/// `OATPP_SWAGGER_RES_PATH` env var are tried before falling back to it.
void registerSwaggerWithEndpoints(const std::shared_ptr<oatpp::web::server::HttpRouter>& router, const oatpp::web::server::api::Endpoints& endpoints,
                                  const std::string& resPath);

}  // namespace app::infrastructure
