#pragma once

#include "AppConfig.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <functional>

namespace fleet
{

// POST /api/auth/login, POST /api/auth/logout, GET /api/auth/me.
// Plain callback-based handlers (see JwtMiddleware.h for why JWT
// verification is a manual call at the top of each protected handler
// rather than a Drogon filter).
class AuthController
{
  public:
    explicit AuthController(AppConfig config) : config_(std::move(config))
    {
    }

    void login(const drogon::HttpRequestPtr &req,
              std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    void logout(const drogon::HttpRequestPtr &req,
               std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    void me(const drogon::HttpRequestPtr &req,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback);

  private:
    AppConfig config_;
};

}  // namespace fleet
