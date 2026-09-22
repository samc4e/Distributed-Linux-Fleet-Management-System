#include "JwtMiddleware.h"

#include "JwtUtils.h"

#include <json/json.h>

namespace fleet
{

namespace
{
drogon::HttpResponsePtr unauthorizedResponse()
{
    Json::Value json;
    json["error"] = "Unauthorized";
    auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
    resp->setStatusCode(drogon::k401Unauthorized);
    return resp;
}
}  // namespace

JwtMiddleware &JwtMiddleware::instance()
{
    static JwtMiddleware inst;
    return inst;
}

void JwtMiddleware::init(const std::string &jwtSecret)
{
    jwtSecret_ = jwtSecret;
}

void JwtMiddleware::addSession(const std::string &token, const std::string &userId)
{
    std::lock_guard<std::mutex> lock(sessionMutex_);
    sessions_[token] = userId;
}

void JwtMiddleware::removeSession(const std::string &token)
{
    std::lock_guard<std::mutex> lock(sessionMutex_);
    sessions_.erase(token);
}

std::string JwtMiddleware::extractBearerToken(const drogon::HttpRequestPtr &req)
{
    const std::string &header = req->getHeader("Authorization");
    const std::string prefix = "Bearer ";
    if (header.size() <= prefix.size() || header.compare(0, prefix.size(), prefix) != 0)
    {
        return "";
    }
    return header.substr(prefix.size());
}

bool JwtMiddleware::verifyRequest(const drogon::HttpRequestPtr &req,
                                  drogon::HttpResponsePtr &errResp,
                                  std::string &outUserId,
                                  std::string &outUsername)
{
    std::string token = extractBearerToken(req);
    if (token.empty())
    {
        errResp = unauthorizedResponse();
        return false;
    }

    JwtClaims claims = JwtUtils::verifyJwt(token, jwtSecret_);
    if (!claims.valid)
    {
        errResp = unauthorizedResponse();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        if (sessions_.find(token) == sessions_.end())
        {
            errResp = unauthorizedResponse();
            return false;
        }
    }

    outUserId = claims.userId;
    outUsername = claims.username;
    return true;
}

}  // namespace fleet
