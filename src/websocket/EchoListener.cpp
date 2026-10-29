#include "app/websocket/EchoListener.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

#include "app/logging/Logger.hpp"

namespace app::websocket
{
namespace
{

bool isValidUtf8(const std::string& text)
{
    for (std::size_t i = 0; i < text.size();)
    {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first <= 0x7F)
        {
            continue;
        }
        if (first < 0xC2 || first > 0xF4)
        {
            return false;
        }
        const std::size_t trailing = first < 0xE0 ? 1 : first < 0xF0 ? 2 : 3;
        if (text.size() - i < trailing)
        {
            return false;
        }
        std::uint32_t code = first & (0x3FU >> trailing);
        for (std::size_t j = 0; j < trailing; ++j)
        {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xC0) != 0x80)
            {
                return false;
            }
            code = (code << 6) | (next & 0x3F);
        }
        const std::uint32_t minimum = trailing == 1 ? 0x80 : trailing == 2 ? 0x800 : 0x10000;
        if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
        {
            return false;
        }
    }
    return true;
}

[[noreturn]] void closeWithError(const oatpp::websocket::WebSocket& socket, v_uint16 code, const char* reason)
{
    socket.sendClose(code, reason);
    // Unwind oatpp's current payload read, not just the outer frame loop.
    throw std::runtime_error(reason);
}

}  // namespace

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
    buffer_.reset();
}

void EchoListener::readMessage(const WebSocket& socket, v_uint8 opcode, p_char8 data, oatpp::v_io_size size)
{
    if (opcode != oatpp::websocket::Frame::OPCODE_TEXT && opcode != oatpp::websocket::Frame::OPCODE_BINARY)
    {
        buffer_.reset();
        closeWithError(socket, 1002, "Invalid message opcode");
    }
    if (size > 0)
    {
        if (size > maxMessageSize_ - buffer_.getCurrentPosition())
        {
            buffer_.reset();
            closeWithError(socket, 1009, "Message exceeds 1 MiB");
        }
        buffer_.writeSimple(data, size);
        return;
    }

    const auto message = buffer_.toString();
    buffer_.reset();
    if (opcode == oatpp::websocket::Frame::OPCODE_TEXT)
    {
        if (!isValidUtf8(*message))
        {
            closeWithError(socket, 1007, "Invalid UTF-8 text");
        }
        logging::logDebug("websocket", "Echoing text message");
        socket.sendOneFrameText("Server echo: " + *message);
    }
    else
    {
        socket.sendOneFrameBinary(message);
    }
}

void EchoInstanceListener::onAfterCreate(const oatpp::websocket::WebSocket& socket,
                                         const std::shared_ptr<const ParameterMap>& params)
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
