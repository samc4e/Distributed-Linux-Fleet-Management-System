#include "NodeController.h"

#include "ApiResponse.h"
#include "Database.h"
#include "HmacUtils.h"
#include "JwtMiddleware.h"

#include <drogon/HttpClient.h>
#include <drogon/utils/Utilities.h>

#include <chrono>
#include <set>

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

void NodeController::list(const HttpRequestPtr &req,
                          std::function<void(const HttpResponsePtr &)> &&callback)
{
    HttpResponsePtr authErr;
    std::string userId, username;
    if (!JwtMiddleware::instance().verifyRequest(req, authErr, userId, username))
    {
        callback(authErr);
        return;
    }

    std::string sql =
        "SELECT n.id::text AS id, n.hostname, host(n.ip_address) AS ip_address, "
        "n.agent_port, n.status, " +
        Database::isoTimestampExpr("n.last_seen") +
        " AS last_seen, "
        "n.labels::text AS labels, n.os_info::text AS os_info, "
        "n.hardware_info::text AS hardware_info, "
        "ms.cpu_percent, ms.mem_used_bytes, ms.mem_total_bytes, ms.load_avg_1m "
        "FROM nodes n "
        "LEFT JOIN LATERAL ( "
        "  SELECT cpu_percent, mem_used_bytes, mem_total_bytes, load_avg_1m "
        "  FROM metric_snapshots "
        "  WHERE node_id = n.id "
        "  ORDER BY collected_at DESC LIMIT 1 "
        ") ms ON true "
        "ORDER BY n.onboarded_at DESC";

    Database::client()->execSqlAsync(
        sql,
        [callback](const orm::Result &result) {
            Json::Value arr(Json::arrayValue);
            for (const auto &row : result)
            {
                Json::Value node = Database::nodeBaseFieldsToJson(row);
                node["cpu_percent"] =
                    row["cpu_percent"].isNull() ? Json::Value(Json::nullValue)
                                                : Json::Value(row["cpu_percent"].as<double>());
                node["mem_used_bytes"] =
                    row["mem_used_bytes"].isNull()
                        ? Json::Value(Json::nullValue)
                        : Json::Value(static_cast<Json::Int64>(row["mem_used_bytes"].as<long long>()));
                node["mem_total_bytes"] =
                    row["mem_total_bytes"].isNull()
                        ? Json::Value(Json::nullValue)
                        : Json::Value(static_cast<Json::Int64>(row["mem_total_bytes"].as<long long>()));
                node["load_avg_1m"] =
                    row["load_avg_1m"].isNull() ? Json::Value(Json::nullValue)
                                                : Json::Value(row["load_avg_1m"].as<double>());
                arr.append(node);
            }
            callback(ApiResponse::json(arr));
        },
        [callback](const orm::DrogonDbException &) {
            callback(ApiResponse::error(k500InternalServerError, "Database error"));
        });
}

void NodeController::create(const HttpRequestPtr &req,
                            std::function<void(const HttpResponsePtr &)> &&callback)
{
    HttpResponsePtr authErr;
    std::string userId, username;
    if (!JwtMiddleware::instance().verifyRequest(req, authErr, userId, username))
    {
        callback(authErr);
        return;
    }

    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("hostname") || !jsonBody->isMember("ip_address"))
    {
        callback(ApiResponse::error(k400BadRequest, "hostname and ip_address are required"));
        return;
    }

    std::string hostname = (*jsonBody)["hostname"].asString();
    std::string ipAddress = (*jsonBody)["ip_address"].asString();
    if (hostname.empty() || ipAddress.empty())
    {
        callback(ApiResponse::error(k400BadRequest, "hostname and ip_address are required"));
        return;
    }

    int agentPort = (jsonBody->isMember("agent_port") && (*jsonBody)["agent_port"].isNumeric())
                       ? (*jsonBody)["agent_port"].asInt()
                       : 9200;

    Json::Value labels = (jsonBody->isMember("labels") && (*jsonBody)["labels"].isObject())
                             ? (*jsonBody)["labels"]
                             : Json::Value(Json::objectValue);
    std::string labelsText = writeJsonCompact(labels);

    // The agent_token is derived from the node's own id, so the id is
    // generated here (client-side) instead of relying on the table's
    // gen_random_uuid() default, and then inserted explicitly.
    std::string nodeId = drogon::utils::getUuid();
    long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
    std::string raw = nodeId + hostname + std::to_string(timestamp);
    std::string agentToken = HmacUtils::computeHmacSha256(config_.jwtSecret, raw);

    std::string backendHost = config_.backendHost.empty() ? "127.0.0.1" : config_.backendHost;
    std::string onboardCmd = "AGENT_TOKEN=" + agentToken + " MANAGER_URL=http://" + backendHost +
                             ":" + std::to_string(config_.listenPort) +
                             " bash <(curl -fsSL http://" + backendHost + ":" +
                             std::to_string(config_.listenPort) + "/onboard.sh)";

    Database::client()->execSqlAsync(
        "INSERT INTO nodes (id, hostname, ip_address, agent_port, agent_token, labels, status) "
        "VALUES ($1::uuid, $2, $3::inet, $4, $5, $6::jsonb, 'pending')",
        [callback, nodeId, agentToken, onboardCmd](const orm::Result &) {
            Json::Value resBody;
            resBody["node_id"] = nodeId;
            resBody["agent_token"] = agentToken;
            resBody["onboarding_command"] = onboardCmd;
            callback(ApiResponse::json(resBody, k201Created));
        },
        [callback](const orm::DrogonDbException &) {
            callback(ApiResponse::error(k400BadRequest,
                                        "Invalid node data (check ip_address format)"));
        },
        nodeId,
        hostname,
        ipAddress,
        agentPort,
        agentToken,
        labelsText);
}

