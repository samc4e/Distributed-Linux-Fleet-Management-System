#pragma once

#include <string>

namespace fleet
{

// One snapshot's worth of derived metric values, computed from a single
// /metrics scrape (counters) plus whatever was recorded on the previous
// scrape of the same node (for the rate/percent fields).
struct MetricValues
{
    double cpu_percent = 0;
    long long mem_used_bytes = 0;
    long long mem_total_bytes = 0;
    double disk_read_rate = 0;   // bytes/sec
    double disk_write_rate = 0;  // bytes/sec
    double net_rx_rate = 0;      // bytes/sec
    double net_tx_rate = 0;      // bytes/sec
    double load_avg_1m = 0;
    double load_avg_5m = 0;
    double load_avg_15m = 0;
};

// Background poller that scrapes every 'online' node's /metrics endpoint on
// a fixed cadence, turns the Prometheus text exposition format into a
// MetricValues, and appends a row to metric_snapshots. Runs in its own
// std::thread (started from main.cc, outside Drogon's event loop), so all
// DB and HTTP access here uses Drogon's blocking/synchronous APIs.
class PrometheusPoller
{
  public:
    // Never returns. Intended to be run in a detached std::thread.
    void run();

    // Parses one /metrics response body into a MetricValues. `nodeId`
    // scopes the previous-poll counter state used to derive rates and
    // cpu_percent; `intervalSeconds` is the actual wall-clock time since
    // the previous poll of this node. Exposed (static, side-effect state
    // hidden behind nodeId) so it can be exercised independently of the
    // network/DB.
    static MetricValues parsePrometheusText(const std::string &body,
                                            const std::string &nodeId,
                                            double intervalSeconds);

  private:
    void pollAllNodes();
};

}  // namespace fleet
