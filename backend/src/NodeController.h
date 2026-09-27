#pragma once

#include "AppConfig.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <functional>
#include <string>

namespace fleet
{

// CRUD for nodes, plus the whitelisted remote-command endpoint. All JWT
// protected (verified manually at the top of every handler).
class NodeController
{
  public:
    explicit NodeController(AppConfig config) : config_(std::move(config))
    {
    }

    void list(const drogon::HttpRequestPtr &req,
             std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    void create(const drogon::HttpRequestPtr &req,
               std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    void get(const drogon::HttpRequestPtr &req,
            std::function<void(const drogon::HttpResponsePtr &)> &&callback,
            std::string id);

    void update(const drogon::HttpRequestPtr &req,
               std::function<void(const drogon::HttpResponsePtr &)> &&callback,
               std::string id);

    void remove(const drogon::HttpRequestPtr &req,
               std::function<void(const drogon::HttpResponsePtr &)> &&callback,
               std::string id);

    void execCommand(const drogon::HttpRequestPtr &req,
                     std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                     std::string id);

  private:
    AppConfig config_;
};

}  // namespace fleet
