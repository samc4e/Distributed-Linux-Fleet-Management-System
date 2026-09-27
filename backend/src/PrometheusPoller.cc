#include "PrometheusPoller.h"

#include "Database.h"

#include <drogon/HttpClient.h>
#include <drogon/drogon.h>
#include <trantor/utils/Logger.h>

#include <chrono>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

using namespace drogon;

namespace fleet
{

namespace
{

// One line of Prometheus text exposition format, split into its parts.
// e.g. `node_cpu_seconds_total{cpu="cpu0",mode="idle"} 12345.6`
struct Sample
{
    std::string name;
    std::string labelsRaw;  // raw text between '{' and '}', empty if none
    double value = 0.0;
    bool valid = false;
};

Sample parseLine(const std::string &line)
{
    Sample s;
    if (line.empty() || line[0] == '#')
    {
        return s;
    }

    size_t nameEnd;
    size_t valueStart;
    size_t bracePos = line.find('{');
    if (bracePos != std::string::npos)
    {
        size_t closeBrace = line.find('}', bracePos);
        if (closeBrace == std::string::npos)
        {
            return s;
        }
        nameEnd = bracePos;
        s.labelsRaw = line.substr(bracePos + 1, closeBrace - bracePos - 1);
        valueStart = closeBrace + 1;
    }
    else
    {
        size_t spacePos = line.find(' ');
        if (spacePos == std::string::npos)
        {
            return s;
        }
        nameEnd = spacePos;
        valueStart = spacePos;
    }

    s.name = line.substr(0, nameEnd);
    while (!s.name.empty() && s.name.back() == ' ')
    {
        s.name.pop_back();
    }

    std::string valueStr = line.substr(valueStart);
    size_t firstNonSpace = valueStr.find_first_not_of(' ');
    if (firstNonSpace == std::string::npos)
    {
        return s;
    }
    valueStr = valueStr.substr(firstNonSpace);
    // A value may be followed by an optional timestamp separated by
    // whitespace; we only ever want the first token.
    size_t tokenEnd = valueStr.find_first_of(" \t\r\n");
    if (tokenEnd != std::string::npos)
    {
        valueStr = valueStr.substr(0, tokenEnd);
    }

    try
    {
        size_t consumed = 0;
        s.value = std::stod(valueStr, &consumed);
        s.valid = consumed > 0;
    }
    catch (const std::exception &)
    {
        s.valid = false;
    }
    return s;
}

// Extracts the value of `key` from a raw label string like
// `cpu="cpu0",mode="idle"`. Returns "" if not present. Our own
// MetricsSerializer never emits commas inside label values, so a plain
// split on ',' is sufficient here.
std::string extractLabel(const std::string &labelsRaw, const std::string &key)
{
    size_t pos = 0;
    while (pos < labelsRaw.size())
    {
        size_t comma = labelsRaw.find(',', pos);
        std::string pair = labelsRaw.substr(
            pos, comma == std::string::npos ? std::string::npos : comma - pos);

        size_t eq = pair.find('=');
        if (eq != std::string::npos)
        {
            std::string k = pair.substr(0, eq);
            std::string v = pair.substr(eq + 1);
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
            {
                v = v.substr(1, v.size() - 2);
            }
            if (k == key)
            {
                return v;
            }
        }

        if (comma == std::string::npos)
        {
            break;
        }
        pos = comma + 1;
    }
    return "";
}

}  // namespace

MetricValues PrometheusPoller::parsePrometheusText(const std::string &body,
                                                    const std::string &nodeId,
                                                    double intervalSeconds)
{
    // Previous-poll counter state, keyed by nodeId. On a node's first ever
    // poll there is no entry yet, so rates/cpu_percent correctly come back
    // as 0 for that one cycle while the baseline is recorded.
    static std::map<std::string, double> prevCpuTotal;
    static std::map<std::string, double> prevCpuIdle;
    static std::map<std::string, double> prevDiskRead;
    static std::map<std::string, double> prevDiskWrite;
    static std::map<std::string, double> prevNetRx;
    static std::map<std::string, double> prevNetTx;

    MetricValues result;

    double cpuTotalNow = 0.0;
    double cpuIdleNow = 0.0;
    bool haveCpu = false;
    double memTotal = 0.0;
    double memAvailable = 0.0;
    double diskReadNow = 0.0;
    double diskWriteNow = 0.0;
    double netRxNow = 0.0;
    double netTxNow = 0.0;

    std::istringstream stream(body);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        Sample s = parseLine(line);
        if (!s.valid)
        {
            continue;
        }

        if (s.name == "node_cpu_seconds_total")
        {
            haveCpu = true;
            cpuTotalNow += s.value;
            if (extractLabel(s.labelsRaw, "mode") == "idle")
            {
                cpuIdleNow += s.value;
            }
        }
        else if (s.name == "node_memory_MemTotal_bytes")
        {
            memTotal = s.value;
        }
        else if (s.name == "node_memory_MemAvailable_bytes")
        {
            memAvailable = s.value;
        }
        else if (s.name == "node_disk_read_bytes_total")
        {
            diskReadNow += s.value;
        }
        else if (s.name == "node_disk_written_bytes_total")
        {
            diskWriteNow += s.value;
        }
        else if (s.name == "node_network_receive_bytes_total")
        {
            if (extractLabel(s.labelsRaw, "device") != "lo")
            {
                netRxNow += s.value;
            }
        }
        else if (s.name == "node_network_transmit_bytes_total")
        {
            if (extractLabel(s.labelsRaw, "device") != "lo")
            {
                netTxNow += s.value;
            }
        }
        else if (s.name == "node_load1")
        {
            result.load_avg_1m = s.value;
        }
        else if (s.name == "node_load5")
        {
            result.load_avg_5m = s.value;
        }
        else if (s.name == "node_load15")
        {
            result.load_avg_15m = s.value;
        }
    }

