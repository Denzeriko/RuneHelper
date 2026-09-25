#pragma once

#include <string>
#include <vector>

struct DebugLine
{
    std::string ocrText;
    std::string matchedText;
    std::string price;
    double priceEx = 0.0;
    int confidence = 0;

    bool Matched() const { return !matchedText.empty(); }
};

struct DebugData
{
    std::vector<DebugLine> lines;
};
