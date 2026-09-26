#include "platform/PlatformShell.h"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "core/Logger.h"

extern char** environ;

namespace
{
std::string OsName()
{
    constexpr std::string_view kKey = "PRETTY_NAME=";

    std::ifstream file("/etc/os-release");
    std::string line;

    while (std::getline(file, line))
    {
        if (!line.starts_with(kKey))
            continue;

        std::string name = line.substr(kKey.size());

        if (name.size() >= 2 && name.front() == '"' && name.back() == '"')
            name = name.substr(1, name.size() - 2);

        return name;
    }

    return "Linux";
}

std::string Environment(const char* name)
{
    const char* value = std::getenv(name);
    return value && *value ? value : "-";
}

pid_t Spawn(const std::filesystem::path& program, const std::vector<std::string>& args, bool quiet)
{
    std::string path = program.string();
    std::vector<std::string> arguments = args;
    std::vector<char*> argv{ path.data() };

    for (std::string& argument : arguments)
        argv.push_back(argument.data());

    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    if (quiet)
    {
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    }

    pid_t pid = 0;
    const int result = posix_spawn(&pid, path.c_str(), &actions, nullptr, argv.data(), environ);

    posix_spawn_file_actions_destroy(&actions);

    return result == 0 ? pid : -1;
}
}

bool OpenExternalUrl(const std::string& url)
{
    if (url.empty())
        return false;

    std::vector<char*> argv;
    std::string opener = "xdg-open";
    std::string target = url;

    argv.push_back(opener.data());
    argv.push_back(target.data());
    argv.push_back(nullptr);

    pid_t pid = 0;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    const int result = posix_spawnp(&pid, opener.c_str(), &actions, nullptr, argv.data(), environ);

    posix_spawn_file_actions_destroy(&actions);

    if (result != 0)
    {
        LOG_ERROR("Could not launch xdg-open for " + url);
        return false;
    }

    std::thread([pid] { waitpid(pid, nullptr, 0); }).detach();

    return true;
}

std::string DescribeSystem()
{
    std::string text = "System: " + OsName() + "\n";

    utsname kernel{};

    if (uname(&kernel) == 0)
        text += std::string("Kernel: ") + kernel.release + "\n";

    text += "Session: " + Environment("XDG_SESSION_TYPE") + ", desktop " + Environment("XDG_CURRENT_DESKTOP") + "\n";

    return text;
}

std::filesystem::path CurrentExecutablePath()
{
    std::error_code ec;
    std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", ec);

    return ec ? std::filesystem::path() : path;
}

ProcessResult RunProcess(const std::filesystem::path& program, const std::vector<std::string>& args, std::chrono::milliseconds timeout)
{
    constexpr std::chrono::milliseconds kPollInterval{ 50 };

    ProcessResult result;
    const pid_t pid = Spawn(program, args, true);

    if (pid < 0)
        return result;

    result.started = true;

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int status = 0;

    while (waitpid(pid, &status, WNOHANG) == 0)
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            result.timedOut = true;
            return result;
        }

        std::this_thread::sleep_for(kPollInterval);
    }

    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

bool StartProcess(const std::filesystem::path& program)
{
    return Spawn(program, {}, false) > 0;
}
