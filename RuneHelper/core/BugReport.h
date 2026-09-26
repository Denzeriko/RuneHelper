#pragma once

#include <filesystem>
#include <optional>
#include <string>

std::optional<std::filesystem::path> WriteBugReport(const std::string& systemInfo);
