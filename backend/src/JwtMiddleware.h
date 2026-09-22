#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <mutex>
#include <string>
#include <unordered_map>

namespace fleet
{

// Tracks active login sessions in memory and validates the JWT bearer token
// on every protected request. There is no Redis/DB-backed session store:
// sessions live only as long as the backend process, which is an accepted
// trade-off for this academic demo (see README).
//
// Applied manually at the top of each protected handler rather than as a
// Drogon filter/plugin, e.g.:
//   HttpResponsePtr authErr;
//   std::string userId, username;
//   if (!JwtMiddleware::instance().verifyRequest(req, authErr, userId, username))
//   {
//       callback(authErr);
//       return;
//   }
class JwtMiddleware
{
  public:
    static JwtMiddleware &instance();

    void init(const std::string &jwtSecret);

    void addSession(const std::string &token, const std::string &userId);
    void removeSession(const std::string &token);

    // Validates the Authorization: Bearer <token> header against both the
    // JWT signature/expiry and the in-memory session map.
    // Returns true and fills outUserId/outUsername on success.
    // Returns false and fills errResp with a 401 JSON response on failure.
    bool verifyRequest(const drogon::HttpRequestPtr &req,
                       drogon::HttpResponsePtr &errResp,
                       std::string &outUserId,
                       std::string &outUsername);

    static std::string extractBearerToken(const drogon::HttpRequestPtr &req);

  private:
    JwtMiddleware() = default;

    std::string jwtSecret_;
    std::unordered_map<std::string, std::string> sessions_;
    std::mutex sessionMutex_;
};

}  // namespace fleet
