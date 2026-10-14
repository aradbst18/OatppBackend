#pragma once

#include <string>

// Exactly one password hash is active by construction (single-valued
// APP_PASSWORD_HASH_ALGO). Multiple APP_PASSWORD_HASH_SHA* defines (e.g.
// hand-passed -D flags in a non-CMake build) are an error; none means the
// definitions were never applied.
#if defined(APP_PASSWORD_HASH_SHA256) + defined(APP_PASSWORD_HASH_SHA384) + defined(APP_PASSWORD_HASH_SHA512) > 1
#error "APP_PASSWORD_HASH_SHA* defines are mutually exclusive (define exactly one via -DAPP_PASSWORD_HASH_ALGO=SHA256|SHA384|SHA512)"
#elif !defined(APP_PASSWORD_HASH_SHA256) && !defined(APP_PASSWORD_HASH_SHA384) && !defined(APP_PASSWORD_HASH_SHA512)
#error "No APP_PASSWORD_HASH_SHA* define set (configure with -DAPP_PASSWORD_HASH_ALGO=SHA256|SHA384|SHA512)"
#endif  // APP_PASSWORD_HASH algorithm selection (SHA256 / SHA384 / SHA512)

namespace app::auth
{

/**
 * Defined user password hashing protocol (see APP_PASSWORD_HASH_ALGO).
 *
 * Credential hashing is owned here — next to JwtService, the crypto owner —
 * not by repositories (which own SQL only) or services (which own
 * use-cases). Storage (UserRepository) and verification (AuthService) both
 * call passwordHashHex(), so switching algorithms is a rebuild
 * (-DAPP_PASSWORD_HASH_ALGO=...), never a code change. NOTE: changing the
 * algorithm invalidates previously stored hashes (re-seed/re-register users
 * after switching).
 */
std::string sha256Hex(const std::string& input);

/// Hex digest with the compile-time-selected APP_PASSWORD_HASH_ALGO
/// (SHA256/SHA384/SHA512 via OpenSSL EVP).
std::string passwordHashHex(const std::string& input);

/// Human-readable name of the active password hash ("SHA256", "SHA384" or
/// "SHA512"). Exactly one is active by construction (single-valued cache var).
const char* passwordHashAlgoName() noexcept;

}  // namespace app::auth
