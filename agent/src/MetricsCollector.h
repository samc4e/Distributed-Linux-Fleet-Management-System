#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace fleet_agent
{

struct CpuStat
{
    std::string label;  // "cpu" for the aggregate line, "cpu0"/"cpu1"/... per-core
    unsigned long long user = 0;
    unsigned long long nice = 0;
    unsigned long long system = 0;
    unsigned long long idle = 0;
    unsigned long long iowait = 0;
    unsigned long long irq = 0;
    unsigned long long softirq = 0;
    unsigned long long steal = 0;

    unsigned long long total() const
    {
        return user + nice + system + idle + iowait + irq + softirq + steal;
    }
    unsigned long long idleTotal() const
    {
        return idle + iowait;
    }
};

struct MemInfo
{
    long long mem_total_kb = 0;
    long long mem_free_kb = 0;
    long long mem_available_kb = 0;
    long long buffers_kb = 0;
    long long cached_kb = 0;
    long long swap_total_kb = 0;
    long long swap_free_kb = 0;
};

struct DiskStat
{
    std::string device;
    unsigned long long sectors_read = 0;
    unsigned long long sectors_written = 0;
    unsigned long long ms_doing_io = 0;
};

struct FilesystemStat
{
    std::string mountpoint;
    std::string fstype;
    long long size_bytes = 0;
    long long avail_bytes = 0;
    long long free_bytes = 0;
};

struct NetStat
{
    std::string interface;
    unsigned long long rx_bytes = 0;
    unsigned long long tx_bytes = 0;
    unsigned long long rx_packets = 0;
    unsigned long long tx_packets = 0;
    unsigned long long rx_errs = 0;
    unsigned long long tx_errs = 0;
};

struct AllMetrics
{
    std::vector<CpuStat> perCoreCpu;  // index 0 = aggregate "cpu" line
    MemInfo mem;
    std::vector<DiskStat> disks;
    std::vector<FilesystemStat> filesystems;
    std::vector<NetStat> networks;
    double load1 = 0;
    double load5 = 0;
    double load15 = 0;
    int procsRunning = 0;
    int procsBlocked = 0;
    double bootTimeSecs = 0;
    long long filefdAllocated = 0;
    long long filefdMax = 0;
};

// Periodically parses /proc (and statfs()'s each mounted filesystem),
// caching the result as Prometheus exposition text so GET /metrics can
// serve it instantly rather than re-reading the kernel on every scrape.
class MetricsCollector
{
  public:
    // Collects once synchronously (so the cache is never empty) and then
    // starts a detached background thread that repeats every
    // intervalSeconds.
    void start(int intervalSeconds);

    std::string getCachedMetrics();

    static AllMetrics collectAll();

    // Each fills in the corresponding section of `m`. Public and static so
    // they can be exercised independently of the collection thread.
    static void collectCpu(AllMetrics &m);
    static void collectMemory(AllMetrics &m);
    static void collectDisk(AllMetrics &m);
    static void collectFilesystems(AllMetrics &m);
    static void collectNetwork(AllMetrics &m);
    static void collectLoadAndUptime(AllMetrics &m);
    static void collectFileDescriptors(AllMetrics &m);

  private:
    void loop(int intervalSeconds);
    void collectOnce();

    std::mutex mutex_;
    std::string cachedMetrics_;
};

}  // namespace fleet_agent
