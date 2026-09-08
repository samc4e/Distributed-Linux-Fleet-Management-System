#include "Heartbeat.h"

#include "httplib.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

namespace fleet_agent
{

namespace
{
std::string readFirstLine(const std::string &path)
{
    std::ifstream file(path);
    std::string line;
    if (file.is_open())
    {
        std::getline(file, line);
    }
    return line;
}

std::string jsonEscape(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        switch (c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) >= 0x20)
                {
                    out += c;
                }
        }
    }
    return out;
}
}  // namespace

std::string getHostname()
{
    char buf[256];
    if (gethostname(buf, sizeof(buf)) == 0)
    {
        buf[sizeof(buf) - 1] = '\0';
        return std::string(buf);
    }
    return "unknown-host";
}

std::string getKernelVersion()
{
    std::string version = readFirstLine("/proc/sys/kernel/osrelease");
    if (!version.empty())
    {
        return version;
    }
    struct utsname u
    {
    };
    if (uname(&u) == 0)
    {
        return std::string(u.release);
    }
    return "unknown";
}

std::string getDistroName()
{
    std::ifstream file("/etc/os-release");
    if (!file.is_open())
    {
        return "unknown";
    }
    const std::string prefix = "PRETTY_NAME=";
    std::string line;
    while (std::getline(file, line))
    {
        if (line.compare(0, prefix.size(), prefix) == 0)
        {
            std::string value = line.substr(prefix.size());
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            {
                value = value.substr(1, value.size() - 2);
            }
            return value;
        }
    }
    return "unknown";
}

std::string getArch()
{
    std::string arch = readFirstLine("/proc/sys/kernel/arch");
    if (!arch.empty())
    {
        return arch;
    }
    struct utsname u
    {
    };
    if (uname(&u) == 0)
    {
        return std::string(u.machine);
    }
    return "unknown";
}

std::string getCpuModel()
{
    std::ifstream file("/proc/cpuinfo");
    if (!file.is_open())
    {
        return "unknown";
    }
    const std::string prefix = "model name";
    std::string line;
    while (std::getline(file, line))
    {
        if (line.compare(0, prefix.size(), prefix) == 0)
        {
            size_t colon = line.find(':');
            if (colon != std::string::npos)
            {
                std::string value = line.substr(colon + 1);
                size_t start = value.find_first_not_of(" \t");
                if (start != std::string::npos)
                {
                    return value.substr(start);
                }
            }
        }
    }
    return "unknown";
}

int getCpuCores()
{
    std::ifstream file("/proc/cpuinfo");
    int count = 0;
    if (file.is_open())
    {
        std::string line;
        const std::string prefix = "processor";
        while (std::getline(file, line))
        {
            if (line.compare(0, prefix.size(), prefix) == 0)
            {
                ++count;
            }
        }
    }
    if (count > 0)
    {
        return count;
    }
    unsigned int hw = std::thread::hardware_concurrency();
    return hw > 0 ? static_cast<int>(hw) : 1;
}

long long getMemTotalBytes()
{
    std::ifstream file("/proc/meminfo");
    if (!file.is_open())
    {
        return 0;
    }
    const std::string prefix = "MemTotal:";
    std::string line;
    while (std::getline(file, line))
    {
        if (line.compare(0, prefix.size(), prefix) == 0)
        {
            std::istringstream ls(line.substr(prefix.size()));
            long long kb = 0;
            ls >> kb;
            return kb * 1024LL;
        }
    }
    return 0;
}

std::string getLocalIp()
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        return "127.0.0.1";
    }

    struct sockaddr_in remote
    {
    };
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);
    if (inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr) != 1)
    {
        close(sock);
        return "127.0.0.1";
    }

    // No data is sent: connect() on a UDP socket only picks a local route
    // and binds a local address, which is what we read back below.
    if (connect(sock, reinterpret_cast<struct sockaddr *>(&remote), sizeof(remote)) != 0)
    {
        close(sock);
        return "127.0.0.1";
    }

    struct sockaddr_in local
    {
    };
    socklen_t len = sizeof(local);
    if (getsockname(sock, reinterpret_cast<struct sockaddr *>(&local), &len) != 0)
    {
        close(sock);
        return "127.0.0.1";
    }
    close(sock);

    char buf[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf)) == nullptr)
    {
        return "127.0.0.1";
    }
    return std::string(buf);
}

Heartbeat::Heartbeat(std::string managerUrl, std::string agentToken, int intervalSeconds)
    : managerUrl_(std::move(managerUrl)),
      agentToken_(std::move(agentToken)),
      intervalSeconds_(intervalSeconds)
{
}

void Heartbeat::sendOnce()
{
    std::ostringstream body;
    body << "{"
        << "\"agent_token\":\"" << jsonEscape(agentToken_) << "\","
        << "\"hostname\":\"" << jsonEscape(getHostname()) << "\","
        << "\"ip_address\":\"" << jsonEscape(getLocalIp()) << "\","
        << "\"agent_version\":\"1.0.0\","
        << "\"os_info\":{"
        << "\"kernel\":\"" << jsonEscape(getKernelVersion()) << "\","
        << "\"distro\":\"" << jsonEscape(getDistroName()) << "\","
        << "\"arch\":\"" << jsonEscape(getArch()) << "\""
        << "},"
        << "\"hardware_info\":{"
        << "\"cpu_model\":\"" << jsonEscape(getCpuModel()) << "\","
        << "\"cpu_cores\":" << getCpuCores() << ","
        << "\"mem_total_bytes\":" << getMemTotalBytes()
        << "}"
        << "}";

    httplib::Client cli(managerUrl_);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(5, 0);

    auto res = cli.Post("/api/agent/heartbeat", body.str(), "application/json");
    if (!res)
    {
        std::cerr << "Heartbeat: failed to reach manager at " << managerUrl_ << ": "
                  << httplib::to_string(res.error()) << std::endl;
    }
    else if (res->status != 200)
    {
        std::cerr << "Heartbeat: manager rejected heartbeat, HTTP " << res->status << ": "
                  << res->body << std::endl;
    }
}

void Heartbeat::loop()
{
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds_));
        sendOnce();
    }
}

void Heartbeat::start()
{
    sendOnce();
    std::thread worker([this]() { loop(); });
    worker.detach();
}

}  // namespace fleet_agent
