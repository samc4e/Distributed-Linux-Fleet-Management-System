#pragma once

#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <string>

// Tiny shared response builders used by every controller so each handler
// doesn't repeat the "build JSON, set status code" boilerplate.
namespace fleet
{
namespace ApiResponse
{

inline drogon::HttpResponsePtr error(drogon::HttpStatusCode code, const std::string &message)
{
    Json::Value body;
    body["error"] = message;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(code);
    return resp;
}

inline drogon::HttpResponsePtr json(const Json::Value &body,
                                    drogon::HttpStatusCode code = drogon::k200OK)
{
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(code);
    return resp;
}

}  // namespace ApiResponse
}  // namespace fleet
