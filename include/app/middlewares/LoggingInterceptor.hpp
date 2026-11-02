#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"
#include "oatpp/web/protocol/http/incoming/Request.hpp"
#include "oatpp/web/protocol/http/outgoing/Response.hpp"
#include "oatpp/web/server/interceptor/RequestInterceptor.hpp"
#include "oatpp/web/server/interceptor/ResponseInterceptor.hpp"

#include OATPP_CODEGEN_BEGIN(DTO)

namespace app::middlewares
{

constexpr const char* kTraceBundleKey = "app.trace.context";

class TraceContextDto : public oatpp::DTO
{
    DTO_INIT(TraceContextDto, DTO)

    DTO_FIELD(String, requestId);
    DTO_FIELD(String, method);
    DTO_FIELD(String, path);
    DTO_FIELD(Int64, startTimeMs);
};

}  // namespace app::middlewares

#include OATPP_CODEGEN_END(DTO)

namespace app::middlewares
{

/**
 * Request and response interceptor for HTTP access logging and request correlation.
 *
 * Responsibilities:
 * - Assigns or forwards a unique `X-Request-Id` header (32-char hex).
 * - Binds the request ID to thread-local `app::logging::RequestContext` so all downstream
 *   subsystem logs on this thread carry `[req=<id>]`.
 * - Records request arrival time.
 * - Emits incoming request trace at TRACE level.
 * - Emits HTTP access summary at DEBUG level on response completion with latency.
 * - Ensures `X-Request-Id` is attached to all outgoing responses.
 */
class LoggingInterceptor : public oatpp::web::server::interceptor::RequestInterceptor,
                           public oatpp::web::server::interceptor::ResponseInterceptor
{
public:
    using IncomingRequest = oatpp::web::protocol::http::incoming::Request;
    using OutgoingResponse = oatpp::web::protocol::http::outgoing::Response;

    LoggingInterceptor() = default;

    std::shared_ptr<OutgoingResponse> intercept(const std::shared_ptr<IncomingRequest>& request) override;

    std::shared_ptr<OutgoingResponse> intercept(const std::shared_ptr<IncomingRequest>& request,
                                                const std::shared_ptr<OutgoingResponse>& response) override;

    static std::string generateRequestId();
};

}  // namespace app::middlewares
