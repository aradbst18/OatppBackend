#include "app/middlewares/AuthInterceptor.hpp"

#include "app/auth/JwtService.hpp"
#include "app/dto/AuthDtos.hpp"
#include "app/errors/AppException.hpp"

namespace app::middlewares
{

AuthInterceptor::AuthInterceptor(std::shared_ptr<auth::JwtService> jwt) : jwt_(std::move(jwt))
{
    if (!jwt_)
    {
        throw errors::internal::OperationFailed("AuthInterceptor requires JwtService");
    }

    publicPaths_ = {
        "POST /auth/login", "POST /auth/refresh", "GET /ws/echo",
#ifdef APP_WITH_SWAGGER
        "GET /swagger/*",   "GET /api-docs/*",    "GET /swagger/ui",
#endif  // APP_WITH_SWAGGER
    };
}

bool AuthInterceptor::isPublicPath(const std::string& method, const std::string& path) const
{
    const std::string key = method + " " + path;
    if (publicPaths_.count(key) != 0)
    {
        return true;
    }

#ifdef APP_WITH_SWAGGER
    // Swagger resources live under these prefixes, but only in builds where the
    // Swagger UI controllers are actually mounted (see Application::initRouting).
    if (path == "/swagger/ui" || path.rfind("/swagger/", 0) == 0 || path.rfind("/api-docs/", 0) == 0)
    {
        return true;
    }
#endif  // APP_WITH_SWAGGER

    return false;
}

std::shared_ptr<AuthInterceptor::OutgoingResponse> AuthInterceptor::intercept(
    const std::shared_ptr<IncomingRequest>& request)
{
    const std::string method = request->getStartingLine().method.std_str();
    const std::string path = request->getStartingLine().path.std_str();

    if (isPublicPath(method, path))
    {
        return nullptr;  // continue down the chain
    }

    const auto authHeader = request->getHeader("Authorization");
    const std::string header = authHeader ? authHeader->c_str() : "";

    // Throw domain exceptions and let `AppErrorHandler`
    // map them to 401 text/plain. No JSON envelope here.
    const std::string token = auth::JwtService::extractBearer(header);
    const auth::TokenClaims claims = jwt_->verify(token);

    // Stash verified claims in request bundle as a typed DTO.
    auto claimsDto = dto::AuthClaimsDto::createShared();
    claimsDto->subject = oatpp::String(claims.subject);
    claimsDto->role = oatpp::String(claims.role);
    claimsDto->issuer = oatpp::String(claims.issuer);

    request->putBundleData(kClaimsBundleKey, claimsDto);
    return nullptr;
}

}  // namespace app::middlewares
