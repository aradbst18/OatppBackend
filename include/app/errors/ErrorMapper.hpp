#pragma once

#include <exception>
#include <memory>
#include <string>

#include "oatpp/web/protocol/http/Http.hpp"
#include "oatpp/web/server/handler/ErrorHandler.hpp"

#include "app/errors/AppException.hpp"

namespace app::errors
{

/**
 * Central error handler: converts thrown domain exceptions into
 * `text/plain` responses via oatpp's `ResponseFactory::createResponse`.
 * Registered once on the HttpConnectionHandler so controllers do not scatter
 * ad-hoc error bodies.
 *
 * - `handleError(exception_ptr)` catches `AppException` (including nested) and
 *   maps `ErrorCode -> HTTP Status` + `what()` message.
 * - `handleError(status, message, headers)` handles oatpp-generated errors
 *   (deserialization, validation, 404s) as plain text.
 *
 * Success payloads keep using `createDtoResponse(Status, dto)` (JSON);
 * errors are `Status + text/plain message` — no numeric `4000`-style envelope.
 */
class AppErrorHandler : public oatpp::web::server::handler::ErrorHandler
{
public:
    static std::shared_ptr<AppErrorHandler> createShared();

    std::shared_ptr<oatpp::web::protocol::http::outgoing::Response> handleError(const std::exception_ptr& exceptionPtr) override;

    std::shared_ptr<oatpp::web::protocol::http::outgoing::Response> handleError(const oatpp::web::protocol::http::Status& status,
                                                                                const oatpp::String& message,
                                                                                const oatpp::web::protocol::http::Headers& headers) override;

private:
    std::shared_ptr<oatpp::web::protocol::http::outgoing::Response> createErrorResponse(const oatpp::web::protocol::http::Status& status,
                                                                                        const std::string& message = "");
};

/// Map a domain ErrorCode to its HTTP Status (shared by handler + controllers).
oatpp::web::protocol::http::Status statusFor(ErrorCode code);

/// Build a `text/plain` error response for non-controller contexts
/// (e.g. `AuthInterceptor`, which cannot use `ApiController::createResponse`).
std::shared_ptr<oatpp::web::protocol::http::outgoing::Response> createTextError(const oatpp::web::protocol::http::Status& status,
                                                                                const std::string& message = "");

}  // namespace app::errors
