#include "MetricsSerializer.h"

#include <iomanip>
#include <sstream>

namespace fleet_agent
{
namespace MetricsSerializer
{

std::string serialize(const AllMetrics &m)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);

    // --- CPU (counter, jiffies converted to seconds assuming HZ=100) ---
    // Only per-core series are emitted (never the "cpu" aggregate line from
    // /proc/stat) so this matches real node_exporter's convention: PromQL
    // queries that avg()/sum() across the cpu label - e.g. Grafana's fleet
    // CPU panels, the HighCPU alert rule - assume every matching series is
    // one core. Emitting an extra pre-summed "total" series under the same
    // metric name would silently skew any unweighted average upward by
    // counting each core's idle time twice (once alone, once inside the
    // aggregate), which is exactly what an early build of this agent did.
    out << "# HELP node_cpu_seconds_total Seconds the CPUs spent in each mode.\n";
    out << "# TYPE node_cpu_seconds_total counter\n";
    for (const auto &c : m.perCoreCpu)
    {
        if (c.label == "cpu")
        {
            continue;
        }
        auto emit = [&](const char *mode, unsigned long long jiffies) {
            out << "node_cpu_seconds_total{cpu=\"" << c.label << "\",mode=\"" << mode << "\"} "
                << (static_cast<double>(jiffies) / 100.0) << "\n";
        };
        emit("user", c.user);
        emit("nice", c.nice);
        emit("system", c.system);
        emit("idle", c.idle);
        emit("iowait", c.iowait);
        emit("irq", c.irq);
        emit("softirq", c.softirq);
        emit("steal", c.steal);
    }

    // --- Memory (gauge) ---
    auto emitMem = [&](const char *fieldName, long long kb) {
        out << "# HELP node_memory_" << fieldName << "_bytes Memory information field "
            << fieldName << "_bytes\n";
        out << "# TYPE node_memory_" << fieldName << "_bytes gauge\n";
        out << "node_memory_" << fieldName << "_bytes " << (kb * 1024LL) << "\n";
    };
    emitMem("MemTotal", m.mem.mem_total_kb);
    emitMem("MemFree", m.mem.mem_free_kb);
    emitMem("MemAvailable", m.mem.mem_available_kb);
    emitMem("Buffers", m.mem.buffers_kb);
    emitMem("Cached", m.mem.cached_kb);
    emitMem("SwapTotal", m.mem.swap_total_kb);
    emitMem("SwapFree", m.mem.swap_free_kb);

    // --- Disk I/O (counter) ---
    out << "# HELP node_disk_read_bytes_total Total bytes read from disk.\n";
    out << "# TYPE node_disk_read_bytes_total counter\n";
    for (const auto &d : m.disks)
    {
        out << "node_disk_read_bytes_total{device=\"" << d.device << "\"} "
            << (d.sectors_read * 512ULL) << "\n";
    }

    out << "# HELP node_disk_written_bytes_total Total bytes written to disk.\n";
    out << "# TYPE node_disk_written_bytes_total counter\n";
    for (const auto &d : m.disks)
    {
        out << "node_disk_written_bytes_total{device=\"" << d.device << "\"} "
            << (d.sectors_written * 512ULL) << "\n";
    }

    out << "# HELP node_disk_io_time_seconds_total Total seconds spent doing I/Os.\n";
    out << "# TYPE node_disk_io_time_seconds_total counter\n";
    for (const auto &d : m.disks)
    {
        out << "node_disk_io_time_seconds_total{device=\"" << d.device << "\"} "
            << (static_cast<double>(d.ms_doing_io) / 1000.0) << "\n";
    }

    // --- Filesystem (gauge) ---
    out << "# HELP node_filesystem_size_bytes Filesystem size in bytes.\n";
    out << "# TYPE node_filesystem_size_bytes gauge\n";
    for (const auto &f : m.filesystems)
    {
        out << "node_filesystem_size_bytes{mountpoint=\"" << f.mountpoint << "\",fstype=\""
            << f.fstype << "\"} " << f.size_bytes << "\n";
    }

    out << "# HELP node_filesystem_avail_bytes Filesystem space available to non-root users.\n";
    out << "# TYPE node_filesystem_avail_bytes gauge\n";
    for (const auto &f : m.filesystems)
    {
        out << "node_filesystem_avail_bytes{mountpoint=\"" << f.mountpoint << "\",fstype=\""
            << f.fstype << "\"} " << f.avail_bytes << "\n";
    }

