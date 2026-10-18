# Source Directory Structure

Visual layout of C++ implementation files located under `src/`.

```text
└── 📁src
    ├── 📁application
    │   ├── Application.cpp
    │   ├── ManagedThread.cpp
    │   └── TerminateHandler.cpp
    ├── 📁auth
    │   ├── JwtService.cpp
    │   └── PasswordHash.cpp
    ├── 📁config
    │   ├── AppSettings.cpp
    │   ├── ConfigFactory.cpp
    │   ├── JsonConfigStore.cpp
    │   ├── TomlConfigStore.cpp
    │   ├── VersionFile.cpp
    │   ├── XmlConfigStore.cpp
    │   └── YamlConfigStore.cpp
    ├── 📁controllers
    │   ├── AuthController.cpp
    │   └── EchoController.cpp
    ├── 📁domain
    │   └── UserRole.cpp
    ├── 📁dto
    │   └── AuthDtos.cpp
    ├── 📁errors
    │   ├── AppException.cpp
    │   └── ErrorMapper.cpp
    ├── 📁infrastructure
    │   ├── 📁database
    │   │   ├── DatabaseExecutor.cpp
    │   │   ├── DatabaseFactory.cpp
    │   │   ├── OrmSupport.cpp
    │   │   ├── PostgresqlDatabase.cpp
    │   │   ├── PostgresqlLocalServer.cpp
    │   │   └── SqlcipherDatabase.cpp
    │   ├── 📁documents
    │   │   ├── DocumentValue.cpp
    │   │   ├── JsonDocument.cpp
    │   │   ├── PostgresqlDocument.cpp
    │   │   ├── SqlcipherDocument.cpp
    │   │   ├── TomlDocument.cpp
    │   │   ├── XmlDocument.cpp
    │   │   └── YamlDocument.cpp
    │   ├── Gzip.cpp
    │   ├── Swagger.cpp
    │   └── TextFile.cpp
    ├── 📁logging
    │   ├── Logger.cpp
    │   └── OatppLoggerBridge.cpp
    ├── 📁middlewares
    │   ├── AuthInterceptor.cpp
    │   ├── CorsInterceptor.cpp
    │   └── RateLimitInterceptor.cpp
    ├── 📁repositories
    │   ├── BaseRepository.cpp
    │   ├── RefreshTokenRepository.cpp
    │   └── UserRepository.cpp
    ├── 📁serialization
    │   └── AppObjectMapper.cpp
    ├── 📁services
    │   └── AuthService.cpp
    ├── 📁validation
    │   └── Validators.cpp
    ├── 📁websocket
    │   └── EchoListener.cpp
    └── main.cpp
```

## Module Directory Reference

| Module | Primary Implementation |
| :--- | :--- |
| **`application/`** | Process bootstrap and shutdown sequences, managed thread lifecycles, and POSIX signal handlers |
| **`auth/`** | OpenSSL EVP password hashing with salt verification and jwt-cpp token encoding/decoding |
| **`config/`** | JSON, TOML, XML, and YAML configuration loading with fallback auto-completion and version parsing |
| **`controllers/`** | HTTP request routing, header parsing, payload extraction, and DTO response dispatching |
| **`domain/`** | Domain enumeration parsing and string conversions (`UserRole`) |
| **`dto/`** | Oat++ DTO field macros and serialization bindings |
| **`errors/`** | Exception construction, HTTP status code translation, and error payload formatting |
| **`infrastructure/`** | Dual-backend database connections (SQLCipher & PostgreSQL), document tree serialization, and Swagger setup |
| **`logging/`** | Thread-safe, non-blocking log appenders with console formatting and Oat++ framework log redirection |
| **`middlewares/`** | Pipeline interceptors verifying JWT Bearer tokens, appending CORS headers, and enforcing request rate limits |
| **`repositories/`** | SQL query execution, result-set cursor mapping, and schema initialization via `ensureSchema()` |
| **`serialization/`** | Oat++ ObjectMapper customization with strict range validation deserializers |
| **`services/`** | Core business use cases (authentication, credential verification, refresh token generation) |
| **`validation/`** | Regex and rule-based validation for usernames, passwords, and numerical field bounds |
| **`websocket/`** | Real-time WebSocket connection handling, message frames, and ping/pong keep-alives |
| **`main.cpp`** | Process entry point parsing CLI arguments, initializing configuration, and invoking `Application::run()` |