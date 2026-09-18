#include "platform/PlatformShell.h"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <thread>
#include <vector>

#include "core/Logger.h"

extern char** environ;

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
