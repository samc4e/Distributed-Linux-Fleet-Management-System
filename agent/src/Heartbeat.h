#pragma once

#include <string>

namespace fleet_agent
{

// Background thread that POSTs a liveness + system-info payload to the
// manager every intervalSeconds. This is what flips a node from "pending"
// to "online" in the dashboard (see AgentController::heartbeat).
class Heartbeat
{
  public:
    Heartbeat(std::string managerUrl, std::string agentToken, int intervalSeconds);

    // Sends one heartbeat immediately (synchronously) so status flips to
    // 'online' as soon as possible, then starts a detached background
    // thread that repeats every intervalSeconds.
    void start();

  private:
    void loop();
    void sendOnce();

    std::string managerUrl_;
    std::string agentToken_;
    int intervalSeconds_;
};

// System-info helpers used to build the heartbeat payload. Exposed at
// namespace scope so they can be exercised independently of the network.
std::string getHostname();
std::string getKernelVersion();
std::string getDistroName();
std::string getArch();
std::string getCpuModel();
int getCpuCores();
long long getMemTotalBytes();
std::string getLocalIp();

}  // namespace fleet_agent
