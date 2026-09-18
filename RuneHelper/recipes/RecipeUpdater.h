#pragma once

#include <stop_token>
#include <thread>

class RecipeUpdater
{
public:
    void Start();
    void Stop();

private:
    void Fetch(const std::stop_token& stop);

    std::jthread thread_;
};
