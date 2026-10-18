# Compression (Zlib & Gzip)

Optional feature (`APP_ENABLE_ZLIB`, `APP_WITH_ZLIB`). Disabled by default (`OFF`).
When disabled, HTTP traffic runs uncompressed, `app/infrastructure/Gzip.hpp` is excluded from compilation, and no zlib dependencies are searched or linked. Enable with `-DAPP_ENABLE_ZLIB=ON`.

---

## Capabilities & Architecture

OatppBackend integrates compression at two distinct architectural levels:

1. **Automatic HTTP Transport Encoding (Streaming)**:
   Negotiates `gzip` and `deflate` transfer encodings transparently between clients and the server via `oatpp-zlib` provider pipelines.
2. **Standalone Buffer Utilities (`app::infrastructure`)**:
   Provides in-memory `gzipCompress()` and `gzipDecompress()` helpers for payloads, logging exports, or binary caching.

Both mechanisms strictly reside in the infrastructure layer (`Application.cpp` and `app/infrastructure/Gzip.hpp`), keeping business services, controllers, and domain models completely free of compression logic and zlib headers.

---

## CMake & Build Configuration

Zlib compression is controlled by the `APP_ENABLE_ZLIB` CMake option:

```bash
# Configure with Zlib enabled (default)
cmake -B build -DAPP_ENABLE_ZLIB=ON

# Configure minimal build without Zlib (offline / zero external zlib dependencies)
cmake -B build-min -DAPP_ENABLE_ZLIB=OFF
```

### Dependency Resolution (`cmake/dependencies.cmake`)

When `APP_ENABLE_ZLIB=ON`:
- Resolves system Zlib via `find_package(ZLIB REQUIRED)`.
- Resolves Oat++ Zlib wrapper via `find_package(oatpp-zlib 1.3.0 EXACT REQUIRED)`.
- Links `oatpp::oatpp-zlib` and `ZLIB::ZLIB` to `app_core`.
- Defines preprocessor macro `-DAPP_WITH_ZLIB=1`.

When `APP_ENABLE_ZLIB=OFF`:
- No `ZLIB` or `oatpp-zlib` package is searched or linked.
- Macro `APP_WITH_ZLIB` remains undefined.
- Compiling or including `include/app/infrastructure/Gzip.hpp` will trigger a compile-time error (`#error "Gzip.hpp requires APP_WITH_ZLIB"`).

---

## HTTP Content-Encoding Negotiation

Compression is wired into the Oat++ server pipeline inside `Application::initRouting()` (`src/application/Application.cpp`):

```cpp
#ifdef APP_WITH_ZLIB
    auto encoders = std::make_shared<oatpp::web::protocol::http::encoding::ProviderCollection>();
    encoders->add(std::make_shared<oatpp::zlib::DeflateEncoderProvider>());
    encoders->add(std::make_shared<oatpp::zlib::GzipEncoderProvider>());
    components->contentEncodingProviders = encoders;

    auto decoders = std::make_shared<oatpp::web::protocol::http::encoding::ProviderCollection>();
    decoders->add(std::make_shared<oatpp::zlib::DeflateDecoderProvider>());
    decoders->add(std::make_shared<oatpp::zlib::GzipDecoderProvider>());
    components->bodyDecoder = std::make_shared<oatpp::web::protocol::http::incoming::SimpleBodyDecoder>(decoders);
#endif  // APP_WITH_ZLIB
```

### Response Compression (`Accept-Encoding`)

If an incoming HTTP request contains an `Accept-Encoding` header including `gzip` or `deflate`:
- Oat++ automatically streams the response body through the corresponding encoder.
- Sets the HTTP response header `Content-Encoding: gzip` or `Content-Encoding: deflate`.
- If the client does not send `Accept-Encoding` (or requests `identity`), responses are sent uncompressed.

```bash
# Requesting gzip-compressed response
curl -H "Accept-Encoding: gzip" -i http://localhost:8000/auth/who-am-i
```

### Request Body Decompression (`Content-Encoding`)

If a client sends a compressed request payload with `Content-Encoding: gzip` or `Content-Encoding: deflate`:
- The server decodes the incoming stream on the fly using `SimpleBodyDecoder`.
- Controllers and deserializers receive the fully decompressed body bytes without requiring custom parsing logic.

---

## In-Memory Utilities (`app::infrastructure`)

Header: [`include/app/infrastructure/Gzip.hpp`](../include/app/infrastructure/Gzip.hpp)  
Source: [`src/infrastructure/Gzip.cpp`](../src/infrastructure/Gzip.cpp)

### API Contract

```cpp
namespace app::infrastructure
{

/// Compress a payload with gzip (oatpp-zlib). Returns the compressed bytes.
std::string gzipCompress(const std::string& plain);

/// Decompress gzip bytes produced by gzipCompress. Throws std::runtime_error on corrupt/empty input.
std::string gzipDecompress(const std::string& compressed);

}  // namespace app::infrastructure
```

### Usage Example

```cpp
#include "app/infrastructure/Gzip.hpp"
#include <iostream>

void demonstrateCompression()
{
    const std::string payload = "OatppBackend payload repeated for compression demonstration. "
                                "OatppBackend payload repeated for compression demonstration.";

    // Compress
    const std::string compressed = app::infrastructure::gzipCompress(payload);
    std::cout << "Original size: " << payload.size() 
              << " | Compressed size: " << compressed.size() << "\n";

    // Decompress
    try
    {
        const std::string restored = app::infrastructure::gzipDecompress(compressed);
        assert(restored == payload);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Decompression failed: " << ex.what() << "\n";
    }
}
```

### Error Handling

- **Empty input**: Passing an empty string to `gzipDecompress("")` throws `std::runtime_error("gzip decompress failed: empty input")`.
- **Corrupt input**: Passing corrupted or invalid bytes throws `std::runtime_error("gzip decompress failed: corrupt input")`.

---

## Architectural Guardrails

1. **Infrastructure Isolation**:
   - Only `src/application/Application.cpp` and `src/infrastructure/Gzip.cpp` may `#include` Oat++ Zlib headers (`<oatpp-zlib/...>`).
   - Domain services and controllers must never reference `Gzip.hpp` or compression headers directly.
2. **Preprocessor Hygiene**:
   - Any file that interacts with `Gzip.hpp` must be conditioned with `#ifdef APP_WITH_ZLIB`.
   - Never assume `APP_WITH_ZLIB` is present; always verify fallback paths when testing.

---

## Testing & Verification

1. **Unit & Roundtrip Tests (`tests/test_features.cpp`)**:
   - `features: gzip roundtrip`: Verifies `gzipCompress` and `gzipDecompress` roundtrip integrity, checks compressed size reduction on repetitive strings, and asserts exception throwing on corrupt inputs (`"definitely-not-gzip"`).
   - `features: zlib disabled has no gzip API`: Validates compile-time isolation when compiled with `-DAPP_ENABLE_ZLIB=OFF`.

2. **Running the Test Suite**:
   ```bash
   ctest --test-dir build/debug -R test_features --output-on-failure
   ```

3. **Matrix Verification (`scripts/test_matrix.sh`)**:
   - The matrix script validates builds across permutations with `-DAPP_ENABLE_ZLIB=ON` and `-DAPP_ENABLE_ZLIB=OFF`.
