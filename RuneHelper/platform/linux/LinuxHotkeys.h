#pragma once

#include <functional>
#include <memory>

enum class HotkeyAction
{
    ToggleOcr,
    SingleSnapshot,
    SelectRegion
};

class LinuxHotkeys
{
public:
    using Dispatch = std::function<void(HotkeyAction)>;

    virtual ~LinuxHotkeys() = default;

    virtual void Init() = 0;
    virtual void Shutdown() = 0;

    virtual void Poll(const Dispatch& dispatch) = 0;

    virtual void Register(int toggleOcrKey, int singleSnapshotKey, int selectRegionKey) = 0;
    virtual void Unregister() = 0;
};

std::unique_ptr<LinuxHotkeys> CreateLinuxHotkeys();

int RunLinuxHotkeyClient(int argc, char** argv);
