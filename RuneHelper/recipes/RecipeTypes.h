#pragma once

#include <string>
#include <vector>

struct Recipe
{
    std::string output;
    int count = 1;
    int level = 0;
    std::string category;
    std::vector<std::string> runes;
};
