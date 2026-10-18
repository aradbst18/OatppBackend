#include "app/auth/PasswordHash.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include "app/errors/AppException.hpp"

namespace app::auth
{

namespace
{

// --- Constants ---------------------------------------------------------------

constexpr int kSaltBytes = 16;
constexpr int kDerivedKeyBytes = 32;
constexpr int kIterations = 100000;

// --- Base64 helpers (OpenSSL-free, URL-safe not needed here) -----------------

static const char kBase64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const unsigned char* data, size_t len)
{
    std::string out;
    out.reserve(4 * ((len + 2) / 3));

    for (size_t i = 0; i < len; i += 3)
    {
        const unsigned int b0 = data[i];
        const unsigned int b1 = (i + 1 < len) ? data[i + 1] : 0;
        const unsigned int b2 = (i + 2 < len) ? data[i + 2] : 0;
        const unsigned int triple = (b0 << 16) | (b1 << 8) | b2;

        out += kBase64Chars[(triple >> 18) & 0x3F];
        out += kBase64Chars[(triple >> 12) & 0x3F];
        out += (i + 1 < len) ? kBase64Chars[(triple >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? kBase64Chars[triple & 0x3F] : '=';
    }

    return out;
}

std::vector<unsigned char> base64Decode(const std::string& encoded)
{
    // Build a reverse lookup table.
    std::array<unsigned char, 256> table{};
    table.fill(0xFF);

    for (int i = 0; i < 64; ++i)
    {
        table[static_cast<unsigned char>(kBase64Chars[i])] = static_cast<unsigned char>(i);
    }

    std::vector<unsigned char> out;
    out.reserve(3 * encoded.size() / 4);

    unsigned int accum = 0;
    int bits = 0;

    for (char c : encoded)
    {
        if (c == '=' || c == '\0')
        {
            break;
        }

        const unsigned char val = table[static_cast<unsigned char>(c)];
        if (val == 0xFF)
        {
            throw errors::crypto::KeyInvalid("Invalid base64 character in stored password hash");
        }

        accum = (accum << 6) | val;
        bits += 6;

        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((accum >> bits) & 0xFF));
        }
    }

    return out;
}

// --- Legacy unsalted EVP digest (kept for sha256Hex and passwordHashHex) -----

std::string evpHex(const std::string& input, const EVP_MD* digest)
{
    if (digest == nullptr)
    {
        throw errors::crypto::KeyInvalid("Unknown password hash algorithm");
    }

    EVP_MD_CTX* raw = EVP_MD_CTX_new();
    if (raw == nullptr)
    {
        throw errors::crypto::KeyInvalid("Cannot create password hash context");
    }

    struct CtxGuard
    {
        EVP_MD_CTX* ctx;
        ~CtxGuard()
        {
            EVP_MD_CTX_free(ctx);
        }
    } guard{raw};

    unsigned char out[EVP_MAX_MD_SIZE]{};
    unsigned int outLen = 0;
    if (EVP_DigestInit_ex(raw, digest, nullptr) != 1 || EVP_DigestUpdate(raw, input.data(), input.size()) != 1 ||
        EVP_DigestFinal_ex(raw, out, &outLen) != 1)
    {
        throw errors::crypto::KeyInvalid("Password hashing failed");
    }

    std::ostringstream hex;
    hex << std::hex << std::setfill('0');

    for (unsigned int i = 0; i < outLen; ++i)
    {
        hex << std::setw(2) << static_cast<int>(out[i]);
    }

    return hex.str();
}

// --- PBKDF2 helpers ----------------------------------------------------------

/// Returns the EVP_MD* for the active APP_PASSWORD_HASH_ALGO.
const EVP_MD* activePrf()
{
#if defined(APP_PASSWORD_HASH_SHA256)
    return EVP_sha256();
#elif defined(APP_PASSWORD_HASH_SHA384)
    return EVP_sha384();
#elif defined(APP_PASSWORD_HASH_SHA512)
    return EVP_sha512();
#endif
}

/// Returns the identifier string embedded in stored hashes (e.g. "pbkdf2-sha256").
const char* prfIdentifier()
{
#if defined(APP_PASSWORD_HASH_SHA256)
    return "pbkdf2-sha256";
#elif defined(APP_PASSWORD_HASH_SHA384)
    return "pbkdf2-sha384";
#elif defined(APP_PASSWORD_HASH_SHA512)
    return "pbkdf2-sha512";
#endif
}

/// Look up the EVP_MD* from a stored PRF identifier string.
const EVP_MD* prfFromIdentifier(const std::string& id)
{
    if (id == "pbkdf2-sha256")
    {
        return EVP_sha256();
    }

    if (id == "pbkdf2-sha384")
    {
        return EVP_sha384();
    }

    if (id == "pbkdf2-sha512")
    {
        return EVP_sha512();
    }

    return nullptr;
}

/// Split a '$'-delimited encoded hash into its fields.
/// Format: $<algorithm>$<iterations>$<base64_salt>$<base64_hash>
std::vector<std::string> splitEncoded(const std::string& encoded)
{
    std::vector<std::string> parts;
    std::string::size_type start = 0;

    if (!encoded.empty() && encoded[0] == '$')
    {
        start = 1;  // skip leading '$'
    }

    while (start < encoded.size())
    {
        const auto pos = encoded.find('$', start);
        if (pos == std::string::npos)
        {
            parts.push_back(encoded.substr(start));
            break;
        }

        parts.push_back(encoded.substr(start, pos - start));
        start = pos + 1;
    }

    return parts;
}

}  // namespace

