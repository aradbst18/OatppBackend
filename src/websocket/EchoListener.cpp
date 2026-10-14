#include "app/websocket/EchoListener.hpp"

#include "app/logging/Logger.hpp"

namespace app::websocket
{

void EchoListener::onPing(const WebSocket& socket, const oatpp::String& message)
{
    socket.sendPong(message);
}

void EchoListener::onPong(const WebSocket& socket, const oatpp::String& message)
{
    (void)socket;
    (void)message;
}

void EchoListener::onClose(const WebSocket& socket, v_uint16 code, const oatpp::String& message)
{
    (void)socket;
    (void)code;
    (void)message;
}

void EchoListener::readMessage(const WebSocket& socket, v_uint8 opcode, p_char8 data, oatpp::v_io_size size)
{
    if (size == 0)
    {
        // End-of-message marker for fragmented frames; single-frame echoes are
        // already sent below. Closing here would break normal single-frame use,
        // so only close on an explicitly empty message.
        return;
    }

    if (opcode == oatpp::websocket::Frame::OPCODE_TEXT)
    {
        oatpp::String text(reinterpret_cast<const char*>(data), static_cast<v_buff_size>(size));
        logging::logDebug("websocket", "Echoing text message");
        const std::string reply = "Server echo: " + std::string(text->c_str());
        socket.sendOneFrameText(reply.c_str());
    }
    else if (opcode == oatpp::websocket::Frame::OPCODE_BINARY)
    {
        // Echo binary payloads as binary to stay protocol-correct.
        oatpp::String blob(reinterpret_cast<const char*>(data), static_cast<v_buff_size>(size));
        socket.sendOneFrameBinary(blob);
    }
    else
    {
        socket.sendClose();
    }
}

void EchoInstanceListener::onAfterCreate(const oatpp::websocket::WebSocket& socket, const std::shared_ptr<const ParameterMap>& params)
{
    (void)params;
    sockets_.fetch_add(1);
    logging::logDebug("websocket", "WebSocket connected");
    socket.setListener(std::make_shared<EchoListener>());
}

void EchoInstanceListener::onBeforeDestroy(const oatpp::websocket::WebSocket& socket)
{
    (void)socket;
    sockets_.fetch_sub(1);
    logging::logDebug("websocket", "WebSocket disconnected");
}

}  // namespace app::websocket
