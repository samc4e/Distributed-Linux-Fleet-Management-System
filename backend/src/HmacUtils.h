#pragma once

#include <string>

namespace fleet
{
namespace HmacUtils
{

// Computes HMAC-SHA256 of `message` keyed by `key`, returned as lowercase hex.
std::string computeHmacSha256(const std::string &key, const std::string &message);

// Recomputes the HMAC over `message` and compares it to `expectedHex` using a
// constant-time comparison.
bool verifyHmac(const std::string &key,
                const std::string &message,
                const std::string &expectedHex);

}  // namespace HmacUtils
}  // namespace fleet
