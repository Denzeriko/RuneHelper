#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

inline constexpr std::size_t kMaxClipboardText = 65536;

class ClipboardWatcher
{
public:
    ClipboardWatcher();
    ~ClipboardWatcher();
    ClipboardWatcher(const ClipboardWatcher&) = delete;
    ClipboardWatcher& operator=(const ClipboardWatcher&) = delete;

    bool Start();
    void Stop();
    bool Running() const;
    std::optional<std::string> Poll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
