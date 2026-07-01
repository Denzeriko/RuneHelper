#pragma once

#include <chrono>
#include <cstddef>

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
    bool wantsTestOcr = false;
    bool wantsResetOcr = false;
    bool wantsOCRRebuild = false;
    bool wantsRegisterHotkeys = false;

    bool regionHovered = false;

    bool priceDownloading = false;
    size_t priceCount = 0;

    bool showSaved = false;
    std::chrono::steady_clock::time_point savedAt;

    int* waitingForHotkey = nullptr;
    bool hotkeyCaptureSkipFrame = false;

    // Reserved for future optimization.
    // Will be incremented whenever overlay data changes and used to
    // skip unnecessary redraws without comparing the entire state.
    int revision = 0; 
};
