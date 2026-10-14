#include "app/auth/PasswordHash.hpp"

#include <iomanip>
#include <sstream>

#include <openssl/evp.h>

#include "app/errors/AppException.hpp"

namespace app::auth
{

namespace
{

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

}  // namespace

std::string sha256Hex(const std::string& input)
{
    return evpHex(input, EVP_sha256());
}

std::string passwordHashHex(const std::string& input)
{
#if defined(APP_PASSWORD_HASH_SHA256) && (defined(APP_PASSWORD_HASH_SHA384) || defined(APP_PASSWORD_HASH_SHA512))
#error "APP_PASSWORD_HASH_SHA* defines are mutually exclusive (configure with -DAPP_PASSWORD_HASH_ALGO=SHA256|SHA384|SHA512)"
#elif defined(APP_PASSWORD_HASH_SHA256)
    return evpHex(input, EVP_sha256());
#elif defined(APP_PASSWORD_HASH_SHA384)
    return evpHex(input, EVP_sha384());
#elif defined(APP_PASSWORD_HASH_SHA512)
    return evpHex(input, EVP_sha512());
#else
#error "No password hash defined (require APP_PASSWORD_HASH_SHA256, _SHA384 or _SHA512 via -DAPP_PASSWORD_HASH_ALGO=SHA256|SHA384|SHA512)"
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

}  // namespace app::auth
