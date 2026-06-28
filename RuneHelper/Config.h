#pragma once

#include <string>

struct AppConfig
{
    int regionX = 0;
    int regionY = 0;
    int regionW = 0;
    int regionH = 0;

    bool ocrEnabled = true;
    float ocrScale = 1.0f;
    float ocrThreshold = 130.0f;
    int ocrIntervalMs = 400;

    int overlayOffsetX = 20;
    int overlayOffsetY = 0;
    int overlayFontSize = 24;

    float priceColorLow = 1.0f;
    float priceColorMedium = 5.0f;
    float priceColorHigh = 20.0f;
    float priceColorVeryHigh = 100.0f;

    int priceRefreshMinutes = 15;

    std::string priceProvider = "poe2scout";

    bool debugOCR = false;
    bool showConsole = false;
};