#include "HmacUtils.h"

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/params.h>

#include <stdexcept>

namespace fleet_agent
{
namespace HmacUtils
{

namespace
{
std::string toHex(const unsigned char *data, size_t len)
{
    static const char hexDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i)
    {
        out.push_back(hexDigits[(data[i] >> 4) & 0x0F]);
        out.push_back(hexDigits[data[i] & 0x0F]);
    }
    return out;
}
}  // namespace

std::string computeHmacSha256(const std::string &key, const std::string &message)
{
    EVP_MAC *mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    if (!mac)
    {
        throw std::runtime_error("HmacUtils: failed to fetch HMAC implementation");
    }

    EVP_MAC_CTX *ctx = EVP_MAC_CTX_new(mac);
    if (!ctx)
    {
        EVP_MAC_free(mac);
        throw std::runtime_error("HmacUtils: failed to create HMAC context");
    }

    char digestName[] = "SHA256";
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, digestName, 0),
        OSSL_PARAM_construct_end()};

    unsigned char result[EVP_MAX_MD_SIZE];
    size_t resultLen = 0;

    bool ok =
        EVP_MAC_init(ctx,
                     reinterpret_cast<const unsigned char *>(key.data()),
                     key.size(),
                     params) == 1 &&
        EVP_MAC_update(ctx,
                       reinterpret_cast<const unsigned char *>(message.data()),
                       message.size()) == 1 &&
        EVP_MAC_final(ctx, result, &resultLen, sizeof(result)) == 1;

    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);

    if (!ok)
    {
        throw std::runtime_error("HmacUtils: HMAC computation failed");
    }

    return toHex(result, resultLen);
}

bool verifyHmac(const std::string &key, const std::string &message, const std::string &expectedHex)
{
    std::string computed = computeHmacSha256(key, message);
    if (computed.size() != expectedHex.size())
    {
        return false;
    }
    return CRYPTO_memcmp(computed.data(), expectedHex.data(), computed.size()) == 0;
}

}  // namespace HmacUtils
}  // namespace fleet_agent
