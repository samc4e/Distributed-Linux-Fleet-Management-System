#pragma once

#include <string>

namespace fleet
{

struct JwtClaims
{
    std::string userId;
    std::string username;
    bool valid = false;
};

namespace JwtUtils
{

// Signs a new HS256 JWT containing sub=userId, username=username,
// iat=now, exp=now+8h.
std::string signJwt(const std::string &userId,
                    const std::string &username,
                    const std::string &secret);

// Verifies signature, expiry and type of `token`. On any failure the
// returned claims have valid=false; fields are otherwise left empty.
JwtClaims verifyJwt(const std::string &token, const std::string &secret);

}  // namespace JwtUtils
}  // namespace fleet
