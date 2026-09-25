#include "MetricsController.h"

#include "ApiResponse.h"
#include "Database.h"
#include "JwtMiddleware.h"

using namespace drogon;

namespace fleet
{

void MetricsController::summary(const HttpRequestPtr &req,
                                std::function<void(const HttpResponsePtr &)> &&callback)
{
    HttpResponsePtr authErr;
    std::string userId, username;
    if (!JwtMiddleware::instance().verifyRequest(req, authErr, userId, username))
    {
        callback(authErr);
        return;
    }

    // Counts cover every node regardless of status. Averages are scoped to
    // currently-online nodes only: pending nodes have no metrics yet and an
    // offline node's last-known reading shouldn't count toward a "current
    // fleet load" figure.
    std::string sql =
        "SELECT "
        "COUNT(*) AS total, "
        "COUNT(*) FILTER (WHERE n.status = 'online') AS online, "
        "COUNT(*) FILTER (WHERE n.status = 'offline') AS offline, "
        "COUNT(*) FILTER (WHERE n.status = 'pending') AS pending, "
        "AVG(ms.cpu_percent) FILTER (WHERE n.status = 'online') AS avg_cpu, "
        "AVG(ms.mem_used_bytes::float / NULLIF(ms.mem_total_bytes, 0)) "
        "  FILTER (WHERE n.status = 'online') * 100 AS avg_mem_pct "
        "FROM nodes n "
        "LEFT JOIN LATERAL ( "
        "  SELECT cpu_percent, mem_used_bytes, mem_total_bytes "
        "  FROM metric_snapshots WHERE node_id = n.id "
        "  ORDER BY collected_at DESC LIMIT 1 "
        ") ms ON true";

    Database::client()->execSqlAsync(
        sql,
        [callback](const orm::Result &result) {
            Json::Value resBody;
            if (result.empty())
            {
                resBody["total"] = 0;
                resBody["online"] = 0;
                resBody["offline"] = 0;
                resBody["pending"] = 0;
                resBody["avg_cpu_percent"] = 0.0;
                resBody["avg_mem_percent"] = 0.0;
                callback(ApiResponse::json(resBody));
                return;
            }
            const auto &row = result[0];
            resBody["total"] = row["total"].as<int>();
            resBody["online"] = row["online"].as<int>();
            resBody["offline"] = row["offline"].as<int>();
            resBody["pending"] = row["pending"].as<int>();
            resBody["avg_cpu_percent"] =
                row["avg_cpu"].isNull() ? 0.0 : row["avg_cpu"].as<double>();
            resBody["avg_mem_percent"] =
                row["avg_mem_pct"].isNull() ? 0.0 : row["avg_mem_pct"].as<double>();
            callback(ApiResponse::json(resBody));
        },
        [callback](const orm::DrogonDbException &) {
            callback(ApiResponse::error(k500InternalServerError, "Database error"));
        });
}

void MetricsController::nodeHistory(const HttpRequestPtr &req,
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

    int minutes = 60;
    const std::string &minutesParam = req->getParameter("minutes");
    if (!minutesParam.empty())
    {
        try
        {
            minutes = std::stoi(minutesParam);
        }
        catch (const std::exception &)
        {
            minutes = 60;
        }
    }
    if (minutes <= 0)
    {
        minutes = 60;
    }

    std::string sql =
        "SELECT " + Database::isoTimestampExpr("collected_at") +
        " AS collected_at, "
        "cpu_percent, mem_used_bytes, mem_total_bytes, disk_read_rate, disk_write_rate, "
        "net_rx_rate, net_tx_rate, load_avg_1m, load_avg_5m, load_avg_15m "
        "FROM metric_snapshots "
        "WHERE node_id = $1::uuid AND collected_at > NOW() - ($2::integer * INTERVAL '1 minute') "
        "ORDER BY collected_at ASC";

    Database::client()->execSqlAsync(
        sql,
        [callback](const orm::Result &result) {
            Json::Value arr(Json::arrayValue);
            for (const auto &row : result)
            {
                arr.append(Database::metricRowToJson(row));
            }
            callback(ApiResponse::json(arr));
        },
        [callback](const orm::DrogonDbException &) {
            callback(ApiResponse::error(k500InternalServerError, "Database error"));
        },
        id,
        minutes);
}

}  // namespace fleet
