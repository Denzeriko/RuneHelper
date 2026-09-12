#pragma once

#include <cstddef>

#include "ocr/RunePatternMatcher.h"

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
    bool wantsRegisterHotkeys = false;
    bool wantsCalibrateRunes = false;

    bool regionHovered = false;
    bool debugTabOpen = false;

    bool configSavePending = false;
    double configSaveAt = 0.0;

    bool priceDownloading = false;
    size_t priceCount = 0;

    RunePatternCalibrationStatus runeCalibrationStatus{};

    int* waitingForHotkey = nullptr;
    bool hotkeyCaptureSkipFrame = false;
};
