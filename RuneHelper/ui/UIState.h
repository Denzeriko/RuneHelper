#pragma once

#include <cstddef>
#include <string>

#include "core/OcrState.h"

struct UIRequests
{
    bool selectRegion = false;
    bool refreshPrices = false;
    bool toggleOcr = false;
    bool singleSnapshot = false;
    bool saveOcrDebug = false;
    bool createReport = false;
    bool installUpdate = false;
    bool registerHotkeys = false;
};

enum class ReportState
{
    None,
    Collecting,
    Saved,
    Failed
};

struct UIState
{
    bool running = false;

    OcrStatus ocr;
    bool overlayAvailable = true;
    bool priceDownloading = false;
    std::size_t priceCount = 0;

    ReportState report = ReportState::None;
    std::string reportFolder;

    UIRequests requests;
    bool regionHovered = false;
    bool debugTabOpen = false;
    bool featureTabOpen = false;

    float titleBarBottom = 0.0f;
    float titleButtonsLeft = 0.0f;

    int* waitingForHotkey = nullptr;
    bool hotkeyCaptureSkipFrame = false;

    char customLeague[64] = {};
};
