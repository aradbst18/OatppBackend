#include "app/controllers/EchoController.hpp"

#include "oatpp-websocket/Handshaker.hpp"

namespace app::controllers
{

EchoController::EchoController(const std::shared_ptr<ObjectMapper>& objectMapper,
                               std::shared_ptr<oatpp::network::ConnectionHandler> websocketHandler)
    : ApiController(objectMapper), websocketHandler_(std::move(websocketHandler))
{
    if (!websocketHandler_)
    {
        throw std::runtime_error("EchoController requires a websocket ConnectionHandler");
    }
}

#ifdef APP_WITH_SWAGGER

void EchoController::Z__ENDPOINT_ADD_INFO_wsEcho(const std::shared_ptr<oatpp::web::server::api::Endpoint::Info>& info)
{
    info->summary = "Open a WebSocket echo channel";
    info->description =
        R"DOC(Public WebSocket endpoint — **no `Authorization` header required**. Performs the HTTP -> WebSocket upgrade handshake and then echoes every message back on the same connection. Ideal first WebSocket to wire up in the frontend.

## Handshake (what the client must send)

A plain `GET /ws/echo` is **not** enough — the request must be a valid WebSocket upgrade:

- `GET /ws/echo` with `Upgrade: websocket` (exact, lowercase value checked by this controller).
- `Connection: Upgrade`, `Host`, `Sec-WebSocket-Key`, and `Sec-WebSocket-Version: 13` (added automatically by browsers / `wscat` / `ws` libraries; `oatpp::websocket::Handshaker` validates them).

If `Upgrade` is missing or not exactly `websocket`, the server answers `400 Expected WebSocket upgrade` as `text/plain` and never upgrades. On success it answers `101 Switching Protocols` and hands the socket to `EchoInstanceListener` (one `EchoListener` per connection).

## Why "Try it out" in Swagger UI answers 400

Swagger UI issues a plain `GET` without `Upgrade: websocket`, so you will always see the `400` path here. That is expected — use a real WebSocket client instead (see below).

## Message protocol (after `101`)

| Client sends | Server answers |
|--------------|----------------|
| Text frame `"hello"` | Text frame `"Server echo: hello"` (`"Server echo: "` + payload). |
| Binary frame (bytes) | Same bytes echoed back as binary (protocol-correct binary echo). |
| Ping | Pong with the same payload (automatic keep-alive). |
| Pong / Close | Tracked / tears the connection down; live-socket counter decremented. |
| Other opcodes | Connection closed (fail-closed). |

Empty fragmented markers (`size == 0`) are ignored; explicitly empty messages do not produce a reply. There is no JSON envelope on the socket — just raw frames.

## Frontend examples

```js
// Browser (native WebSocket)
const ws = new WebSocket("ws://localhost:8080/ws/echo");
ws.onopen = () => ws.send("hello from frontend");
ws.onmessage = (ev) => console.log("echoed:", ev.data); // "Server echo: hello from frontend"
ws.onerror = (e) => console.error("ws error", e);
ws.onclose = (ev) => console.log("closed", ev.code, ev.reason);
```

```bash
# CLI check with wscat (npm i -g wscat)
wscat -c ws://localhost:8080/ws/echo
> hello
< Server echo: hello
```

```python
# Python quick check (pip install websocket-client)
from websocket import create_connection
ws = create_connection("ws://localhost:8080/ws/echo")
ws.send("hello")
print(ws.recv())  # Server echo: hello
ws.close()
```

## Responses

- `101 Switching Protocols`: upgrade accepted. The HTTP exchange ends here; everything after is WebSocket frames. There is no JSON body to show — connect with a WS client.
- `400 Bad Request`: upgrade headers missing/invalid. Plain-text body `Expected WebSocket upgrade`. Fix by sending a real WebSocket handshake (browsers do this for you via `new WebSocket(...)`).

No authentication, no request body, no JSON — just the upgrade headers above.)DOC";

    info->addTag("websocket");

    // Document the handshake headers so frontend authors know what a WS client
    // sends automatically. These are informational only (the endpoint reads the
    // raw REQUEST); they do not change routing. NOTE: oatpp-swagger 1.3.1's
    // Generator drops header params via its filteredHeaders copy (order lost),
    // so the description above carries the handshake docs even when `parameters`
    // renders empty — keep both in sync.
    info->headers.add<String>("Upgrade").description =
        "Must be exactly `websocket` or the server returns 400 without upgrading.";
    info->headers["Upgrade"].required = true;
    info->headers["Upgrade"].addExample("WebSocket upgrade", oatpp::String("websocket"));

    info->headers.add<String>("Connection").description =
        "Must contain `Upgrade` for a valid WebSocket handshake (sent automatically by WS "
        "clients).";
    info->headers["Connection"].required = true;
    info->headers["Connection"].addExample("Upgrade connection", oatpp::String("Upgrade"));

    info->addResponse<String>(Status::CODE_101, "application/octet-stream",
                              "Switching Protocols — handshake accepted and the connection is now a WebSocket. "
                              "No JSON body: open `ws://<host>/ws/echo` with a WebSocket client; every text message "
                              "returns `Server echo: <message>`, binary is echoed as binary, pings get pongs.");

    info->addResponse<String>(Status::CODE_400, "text/plain",
                              "Bad Request — not a WebSocket upgrade. The `Upgrade` header was "
                              "missing or not exactly "
                              "`websocket`. Body is plain text `Expected WebSocket upgrade`. "
                              "Swagger UI \"Try it out\" "
                              "always hits this path because it sends a plain GET; use `new "
                              "WebSocket(...)` or `wscat` "
                              "instead.")
        .addExample("Missing Upgrade header", oatpp::String("Expected WebSocket upgrade"));
}

#endif  // APP_WITH_SWAGGER

std::shared_ptr<EchoController::OutgoingResponse> EchoController::wsEcho(
    const std::shared_ptr<IncomingRequest>& request)
{
    const auto upgrade = request->getHeader("Upgrade");
    if (!upgrade || std::string(upgrade->c_str()) != "websocket")
    {
        return createResponse(Status::CODE_400, "Expected WebSocket upgrade");
    }

    return oatpp::websocket::Handshaker::serversideHandshake(request->getHeaders(), websocketHandler_);
}

}  // namespace app::controllers
