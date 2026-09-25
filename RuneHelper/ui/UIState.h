#pragma once

#include <cstddef>

#include "core/OcrState.h"

struct UIRequests
{
    bool selectRegion = false;
    bool refreshPrices = false;
    bool toggleOcr = false;
    bool singleSnapshot = false;
    bool saveOcrDebug = false;
    bool registerHotkeys = false;
};

struct UIState
{
    bool running = false;

    OcrStatus ocr;
    bool overlayAvailable = true;
    bool priceDownloading = false;
    std::size_t priceCount = 0;

    UIRequests requests;
    bool regionHovered = false;
    bool debugTabOpen = false;
    bool featureTabOpen = false;

    int* waitingForHotkey = nullptr;
    bool hotkeyCaptureSkipFrame = false;

    char customLeague[64] = {};
};
