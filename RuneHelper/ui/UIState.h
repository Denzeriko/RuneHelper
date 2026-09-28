#pragma once

#include <string>

#include "core/OcrState.h"
#include "price/PriceCache.h"

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
    PriceStatus prices;

    ReportState report = ReportState::None;
    std::string reportFolder;

    bool regionHovered = false;
    bool debugTabOpen = false;
    bool featureTabOpen = false;

    float titleBarBottom = 0.0f;
    float titleButtonsLeft = 0.0f;

    int* waitingForHotkey = nullptr;
    bool hotkeyCaptureSkipFrame = false;

    char customLeague[64] = {};
};
