#include "app/errors/ErrorMapper.hpp"

#include <cctype>
#include <exception>

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

    if (const auto* nested = dynamic_cast<const std::nested_exception*>(&exception))
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
    try
    {
        std::rethrow_exception(exceptionPtr);
    }
    catch (const std::exception& exception)
    {
        if (const auto* applicationError = findNestedException<AppException>(exception))
        {
            return createErrorResponse(statusFor(applicationError->code()), applicationError->what());
        }

        // oatpp surfaces BODY_DTO deserialization failures (Int8 range, malformed
        // JSON) as HttpError/generic 500s. Demote recognized client parse failures
        // to 400 (keeping the parser message, never echoing secrets); true server
        // failures stay 500 generic. Mirrors the status-overload normalization.
        if (const auto* httpError = findNestedException<oatpp::web::protocol::http::HttpError>(exception))
        {
            auto info = const_cast<oatpp::web::protocol::http::HttpError*>(httpError)->getInfo();
            const int http = info.status.code;
            std::string text = httpError->what();

            if (http == 422 || text.find("validate") != std::string::npos)
            {
                return createErrorResponse(oatpp::web::protocol::http::Status(422, ""), text);
            }

            std::string lower = text;
            for (char& c : lower)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }

            const bool isParseFailure =
                http == 400 || lower.find("pars") != std::string::npos || lower.find("deserial") != std::string::npos ||
                lower.find("out of range") != std::string::npos || lower.find("invalid") != std::string::npos ||
                lower.find("body") != std::string::npos || lower.find("range") != std::string::npos ||
                lower.find("dto") != std::string::npos;

            if (http >= 500 && isParseFailure)
            {
                logging::logWarn("error", std::string("HTTP 400: ") + text);
                return createErrorResponse(oatpp::web::protocol::http::Status(400, ""), text);
            }

            if (http >= 400 && http < 500)
            {
                return createErrorResponse(oatpp::web::protocol::http::Status(http, ""), text);
            }
        }

        std::string text = exception.what();
        std::string lower = text;
        for (char& c : lower)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        if (lower.find("out of range") != std::string::npos || lower.find("deserial") != std::string::npos ||
            (lower.find("pars") != std::string::npos && lower.find("json") != std::string::npos))
        {
            logging::logWarn("error", std::string("HTTP 400: ") + text);

            // Never echo huge offending values (e.g. pinCode 64654646).
            if (text.size() > 300)
            {
                text = "Invalid request body";
            }

            return createErrorResponse(oatpp::web::protocol::http::Status(400, ""), text);
        }

        logging::logWarn("error", std::string("unhandled: ") + exception.what());
        return createErrorResponse(oatpp::web::protocol::http::Status::CODE_500, "an internal server error occurred");
    }
    catch (...)
    {
        logging::logWarn("error", "unhandled non-standard exception");
        return createErrorResponse(oatpp::web::protocol::http::Status::CODE_500, "an internal server error occurred");
    }
}

std::shared_ptr<oatpp::web::protocol::http::outgoing::Response> AppErrorHandler::handleError(
    const oatpp::web::protocol::http::Status& status, const oatpp::String& message,
    const oatpp::web::protocol::http::Headers& headers)
{
    (void)headers;

    // Errors raised by oatpp itself (deserialization, validation, 404s).
    std::string text = message ? message->c_str() : (status.description != nullptr ? status.description : "Error");
    int http = status.code;

    // Oatpp reports DTO validation failures with 400 + "validation" text in
    // some versions; normalize those to 422 to match our documented contract.
    if (http == 422 || text.find("validate") != std::string::npos)
    {
        http = 422;
    }
    else if (http >= 500)
    {
        // oatpp surfaces BODY_DTO deserialization failures (range violations,
        // malformed JSON) as generic 500s thrown from readBodyToDto. Those are
        // client errors: demote recognized parse failures to 400 while keeping
        // the parser message (field-level, no secrets). True server failures
        // keep the generic 500 without internals.
        std::string lower = text;
        for (char& c : lower)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        const bool isParseFailure =
            lower.find("pars") != std::string::npos || lower.find("deserial") != std::string::npos ||
            lower.find("out of range") != std::string::npos || lower.find("invalid") != std::string::npos ||
            lower.find("body") != std::string::npos;

        if (isParseFailure)
        {
            http = 400;
        }
        else
        {
            text = "an internal server error occurred";
        }
    }

    logging::logWarn("error", std::string("HTTP ") + std::to_string(http) + ": " + text);

    auto response = createErrorResponse(oatpp::web::protocol::http::Status(http, ""), text);
    for (const auto& header : headers.getAll())
    {
        response->putOrReplaceHeader(header.first.std_str(), header.second.std_str());
    }

    return response;
}

std::shared_ptr<oatpp::web::protocol::http::outgoing::Response> AppErrorHandler::createErrorResponse(
    const oatpp::web::protocol::http::Status& status, const std::string& message)
{
    return createTextError(status, message);
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
