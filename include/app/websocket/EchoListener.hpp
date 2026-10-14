#pragma once

#include <atomic>
#include <memory>

#include "oatpp-websocket/ConnectionHandler.hpp"
#include "oatpp-websocket/WebSocket.hpp"

namespace app::websocket
{

/**
 * Per-connection echo listener: replies to every text message with
 * "Server echo: <message>" and answers pings with pongs. Binary frames are
 * echoed as text only when they form valid UTF-8; otherwise the connection
 * is closed (fail-closed on malformed input).
 */
class EchoListener : public oatpp::websocket::WebSocket::Listener
{
public:
    ~EchoListener() override = default;
    void onPing(const WebSocket& socket, const oatpp::String& message) override;
    void onPong(const WebSocket& socket, const oatpp::String& message) override;
    void onClose(const WebSocket& socket, v_uint16 code, const oatpp::String& message) override;
    void readMessage(const WebSocket& socket, v_uint8 opcode, p_char8 data, oatpp::v_io_size size) override;

private:
    oatpp::data::stream::BufferOutputStream buffer_;
};

/**
 * Assigns one EchoListener per connection and tracks live socket count.
 * Registered on the websocket ConnectionHandler during Application startup.
 *
 * NOTE: oatpp's SocketInstanceListener has a non-virtual destructor, so this
 * class intentionally follows the base design (owned via shared_ptr to the
 * concrete type, never deleted through the base). The -Wnon-virtual-dtor
 * warning is suppressed locally for this inheritance.
 */
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif  // __GNUC__

class EchoInstanceListener : public oatpp::websocket::ConnectionHandler::SocketInstanceListener
{
public:
    virtual ~EchoInstanceListener() = default;
    void onAfterCreate(const oatpp::websocket::WebSocket& socket, const std::shared_ptr<const ParameterMap>& params) override;
    void onBeforeDestroy(const oatpp::websocket::WebSocket& socket) override;

    std::int32_t liveSockets() const noexcept
    {
        return sockets_.load();
    }

private:
    std::atomic<std::int32_t> sockets_{0};
};

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif  // __GNUC__

}  // namespace app::websocket
