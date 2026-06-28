#pragma once

#include <string>

struct AppConfig
{
    int regionX = 0;
    int regionY = 0;
    int regionW = 0;
    int regionH = 0;

    double ocrScale = 1.0;
    double ocrThreshold = 130.0;
    int ocrIntervalMs = 400;

    int overlayOffsetX = 20;
    int overlayOffsetY = 0;
    int overlayFontSize = 32;

    int priceRefreshMinutes = 15;

    std::string priceProvider = "poe2scout";

    bool debugOCR = false;
    bool showConsole = false;
};