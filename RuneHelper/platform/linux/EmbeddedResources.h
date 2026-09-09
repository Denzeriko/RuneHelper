#pragma once

#include <string_view>
#include <vector>

struct EmbeddedResource
{
    std::string_view name;
    const unsigned char* begin;
    const unsigned char* end;
};

const std::vector<EmbeddedResource>& GetEmbeddedResources();
