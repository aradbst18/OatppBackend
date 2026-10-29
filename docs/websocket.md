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
  - complete text message -> replies `Server echo: <message>` via `sendOneFrameText`;
  - complete binary message -> echoed back as binary;
  - chunks and continuation frames are accumulated until message completion, including empty messages;
  - incoming payloads are limited to 1 MiB across all fragments (close code 1009 on overflow);
  - text is validated as UTF-8 after assembly (close code 1007 for malformed text);
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
   masked client frames, decoded headers and complete payloads).

## Testing

`test_websocket.cpp` performs a real handshake, sends masked text and binary
messages, and decodes short, 16-bit, and 64-bit payload lengths. Coverage includes
empty and NUL-containing payloads, read-buffer boundaries, large messages,
fragmentation with interleaved pings, split UTF-8 characters, and messages after
fragmentation. Pong barriers check that exactly one echo is sent per complete
message. Boundary and overflow cases verify the 1 MiB limit, including fragmented
messages and unfinished oversized frames; malformed UTF-8 must close with 1007.

## Dependency compatibility

`cmake/oatpp_websocket.cmake` builds the existing bundled oatpp-websocket 1.3.0
archive offline. It corrects the synchronous reader to retain the original
text/binary opcode through continuation frames and reset its continuation state
at message completion. System-installed WebSocket libraries are not modified.
