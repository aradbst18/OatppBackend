# WebSocket

Foundation: one public echo endpoint demonstrating the full upgrade +
messaging path. Future features follow the same split.

## Endpoint

`GET /ws/echo` (`EchoController`): checks the `Upgrade: websocket` header
(400 otherwise) and delegates to `oatpp::websocket::Handshaker::
serversideHandshake` with the injected `ConnectionHandler`. Public (no auth)
by design for the echo demo; add it to `AuthInterceptor::publicPaths_` — done.

## Messaging

- `EchoInstanceListener` (per-process): assigns one `EchoListener` per
  connection, counts live sockets atomically.
- `EchoListener` (per-connection):
  - text frame -> replies `Server echo: <message>` via `sendOneFrameText`;
  - binary frame -> echoed back as binary;
  - ping -> pong; other opcodes -> close.
- No business logic in the listener. Parse/validate in a service for real
  features; keep the listener as transport.

## Swagger

The endpoint documents 101/400. The generator requires no security entries
for public WS endpoints — do not add `addSecurityRequirement` without
`info->authorization` (it asserts).

## Adding new WebSocket behavior

1. Add a method to a service for the message logic (unit-testable).
2. Call it from `EchoListener::readMessage` (or a new listener) and send the
   result with `sendOneFrameText/Binary`.
3. For a new path, add a controller method + route + interceptor entry +
   an integration test following `test_websocket.cpp` (raw socket handshake,
   masked client frame, looped recv until `2+len` bytes).

## Testing

`test_websocket.cpp` performs a real handshake (`Sec-WebSocket-Key:
dGhlIHNhbXBsZSBub25jZQ==`, expects 101), sends a masked `hello` frame, and
asserts the payload is `Server echo: hello`, tolerating header/payload TCP
segmentation.
