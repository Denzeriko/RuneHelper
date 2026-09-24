set(RUNEHELPER_CORE_SOURCES
    RuneHelper/core/AtomicFile.cpp
    RuneHelper/core/ConfigManager.cpp
    RuneHelper/core/Logger.cpp
    RuneHelper/ocr/LineReader.cpp
    RuneHelper/ocr/LootParser.cpp
    RuneHelper/ocr/LootRows.cpp
    RuneHelper/ocr/NameNormalizer.cpp
    RuneHelper/ocr/OCR.cpp
    RuneHelper/ocr/OcrFrameDiffer.cpp
    RuneHelper/ocr/OcrRowCache.cpp
    RuneHelper/ocr/RuneTileLocator.cpp
    RuneHelper/price/PoeNinjaPriceProvider.cpp
    RuneHelper/price/PriceCache.cpp
    RuneHelper/price/PriceService.cpp
    RuneHelper/recipes/RecipeDatabase.cpp
    RuneHelper/recipes/RecipeUpdater.cpp
)

set(RUNEHELPER_COMMON_SOURCES
    ${RUNEHELPER_CORE_SOURCES}
    RuneHelper/RuneHelper.cpp
    RuneHelper/core/Feature.cpp
    RuneHelper/core/OcrService.cpp
    RuneHelper/core/RuneHelperApp.cpp
    RuneHelper/core/ScreenCaptureService.cpp
    RuneHelper/core/UpdateChecker.cpp
    RuneHelper/features/ExpeditionFeature.cpp
    RuneHelper/features/PriceOverlayFeature.cpp
    RuneHelper/ui/ImGuiStyleSetup.cpp
    RuneHelper/ui/TextRaster.cpp
    RuneHelper/ui/Overlay.cpp
    RuneHelper/ui/OverlayRenderer.cpp
    RuneHelper/ui/UIDraw.cpp
    RuneHelper/ui/UIManager.cpp
)

if(WIN32)
    set(RUNEHELPER_CORE_PLATFORM_SOURCES
        RuneHelper/platform/windows/PlatformPaths.cpp
        RuneHelper/platform/windows/ResourceHelper.cpp
    )

    set(RUNEHELPER_PLATFORM_SOURCES
        ${RUNEHELPER_CORE_PLATFORM_SOURCES}
        RuneHelper/resources/RuneHelper.rc
        RuneHelper/platform/windows/OverlayBackend.cpp
        RuneHelper/platform/windows/PlatformShell.cpp
        RuneHelper/platform/windows/UIBackend.cpp
        RuneHelper/platform/windows/ScreenCapture.cpp
        RuneHelper/platform/windows/ScreenCaptureDXGI.cpp
        RuneHelper/platform/windows/RegionSelect.cpp
    )
elseif(UNIX AND NOT APPLE)
    set(RUNEHELPER_CORE_PLATFORM_SOURCES
        RuneHelper/platform/linux/PlatformPaths.cpp
        RuneHelper/platform/linux/ResourceHelper.cpp
    )

    set(RUNEHELPER_PLATFORM_SOURCES
        ${RUNEHELPER_CORE_PLATFORM_SOURCES}
        RuneHelper/platform/linux/PlatformShell.cpp
        RuneHelper/platform/linux/UIBackend.cpp
    )

    if(RUNEHELPER_LINUX_BACKEND STREQUAL "wayland")
        list(APPEND RUNEHELPER_PLATFORM_SOURCES
            RuneHelper/platform/linux/wayland/Hotkeys.cpp
            RuneHelper/platform/linux/wayland/OverlayBackend.cpp
            RuneHelper/platform/linux/wayland/PortalScreenCast.cpp
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
