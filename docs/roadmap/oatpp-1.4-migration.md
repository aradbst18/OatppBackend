# Oat++ 1.4.x Migration Guide & Planning

This document tracks the planning and roadmap for upgrading OatppBackend from **Oat++ 1.3.0** to **Oat++ 1.4.x**.

## Motivation

Oat++ 1.3.0 is a stable release, but 1.4.x brings several major architectural improvements:
- **Refined Async APIs & Coroutines:** Native C++20 coroutine support (`co_await`), drastically reducing memory overhead and callback complexity for high-concurrency microservices.
- **Built-in Connection Pooling Enhancements:** First-class native connection pool management across database and client adapters.
- **WebSocket Enhancements:** Cleaner framing and backpressure handling for high-throughput streaming.
- **Updated OpenSSL & TLS Integration:** Native OpenSSL 3.x provider fixes and memory lifecycle improvements.

## Compatibility & Breaking Changes

1. **Header Paths:**
   - Oat++ 1.4.x reorganizes some core headers under `oatpp/` namespace subdirectories.
2. **DTO Macros:**
   - `DTO_INIT` and code generation macros remain largely backward-compatible.
3. **Database Client (`DbClient`):**
   - The ORM executor interfaces in 1.4.x standardize transaction scopes. Our current custom `DatabaseExecutor` transparent transaction tracking will map cleanly to the 1.4.x connection context.
4. **Third-Party Module Versions:**
   - Upgrading `oatpp` requires simultaneous upgrades to matching 1.4.x branches for:
     - `oatpp-swagger`
     - `oatpp-websocket`
     - `oatpp-zlib`
     - `oatpp-sqlite`
     - `oatpp-postgresql`

## Step-by-Step Upgrade Checklist

- [ ] Tag the current 1.3.0 baseline release.
- [ ] Build and install Oat++ 1.4.x and matching modules into a staging environment (`/usr/local` or vendor path).
- [ ] Update `CMakeLists.txt` version constraints (`find_package(oatpp 1.4.0)`).
- [ ] Verify `DTO` generation and serialization with JSON, TOML, XML, and YAML.
- [ ] Run test suite (`ctest --output-on-failure`) across all 14 test suites.
- [ ] Re-run sanitizers (`debug-asan-ubsan`, `debug-tsan`).
