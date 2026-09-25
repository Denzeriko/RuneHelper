#pragma once

#include <map>
#include <string>
#include <vector>

struct Recipe
{
    std::string output;
    std::map<std::string, std::string> names;
    int count = 1;
    std::vector<std::string> runes;
};