    result.mem_total_bytes = static_cast<long long>(memTotal);
    result.mem_used_bytes = static_cast<long long>(memTotal - memAvailable);
    if (result.mem_used_bytes < 0)
    {
        result.mem_used_bytes = 0;
    }

    if (haveCpu)
    {
        auto totalIt = prevCpuTotal.find(nodeId);
        auto idleIt = prevCpuIdle.find(nodeId);
        if (totalIt != prevCpuTotal.end() && idleIt != prevCpuIdle.end())
        {
            double totalDelta = cpuTotalNow - totalIt->second;
            double idleDelta = cpuIdleNow - idleIt->second;
            if (totalDelta > 0)
            {
                double pct = (1.0 - (idleDelta / totalDelta)) * 100.0;
                if (pct < 0)
                {
                    pct = 0;
                }
                if (pct > 100)
                {
                    pct = 100;
                }
                result.cpu_percent = pct;
            }
        }
        prevCpuTotal[nodeId] = cpuTotalNow;
        prevCpuIdle[nodeId] = cpuIdleNow;
    }

    auto rateFrom = [intervalSeconds](std::map<std::string, double> &prevMap,
                                      const std::string &key,
                                      double nowValue) -> double {
        double rate = 0.0;
        auto it = prevMap.find(key);
        if (it != prevMap.end() && intervalSeconds > 0)
        {
            double delta = nowValue - it->second;
            if (delta >= 0)
            {
                rate = delta / intervalSeconds;
            }
        }
        prevMap[key] = nowValue;
        return rate;
    };

    result.disk_read_rate = rateFrom(prevDiskRead, nodeId, diskReadNow);
    result.disk_write_rate = rateFrom(prevDiskWrite, nodeId, diskWriteNow);
    result.net_rx_rate = rateFrom(prevNetRx, nodeId, netRxNow);
    result.net_tx_rate = rateFrom(prevNetTx, nodeId, netTxNow);

    return result;
}

