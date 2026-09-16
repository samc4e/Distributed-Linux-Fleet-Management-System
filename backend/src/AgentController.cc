#include "AgentController.h"

#include "ApiResponse.h"
#include "Database.h"

using namespace drogon;

namespace fleet
{

namespace
{
std::string writeJsonCompact(const Json::Value &value)
{
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, value);
}
}  // namespace

void AgentController::heartbeat(const HttpRequestPtr &req,
                                std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("agent_token"))
    {
        callback(ApiResponse::error(k400BadRequest, "agent_token is required"));
        return;
    }

    std::string agentToken = (*jsonBody)["agent_token"].asString();
    if (agentToken.empty())
    {
        callback(ApiResponse::error(k400BadRequest, "agent_token is required"));
        return;
    }

    std::string osInfoText =
        jsonBody->isMember("os_info") ? writeJsonCompact((*jsonBody)["os_info"]) : "{}";
    std::string hwInfoText =
        jsonBody->isMember("hardware_info") ? writeJsonCompact((*jsonBody)["hardware_info"]) : "{}";

    Database::client()->execSqlAsync(
        "SELECT id::text AS id FROM nodes WHERE agent_token = $1",
        [callback, osInfoText, hwInfoText](const orm::Result &result) {
            if (result.empty())
            {
                callback(ApiResponse::error(k401Unauthorized, "Unknown agent token"));
                return;
            }
            std::string nodeId = result[0]["id"].as<std::string>();

            Database::client()->execSqlAsync(
                "UPDATE nodes SET status = 'online', last_seen = NOW(), "
                "os_info = $1::jsonb, hardware_info = $2::jsonb WHERE id = $3::uuid",
                [callback](const orm::Result &) {
                    Json::Value resBody;
                    resBody["message"] = "heartbeat received";
                    callback(ApiResponse::json(resBody));
                },
                [callback](const orm::DrogonDbException &) {
                    callback(ApiResponse::error(k500InternalServerError, "Database error"));
                },
                osInfoText,
                hwInfoText,
                nodeId);
        },
        [callback](const orm::DrogonDbException &) {
            callback(ApiResponse::error(k500InternalServerError, "Database error"));
        },
        agentToken);
}

}  // namespace fleet
