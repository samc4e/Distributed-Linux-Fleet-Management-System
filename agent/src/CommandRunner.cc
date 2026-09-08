#include "CommandRunner.h"

#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <set>

namespace fleet_agent
{
namespace CommandRunner
{

namespace
{
const std::set<std::string> kWhitelist = {
    "df", "free", "ps", "uptime", "netstat", "ss", "lscpu", "uname", "top"};

// Drains stdoutFd and stderrFd concurrently via poll() until both report
// EOF. Reading them one at a time (fully draining stdout, then reading
// stderr) would deadlock if a command writes enough to the *other* pipe to
// fill its kernel buffer before we get to it.
void readBothPipes(int stdoutFd, int stderrFd, std::string &outStdout, std::string &outStderr)
{
    bool stdoutOpen = true;
    bool stderrOpen = true;
    std::array<char, 4096> buf{};

    while (stdoutOpen || stderrOpen)
    {
        struct pollfd fds[2];
        fds[0].fd = stdoutOpen ? stdoutFd : -1;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = stderrOpen ? stderrFd : -1;
        fds[1].events = POLLIN;
        fds[1].revents = 0;

        int rv = poll(fds, 2, -1);
        if (rv < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }

        if (stdoutOpen && (fds[0].revents & (POLLIN | POLLHUP | POLLERR)))
        {
            ssize_t n = read(stdoutFd, buf.data(), buf.size());
            if (n > 0)
            {
                outStdout.append(buf.data(), static_cast<size_t>(n));
            }
            else
            {
                stdoutOpen = false;
            }
        }
        if (stderrOpen && (fds[1].revents & (POLLIN | POLLHUP | POLLERR)))
        {
            ssize_t n = read(stderrFd, buf.data(), buf.size());
            if (n > 0)
            {
                outStderr.append(buf.data(), static_cast<size_t>(n));
            }
            else
            {
                stderrOpen = false;
            }
        }
    }
}
}  // namespace

bool isWhitelisted(const std::string &command)
{
    return kWhitelist.find(command) != kWhitelist.end();
}

CommandResult execute(const std::string &command, const std::vector<std::string> &args)
{
    CommandResult result;

    if (!isWhitelisted(command))
    {
        result.stderr_output = "command not allowed";
        result.exit_code = 403;
        result.duration_ms = 0;
        return result;
    }

    int stdoutPipe[2];
    int stderrPipe[2];
    if (pipe(stdoutPipe) != 0 || pipe(stderrPipe) != 0)
    {
        result.stderr_output = std::string("pipe() failed: ") + std::strerror(errno);
        result.exit_code = -1;
        return result;
    }

    auto startTime = std::chrono::steady_clock::now();

    pid_t pid = fork();
    if (pid < 0)
    {
        result.stderr_output = std::string("fork() failed: ") + std::strerror(errno);
        result.exit_code = -1;
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        close(stderrPipe[0]);
        close(stderrPipe[1]);
        return result;
    }

    if (pid == 0)
    {
        // Child: wire pipes to stdout/stderr and exec. execvp (never a
        // shell) means args reach the target program as an argv array, with
        // no shell metacharacter interpretation possible.
        close(stdoutPipe[0]);
        close(stderrPipe[0]);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        dup2(stderrPipe[1], STDERR_FILENO);
        close(stdoutPipe[1]);
        close(stderrPipe[1]);

        std::vector<char *> argv;
        argv.push_back(const_cast<char *>(command.c_str()));
        for (const auto &a : args)
        {
            argv.push_back(const_cast<char *>(a.c_str()));
        }
        argv.push_back(nullptr);

        execvp(command.c_str(), argv.data());
        _exit(127);  // execvp only returns on failure
    }

    // Parent
    close(stdoutPipe[1]);
    close(stderrPipe[1]);

    readBothPipes(stdoutPipe[0], stderrPipe[0], result.stdout_output, result.stderr_output);

    close(stdoutPipe[0]);
    close(stderrPipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status))
    {
        result.exit_code = WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status))
    {
        result.exit_code = 128 + WTERMSIG(status);
    }
    else
    {
        result.exit_code = -1;
    }

    auto endTime = std::chrono::steady_clock::now();
    result.duration_ms = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count());

    return result;
}

}  // namespace CommandRunner
}  // namespace fleet_agent
