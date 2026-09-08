#pragma once

#include <string>
#include <vector>

namespace fleet_agent
{

struct CommandResult
{
    std::string stdout_output;
    std::string stderr_output;
    int exit_code = 0;
    long duration_ms = 0;
};

namespace CommandRunner
{

// True if `command` is in the fixed whitelist of read-only diagnostic
// commands the manager is allowed to run on this node.
bool isWhitelisted(const std::string &command);

// Runs `command` with `args` via fork+execvp (never a shell, so there is no
// shell-injection surface) and captures stdout/stderr/exit code. Refuses
// non-whitelisted commands defensively even though the HTTP handler also
// checks isWhitelisted() before calling this.
CommandResult execute(const std::string &command, const std::vector<std::string> &args);

}  // namespace CommandRunner
}  // namespace fleet_agent