void NodeController::get(const HttpRequestPtr &req,
                         std::function<void(const HttpResponsePtr &)> &&callback,
                         std::string id)
{
    HttpResponsePtr authErr;
    std::string userId, username;
    if (!JwtMiddleware::instance().verifyRequest(req, authErr, userId, username))
    {
        callback(authErr);
        return;
    }

    std::string nodeSql =
        "SELECT id::text AS id, hostname, host(ip_address) AS ip_address, agent_port, status, " +
        Database::isoTimestampExpr("last_seen") +
        " AS last_seen, "
        "labels::text AS labels, os_info::text AS os_info, hardware_info::text AS hardware_info "
        "FROM nodes WHERE id = $1::uuid";

    Database::client()->execSqlAsync(
        nodeSql,
        [callback, id](const orm::Result &nodeResult) {
            if (nodeResult.empty())
            {
                callback(ApiResponse::error(k404NotFound, "Node not found"));
                return;
            }
            Json::Value nodeJson = Database::nodeBaseFieldsToJson(nodeResult[0]);

            std::string metricsSql =
                "SELECT " + Database::isoTimestampExpr("collected_at") +
                " AS collected_at, "
                "cpu_percent, mem_used_bytes, mem_total_bytes, disk_read_rate, "
                "disk_write_rate, net_rx_rate, net_tx_rate, load_avg_1m, load_avg_5m, "
                "load_avg_15m "
                "FROM metric_snapshots WHERE node_id = $1::uuid "
                "ORDER BY collected_at DESC LIMIT 100";

            Database::client()->execSqlAsync(
                metricsSql,
                [callback, nodeJson](const orm::Result &metricsResult) mutable {
                    Json::Value metrics(Json::arrayValue);
                    // Query is DESC (most recent first); reverse to
                    // chronological order for charting.
                    for (auto it = metricsResult.rbegin(); it != metricsResult.rend(); ++it)
                    {
                        metrics.append(Database::metricRowToJson(*it));
                    }
                    nodeJson["recent_metrics"] = metrics;
                    callback(ApiResponse::json(nodeJson));
                },
                [callback](const orm::DrogonDbException &) {
                    callback(ApiResponse::error(k500InternalServerError, "Database error"));
                },
                id);
        },
        [callback](const orm::DrogonDbException &) {
            callback(ApiResponse::error(k500InternalServerError, "Database error"));
        },
        id);
}

void NodeController::update(const HttpRequestPtr &req,
                            std::function<void(const HttpResponsePtr &)> &&callback,
                            std::string id)
{
    HttpResponsePtr authErr;
    std::string userId, username;
    if (!JwtMiddleware::instance().verifyRequest(req, authErr, userId, username))
    {
        callback(authErr);
        return;
    }

    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        callback(ApiResponse::error(k400BadRequest, "Request body required"));
        return;
    }

    // Read current values first so a partial update (hostname only, or
    // labels only) doesn't clobber the field that wasn't supplied.
    Database::client()->execSqlAsync(
        "SELECT hostname, labels::text AS labels FROM nodes WHERE id = $1::uuid",
        [callback, jsonBody, id](const orm::Result &result) {
            if (result.empty())
            {
                callback(ApiResponse::error(k404NotFound, "Node not found"));
                return;
            }

            std::string hostname =
                (jsonBody->isMember("hostname") && (*jsonBody)["hostname"].isString())
                    ? (*jsonBody)["hostname"].asString()
                    : result[0]["hostname"].as<std::string>();

            std::string labelsText;
            if (jsonBody->isMember("labels") && (*jsonBody)["labels"].isObject())
            {
                labelsText = writeJsonCompact((*jsonBody)["labels"]);
            }
            else
            {
                labelsText = result[0]["labels"].as<std::string>();
            }

            Database::client()->execSqlAsync(
                "UPDATE nodes SET hostname = $1, labels = $2::jsonb WHERE id = $3::uuid",
                [callback](const orm::Result &) {
                    Json::Value resBody;
                    resBody["message"] = "Node updated";
                    callback(ApiResponse::json(resBody));
                },
                [callback](const orm::DrogonDbException &) {
                    callback(ApiResponse::error(k500InternalServerError, "Database error"));
                },
                hostname,
                labelsText,
                id);
        },
        [callback](const orm::DrogonDbException &) {
            callback(ApiResponse::error(k500InternalServerError, "Database error"));
        },
        id);
}

