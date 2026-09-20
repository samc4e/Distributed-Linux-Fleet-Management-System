#pragma once

#include <drogon/orm/DbClient.h>
#include <drogon/orm/Row.h>
#include <json/json.h>

#include <string>

namespace fleet
{
namespace Database
{

// The single configured Drogon Postgres client ("default").
drogon::orm::DbClientPtr client();

// Parses a JSON/JSONB column's raw text form into a Json::Value. Returns a
// Json::nullValue for empty input or on parse failure.
Json::Value parseJsonColumn(const std::string &text);

// SQL fragment that formats a timestamptz column as strict ISO-8601
// ("YYYY-MM-DDTHH:MM:SS.sssZ"). Postgres's own implicit ::text cast uses a
// space separator and a bare "+00" offset, which not every JS Date parser
// accepts, so every timestamp we send to the frontend goes through this.
std::string isoTimestampExpr(const std::string &column);

// Converts the common "nodes" table columns of a row into the node JSON
// shape shared by NodeController::list and NodeController::get. Callers
// that joined in extra columns (e.g. the latest metric snapshot) attach
// those separately.
Json::Value nodeBaseFieldsToJson(const drogon::orm::Row &row);

// Converts one metric_snapshots row (aliased as: collected_at, cpu_percent,
// mem_used_bytes, mem_total_bytes, disk_read_rate, disk_write_rate,
// net_rx_rate, net_tx_rate, load_avg_1m, load_avg_5m, load_avg_15m) into the
// JSON shape shared by NodeController::get's recent_metrics array and
// MetricsController::nodeHistory.
Json::Value metricRowToJson(const drogon::orm::Row &row);

}  // namespace Database
}  // namespace fleet
