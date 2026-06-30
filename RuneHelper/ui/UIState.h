#pragma once

#include <chrono>

struct UIState
{
    bool running = false;

    bool ocrInitializing = false;
    bool ocrReady = false;
    bool ocrFailed = false;

    bool wantsSelectRegion = false;
    bool wantsRefreshPrices = false;
    bool wantsToggleOCR = false;
    bool wantsSingleSnapshot = false;
    bool wantsOCRRebuild = false;

    bool regionHovered = false;

    bool priceDownloading = false;
    size_t priceCount = 0;

    bool showSaved = false;
    std::chrono::steady_clock::time_point savedAt;
};