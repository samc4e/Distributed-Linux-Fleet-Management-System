#include "InternalController.h"

#include "ApiResponse.h"
#include "Database.h"

using namespace drogon;

namespace fleet
{

namespace
{
bool startsWith(const std::string &s, const std::string &prefix)
{
    return !prefix.empty() && s.size() >= prefix.size() &&
          s.compare(0, prefix.size(), prefix) == 0;
}
}  // namespace

void InternalController::targets(const HttpRequestPtr &req,
                                 std::function<void(const HttpResponsePtr &)> &&callback)
{
    // Docker network and localhost, plus whatever CIDR/prefix is configured
    // for this deployment. A simple string-prefix check is enough for this
    // demo (see README for the production alternative).
    std::string peerIp = req->peerAddr().toIp();
    bool allowed = startsWith(peerIp, "172.") || startsWith(peerIp, "10.") ||
                  startsWith(peerIp, "127.") || startsWith(peerIp, config_.allowedPrometheusCidr);

    if (!allowed)
    {
        callback(ApiResponse::error(k403Forbidden, "Forbidden"));
        return;
    }

    Database::client()->execSqlAsync(
        "SELECT host(ip_address) AS ip_address, agent_port, hostname, id::text AS id, "
        "labels::text AS labels "
        "FROM nodes WHERE status IN ('pending', 'online')",
        [callback](const orm::Result &result) {
            Json::Value arr(Json::arrayValue);
            for (const auto &row : result)
            {
                Json::Value targetsArr(Json::arrayValue);
                targetsArr.append(row["ip_address"].as<std::string>() + ":" +
                                  std::to_string(row["agent_port"].as<int>()));

                Json::Value labels;
                labels["__metrics_path__"] = "/metrics";
                labels["hostname"] = row["hostname"].as<std::string>();
                labels["node_id"] = row["id"].as<std::string>();
                labels["job"] = "fleet-agents";

                Json::Value customLabels = Database::parseJsonColumn(row["labels"].as<std::string>());
                if (customLabels.isObject())
                {
                    for (const auto &key : customLabels.getMemberNames())
                    {
                        if (!labels.isMember(key))
                        {
                            labels[key] = customLabels[key];
                        }
                    }
                }

                Json::Value target;
                target["targets"] = targetsArr;
                target["labels"] = labels;
                arr.append(target);
            }
            callback(ApiResponse::json(arr));
        },
        [callback](const orm::DrogonDbException &) {
            callback(ApiResponse::error(k500InternalServerError, "Database error"));
        });
}

}  // namespace fleet
