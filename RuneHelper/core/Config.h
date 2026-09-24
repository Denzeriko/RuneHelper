#pragma once

#include <array>
#include <string>
#include <string_view>

struct GameLanguage
{
    std::string_view code;
    std::string_view name;
};

inline constexpr std::array<GameLanguage, 9> kGameLanguages = { {
    { "en", "English" },
    { "ru", "Russian" },
    { "ko", "Korean" },
    { "ja", "Japanese" },
    { "de", "German" },
    { "fr", "French" },
    { "es", "Spanish" },
    { "pt", "Portuguese" },
    { "th", "Thai" },
} };

enum class PriceUnit
{
    Exalted,
    ExaltedWithDivine,
    Divine
};

struct AppConfig
{
    int regionX = 0;
    int regionY = 0;
    int regionW = 0;
    int regionH = 0;

    bool ocrEnabled = true;
    bool priceSearchEnabled = true;

    int hotkeyToggleOCR = 0x77;      // VK_F8;
    int hotkeySingleSnapshot = 0x78; // VK_F9;
    int hotkeySelectRegion = 0x79;   // VK_F10;

    int overlayOffsetX = 20;
    int overlayOffsetY = 0;
    int overlayFontSize = 24;
    bool overlayBackground = true;
    bool overlayOutline = false;

    PriceUnit priceUnit = PriceUnit::ExaltedWithDivine;

    int priceColorMedium = 5;
    int priceColorHigh = 20;
    int priceColorVeryHigh = 100;

    int priceRefreshMinutes = 15;

    std::string priceLeague = "Forbidden Rites";

    std::string gameLanguage = "en";
};
