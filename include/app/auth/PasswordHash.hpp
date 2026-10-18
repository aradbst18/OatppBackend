#pragma once

#include <string>

// Exactly one password hash is active by construction (single-valued
// APP_PASSWORD_HASH_ALGO). Multiple APP_PASSWORD_HASH_SHA* defines (e.g.
// hand-passed -D flags in a non-CMake build) are an error; none means the
// definitions were never applied.
#if defined(APP_PASSWORD_HASH_SHA256) + defined(APP_PASSWORD_HASH_SHA384) + defined(APP_PASSWORD_HASH_SHA512) > 1
#error \
    "APP_PASSWORD_HASH_SHA* defines are mutually exclusive (define exactly one via -DAPP_PASSWORD_HASH_ALGO=SHA256|SHA384|SHA512)"
#elif !defined(APP_PASSWORD_HASH_SHA256) && !defined(APP_PASSWORD_HASH_SHA384) && !defined(APP_PASSWORD_HASH_SHA512)
#error "No APP_PASSWORD_HASH_SHA* define set (configure with -DAPP_PASSWORD_HASH_ALGO=SHA256|SHA384|SHA512)"
#endif  // APP_PASSWORD_HASH algorithm selection (SHA256 / SHA384 / SHA512)

namespace app::auth
{

/**
 * Salted password hashing with PBKDF2-HMAC.
 *
 * Credential hashing is owned here — next to JwtService, the crypto owner —
 * not by repositories (which own SQL only) or services (which own
 * use-cases). Storage (UserRepository) calls passwordHash(); verification
 * (AuthService) calls passwordVerify().
 *
 * The stored format is self-describing:
 *   $pbkdf2-sha256$<iterations>$<base64_salt>$<base64_hash>
 *
 * This embeds the algorithm, cost and salt so future upgrades (Argon2id,
 * higher iterations) can verify old hashes without a flag day. The PRF
 * digest follows the compile-time APP_PASSWORD_HASH_ALGO selection
 * (SHA256/SHA384/SHA512).
 *
 * NOTE: changing APP_PASSWORD_HASH_ALGO invalidates previously stored
 * hashes (re-seed/re-register users after switching) because the PRF
 * identifier in the stored string will no longer match.
 */

/// SHA-256 hex digest — low-level utility for non-password uses (e.g.
/// checksums). NOT suitable for password storage; use passwordHash().
std::string sha256Hex(const std::string& input);

/// Hash a password with PBKDF2-HMAC using a cryptographically random 16-byte
/// salt and 100 000 iterations. Returns an encoded string:
///   $pbkdf2-sha256$100000$<base64_salt>$<base64_hash>
/// Each call produces a different output (different salt).
std::string passwordHash(const std::string& password);

/// Verify a plaintext password against a stored PBKDF2 hash produced by
/// passwordHash(). Extracts algorithm, iterations and salt from the stored
/// string and recomputes the hash. Returns true on match, false otherwise.
/// Never throws for wrong passwords; throws only on malformed stored hashes.
bool passwordVerify(const std::string& password, const std::string& storedHash);

/// Legacy unsalted hex digest — DEPRECATED. Kept only for test backward
/// compatibility and non-password uses. Do NOT use for new password storage.
std::string passwordHashHex(const std::string& input);

/// Human-readable name of the active password hash PRF ("SHA256", "SHA384" or
/// "SHA512"). Exactly one is active by construction (single-valued cache var).
const char* passwordHashAlgoName() noexcept;

/// Generate cryptographically secure random bytes encoded as base64 (e.g. for JWT secrets).
std::string generateRandomSecret(size_t numBytes = 32);

}  // namespace app::auth
