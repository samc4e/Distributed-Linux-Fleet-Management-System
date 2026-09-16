#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <functional>

namespace fleet
{

// POST /api/agent/heartbeat. No JWT: the agent_token in the body is a
// per-node secret shared only with that node (see HmacUtils/NodeController
// for how it's minted), so its presence and a matching row in `nodes` is
// the authentication for this endpoint.
class AgentController
{
  public:
    void heartbeat(const drogon::HttpRequestPtr &req,
                  std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};

}  // namespace fleet
