#pragma once

#include <memory>

#include "oatpp/core/macro/codegen.hpp"
#include "oatpp/network/ConnectionHandler.hpp"
#include "oatpp/web/server/api/ApiController.hpp"

#include "oatpp-websocket/Handshaker.hpp"

#include OATPP_CODEGEN_BEGIN(ApiController)

namespace app::controllers
{

/**
 * WebSocket upgrade endpoint: GET /ws/echo.
 * Validates the Upgrade header and hands the connection to the websocket
 * ConnectionHandler (EchoInstanceListener). Messaging behavior lives in
 * websocket/EchoListener.hpp, not here.
 *
 * The websocket handler is injected explicitly (no global component lookup)
 * so the controller is constructible in tests without oatpp Environment setup.
 */
class EchoController : public oatpp::web::server::api::ApiController
{
public:
    EchoController(const std::shared_ptr<ObjectMapper>& objectMapper, std::shared_ptr<oatpp::network::ConnectionHandler> websocketHandler);

#if defined(APP_WITH_SWAGGER)
    ENDPOINT_INFO(wsEcho);
#endif  // APP_WITH_SWAGGER

    ENDPOINT("GET", "/ws/echo", wsEcho, REQUEST(std::shared_ptr<IncomingRequest>, request));

private:
    std::shared_ptr<oatpp::network::ConnectionHandler> websocketHandler_;
};

}  // namespace app::controllers

#include OATPP_CODEGEN_END(ApiController)
