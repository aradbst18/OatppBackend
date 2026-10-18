#pragma once

#include <memory>

#include "oatpp/core/macro/codegen.hpp"
#include "oatpp/core/macro/component.hpp"
#include "oatpp/web/server/api/ApiController.hpp"

#include "app/auth/JwtService.hpp"
#include "app/dto/AuthDtos.hpp"
#include "app/services/AuthService.hpp"

#include OATPP_CODEGEN_BEGIN(ApiController)

namespace app::controllers
{

/**
 * Authentication endpoints.
 *
 * - POST /auth/login   public, issues a JWT from demo credentials.
 * - GET  /auth/who-am-i requires a valid `Authorization: Bearer <jwt>`.
 *
 * The controller does no crypto and no SQL: it validates the DTO shape,
 * delegates to AuthService, and maps results to DTOs. Auth enforcement for
 * who-am-i happens in AuthInterceptor; the endpoint additionally re-checks
 * so direct router use without the interceptor still fails closed.
 */
class AuthController : public oatpp::web::server::api::ApiController
{
public:
    AuthController(const std::shared_ptr<ObjectMapper>& objectMapper,
                   std::shared_ptr<services::AuthService> authService, std::shared_ptr<auth::JwtService> jwtService);

#if defined(APP_WITH_SWAGGER)
    ENDPOINT_INFO(login);
    ENDPOINT_INFO(refresh);
    ENDPOINT_INFO(whoAmI);
#endif  // APP_WITH_SWAGGER

    ENDPOINT("POST", "/auth/login", login, BODY_DTO(Object<dto::LoginRequestDto>, body));
    ENDPOINT("POST", "/auth/refresh", refresh, BODY_DTO(Object<dto::RefreshRequestDto>, body));
    ENDPOINT("GET", "/auth/who-am-i", whoAmI, REQUEST(std::shared_ptr<IncomingRequest>, request));

private:
    std::shared_ptr<services::AuthService> authService_;
    std::shared_ptr<auth::JwtService> jwtService_;
};

}  // namespace app::controllers

#include OATPP_CODEGEN_END(ApiController)
