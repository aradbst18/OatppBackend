#include "app/errors/ErrorMapper.hpp"

#include <array>
#include <cctype>
#include <exception>

#include "oatpp/core/utils/Random.hpp"
#include "oatpp/web/protocol/http/outgoing/ResponseFactory.hpp"

#include "app/errors/AppException.hpp"
#include "app/logging/Logger.hpp"

namespace app::errors
{

namespace
{

template <typename ExceptionType>
const ExceptionType* findNestedException(const std::exception& exception)
{
    if (const auto* match = dynamic_cast<const ExceptionType*>(&exception))
    {
        return match;
    }

    if (const auto* nested = dynamic_cast<const std::nested_exception*>(&exception); nested && nested->nested_ptr())
    {
        try
        {
            nested->rethrow_nested();
        }
        catch (const std::exception& inner)
        {
            return findNestedException<ExceptionType>(inner);
        }
        catch (...)
        {
        }
    }

    return nullptr;
}

const char* publicMessage(int http)
{
    switch (http)
    {
        case 400:
            return "Invalid request";
        case 401:
            return "Invalid credentials";
        case 403:
            return "Resource access denied";
        case 404:
            return "Resource not found";
        case 405:
            return "Method not allowed";
        case 409:
            return "Resource conflict";
        case 410:
            return "Resource is gone";
        case 413:
            return "Payload too large";
        case 415:
            return "Unsupported media type";
        case 422:
            return "Invalid request data";
        case 429:
            return "Rate limit exceeded";
        default:
            return http >= 500 ? "an internal server error occurred" : "Request failed";
    }
}

}  // namespace

oatpp::web::protocol::http::Status statusFor(ErrorCode code)
{
    return oatpp::web::protocol::http::Status(toHttpStatus(code), "");
}

std::shared_ptr<AppErrorHandler> AppErrorHandler::createShared()
{
    return std::make_shared<AppErrorHandler>();
}

std::shared_ptr<oatpp::web::protocol::http::outgoing::Response> AppErrorHandler::handleError(
    const std::exception_ptr& exceptionPtr)
{
    if (!exceptionPtr)
    {
        return createErrorResponse(oatpp::web::protocol::http::Status::CODE_500, "UnhandledException");
    }

    try
    {
        std::rethrow_exception(exceptionPtr);
    }
    catch (const std::exception& exception)
    {
        if (const auto* applicationError = findNestedException<AppException>(exception))
        {
            return createErrorResponse(statusFor(applicationError->code()), toString(applicationError->code()));
        }

        if (const auto* httpError = findNestedException<oatpp::web::protocol::http::HttpError>(exception))
        {
            auto info = const_cast<oatpp::web::protocol::http::HttpError*>(httpError)->getInfo();
            return handleError(info.status, httpError->what(), httpError->getHeaders());
        }

        std::string lower = exception.what();
        for (char& c : lower)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        if (lower.find("out of range") != std::string::npos || lower.find("deserial") != std::string::npos ||
            (lower.find("pars") != std::string::npos && lower.find("json") != std::string::npos))
        {
            return createErrorResponse(oatpp::web::protocol::http::Status::CODE_400, "RequestParsingFailed");
        }

        return createErrorResponse(oatpp::web::protocol::http::Status::CODE_500, "UnhandledException");
    }
    catch (...)
    {
        return createErrorResponse(oatpp::web::protocol::http::Status::CODE_500, "UnknownException");
    }
}

std::shared_ptr<oatpp::web::protocol::http::outgoing::Response> AppErrorHandler::handleError(
    const oatpp::web::protocol::http::Status& status, const oatpp::String& message,
    const oatpp::web::protocol::http::Headers& headers)
{
    // Errors raised by oatpp itself (deserialization, validation, 404s).
    std::string lower = message ? *message : "";
    for (char& c : lower)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    int http = status.code;

    // Oatpp reports DTO validation failures with 400 + "validation" text in
    // some versions; normalize those to 422 to match our documented contract.
    if ((http == 400 || http == 500) && lower.find("validat") != std::string::npos)
    {
        http = 422;
    }
    else if (http == 500)
    {
        // oatpp surfaces BODY_DTO deserialization failures (range violations,
        // malformed JSON) as generic 500s thrown from readBodyToDto. Those are
        // client errors: retain the status normalization, never the raw text.
        const bool isParseFailure =
            lower.find("pars") != std::string::npos || lower.find("deserial") != std::string::npos ||
            lower.find("out of range") != std::string::npos || lower.find("invalid") != std::string::npos ||
            lower.find("body") != std::string::npos || lower.find("range") != std::string::npos ||
            lower.find("dto") != std::string::npos;

        if (isParseFailure)
        {
            http = 400;
        }
    }

    return createErrorResponse(oatpp::web::protocol::http::Status(http, ""), "HttpError", headers);
}

std::shared_ptr<oatpp::web::protocol::http::outgoing::Response> AppErrorHandler::createErrorResponse(
    const oatpp::web::protocol::http::Status& status, const char* errorName,
    const oatpp::web::protocol::http::Headers& headers)
{
    std::array<unsigned char, 16> bytes{};
    oatpp::utils::random::Random::randomBytes(bytes.data(), bytes.size());
    constexpr char hex[] = "0123456789abcdef";
    std::string eventId;
    for (unsigned char byte : bytes)
    {
        eventId += hex[byte >> 4];
        eventId += hex[byte & 15];
    }

    auto response = createTextError(oatpp::web::protocol::http::Status(status.code, ""), publicMessage(status.code));
    // Only forward protocol-required public headers, never diagnostic or body headers.
    for (const char* name : {"WWW-Authenticate", "Allow", "Retry-After"})
    {
        if (const auto value = headers.get(name))
        {
            response->putOrReplaceHeader(name, value);
        }
    }
    response->putOrReplaceHeader("X-Error-Id", eventId);

    // Free-form exception text can contain credentials or SQL values; redact it in full.
    try
    {
        logging::log(status.code >= 500 ? logging::Level::Error : logging::Level::Warn, "error",
                     "event=" + eventId + " status=" + std::to_string(status.code) + " code=" + errorName +
                         " diagnostic=[redacted]");
    }
    catch (...)
    {
        // A failing sink must not replace the sanitized response.
    }
    return response;
}

std::shared_ptr<oatpp::web::protocol::http::outgoing::Response> createTextError(
    const oatpp::web::protocol::http::Status& status, const std::string& message)
{
    auto response = oatpp::web::protocol::http::outgoing::ResponseFactory::createResponse(
        status, message.empty() ? status.description : message);
    response->putOrReplaceHeader(oatpp::web::protocol::http::Header::CONTENT_TYPE, "text/plain");
    return response;
}

}  // namespace app::errors
