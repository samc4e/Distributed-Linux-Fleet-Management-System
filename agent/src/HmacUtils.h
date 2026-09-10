#pragma once

#include <string>

namespace fleet_agent
{
namespace HmacUtils
{

// Computes HMAC-SHA256 of `message` keyed by `key`, returned as lowercase
// hex. Mirrors backend/src/HmacUtils.h (the two are separate binaries with
// no shared build, hence the duplication) so the manager's signature and
// the agent's verification are byte-for-byte compatible.
std::string computeHmacSha256(const std::string &key, const std::string &message);

// Recomputes the HMAC over `message` and compares it to `expectedHex` using
// a constant-time comparison.
bool verifyHmac(const std::string &key, const std::string &message, const std::string &expectedHex);

}  // namespace HmacUtils
}  // namespace fleet_agent
