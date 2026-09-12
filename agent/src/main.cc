#include "CommandRunner.h"
#include "Heartbeat.h"
#include "HmacUtils.h"
#include "MetricsCollector.h"

#include "httplib.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace fleet_agent;

namespace
{

// --- Minimal ad-hoc JSON reading -------------------------------------------
// The agent deliberately has no JSON library dependency (see CMakeLists.txt
// / README: OpenSSL + cpp-httplib only). Both /etc/fleet-agent/config.json
// and the POST /execute request body are small, known, flat shapes, so a
// tiny hand-rolled reader is simpler and more honest here than pulling in a
// whole JSON library for two field lookups.

std::string extractJsonValue(const std::string &text, const std::string &key)
{
    std::string pattern = "\"" + key + "\"";
    size_t pos = text.find(pattern);
    if (pos == std::string::npos)
    {
        return "";
    }
    pos = text.find(':', pos + pattern.size());
    if (pos == std::string::npos)
    {
        return "";
    }
    ++pos;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
    {
        ++pos;
    }
    if (pos >= text.size())
    {
        return "";
    }

    if (text[pos] == '"')
    {
        size_t end = pos + 1;
        std::string value;
        while (end < text.size() && text[end] != '"')
        {
            if (text[end] == '\\' && end + 1 < text.size())
            {
                value += text[end + 1];
                end += 2;
            }
            else
            {
                value += text[end];
                ++end;
            }
        }
        return value;
    }

    size_t end = pos;
    while (end < text.size() && text[end] != ',' && text[end] != '}' && text[end] != '\n' &&
          text[end] != '\r')
    {
        ++end;
    }
    std::string value = text.substr(pos, end - pos);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
    {
        value.pop_back();
    }
    return value;
}

std::vector<std::string> extractJsonStringArray(const std::string &text, const std::string &key)
{
    std::vector<std::string> result;
    std::string pattern = "\"" + key + "\"";
    size_t pos = text.find(pattern);
    if (pos == std::string::npos)
    {
        return result;
    }
    pos = text.find(':', pos + pattern.size());
    if (pos == std::string::npos)
    {
        return result;
    }
    size_t arrStart = text.find('[', pos);
    if (arrStart == std::string::npos)
    {
        return result;
    }
    size_t arrEnd = text.find(']', arrStart);
    if (arrEnd == std::string::npos)
    {
        return result;
    }

    size_t i = arrStart + 1;
    while (i < arrEnd)
    {
        size_t quoteStart = text.find('"', i);
        if (quoteStart == std::string::npos || quoteStart > arrEnd)
        {
            break;
        }
        size_t j = quoteStart + 1;
        std::string value;
        while (j < text.size() && text[j] != '"')
        {
            if (text[j] == '\\' && j + 1 < text.size())
            {
                value += text[j + 1];
                j += 2;
            }
            else
            {
                value += text[j];
                ++j;
            }
        }
        result.push_back(value);
        i = j + 1;
    }
    return result;
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

struct AgentConfig
{
    std::string managerUrl;
    std::string agentToken;
    int agentPort = 9200;
    int collectIntervalSeconds = 15;
    int heartbeatIntervalSeconds = 30;
};

AgentConfig loadConfig()
{
    AgentConfig cfg;

    std::ifstream file("/etc/fleet-agent/config.json");
    if (file.is_open())
    {
        std::ostringstream ss;
        ss << file.rdbuf();
        std::string text = ss.str();

        cfg.managerUrl = extractJsonValue(text, "manager_url");
        cfg.agentToken = extractJsonValue(text, "agent_token");

        if (std::string v = extractJsonValue(text, "agent_port"); !v.empty())
        {
            try
            {
                cfg.agentPort = std::stoi(v);
            }
            catch (const std::exception &)
            {
            }
        }
        if (std::string v = extractJsonValue(text, "collect_interval_seconds"); !v.empty())
        {
            try
            {
                cfg.collectIntervalSeconds = std::stoi(v);
            }
            catch (const std::exception &)
            {
            }
        }
        if (std::string v = extractJsonValue(text, "heartbeat_interval_seconds"); !v.empty())
        {
            try
            {
                cfg.heartbeatIntervalSeconds = std::stoi(v);
            }
            catch (const std::exception &)
            {
            }
        }

        std::cout << "fleet-agent: loaded config from /etc/fleet-agent/config.json" << std::endl;
    }
    else
    {
        std::cout << "fleet-agent: /etc/fleet-agent/config.json not found, falling back to "
                    "environment variables"
                 << std::endl;
    }

    // Environment variables take precedence, so `AGENT_TOKEN=... MANAGER_URL=...
    // fleet-agent` works even with no config file at all (matches the
    // onboarding command built by NodeController::create).
    if (const char *env = std::getenv("MANAGER_URL"))
    {
        cfg.managerUrl = env;
    }
    if (const char *env = std::getenv("AGENT_TOKEN"))
    {
        cfg.agentToken = env;
    }
    if (const char *env = std::getenv("AGENT_PORT"))
    {
        try
        {
            cfg.agentPort = std::stoi(env);
        }
        catch (const std::exception &)
        {
        }
    }

    return cfg;
}

}  // namespace

int main()
{
    AgentConfig cfg = loadConfig();

    if (cfg.managerUrl.empty() || cfg.agentToken.empty())
    {
        std::cerr << "fleet-agent: manager_url and agent_token must be set, via "
                    "/etc/fleet-agent/config.json or the MANAGER_URL / AGENT_TOKEN "
                    "environment variables"
                 << std::endl;
        return 1;
    }

    std::cout << "fleet-agent starting: manager=" << cfg.managerUrl << " port=" << cfg.agentPort
             << " collect_interval=" << cfg.collectIntervalSeconds
             << "s heartbeat_interval=" << cfg.heartbeatIntervalSeconds << "s" << std::endl;

    static MetricsCollector collector;
    collector.start(cfg.collectIntervalSeconds);

    static Heartbeat heartbeat(cfg.managerUrl, cfg.agentToken, cfg.heartbeatIntervalSeconds);
    heartbeat.start();

    httplib::Server server;

    server.Get("/metrics", [](const httplib::Request &, httplib::Response &res) {
        res.set_content(collector.getCachedMetrics(), "text/plain; version=0.0.4; charset=utf-8");
    });

    server.Get("/health", [](const httplib::Request &, httplib::Response &res) {
        res.set_content(R"({"status":"ok","version":"1.0.0"})", "application/json");
    });

    server.Post("/execute", [&cfg](const httplib::Request &req, httplib::Response &res) {
        std::string signature = req.get_header_value("X-Agent-Signature");
        if (signature.empty() || !HmacUtils::verifyHmac(cfg.agentToken, req.body, signature))
        {
            res.status = 401;
            res.set_content(R"({"error":"invalid signature"})", "application/json");
            return;
        }

        std::string command = extractJsonValue(req.body, "command");
        std::vector<std::string> args = extractJsonStringArray(req.body, "args");

        if (!CommandRunner::isWhitelisted(command))
        {
            res.status = 400;
            res.set_content(R"({"error":"command not allowed"})", "application/json");
            return;
        }

        CommandResult result = CommandRunner::execute(command, args);

        std::ostringstream out;
        out << "{"
            << "\"stdout\":\"" << jsonEscape(result.stdout_output) << "\","
            << "\"stderr\":\"" << jsonEscape(result.stderr_output) << "\","
            << "\"exit_code\":" << result.exit_code << ","
            << "\"duration_ms\":" << result.duration_ms << "}";
        res.set_content(out.str(), "application/json");
    });

    std::cout << "fleet-agent listening on 0.0.0.0:" << cfg.agentPort << std::endl;
    server.listen("0.0.0.0", cfg.agentPort);

    return 0;
}