void PrometheusPoller::pollAllNodes()
{
    static std::map<std::string, std::chrono::steady_clock::time_point> lastPollTime;

    std::vector<std::tuple<std::string, std::string, int>> nodeList;
    try
    {
        auto result = Database::client()->execSqlSync(
            "SELECT id::text AS id, host(ip_address) AS ip_address, agent_port "
            "FROM nodes WHERE status = 'online'");
        nodeList.reserve(result.size());
        for (const auto &row : result)
        {
            nodeList.emplace_back(row["id"].as<std::string>(),
                                  row["ip_address"].as<std::string>(),
                                  row["agent_port"].as<int>());
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "PrometheusPoller: failed to load node list: " << e.what();
        return;
    }

    auto now = std::chrono::steady_clock::now();

    for (const auto &nodeTuple : nodeList)
    {
        const std::string &nodeId = std::get<0>(nodeTuple);
        const std::string &ip = std::get<1>(nodeTuple);
        int port = std::get<2>(nodeTuple);

        double intervalSeconds = 15.0;
        auto timeIt = lastPollTime.find(nodeId);
        if (timeIt != lastPollTime.end())
        {
            double elapsed = std::chrono::duration<double>(now - timeIt->second).count();
            if (elapsed > 0)
            {
                intervalSeconds = elapsed;
            }
        }
        lastPollTime[nodeId] = now;

        auto client = HttpClient::newHttpClient("http://" + ip + ":" + std::to_string(port));
        auto req = HttpRequest::newHttpRequest();
        req->setMethod(Get);
        req->setPath("/metrics");

        auto sendResult = client->sendRequest(req, 5.0);
        ReqResult reqResult = sendResult.first;
        HttpResponsePtr resp = sendResult.second;

        if (reqResult != ReqResult::Ok || !resp || resp->statusCode() != k200OK)
        {
            LOG_WARN << "PrometheusPoller: node " << nodeId << " (" << ip << ":" << port
                    << ") unreachable, marking error";
            try
            {
                Database::client()->execSqlSync(
                    "UPDATE nodes SET status = 'error', last_seen = NOW() WHERE id = $1::uuid",
                    nodeId);
            }
            catch (const std::exception &e)
            {
                LOG_ERROR << "PrometheusPoller: failed to mark node error: " << e.what();
            }
            continue;
        }

        std::string body(resp->body());
        MetricValues values = parsePrometheusText(body, nodeId, intervalSeconds);

        try
        {
            Database::client()->execSqlSync(
                "INSERT INTO metric_snapshots (node_id, cpu_percent, mem_used_bytes, "
                "mem_total_bytes, disk_read_rate, disk_write_rate, net_rx_rate, net_tx_rate, "
                "load_avg_1m, load_avg_5m, load_avg_15m) "
                "VALUES ($1::uuid, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11)",
                nodeId,
                values.cpu_percent,
                values.mem_used_bytes,
                values.mem_total_bytes,
                values.disk_read_rate,
                values.disk_write_rate,
                values.net_rx_rate,
                values.net_tx_rate,
                values.load_avg_1m,
                values.load_avg_5m,
                values.load_avg_15m);

            Database::client()->execSqlSync(
                "UPDATE nodes SET status = 'online', last_seen = NOW() WHERE id = $1::uuid",
                nodeId);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR << "PrometheusPoller: failed to store snapshot for " << nodeId << ": "
                      << e.what();
        }
    }

    try
    {
        // Retention: keep only 24h of snapshots (see migrations/003_create_metric_snapshots.sql).
        Database::client()->execSqlSync(
            "DELETE FROM metric_snapshots WHERE collected_at < NOW() - INTERVAL '24 hours'");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "PrometheusPoller: retention cleanup failed: " << e.what();
    }
}

void PrometheusPoller::run()
{
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(15));
        try
        {
            pollAllNodes();
        }
        catch (const std::exception &e)
        {
            LOG_ERROR << "PrometheusPoller: unhandled exception in poll cycle: " << e.what();
        }
    }
}

}  // namespace fleet
