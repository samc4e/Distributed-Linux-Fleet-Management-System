#include "JwtUtils.h"

// jwt-cpp is header-only. We use the jsoncpp traits (rather than the default
// picojson ones) so the JWT claim JSON uses the same jsoncpp library that
// Drogon already links against, instead of pulling in a second JSON library.
#include "jwt-cpp/traits/open-source-parsers-jsoncpp/defaults.h"

#include <chrono>

namespace fleet
{
namespace JwtUtils
{

std::string signJwt(const std::string &userId,
                    const std::string &username,
                    const std::string &secret)
{
    const auto now = std::chrono::system_clock::now();
    return jwt::create()
        .set_type("JWT")
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::hours(8))
        .set_subject(userId)
        .set_payload_claim("username", jwt::claim(username))
        .sign(jwt::algorithm::hs256{secret});
}

JwtClaims verifyJwt(const std::string &token, const std::string &secret)
{
    JwtClaims claims;
    try
    {
        auto decoded = jwt::decode(token);

        auto verifier = jwt::verify()
                            .allow_algorithm(jwt::algorithm::hs256{secret})
                            .with_type("JWT");
        verifier.verify(decoded);

        claims.userId = decoded.get_subject();
        claims.username = decoded.get_payload_claim("username").as_string();
        claims.valid = true;
    }
    catch (const std::exception &)
    {
        claims.valid = false;
    }
    return claims;
}

}  // namespace JwtUtils
}  // namespace fleet
