#include "Database.h"

#include <drogon/drogon.h>

#include <memory>

namespace fleet
{
namespace Database
{

drogon::orm::DbClientPtr client()
{
    return drogon::app().getDbClient();
}

Json::Value parseJsonColumn(const std::string &text)
{
    if (text.empty())
    {
        return Json::Value(Json::nullValue);
    }
    Json::CharReaderBuilder builder;
    Json::Value value;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(text.data(), text.data() + text.size(), &value, &errs))
    {
        return Json::Value(Json::nullValue);
    }
    return value;
}

std::string isoTimestampExpr(const std::string &column)
{
    return "to_char(" + column +
          " AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS.MS\"Z\"')";
}

namespace
{
Json::Value nullableDouble(const drogon::orm::Row &row, const char *col)
{
    return row[col].isNull() ? Json::Value(Json::nullValue) : Json::Value(row[col].as<double>());
}

Json::Value nullableInt64(const drogon::orm::Row &row, const char *col)
{
    return row[col].isNull()
              ? Json::Value(Json::nullValue)
              : Json::Value(static_cast<Json::Int64>(row[col].as<long long>()));
}
}  // namespace

Json::Value nodeBaseFieldsToJson(const drogon::orm::Row &row)
{
    Json::Value node;
    node["id"] = row["id"].as<std::string>();
    node["hostname"] = row["hostname"].as<std::string>();
    node["ip_address"] = row["ip_address"].as<std::string>();
    node["agent_port"] = row["agent_port"].as<int>();
    node["status"] = row["status"].as<std::string>();
    node["last_seen"] = row["last_seen"].isNull()
                            ? Json::Value(Json::nullValue)
                            : Json::Value(row["last_seen"].as<std::string>());

    if (row["labels"].isNull())
    {
        node["labels"] = Json::Value(Json::objectValue);
    }
    else
    {
        Json::Value parsed = parseJsonColumn(row["labels"].as<std::string>());
        node["labels"] = parsed.isNull() ? Json::Value(Json::objectValue) : parsed;
    }

    node["os_info"] = row["os_info"].isNull()
                          ? Json::Value(Json::nullValue)
                          : parseJsonColumn(row["os_info"].as<std::string>());
    node["hardware_info"] =
        row["hardware_info"].isNull()
            ? Json::Value(Json::nullValue)
            : parseJsonColumn(row["hardware_info"].as<std::string>());

    return node;
}

Json::Value metricRowToJson(const drogon::orm::Row &row)
{
    Json::Value m;
    m["collected_at"] = row["collected_at"].as<std::string>();
    m["cpu_percent"] = nullableDouble(row, "cpu_percent");
    m["mem_used_bytes"] = nullableInt64(row, "mem_used_bytes");
    m["mem_total_bytes"] = nullableInt64(row, "mem_total_bytes");
    m["disk_read_rate"] = nullableDouble(row, "disk_read_rate");
    m["disk_write_rate"] = nullableDouble(row, "disk_write_rate");
    m["net_rx_rate"] = nullableDouble(row, "net_rx_rate");
    m["net_tx_rate"] = nullableDouble(row, "net_tx_rate");
    m["load_avg_1m"] = nullableDouble(row, "load_avg_1m");
    m["load_avg_5m"] = nullableDouble(row, "load_avg_5m");
    m["load_avg_15m"] = nullableDouble(row, "load_avg_15m");
    return m;
}

}  // namespace Database
}  // namespace fleet
