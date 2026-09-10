#include "MetricsCollector.h"

#include "MetricsSerializer.h"

#include <sys/statfs.h>
#include <sys/vfs.h>

#include <chrono>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>

namespace fleet_agent
{

namespace
{
bool startsWith(const std::string &s, const std::string &prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}
}  // namespace

void MetricsCollector::collectCpu(AllMetrics &m)
{
    m.perCoreCpu.clear();
    m.procsRunning = 0;
    m.procsBlocked = 0;

    std::ifstream file("/proc/stat");
    if (!file.is_open())
    {
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        if (startsWith(line, "cpu"))
        {
            std::istringstream ls(line);
            std::string label;
            ls >> label;

            // Read whatever fields are present rather than assuming an
            // exact count: older kernels expose only 4 (user..idle), newer
            // ones add iowait/irq/softirq/steal/guest/guest_nice.
            std::vector<unsigned long long> fields;
            unsigned long long v;
            while (ls >> v)
            {
                fields.push_back(v);
            }

            CpuStat stat;
            stat.label = label;
            unsigned long long *slots[8] = {&stat.user,
                                            &stat.nice,
                                            &stat.system,
                                            &stat.idle,
                                            &stat.iowait,
                                            &stat.irq,
                                            &stat.softirq,
                                            &stat.steal};
            for (size_t i = 0; i < 8 && i < fields.size(); ++i)
            {
                *slots[i] = fields[i];
            }
            m.perCoreCpu.push_back(stat);
        }
        else if (startsWith(line, "procs_running"))
        {
            std::istringstream ls(line.substr(std::string("procs_running").size()));
            ls >> m.procsRunning;
        }
        else if (startsWith(line, "procs_blocked"))
        {
            std::istringstream ls(line.substr(std::string("procs_blocked").size()));
            ls >> m.procsBlocked;
        }
    }
}

void MetricsCollector::collectMemory(AllMetrics &m)
{
    std::ifstream file("/proc/meminfo");
    if (!file.is_open())
    {
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        size_t colon = line.find(':');
        if (colon == std::string::npos)
        {
            continue;
        }
        std::string key = line.substr(0, colon);
        std::istringstream valueStream(line.substr(colon + 1));
        long long value = 0;
        valueStream >> value;  // stops at whitespace, ignores trailing "kB"

        if (key == "MemTotal")
        {
            m.mem.mem_total_kb = value;
        }
        else if (key == "MemFree")
        {
            m.mem.mem_free_kb = value;
        }
        else if (key == "MemAvailable")
        {
            m.mem.mem_available_kb = value;
        }
        else if (key == "Buffers")
        {
            m.mem.buffers_kb = value;
        }
        else if (key == "Cached")
        {
            m.mem.cached_kb = value;
        }
        else if (key == "SwapTotal")
        {
            m.mem.swap_total_kb = value;
        }
        else if (key == "SwapFree")
        {
            m.mem.swap_free_kb = value;
        }
    }
}

void MetricsCollector::collectDisk(AllMetrics &m)
{
    m.disks.clear();
    std::ifstream file("/proc/diskstats");
    if (!file.is_open())
    {
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream ls(line);
        std::vector<std::string> fields;
        std::string tok;
        while (ls >> tok)
        {
            fields.push_back(tok);
        }

        // major minor devname reads_completed reads_merged sectors_read
        // ms_reading writes_completed writes_merged sectors_written
        // ms_writing ios_in_progress ms_doing_io ...
        if (fields.size() < 13)
        {
            continue;
        }

        const std::string &devname = fields[2];
        if (startsWith(devname, "loop") || startsWith(devname, "ram"))
        {
            continue;
        }

        try
        {
            DiskStat stat;
            stat.device = devname;
            stat.sectors_read = std::stoull(fields[5]);
            stat.sectors_written = std::stoull(fields[9]);
            stat.ms_doing_io = std::stoull(fields[12]);
            m.disks.push_back(stat);
        }
        catch (const std::exception &)
        {
            continue;
        }
    }
}

void MetricsCollector::collectFilesystems(AllMetrics &m)
{
    m.filesystems.clear();

    static const std::set<std::string> kSkipFstypes = {
        "tmpfs",   "devtmpfs", "sysfs",   "proc",    "devpts",  "securityfs",
        "cgroup",  "cgroup2",  "pstore",  "bpf",     "tracefs", "hugetlbfs",
        "mqueue",  "debugfs",  "fusectl", "configfs", "overlay"};

    std::ifstream file("/proc/mounts");
    if (!file.is_open())
    {
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream ls(line);
        std::string device, mountpoint, fstype, options;
        int dump = 0, pass = 0;
        if (!(ls >> device >> mountpoint >> fstype >> options >> dump >> pass))
        {
            continue;
        }

        if (kSkipFstypes.count(fstype) > 0)
        {
            continue;
        }

        struct statfs st
        {
        };
        if (statfs(mountpoint.c_str(), &st) != 0)
        {
            continue;
        }

        long long sizeBytes = static_cast<long long>(st.f_blocks) * st.f_frsize;
        if (sizeBytes == 0)
        {
            continue;
        }

        FilesystemStat fsStat;
        fsStat.mountpoint = mountpoint;
        fsStat.fstype = fstype;
        fsStat.size_bytes = sizeBytes;
        fsStat.avail_bytes = static_cast<long long>(st.f_bavail) * st.f_frsize;
        fsStat.free_bytes = static_cast<long long>(st.f_bfree) * st.f_frsize;
        m.filesystems.push_back(fsStat);
    }
}

void MetricsCollector::collectNetwork(AllMetrics &m)
{
    m.networks.clear();
    std::ifstream file("/proc/net/dev");
    if (!file.is_open())
    {
        return;
    }

    std::string line;
    int lineNum = 0;
    while (std::getline(file, line))
    {
        ++lineNum;
        if (lineNum <= 2)
        {
            continue;  // "Inter-|" and "face |" header lines
        }

        size_t colon = line.find(':');
        if (colon == std::string::npos)
        {
            continue;
        }

        std::string iface = line.substr(0, colon);
        size_t start = iface.find_first_not_of(" \t");
        size_t end = iface.find_last_not_of(" \t");
        if (start == std::string::npos)
        {
            continue;
        }
        iface = iface.substr(start, end - start + 1);

        if (iface == "lo")
        {
            continue;
        }

        std::istringstream ls(line.substr(colon + 1));
        std::vector<unsigned long long> fields;
        unsigned long long v;
        while (ls >> v)
        {
            fields.push_back(v);
        }
        if (fields.size() < 16)
        {
            continue;
        }

        NetStat stat;
        stat.interface = iface;
        stat.rx_bytes = fields[0];
        stat.rx_packets = fields[1];
        stat.rx_errs = fields[2];
        stat.tx_bytes = fields[8];
        stat.tx_packets = fields[9];
        stat.tx_errs = fields[10];
        m.networks.push_back(stat);
    }
}

void MetricsCollector::collectLoadAndUptime(AllMetrics &m)
{
    {
        std::ifstream file("/proc/loadavg");
        if (file.is_open())
        {
            std::string line;
            std::getline(file, line);
            std::istringstream ls(line);
            ls >> m.load1 >> m.load5 >> m.load15;
        }
    }
    {
        std::ifstream file("/proc/uptime");
        if (file.is_open())
        {
            std::string line;
            std::getline(file, line);
            std::istringstream ls(line);
            double uptimeSeconds = 0;
            ls >> uptimeSeconds;
            m.bootTimeSecs = static_cast<double>(std::time(nullptr)) - uptimeSeconds;
        }
    }
}

void MetricsCollector::collectFileDescriptors(AllMetrics &m)
{
    std::ifstream file("/proc/sys/fs/file-nr");
    if (!file.is_open())
    {
        return;
    }
    std::string line;
    std::getline(file, line);
    std::istringstream ls(line);
    long long allocated = 0, unused = 0, maxFd = 0;
    ls >> allocated >> unused >> maxFd;
    m.filefdAllocated = allocated;
    m.filefdMax = maxFd;
}

AllMetrics MetricsCollector::collectAll()
{
    AllMetrics m;
    collectCpu(m);
    collectMemory(m);
    collectDisk(m);
    collectFilesystems(m);
    collectNetwork(m);
    collectLoadAndUptime(m);
    collectFileDescriptors(m);
    return m;
}

void MetricsCollector::collectOnce()
{
    AllMetrics m = collectAll();
    std::string serialized = MetricsSerializer::serialize(m);
    std::lock_guard<std::mutex> lock(mutex_);
    cachedMetrics_ = std::move(serialized);
}

void MetricsCollector::loop(int intervalSeconds)
{
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
        collectOnce();
    }
}

void MetricsCollector::start(int intervalSeconds)
{
    collectOnce();
    std::thread worker([this, intervalSeconds]() { loop(intervalSeconds); });
    worker.detach();
}

std::string MetricsCollector::getCachedMetrics()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return cachedMetrics_;
}

}  // namespace fleet_agent
