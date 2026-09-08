set(RUNEHELPER_COMMON_SOURCES
    RuneHelper/RuneHelper.cpp
    RuneHelper/core/RuneHelperApp.cpp
    RuneHelper/core/ConfigManager.cpp
    RuneHelper/core/Logger.cpp
    RuneHelper/core/OcrService.cpp
    RuneHelper/core/ScreenCaptureService.cpp
    RuneHelper/core/UpdateChecker.cpp
    RuneHelper/ocr/LootParser.cpp
    RuneHelper/ocr/LootOverlayBuilder.cpp
    RuneHelper/ocr/NameNormalizer.cpp
    RuneHelper/ocr/OcrFrameDiffer.cpp
    RuneHelper/ocr/OCR.cpp
    RuneHelper/ocr/RunePatternMatcher.cpp
    RuneHelper/price/PoeNinjaPriceProvider.cpp
    RuneHelper/price/PriceCache.cpp
    RuneHelper/ui/ImGuiStyleSetup.cpp
    RuneHelper/ui/Overlay.cpp
    RuneHelper/ui/UIManager.cpp
    RuneHelper/ui/UIDraw.cpp
)

if(WIN32)
    set(RUNEHELPER_PLATFORM_SOURCES
        RuneHelper/resources/RuneHelper.rc
        RuneHelper/platform/windows/OverlayBackend.cpp
        RuneHelper/platform/windows/PlatformPaths.cpp
        RuneHelper/platform/windows/UIBackend.cpp
        RuneHelper/platform/windows/ScreenCapture.cpp
        RuneHelper/platform/windows/ScreenCaptureDXGI.cpp
        RuneHelper/platform/windows/RegionSelect.cpp
        RuneHelper/platform/windows/ResourceHelper.cpp
    )
elseif(UNIX AND NOT APPLE)
    set(RUNEHELPER_PLATFORM_SOURCES
        RuneHelper/platform/linux/PlatformPaths.cpp
        RuneHelper/platform/linux/UIBackend.cpp
        RuneHelper/platform/linux/ResourceHelper.cpp
    )

    if(RUNEHELPER_LINUX_BACKEND STREQUAL "wayland")
        list(APPEND RUNEHELPER_PLATFORM_SOURCES
            RuneHelper/platform/linux/wayland/Hotkeys.cpp
            RuneHelper/platform/linux/wayland/OverlayBackend.cpp
            RuneHelper/platform/linux/wayland/RegionSelect.cpp
            RuneHelper/platform/linux/wayland/ScreenCapture.cpp
            RuneHelper/platform/linux/wayland/WaylandSession.cpp
        )
    else()
        list(APPEND RUNEHELPER_PLATFORM_SOURCES
            RuneHelper/platform/linux/x11/Hotkeys.cpp
            RuneHelper/platform/linux/x11/OverlayBackend.cpp
            RuneHelper/platform/linux/x11/RegionSelect.cpp
            RuneHelper/platform/linux/x11/ScreenCapture.cpp
        )
    endif()
endif()
