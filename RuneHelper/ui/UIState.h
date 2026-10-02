#pragma once

#include <string>

#include "core/OcrState.h"
#include "price/PriceCache.h"

enum class UIPage
{
    Tools,
    Maps,
    Settings,
    Diagnostics,
    About
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
    UIPage page = UIPage::Tools;
    int settingsSection = 0;
    int appearanceSection = 0;

    OcrStatus ocr;
    bool overlayAvailable = true;
    bool clipboardUnavailable = false;
    PriceStatus prices;

    ReportState report = ReportState::None;
    std::string reportFolder;

    bool regionHovered = false;
    bool showImGuiMetrics = false;
    bool showImGuiDebugLog = false;

    float titleBarBottom = 0.0f;
    float titleButtonsLeft = 0.0f;

    int* waitingForHotkey = nullptr;
    bool hotkeyCaptureSkipFrame = false;

    char customLeague[64] = {};
};
