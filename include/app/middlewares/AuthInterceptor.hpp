#pragma once

#include <memory>
#include <string>
#include <unordered_set>

#include "oatpp/web/server/interceptor/RequestInterceptor.hpp"

#include "app/auth/JwtService.hpp"
#include "app/services/AuthService.hpp"

namespace app::middlewares
{

/// Bundle key under which verified claims (dto::AuthClaimsDto) are stored for controllers.
constexpr const char* kClaimsBundleKey = "app.auth.claims";

/**
 * JWT request interceptor.
 *
 * - Public paths (login, swagger, docs) pass through untouched.
 * - Every other path requires `Authorization: Bearer <jwt>`; valid tokens are
 *   verified and their claims stored as dto::AuthClaimsDto in the request bundle.
 * - Failures short-circuit with a 401 JSON error (never throws past oatpp).
 */
class AuthInterceptor : public oatpp::web::server::interceptor::RequestInterceptor
{
public:
    explicit AuthInterceptor(std::shared_ptr<services::AuthService> authService);

    std::shared_ptr<OutgoingResponse> intercept(const std::shared_ptr<IncomingRequest>& request) override;

    bool isPublicPath(const std::string& method, const std::string& path) const;

private:
    std::shared_ptr<services::AuthService> authService_;
    std::unordered_set<std::string> publicPaths_;
};

}  // namespace app::middlewares