    out << "# HELP node_filesystem_free_bytes Filesystem free space.\n";
    out << "# TYPE node_filesystem_free_bytes gauge\n";
    for (const auto &f : m.filesystems)
    {
        out << "node_filesystem_free_bytes{mountpoint=\"" << f.mountpoint << "\",fstype=\""
            << f.fstype << "\"} " << f.free_bytes << "\n";
    }

    // --- Network (counter) ---
    out << "# HELP node_network_receive_bytes_total Total bytes received.\n";
    out << "# TYPE node_network_receive_bytes_total counter\n";
    for (const auto &n : m.networks)
    {
        out << "node_network_receive_bytes_total{device=\"" << n.interface << "\"} "
            << n.rx_bytes << "\n";
    }

    out << "# HELP node_network_transmit_bytes_total Total bytes transmitted.\n";
    out << "# TYPE node_network_transmit_bytes_total counter\n";
    for (const auto &n : m.networks)
    {
        out << "node_network_transmit_bytes_total{device=\"" << n.interface << "\"} "
            << n.tx_bytes << "\n";
    }

    out << "# HELP node_network_receive_packets_total Total packets received.\n";
    out << "# TYPE node_network_receive_packets_total counter\n";
    for (const auto &n : m.networks)
    {
        out << "node_network_receive_packets_total{device=\"" << n.interface << "\"} "
            << n.rx_packets << "\n";
    }

    out << "# HELP node_network_transmit_packets_total Total packets transmitted.\n";
    out << "# TYPE node_network_transmit_packets_total counter\n";
    for (const auto &n : m.networks)
    {
        out << "node_network_transmit_packets_total{device=\"" << n.interface << "\"} "
            << n.tx_packets << "\n";
    }

    out << "# HELP node_network_receive_errs_total Total receive errors.\n";
    out << "# TYPE node_network_receive_errs_total counter\n";
    for (const auto &n : m.networks)
    {
        out << "node_network_receive_errs_total{device=\"" << n.interface << "\"} " << n.rx_errs
            << "\n";
    }

    out << "# HELP node_network_transmit_errs_total Total transmit errors.\n";
    out << "# TYPE node_network_transmit_errs_total counter\n";
    for (const auto &n : m.networks)
    {
        out << "node_network_transmit_errs_total{device=\"" << n.interface << "\"} " << n.tx_errs
            << "\n";
    }

    // --- Load average (gauge) ---
    out << "# HELP node_load1 1m load average.\n";
    out << "# TYPE node_load1 gauge\n";
    out << "node_load1 " << m.load1 << "\n";
    out << "# HELP node_load5 5m load average.\n";
    out << "# TYPE node_load5 gauge\n";
    out << "node_load5 " << m.load5 << "\n";
    out << "# HELP node_load15 15m load average.\n";
    out << "# TYPE node_load15 gauge\n";
    out << "node_load15 " << m.load15 << "\n";

    // --- Process counts (gauge) ---
    out << "# HELP node_procs_running Number of processes in runnable state.\n";
    out << "# TYPE node_procs_running gauge\n";
    out << "node_procs_running " << m.procsRunning << "\n";
    out << "# HELP node_procs_blocked Number of processes blocked waiting for I/O.\n";
    out << "# TYPE node_procs_blocked gauge\n";
    out << "node_procs_blocked " << m.procsBlocked << "\n";

    // --- Boot time (gauge) ---
    out << "# HELP node_boot_time_seconds Unix time of last boot.\n";
    out << "# TYPE node_boot_time_seconds gauge\n";
    out << "node_boot_time_seconds " << m.bootTimeSecs << "\n";

    // --- File descriptors (gauge) ---
    out << "# HELP node_filefd_allocated File descriptors currently allocated.\n";
    out << "# TYPE node_filefd_allocated gauge\n";
    out << "node_filefd_allocated " << m.filefdAllocated << "\n";
    out << "# HELP node_filefd_maximum Maximum number of open file descriptors.\n";
    out << "# TYPE node_filefd_maximum gauge\n";
    out << "node_filefd_maximum " << m.filefdMax << "\n";

    return out.str();
}

}  // namespace MetricsSerializer
}  // namespace fleet_agent
