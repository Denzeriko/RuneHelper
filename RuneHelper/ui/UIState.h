#pragma once

#include <cstddef>


struct UIState
{
    bool running = false;

    bool ocrInitializing = false;
    bool ocrReady = false;
    bool ocrFailed = false;

    bool overlayAvailable = true;
    bool captureFailing = false;

    bool wantsSelectRegion = false;
    bool wantsRefreshPrices = false;
    bool wantsToggleOCR = false;
    bool wantsSingleSnapshot = false;
    bool wantsRegisterHotkeys = false;

    bool regionHovered = false;
    bool debugTabOpen = false;
    bool featureTabOpen = false;

    bool configSavePending = false;
    double configSaveAt = 0.0;

    bool priceDownloading = false;
    size_t priceCount = 0;


    int* waitingForHotkey = nullptr;
    bool hotkeyCaptureSkipFrame = false;

    char customLeague[64] = {};
};
