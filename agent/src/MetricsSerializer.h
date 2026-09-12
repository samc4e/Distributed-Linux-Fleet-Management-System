#pragma once

#include "MetricsCollector.h"

#include <string>

namespace fleet_agent
{
namespace MetricsSerializer
{

// Renders AllMetrics as Prometheus text exposition format
// (text/plain; version=0.0.4). Metric names/labels here must match what
// backend/src/PrometheusPoller.cc parses.
std::string serialize(const AllMetrics &m);

}  // namespace MetricsSerializer
}  // namespace fleet_agent