void NodeController::remove(const HttpRequestPtr &req,
                            std::function<void(const HttpResponsePtr &)> &&callback,
                            std::string id)
{
    HttpResponsePtr authErr;
    std::string userId, username;
    if (!JwtMiddleware::instance().verifyRequest(req, authErr, userId, username))
    {
        callback(authErr);
        return;
    }

    Database::client()->execSqlAsync(
        "UPDATE nodes SET status = 'offline' WHERE id = $1::uuid",
        [callback](const orm::Result &) {
            Json::Value resBody;
            resBody["message"] = "Node decommissioned";
            callback(ApiResponse::json(resBody));
        },
        [callback](const orm::DrogonDbException &) {
            callback(ApiResponse::error(k500InternalServerError, "Database error"));
        },
        id);
}

void NodeController::execCommand(const HttpRequestPtr &req,
                                 std::function<void(const HttpResponsePtr &)> &&callback,
                                 std::string id)
{
    HttpResponsePtr authErr;
    std::string userId, username;
    if (!JwtMiddleware::instance().verifyRequest(req, authErr, userId, username))
    {
        callback(authErr);
        return;
    }

    static const std::set<std::string> kWhitelist = {
        "df", "free", "ps", "uptime", "netstat", "ss", "lscpu", "uname"};

    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("command"))
    {
        callback(ApiResponse::error(k400BadRequest, "command is required"));
        return;
    }

    std::string command = (*jsonBody)["command"].asString();
    if (kWhitelist.find(command) == kWhitelist.end())
    {
        callback(ApiResponse::error(k400BadRequest, "command not allowed"));
        return;
    }

    std::vector<std::string> args;
    if (jsonBody->isMember("args") && (*jsonBody)["args"].isArray())
    {
        for (const auto &a : (*jsonBody)["args"])
        {
            args.push_back(a.asString());
        }
    }

    Database::client()->execSqlAsync(
        "SELECT host(ip_address) AS ip_address, agent_port, agent_token FROM nodes "
        "WHERE id = $1::uuid",
        [callback, command, args](const orm::Result &result) {
            if (result.empty())
            {
                callback(ApiResponse::error(k404NotFound, "Node not found"));
                return;
            }

            std::string ip = result[0]["ip_address"].as<std::string>();
            int port = result[0]["agent_port"].as<int>();
            std::string agentToken = result[0]["agent_token"].as<std::string>();

            Json::Value bodyJson;
            bodyJson["command"] = command;
            Json::Value argsJson(Json::arrayValue);
            for (const auto &a : args)
            {
                argsJson.append(a);
            }
            bodyJson["args"] = argsJson;
            std::string bodyText = writeJsonCompact(bodyJson);

            std::string signature = HmacUtils::computeHmacSha256(agentToken, bodyText);

            auto client =
                HttpClient::newHttpClient("http://" + ip + ":" + std::to_string(port));
            auto agentReq = HttpRequest::newHttpRequest();
            agentReq->setMethod(Post);
            agentReq->setPath("/execute");
            agentReq->addHeader("X-Agent-Signature", signature);
            agentReq->setContentTypeCode(CT_APPLICATION_JSON);
            agentReq->setBody(bodyText);

            client->sendRequest(
                agentReq,
                [callback, client](ReqResult result, const HttpResponsePtr &resp) {
                    if (result != ReqResult::Ok || !resp)
                    {
                        callback(ApiResponse::error(k500InternalServerError, "Agent unreachable"));
                        return;
                    }
                    auto respJson = resp->getJsonObject();
                    if (!respJson)
                    {
                        callback(
                            ApiResponse::error(k500InternalServerError, "Invalid agent response"));
                        return;
                    }
                    callback(ApiResponse::json(*respJson));
                },
                15.0);
        },
        [callback](const orm::DrogonDbException &) {
            callback(ApiResponse::error(k500InternalServerError, "Database error"));
        },
        id);
}

}  // namespace fleet
