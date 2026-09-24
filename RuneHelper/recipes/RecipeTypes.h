#pragma once

#include <map>
#include <string>
#include <vector>

struct Recipe
{
    std::string output;
    std::map<std::string, std::string> names;
    int count = 1;
    int level = 0;
    std::string category;
    std::vector<std::string> runes;
};
