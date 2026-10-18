#pragma once

#include <memory>
#include <string>
#include <unordered_set>

#include "oatpp/web/protocol/http/outgoing/ResponseFactory.hpp"
#include "oatpp/web/server/interceptor/RequestInterceptor.hpp"
#include "oatpp/web/server/interceptor/ResponseInterceptor.hpp"

namespace app::middlewares
{

/**
 * Production CORS interceptor.
 *
 * Implements both RequestInterceptor (for preflight OPTIONS requests) and
 * ResponseInterceptor (for attaching CORS headers to outgoing responses).
 */
class CorsInterceptor : public oatpp::web::server::interceptor::RequestInterceptor,
                        public oatpp::web::server::interceptor::ResponseInterceptor
{
public:
    using IncomingRequest = oatpp::web::protocol::http::incoming::Request;
    using OutgoingResponse = oatpp::web::protocol::http::outgoing::Response;

    explicit CorsInterceptor(const std::string& allowedOrigins = "*",
                             const std::string& allowedMethods = "GET, POST, PUT, PATCH, DELETE, OPTIONS, HEAD",
                             const std::string& allowedHeaders =
                                 "Authorization, Content-Type, Accept, Origin, X-Requested-With, X-Forwarded-For",
                             const std::string& maxAgeSeconds = "86400");

    /// RequestInterceptor: handles preflight OPTIONS requests with 204 No Content.
    std::shared_ptr<OutgoingResponse> intercept(const std::shared_ptr<IncomingRequest>& request) override;

    /// ResponseInterceptor: attaches CORS headers to outgoing responses.
    std::shared_ptr<OutgoingResponse> intercept(const std::shared_ptr<IncomingRequest>& request,
                                                const std::shared_ptr<OutgoingResponse>& response) override;

    bool isOriginAllowed(const std::string& origin) const;

private:
    std::string allowedOriginsRaw_;
    std::string allowedMethods_;
    std::string allowedHeaders_;
    std::string maxAgeSeconds_;
    bool allowAllOrigins_{true};
    std::unordered_set<std::string> allowedOrigins_;

    void attachCorsHeaders(const std::shared_ptr<IncomingRequest>& request,
                           const std::shared_ptr<OutgoingResponse>& response) const;
};

}  // namespace app::middlewares
