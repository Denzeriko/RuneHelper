#pragma once

#include <string>

struct AppConfig
{
    int regionX = 0;
    int regionY = 0;
    int regionW = 0;
    int regionH = 0;


    bool ocrEnabled         = true;
    bool priceSearchEnabled = true;

    int hotkeyToggleOCR         = 0x77; //VK_F8;
    int hotkeySingleSnapshot    = 0x78; //VK_F9;
    int hotkeySelectRegion      = 0x79; //VK_F10;

    int overlayOffsetX  = 20;
    int overlayOffsetY  = 0;
    int overlayFontSize = 24;
    bool overlayBackground = true;
    bool overlayOutline    = false;

    int priceColorMedium    = 5;
    int priceColorHigh      = 20;
    int priceColorVeryHigh  = 100;

    int priceRefreshMinutes = 15;

    std::string priceLeague = "Forbidden Rites";

    bool debugOCR = false;
};
