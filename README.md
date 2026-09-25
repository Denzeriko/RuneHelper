# RuneHelper

A lightweight overlay tool for **Path of Exile 2** that uses **OCR** to detect item names on the screen and display their current market prices.

Reads the game client in **9 languages**: English, Русский, Deutsch, Français, Español, Português, 한국어, 日本語 and ไทย.

![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-blue)
![Language](https://img.shields.io/badge/language-C%2B%2B20-orange)
![License](https://img.shields.io/badge/license-MIT-green)
[![Windows build](https://github.com/Denzeriko/RuneHelper/actions/workflows/msbuild.yml/badge.svg?branch=master)](https://github.com/Denzeriko/RuneHelper/actions/workflows/msbuild.yml?query=branch%3Amaster)
[![Linux build](https://github.com/Denzeriko/RuneHelper/actions/workflows/linux-build.yml/badge.svg?branch=master)](https://github.com/Denzeriko/RuneHelper/actions/workflows/linux-build.yml?query=branch%3Amaster)

![RuneHelper screenshot](assets/screenshot.png)

## Features

* Prices next to every item of the Runeshape loot list, read from the screen in real time.
* A small built-in text recognizer for each client language, trained on the game fonts. Localized item names are translated to English for prices and recipes.
* Works on HDR, dimmed and 4K screens: brightness is normalised and large regions are scaled down when needed.
* Fuzzy matching absorbs OCR mistakes.
* Expedition advisor: reward value per monster wave, so you can compare combinations at equal risk.
* Highlights rare runeshape tiles directly in the remnant panel.
* Offline database of every runeshape combination, refreshed from a proxy at startup.
* League-specific price cache, updated automatically, which keeps API requests low.
* Debug window with OCR and matching results, plus optional image and text dumps.
* No game memory reading or injection.

## Download

[![Download](https://img.shields.io/badge/download-latest%20release-blue?logo=github)](https://github.com/Denzeriko/RuneHelper/releases/latest)

Every build is published on the [Releases](https://github.com/Denzeriko/RuneHelper/releases/latest) page. OpenCV, GLFW and cpr are linked in, so nothing has to be installed first. Pick the file that matches the system:

* `RuneHelper-windows-x86_64.exe` - Windows 10 and newer.
* `RuneHelper-linux-x86_64-wayland` - Hyprland, Sway, river, labwc, KDE Plasma on Wayland.
* `RuneHelper-linux-x86_64-x11` - any X11 session.

The Linux builds target glibc 2.35, which covers Ubuntu 22.04 and newer, Debian 12 and newer, and current rolling distributions. They also need the executable bit after downloading:

```bash
chmod +x RuneHelper-linux-x86_64-wayland
```

The jobs under **Actions** are build checks, not downloads. Their artifacts need a GitHub login, and the Linux one links Ubuntu's own OpenCV dynamically, so it fails with an `undefined symbol` error anywhere else.

## How to use

1. Pick the language your game client runs in under **Game language**.
2. Click **Select Region** and drag a rectangle around the Runeshape loot list. It does not have to be tight: RuneHelper finds the list inside it. The region is saved, so select it again only if the game window, UI scale or menu position changes.
3. Tick **Enable OCR**. Prices appear next to the items whenever the loot list is on the screen.

![Region selection guide](assets/howto.gif)

F8 toggles OCR, F9 reads the region once and F10 starts a region selection. All three can be changed under **HOTKEYS**.

### Hotkeys on Wayland

Wayland lets no client grab keys globally, so the Wayland build listens on a control socket at `$XDG_RUNTIME_DIR/runehelper.sock`. Starting the binary with a command forwards it to the running instance and exits:

```bash
RuneHelper --toggle-ocr
RuneHelper --snapshot
RuneHelper --select-region
```

Bind them in the compositor config, for example in `hyprland.conf`:

```text
bind = , F8, exec, /path/to/RuneHelper --toggle-ocr
bind = , F9, exec, /path/to/RuneHelper --snapshot
bind = , F10, exec, /path/to/RuneHelper --select-region
```

## Known issues

* The Wayland backend needs `wlr-layer-shell`, so GNOME Wayland sessions are not supported.
* On KDE the portal asks which monitor to share; it has to be the one holding the capture region.
* Prices come from the proxy at `denz.pw`; if it is unreachable the client falls back to poe.ninja directly.
* Wayland has no global key grabs: hotkeys go through the control socket and a compositor binding.

## OCR debug

When items are missed or misread, click **Save OCR Debug** in the Debug tab. RuneHelper reads the region once and writes what OCR saw to:

```text
Windows: %APPDATA%\Denz\RuneHelper\ocr_debug\latest
Linux:   ~/.config/RuneHelper/ocr_debug/latest
```

Each save replaces the folder:

* `source.png` - the captured region.
* `prepared.png` - the panel as OCR reads it, when it was cut out, scaled down or brightness-normalised.
* `rows_detected.png` - detected rows and where their text starts.
* `row_XX_row.png`, `row_XX_text.png` - each row and its text crop.
* `row_XX_read.png` - the crop as the recognizer sees it.
* `row_XX_read.txt` - the reading, its confidence and whether it was accepted.

## How it works

1. RuneHelper periodically captures the selected region and finds the loot panel inside it. 4K panels are scaled down, and HDR or dimmed ones have their brightness normalised.
2. It finds the text rows in the right part of the panel and cuts each name out after its rune tiles.
3. Each row is read by the small convolutional network of the selected client language (see `tools/text-model`), on up to eight worker threads. Rows read with low confidence are dropped, which keeps rune icons and background out of the results.
4. Fuzzy matching fixes OCR mistakes and translates localized names to English.
5. Prices come from the cache or the API and are drawn next to the items.

`docs/ARCHITECTURE.md` goes through the threads, each OCR stage and the reasons behind its thresholds, for anyone reading the code.

## Price API

poe.ninja asks that desktop clients not call its API from end-user machines, so prices go through a proxy at `https://denz.pw/poe2/economy?league=LEAGUE&type=TYPE`. It caches each league/type pair for an hour, which is how often the PoE 2 economy is recomputed. After three failed requests in a refresh cycle the client calls poe.ninja directly until the next cycle, and `RUNEHELPER_PRICE_API` points it at a proxy of your own.

Prices are cached per league in `prices_dump_<league>.json` in the app data directory: `%APPDATA%\Denz\RuneHelper` on Windows, `~/.config/RuneHelper` on Linux.

## Building from source

### Dependencies

* C++20
* OpenCV
* cpr
* nlohmann/json
* Dear ImGui
* GLFW on Linux

On Linux, cpr, GLFW and Dear ImGui are vendored as git submodules under `external/`, so clone with them:

```bash
git clone --recurse-submodules https://github.com/Denzeriko/RuneHelper.git
```

An existing clone catches up with:

```bash
git submodule update --init --recursive
```

Nothing is downloaded at configure time: system copies of `cpr`, `glfw3` and `nlohmann_json` win over the submodules, and packagers can point `RUNEHELPER_IMGUI_DIR` and `RUNEHELPER_WLR_PROTOCOLS_DIR` at their own trees.

### Windows

The Windows build uses vcpkg in manifest mode: `vcpkg.json` lists the dependencies, and the CMake presets link them statically with clang-cl. It needs Visual Studio with the C++ Clang tools and a vcpkg checkout that `VCPKG_ROOT` points to. From a Developer PowerShell:

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
cmake --preset windows-clang-release
cmake --build --preset windows-clang-release
```

The first configure builds the dependencies, which takes a while. The executable lands in `build\windows-clang-release\RuneHelper.exe`.

### Linux

The Linux build targets X11 or Wayland, chosen at configure time with `RUNEHELPER_LINUX_BACKEND` (`x11` by default).

The Wayland backend draws through `wlr-layer-shell` (wlroots compositors and KWin). It captures with `wlr-screencopy` where the compositor offers it (Hyprland, Sway, river, labwc), and otherwise through the `xdg-desktop-portal` ScreenCast (KDE Plasma), which asks once which monitor to share. `RUNEHELPER_CAPTURE_PORTAL=1` forces the portal path for testing.

Dependencies on Ubuntu:

```bash
sudo apt update
sudo apt install \
    build-essential \
    cmake \
    git \
    pkg-config \
    libopencv-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    libglfw3-dev \
    libgl1-mesa-dev \
    libx11-dev \
    libxext-dev
```

For the Wayland backend, add:

```bash
sudo apt install \
    libwayland-dev \
    wayland-protocols \
    libxkbcommon-dev \
    libdbus-1-dev \
    libpipewire-0.3-dev
```

Configure and build, adding `-DRUNEHELPER_LINUX_BACKEND=wayland` for Wayland:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Docker

The Dockerfile builds the same self-contained binary the releases ship, with OpenCV, GLFW, cpr and libstdc++ linked statically and a glibc 2.35 floor. Its last stage only exports the binary, so there is nothing to `docker run`:

```bash
docker build --build-arg RUNEHELPER_LINUX_BACKEND=x11 --output out .
```

The binary lands in `out/RuneHelper`, with the text models and `combinations.json` embedded. Leave out the build argument for Wayland, and add `--network host` if the container cannot reach the package mirrors.

## Disclaimer

This project:

* does **not** inject into the game;
* does **not** read game memory;
* only captures a user-selected screen region and performs OCR on the image.

## Credits

The runeshape combination database and the poe2db scraper behind it come from [imbermuda/expeditionWiz](https://github.com/imbermuda/expeditionWiz) by [imbermuda](https://github.com/imbermuda), used with the author's permission.

## License

MIT License.
