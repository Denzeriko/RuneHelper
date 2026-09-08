#include "platform/linux/LinuxHotkeys.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "core/Logger.h"

namespace
{
std::string SocketPath()
{
    if (const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR"); runtimeDir && *runtimeDir)
        return std::string(runtimeDir) + "/runehelper.sock";

    return "/tmp/runehelper-" + std::to_string(getuid()) + ".sock";
}

bool FillAddress(sockaddr_un& address, const std::string& path)
{
    if (path.size() >= sizeof(address.sun_path))
        return false;

    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
    return true;
}

bool ParseCommand(const std::string& command, HotkeyAction& action)
{
    if (command == "toggle-ocr" || command == "--toggle-ocr")
    {
        action = HotkeyAction::ToggleOcr;
        return true;
    }

    if (command == "snapshot" || command == "--snapshot")
    {
        action = HotkeyAction::SingleSnapshot;
        return true;
    }

    if (command == "select-region" || command == "--select-region")
    {
        action = HotkeyAction::SelectRegion;
        return true;
    }

    return false;
}

class WaylandHotkeys final : public LinuxHotkeys
{
public:
    void Init() override;
    void Shutdown() override;

    void Poll(const Dispatch& dispatch) override;

    void Register(int toggleOcrKey, int singleSnapshotKey, int selectRegionKey) override;
    void Unregister() override;

private:
    int socket_ = -1;
    std::string path_;
    bool loggedUsage_ = false;
};

bool IsStaleSocket(const std::string& path)
{
    const int probe = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);

    if (probe < 0)
        return false;

    sockaddr_un address{};

    if (!FillAddress(address, path))
    {
        close(probe);
        return false;
    }

    const bool stale = connect(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0;
    close(probe);
    return stale;
}

void WaylandHotkeys::Init()
{
    path_ = SocketPath();

    if (access(path_.c_str(), F_OK) == 0 && IsStaleSocket(path_))
        unlink(path_.c_str());

    socket_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

    if (socket_ < 0)
    {
        LOG_ERROR("Wayland hotkeys: failed to create the control socket");
        return;
    }

    sockaddr_un address{};

    if (!FillAddress(address, path_))
    {
        LOG_ERROR("Wayland hotkeys: control socket path is too long: " + path_);
        close(socket_);
        socket_ = -1;
        return;
    }

    if (bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        LOG_ERROR("Wayland hotkeys: another instance already owns " + path_);
        close(socket_);
        socket_ = -1;
        return;
    }

    LOG_INFO("Wayland hotkeys: listening on " + path_);
}

void WaylandHotkeys::Shutdown()
{
    if (socket_ < 0)
        return;

    close(socket_);
    socket_ = -1;
    unlink(path_.c_str());
}

void WaylandHotkeys::Poll(const Dispatch& dispatch)
{
    if (socket_ < 0)
        return;

    char buffer[64];

    while (true)
    {
        const ssize_t received = recv(socket_, buffer, sizeof(buffer) - 1, 0);

        if (received <= 0)
            return;

        buffer[received] = '\0';

        HotkeyAction action = HotkeyAction::ToggleOcr;

        if (ParseCommand(buffer, action))
            dispatch(action);
        else
            LOG_ERROR(std::string("Wayland hotkeys: unknown command ") + buffer);
    }
}

void WaylandHotkeys::Register(int, int, int)
{
    if (loggedUsage_ || socket_ < 0)
        return;

    LOG_INFO("Wayland has no global key grabs: bind keys in the compositor to 'RuneHelper --toggle-ocr', '--snapshot' or '--select-region'");
    loggedUsage_ = true;
}

void WaylandHotkeys::Unregister()
{
}
}

std::unique_ptr<LinuxHotkeys> CreateLinuxHotkeys()
{
    return std::make_unique<WaylandHotkeys>();
}

int RunLinuxHotkeyClient(int argc, char** argv)
{
    if (argc < 2 || !argv || !argv[1])
        return -1;

    HotkeyAction action = HotkeyAction::ToggleOcr;
    const std::string command(argv[1]);

    if (!ParseCommand(command, action))
        return -1;

    const std::string path = SocketPath();
    const int handle = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);

    if (handle < 0)
        return 1;

    sockaddr_un address{};

    if (!FillAddress(address, path))
    {
        close(handle);
        return 1;
    }

    const std::string payload = command.substr(command.find_first_not_of('-'));
    const ssize_t sent = sendto(
        handle,
        payload.c_str(),
        payload.size(),
        0,
        reinterpret_cast<sockaddr*>(&address),
        sizeof(address)
    );

    close(handle);
    return sent == static_cast<ssize_t>(payload.size()) ? 0 : 1;
}
