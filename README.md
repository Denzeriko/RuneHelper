# RuneHelper

A lightweight overlay tool for **Path of Exile 2** that uses **OCR (Tesseract)** to detect item names on the screen and display their current market prices.

![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-blue)
![Language](https://img.shields.io/badge/language-C%2B%2B20-orange)
![License](https://img.shields.io/badge/license-MIT-green)
[![Windows build](https://github.com/Denzeriko/RuneHelper/actions/workflows/msbuild.yml/badge.svg?branch=master)](https://github.com/Denzeriko/RuneHelper/actions/workflows/msbuild.yml?query=branch%3Amaster)
[![Linux build](https://github.com/Denzeriko/RuneHelper/actions/workflows/linux-build.yml/badge.svg?branch=master)](https://github.com/Denzeriko/RuneHelper/actions/workflows/linux-build.yml?query=branch%3Amaster)

## Download

[![Download Linux](https://img.shields.io/badge/download-Linux%20x86__64-blue?logo=linux)](https://github.com/Denzeriko/RuneHelper/releases/latest)
[![Download Windows artifact](https://img.shields.io/badge/download-Windows%20x86__64-blue?logo=windows)](https://github.com/Denzeriko/RuneHelper/actions/workflows/msbuild.yml?query=branch%3Amaster)

Linux binaries are published on the [Releases](https://github.com/Denzeriko/RuneHelper/releases/latest) page. OpenCV, Tesseract, Leptonica, GLFW and cpr are linked in, so nothing has to be installed first. Pick the build that matches the session:

* `RuneHelper-linux-x86_64-wayland` - Hyprland, Sway, river, labwc, KDE Plasma on Wayland.
* `RuneHelper-linux-x86_64-x11` - any X11 session.

They are built against glibc 2.35, which covers Ubuntu 22.04 and newer, Debian 12 and newer, and current rolling distributions.

The Linux job under **Actions** builds against Ubuntu's own OpenCV and links it dynamically. That artifact is a build check, not a download: it only runs on the same Ubuntu release, and it fails with an `undefined symbol` error anywhere else. Windows has no release job yet, so its artifact still comes from Actions and needs a GitHub login to download.

## Features

* Select any loot area on the screen.
* Real-time OCR using Tesseract.
* Single-pass OCR tuned for the Runeshape loot menu.
* Fuzzy matching for OCR mistakes.
* Overlay displaying item prices next to detected items.
* Automatic price cache updates.
* League-specific offline price cache to reduce API requests.
* Debug window showing OCR and matching results.
* Optional OCR debug image/text dumps.
* No game memory reading or injection.

## Screenshot

![RuneHelper screenshot](assets/screenshot.png)

## How to use

Click **Select Region**, then drag a rectangle around the Runeshape loot list. This only needs to be done once; RuneHelper saves the selected region in its config. Select it again only if the game window, UI scale, or menu position changes.

![Region selection guide](assets/howto.gif)

## How it works

1. Select the loot area on your screen.
2. RuneHelper periodically captures the selected region.
3. The OCR pipeline finds text rows in the right side of the Runeshape loot menu.
4. Each detected row is cropped, binarized, and passed to Tesseract.
5. OCR mistakes are corrected using fuzzy matching.
6. Prices are loaded from cache or downloaded from the API.
7. An overlay is rendered next to the detected items.

## OCR Debug

Enable **Debug OCR** in the UI to write the latest OCR inputs and recognition logs to:

```text
Windows: %APPDATA%\Denz\RuneHelper\ocr_debug\latest
Linux:   ~/.config/RuneHelper/ocr_debug/latest
```

The folder is overwritten on each OCR run and may contain:

* `source.png` - captured source region.
* `rows_detected.png` - detected text rows and crop start markers.
* `row_XX_row.png` - detected row crop.
* `row_XX_text.png` - text crop sent to OCR preprocessing.
* `row_XX_bin.png` - binarized image passed to Tesseract.
* `row_XX_bin.txt` - raw OCR text, trimmed text, confidence, and accept/reject status.

## Dependencies

* C++20
* OpenCV
* Tesseract OCR
* cpr
* nlohmann/json
* ImGui

On Linux, cpr, GLFW and Dear ImGui are vendored as git submodules under `external/`, so clone with them:

```bash
git clone --recurse-submodules https://github.com/Denzeriko/RuneHelper.git
```

An existing clone catches up with:

```bash
git submodule update --init --recursive
```

Nothing is downloaded at configure time. System copies win when they exist: `cpr`, `glfw3` and `nlohmann_json` are looked up with `find_package` and the submodule is built only as a fallback, and the Wayland backend reads the `wlr-protocols` XML tree from its pkg-config `pkgdatadir`. Packagers can override the two source paths directly:

```bash
cmake -S . -B build \
    -DRUNEHELPER_IMGUI_DIR=/usr/src/imgui \
    -DRUNEHELPER_WLR_PROTOCOLS_DIR=/usr/share/wlr-protocols
```

## Building on Windows

Installed via vcpkg:

```powershell
vcpkg install opencv:x64-windows
vcpkg install tesseract:x64-windows
vcpkg install cpr:x64-windows
vcpkg install nlohmann-json:x64-windows
vcpkg install imgui[dx11-binding,win32-binding]:x64-windows
```

## Building on Ubuntu

The Linux build targets X11 or Wayland, chosen at configure time with `RUNEHELPER_LINUX_BACKEND` (`x11` by default).

The Wayland backend draws its overlay and region selector through `wlr-layer-shell`, which both wlroots compositors and KWin implement. For screen capture it picks a path at runtime:

* `wlr-screencopy` when the compositor offers it (Hyprland, Sway, river, labwc) — captures just the configured region, with no permission prompt;
* `xdg-desktop-portal` ScreenCast over PipeWire otherwise (KDE Plasma) — the desktop asks once which monitor to share, and the answer is remembered in `~/.config/RuneHelper/screencast_token`.

Share the monitor that contains the capture region: the portal gives the app a single output and the app cannot choose it for you. If the wrong monitor is shared, the log says which output arrived and which region was expected, and the saved permission is dropped so the picker opens again.

Setting `RUNEHELPER_CAPTURE_PORTAL=1` forces the portal path on compositors that also support `wlr-screencopy`, which is useful for reproducing KDE behaviour.

GNOME is not supported: it implements neither `wlr-layer-shell` nor `wlr-screencopy`, so the overlay has nowhere to live.

### Install dependencies

```bash
sudo apt update
sudo apt install \
    build-essential \
    cmake \
    pkg-config \
    libopencv-dev \
    libtesseract-dev \
    libleptonica-dev \
    libblas-dev \
    liblapack-dev \
    libx11-dev
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

> **Note:** `libtesseract-dev` provides the C++ API, while `libleptonica-dev` is required by Tesseract.

### Configure and build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

For Wayland:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DRUNEHELPER_LINUX_BACKEND=wayland
cmake --build build -j$(nproc)
```

## Building with Docker

The Dockerfile produces the same self-contained binary the releases ship: OpenCV, Leptonica, Tesseract, GLFW, cpr and libstdc++ are linked statically, leaving only glibc, libcurl, the display server client libraries and libGL dynamic. It builds on Ubuntu 22.04 so the result keeps a glibc 2.35 floor and runs on newer distributions.

The last stage is an export stage, not a runnable image. `--output` writes the binary onto the host and there is nothing to `docker run`:

```bash
docker build --output out .
```

The binary lands in `out/RuneHelper`. The backend defaults to Wayland and is switched with a build argument:

```bash
docker build --build-arg RUNEHELPER_LINUX_BACKEND=x11 --output out .
```

Add `--network host` if the container cannot reach the package mirrors on your setup.

The Linux build embeds `eng.traineddata_fast` and the rune templates into the executable the same way the Windows resource script does, so the binary runs from any working directory:

```bash
./out/RuneHelper
```

## Global hotkeys on Wayland

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

## Price API

poe.ninja asks that desktop clients not call its API from end-user machines, so prices go through a proxy:

```text
https://denz.pw/poe2/economy?league=LEAGUE&type=TYPE
```

The proxy fetches `https://poe.ninja/poe2/api/economy/exchange/current/overview` upstream, identifies itself with a descriptive User-Agent, revalidates with `If-None-Match`, and caches each league/type pair for an hour, which is how often the PoE 2 economy is recomputed. Clients therefore poll the proxy, not poe.ninja, and the interval in the settings only controls how soon a client picks up an already-cached answer.

If the proxy fails three times in a refresh cycle, the client falls back to calling poe.ninja directly for the rest of that cycle and probes the proxy again on the next one. `RUNEHELPER_PRICE_API` overrides the proxy base URL, which is useful when running a proxy of your own.

The cache is stored in the RuneHelper app data directory as a league-specific dump:

```text
Windows: %APPDATA%\Denz\RuneHelper\prices_dump_<league>.json
Linux:   ~/.config/RuneHelper/prices_dump_<league>.json
```

## Known Issues

* The Wayland backend needs `wlr-layer-shell`, so GNOME Wayland sessions are not supported.
* On KDE the portal asks which monitor to share; it has to be the one holding the capture region.
* Prices come from the proxy at `denz.pw`; if it is unreachable the client falls back to poe.ninja directly.
* Wayland has no global key grabs: hotkeys go through the control socket and a compositor binding.

## Disclaimer

This project:

* does **not** inject into the game;
* does **not** read game memory;
* only captures a user-selected screen region and performs OCR on the image.

## License

MIT License.
