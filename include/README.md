# Include Directory Structure

Visual layout of public headers and interface definitions located under `include/app/`.

```text
└── 📁include
    └── 📁app
        ├── 📁application
        │   ├── Application.hpp
        │   ├── ManagedThread.hpp
        │   └── TerminateHandler.hpp
        ├── 📁auth
        │   ├── JwtService.hpp
        │   └── PasswordHash.hpp
        ├── 📁config
        │   ├── AppSettings.hpp
        │   ├── ConfigStore.hpp
        │   └── VersionFile.hpp
        ├── 📁controllers
        │   ├── AuthController.hpp
        │   └── EchoController.hpp
        ├── 📁domain
        │   └── UserRole.hpp
        ├── 📁dto
        │   └── AuthDtos.hpp
        ├── 📁errors
        │   ├── AppException.hpp
        │   └── ErrorMapper.hpp
        ├── 📁infrastructure
        │   ├── 📁database
        │   │   ├── DatabaseExecutor.hpp
        │   │   ├── DatabaseFactory.hpp
        │   │   ├── IDatabase.hpp
        │   │   ├── OrmSupport.hpp
        │   │   ├── PostgresqlDatabase.hpp
        │   │   ├── PostgresqlLocalServer.hpp
        │   │   └── SqlcipherDatabase.hpp
        │   ├── 📁documents
        │   │   ├── DocumentValue.hpp
        │   │   ├── JsonDocument.hpp
        │   │   ├── PostgresqlDocument.hpp
        │   │   ├── SqlcipherDocument.hpp
        │   │   ├── TomlDocument.hpp
        │   │   ├── XmlDocument.hpp
        │   │   └── YamlDocument.hpp
        │   ├── Gzip.hpp
        │   ├── Swagger.hpp
        │   └── TextFile.hpp
        ├── 📁logging
        │   ├── Logger.hpp
        │   └── OatppLoggerBridge.hpp
        ├── 📁middlewares
        │   ├── AuthInterceptor.hpp
        │   ├── CorsInterceptor.hpp
        │   └── RateLimitInterceptor.hpp
        ├── 📁repositories
        │   ├── BaseRepository.hpp
        │   ├── RefreshTokenDto.hpp
        │   ├── RefreshTokenRepository.hpp
        │   ├── UserDto.hpp
        │   └── UserRepository.hpp
        ├── 📁serialization
        │   ├── AppObjectMapper.hpp
        │   └── RangeValidatingDeserializer.hpp
        ├── 📁services
        │   └── AuthService.hpp
        ├── 📁validation
        │   ├── NumericBounds.hpp
        │   └── Validators.hpp
        └── 📁websocket
            └── EchoListener.hpp
```

## Module Directory Reference

| Module | Primary Responsibility |
| :--- | :--- |
| **`application/`** | Process lifecycle orchestration, managed background threads (`ManagedThread`), and signal termination handlers |
| **`auth/`** | JWT token lifecycle management (`JwtService`) and cryptographic password hashing (`PasswordHash`) |
| **`config/`** | Strongly-typed application settings (`AppSettings`), 4-store configuration parser interfaces, and version file loader |
| **`controllers/`** | Oat++ REST API endpoint controllers (`AuthController`, `EchoController`) operating strictly on DTOs |
| **`domain/`** | Core business domain models and types (`UserRole`) free from framework dependencies |
| **`dto/`** | Data Transfer Objects representing API wire models and serialized request/response contracts |
| **`errors/`** | Domain exception hierarchy (`AppException`) and framework error mapper (`ErrorMapper`) |
| **`infrastructure/`** | Database abstraction (`IDatabase`, SQLCipher, PostgreSQL), generic document parsers, GZIP compression, and Swagger |
| **`logging/`** | Thread-safe logging subsystem (`Logger`) and Oat++ log bridge integration (`OatppLoggerBridge`) |
| **`middlewares/`** | HTTP request interceptors for Bearer token verification, CORS headers, and token-bucket rate limiting |
| **`repositories/`** | Persistence abstraction layer and ORM query templates over `DatabaseExecutor` |
| **`serialization/`** | JSON object mapper configuration (`AppObjectMapper`) and range-validating deserializers |
| **`services/`** | Pure C++ business logic use cases (`AuthService`) decoupled from Oat++ HTTP layer |
| **`validation/`** | Input validation rules, username/password constraints, and numeric range bounds |
| **`websocket/`** | WebSocket connection listeners and real-time event handlers (`EchoListener`) |