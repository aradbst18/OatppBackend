#include "app/middlewares/LoggingInterceptor.hpp"

#include <array>
#include <cctype>
#include <chrono>
#include <string>

#include "oatpp/core/utils/Random.hpp"

#include "app/logging/Logger.hpp"

namespace app::middlewares
{

std::string LoggingInterceptor::generateRequestId()
{
    std::array<unsigned char, 16> bytes{};
    oatpp::utils::random::Random::randomBytes(bytes.data(), bytes.size());
    constexpr char hex[] = "0123456789abcdef";
    std::string id;
    id.reserve(32);
    for (unsigned char byte : bytes)
    {
        id += hex[byte >> 4];
        id += hex[byte & 15];
    }
    return id;
}

std::shared_ptr<LoggingInterceptor::OutgoingResponse> LoggingInterceptor::intercept(
    const std::shared_ptr<IncomingRequest>& request)
{
    if (!request)
    {
        return nullptr;
    }

    std::string reqId;
    const auto header = request->getHeader("X-Request-Id");
    if (header && !header->empty() && header->size() <= 64)
    {
        bool valid = true;
        for (char c : *header)
        {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_')
            {
                valid = false;
                break;
            }
        }
        if (valid)
        {
            reqId = *header;
        }
    }

    if (reqId.empty())
    {
        reqId = generateRequestId();
    }

    logging::RequestContext::setRequestId(reqId);

    const auto now = std::chrono::steady_clock::now();
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    auto trace = TraceContextDto::createShared();
    trace->requestId = oatpp::String(reqId);
    trace->method = request->getStartingLine().method.toString();
    trace->path = request->getStartingLine().path.toString();
    trace->startTimeMs = nowMs;

    request->putBundleData(kTraceBundleKey, trace);

    const std::string method = trace->method ? trace->method->c_str() : "";
    const std::string path = trace->path ? trace->path->c_str() : "";
    APP_LOG_TRACE("http", "--> " + method + " " + path);

    return nullptr;
}

std::shared_ptr<LoggingInterceptor::OutgoingResponse> LoggingInterceptor::intercept(
    const std::shared_ptr<IncomingRequest>& request, const std::shared_ptr<OutgoingResponse>& response)
{
    if (!response)
    {
        logging::RequestContext::clear();
        return response;
    }

    oatpp::Object<TraceContextDto> trace;
    if (request)
    {
        trace = request->getBundleData<oatpp::Object<TraceContextDto>>(kTraceBundleKey);
    }

    std::string reqId;
    if (trace && trace->requestId && !trace->requestId->empty())
    {
        reqId = *trace->requestId;
    }
    else
    {
        reqId = logging::RequestContext::requestId();
    }

    if (!reqId.empty())
    {
        logging::RequestContext::setRequestId(reqId);
        if (!response->getHeader("X-Request-Id"))
        {
            response->putHeader("X-Request-Id", reqId);
        }
    }

    if (trace && trace->startTimeMs)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        const auto elapsed = nowMs - trace->startTimeMs.getValue(0);

        const std::string method = trace->method ? trace->method->c_str() : "";
        const std::string path = trace->path ? trace->path->c_str() : "";
        const int statusCode = response->getStatus().code;

        APP_LOG_DEBUG("http", "<-- " + method + " " + path + " status=" + std::to_string(statusCode) +
                                  " duration=" + std::to_string(elapsed) + "ms");
    }

    logging::RequestContext::clear();
    return response;
}

}  // namespace app::middlewares
