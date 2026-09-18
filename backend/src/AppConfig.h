#pragma once

#include <string>

namespace fleet
{

// Plain data holder for the handful of config.json values that controllers
// need directly (as opposed to DB connection settings, which are only ever
// used once, in main.cc, to set up the Drogon DB client). Passed by value
// into controller constructors since it is small and copied only at
// startup.
struct AppConfig
{
    std::string jwtSecret;
    std::string backendHost;
    int listenPort = 8080;
    std::string allowedPrometheusCidr = "127.0.0.1";
};

}  // namespace fleet
