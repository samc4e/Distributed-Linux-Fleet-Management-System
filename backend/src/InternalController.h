#pragma once

#include "AppConfig.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <functional>

namespace fleet
{

// GET /internal/prometheus-targets. No JWT: gated instead by a source-IP
// check, since this is only ever meant to be hit by Prometheus's HTTP
// service discovery from inside the Docker network (or localhost).
class InternalController
{
  public:
    explicit InternalController(AppConfig config) : config_(std::move(config))
    {
    }

    void targets(const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&callback);

  private:
    AppConfig config_;
};

}  // namespace fleet
