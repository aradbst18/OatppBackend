# Examples

## Login

Use the username and private password configured during
[administrator provisioning](authentication.md#administrator-provisioning).
The placeholders below must be replaced with those credentials.

```bash
curl -X POST http://127.0.0.1:8080/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"<admin-username>","password":"<your-private-password>"}'
```

```json
{
  "access_token": "eyJhbGciOiJIUzI1NiJ9...",
  "token_type": "Bearer",
  "username": "admin",
  "role": "admin",
  "expires_in": 3600
}
```

## Authenticated who-am-i

```bash
TOKEN=$(curl -s -X POST http://127.0.0.1:8080/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"<admin-username>","password":"<your-private-password>"}' \
  | python3 -c 'import sys,json; print(json.load(sys.stdin)["access_token"])')
curl http://127.0.0.1:8080/auth/who-am-i -H "Authorization: Bearer $TOKEN"
```

```json
{"message": "Hello, authenticated world", "username": "admin", "role": "admin"}
```

## Unauthorized

```bash
curl -w '\nHTTP:%{http_code}\n' http://127.0.0.1:8080/auth/who-am-i
```

```text
Missing or malformed Authorization header
# HTTP status 401, text/plain
```

```bash
curl -X POST http://127.0.0.1:8080/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"admin","password":"Wrong1234"}'
# -> 401 text/plain "Invalid credentials"
```

## Int8 range rejection

```bash
curl -X POST http://127.0.0.1:8080/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"<admin-username>","password":"<your-private-password>","pinCode":64654646}'
# -> 400, deserialization "Value out of range"; never reaches the service
```

## WebSocket echo (Python)

```python
import socket
s = socket.create_connection(("127.0.0.1", 8080), timeout=5)
s.sendall(b"GET /ws/echo HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\n"
          b"Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
          b"Sec-WebSocket-Version: 13\r\n\r\n")
print(s.recv(1024).decode())  # HTTP/1.1 101 ...
mask = b"\x11\x22\x33\x44"
payload = b"hello"
frame = bytes([0x81, 0x80 | len(payload)]) + mask + bytes(
    b ^ mask[i % 4] for i, b in enumerate(payload))
s.sendall(frame)
data = b""
while len(data) < 20:
    data += s.recv(4096)
print(data[2:])  # b'Server echo: hello'
```

## Configuration

The runtime file is `configs/app.conf` in the active format's syntax (JSON
shown here). Samples for every format live in
`configs/app.{json,toml,xml,yaml}`; a missing file is created from defaults
on startup.

```json
{
  "server": {"host": "127.0.0.1", "port": 8080},
  "database": {"path": "data/app.db"},
  "jwt": {"secret": "change-me-in-production", "issuer": "oatpp-backend", "ttlSeconds": 3600},
  "logging": {"level": "INFO", "path": "logs"}
}
```

TOML, XML, and YAML equivalents live in `configs/app.toml` / `configs/app.xml`
/ `configs/app.yaml` (same groups and leaf names). `database.key` is present
in encrypted SQLCIPHER and POSTGRESQL files and omitted in plaintext SQLCIPHER
files (see [`docs/configuration.md`](docs/configuration.md)).

## CMake configurations

```bash
# Default full build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
# Minimal (no Swagger/Zlib, TOML)
cmake -S . -B build-min -DAPP_ENABLE_SWAGGER=OFF -DAPP_ENABLE_ZLIB=OFF \
  -DAPP_CONFIG_FORMAT=TOML && cmake --build build-min -j
# Encrypted database
cmake -S . -B build-cipher -DAPP_USERS_DATABASE_TYPE=SQLCIPHER \
  -DAPP_ENCRYPTION_USER_DATABASE=ON && cmake --build build-cipher -j
```