// --- Public API --------------------------------------------------------------

std::string sha256Hex(const std::string& input)
{
    return evpHex(input, EVP_sha256());
}

std::string passwordHash(const std::string& password)
{
    // 1) Generate a cryptographically random salt.
    unsigned char salt[kSaltBytes]{};
    if (RAND_bytes(salt, kSaltBytes) != 1)
    {
        throw errors::crypto::KeyInvalid("Failed to generate random salt for password hashing");
    }

    // 2) Derive key with PBKDF2-HMAC using the active PRF.
    unsigned char derived[kDerivedKeyBytes]{};
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt, kSaltBytes, kIterations,
                          activePrf(), kDerivedKeyBytes, derived) != 1)
    {
        throw errors::crypto::KeyInvalid("PBKDF2 password hashing failed");
    }

    // 3) Encode as: $pbkdf2-shaXXX$iterations$base64(salt)$base64(derived)
    return std::string("$") + prfIdentifier() + "$" + std::to_string(kIterations) + "$" +
           base64Encode(salt, kSaltBytes) + "$" + base64Encode(derived, kDerivedKeyBytes);
}

bool passwordVerify(const std::string& password, const std::string& storedHash)
{
    // Parse the stored hash: $<algorithm>$<iterations>$<base64_salt>$<base64_hash>
    const auto parts = splitEncoded(storedHash);
    if (parts.size() != 4)
    {
        throw errors::crypto::KeyInvalid("Malformed stored password hash (expected 4 '$'-delimited fields)");
    }

    const std::string& algorithm = parts[0];
    const int iterations = std::stoi(parts[1]);
    const auto salt = base64Decode(parts[2]);
    const auto expected = base64Decode(parts[3]);

    const EVP_MD* prf = prfFromIdentifier(algorithm);
    if (prf == nullptr)
    {
        throw errors::crypto::KeyInvalid("Unknown PRF in stored password hash: " + algorithm);
    }

    if (iterations <= 0 || salt.empty() || expected.empty())
    {
        throw errors::crypto::KeyInvalid("Invalid parameters in stored password hash");
    }

    // Derive with the same parameters and compare.
    std::vector<unsigned char> derived(expected.size());
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(),
                          static_cast<int>(salt.size()), iterations, prf, static_cast<int>(derived.size()),
                          derived.data()) != 1)
    {
        throw errors::crypto::KeyInvalid("PBKDF2 verification failed");
    }

    // Constant-time comparison to prevent timing side-channels.
    if (derived.size() != expected.size())
    {
        return false;
    }

    return CRYPTO_memcmp(derived.data(), expected.data(), derived.size()) == 0;
}

std::string passwordHashHex(const std::string& input)
{
#if defined(APP_PASSWORD_HASH_SHA256) && (defined(APP_PASSWORD_HASH_SHA384) || defined(APP_PASSWORD_HASH_SHA512))
#error \
    "APP_PASSWORD_HASH_SHA* defines are mutually exclusive (configure with -DAPP_PASSWORD_HASH_ALGO=SHA256|SHA384|SHA512)"
#elif defined(APP_PASSWORD_HASH_SHA256)
    return evpHex(input, EVP_sha256());
#elif defined(APP_PASSWORD_HASH_SHA384)
    return evpHex(input, EVP_sha384());
#elif defined(APP_PASSWORD_HASH_SHA512)
    return evpHex(input, EVP_sha512());
#else
#error \
    "No password hash defined (require APP_PASSWORD_HASH_SHA256, _SHA384 or _SHA512 via -DAPP_PASSWORD_HASH_ALGO=SHA256|SHA384|SHA512)"
#endif  // APP_PASSWORD_HASH algorithm selection (SHA256 / SHA384 / SHA512)
}

const char* passwordHashAlgoName() noexcept
{
#if defined(APP_PASSWORD_HASH_SHA256)
    return "SHA256";
#elif defined(APP_PASSWORD_HASH_SHA384)
    return "SHA384";
#elif defined(APP_PASSWORD_HASH_SHA512)
    return "SHA512";
#else
    return "<none>";
#endif  // APP_PASSWORD_HASH algorithm selection (SHA256 / SHA384 / SHA512)
}

std::string generateRandomSecret(size_t numBytes)
{
    if (numBytes == 0)
    {
        return "";
    }

    std::vector<unsigned char> buf(numBytes);
    if (RAND_bytes(buf.data(), static_cast<int>(numBytes)) != 1)
    {
        throw errors::crypto::TokenGenerationFailed("Failed to generate cryptographically secure random bytes");
    }

    return base64Encode(buf.data(), numBytes);
}

}  // namespace app::auth
