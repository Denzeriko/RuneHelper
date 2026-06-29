#pragma once

#include <string>
#include <vector>

struct DebugLine
{
    std::string ocrText;
    std::string matchedText;
    std::string price;
    int confidence = 0;
};

struct DebugData
{
    std::vector<DebugLine> lines;
};