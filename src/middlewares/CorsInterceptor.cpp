#include "app/middlewares/CorsInterceptor.hpp"

#include <sstream>

namespace app::middlewares
{

CorsInterceptor::CorsInterceptor(const std::string& allowedOrigins, const std::string& allowedMethods,
                                 const std::string& allowedHeaders, const std::string& maxAgeSeconds)
    : allowedOriginsRaw_(allowedOrigins),
      allowedMethods_(allowedMethods),
      allowedHeaders_(allowedHeaders),
      maxAgeSeconds_(maxAgeSeconds)
{
    if (allowedOriginsRaw_ == "*" || allowedOriginsRaw_.empty())
    {
        allowAllOrigins_ = true;
    }
    else
    {
        allowAllOrigins_ = false;
        std::stringstream ss(allowedOriginsRaw_);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            // Trim whitespace
            auto start = item.find_first_not_of(" \t\r\n");
            auto end = item.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos)
            {
                allowedOrigins_.insert(item.substr(start, end - start + 1));
            }
        }
    }
}

bool CorsInterceptor::isOriginAllowed(const std::string& origin) const
{
    if (allowAllOrigins_)
    {
        return true;
    }

    return allowedOrigins_.find(origin) != allowedOrigins_.end();
}

void CorsInterceptor::attachCorsHeaders(const std::shared_ptr<IncomingRequest>& request,
                                        const std::shared_ptr<OutgoingResponse>& response) const
{
    const auto originHeader = request->getHeader("Origin");
    std::string origin = originHeader ? std::string(*originHeader) : "";

    std::string effectiveOrigin = "*";
    if (allowAllOrigins_)
    {
        effectiveOrigin = origin.empty() ? "*" : origin;
    }
    else if (!origin.empty() && isOriginAllowed(origin))
    {
        effectiveOrigin = origin;
        response->putOrReplaceHeader("Vary", "Origin");
    }
    else if (!allowAllOrigins_)
    {
        return;
    }

    response->putOrReplaceHeader("Access-Control-Allow-Origin", effectiveOrigin);
    response->putOrReplaceHeader("Access-Control-Allow-Methods", allowedMethods_);
    response->putOrReplaceHeader("Access-Control-Allow-Headers", allowedHeaders_);
    response->putOrReplaceHeader("Access-Control-Max-Age", maxAgeSeconds_);
    response->putOrReplaceHeader("Access-Control-Allow-Credentials", "true");
}

std::shared_ptr<CorsInterceptor::OutgoingResponse> CorsInterceptor::intercept(
    const std::shared_ptr<IncomingRequest>& request)
{
    const std::string method = request->getStartingLine().method.std_str();
    if (method == "OPTIONS")
    {
        auto response = oatpp::web::protocol::http::outgoing::ResponseFactory::createResponse(
            oatpp::web::protocol::http::Status::CODE_204, "");
        attachCorsHeaders(request, response);
        return response;
    }

    return nullptr;
}

std::shared_ptr<CorsInterceptor::OutgoingResponse> CorsInterceptor::intercept(
    const std::shared_ptr<IncomingRequest>& request, const std::shared_ptr<OutgoingResponse>& response)
{
    if (response)
    {
        attachCorsHeaders(request, response);
    }

    return response;
}

}  // namespace app::middlewares
