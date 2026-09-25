#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <functional>
#include <string>

namespace fleet
{

// Fleet-wide summary stats and per-node metric history, both read from
// metric_snapshots (populated by PrometheusPoller).
class MetricsController
{
  public:
    void summary(const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    void nodeHistory(const drogon::HttpRequestPtr &req,
                     std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                     std::string id);
};

}  // namespace fleet
