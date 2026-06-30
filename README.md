# RuneHelper

A lightweight overlay tool for **Path of Exile 2** that uses **OCR (Tesseract)** to detect item names on the screen and display their current market prices.

![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-blue)
![Language](https://img.shields.io/badge/language-C%2B%2B20-orange)
![License](https://img.shields.io/badge/license-MIT-green)

## Features

* Select any loot area on the screen.
* Real-time OCR using Tesseract.
* Multi-pass OCR with configurable threshold presets.
* Fuzzy matching for OCR mistakes.
* Overlay displaying item prices next to detected items.
* Automatic price cache updates.
* Offline cache (`prices_dump.json`) to reduce API requests.
* Debug window showing OCR and matching results.
* No game memory reading or injection.

## Screenshot

![RuneHelper screenshot](assets/screenshot.png)

## How it works

1. Select the loot area on your screen.
2. RuneHelper periodically captures the selected region.
3. One or more OCR passes are performed using different threshold values.
4. OCR mistakes are corrected using fuzzy matching.
5. Prices are loaded from cache or downloaded from the API.
6. An overlay is rendered next to the detected items.

## OCR Modes

RuneHelper supports multiple OCR passes to improve recognition of different texts.

| Passes | Thresholds                |
| -----: | ------------------------- |
|      1 | 130                       |
|      2 | 60, 130                   |
|      3 | 30, 60, 130               |
|      4 | 30, 60, 130, 180          |
|      5 | 20, 30, 60, 130, 180      |
|      6 | 20, 30, 60, 130, 180, 220 |

More passes generally improve OCR accuracy but increase CPU usage.

## Dependencies

* C++20
* OpenCV
* Tesseract OCR
* cpr
* nlohmann/json
* ImGui

Installed via vcpkg:

```powershell
vcpkg install opencv:x64-windows
vcpkg install tesseract:x64-windows
vcpkg install cpr:x64-windows
vcpkg install nlohmann-json:x64-windows
vcpkg install imgui[dx11-binding,win32-binding]:x64-windows
```

## Building on Ubuntu

Linux support currently targets X11 only. Run RuneHelper from an X11 session; Wayland support is not implemented yet.

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
    libx11-dev
```

> **Note:** `libtesseract-dev` provides the C++ API, while `libleptonica-dev` is required by Tesseract.

### Configure and build

```bash
mkdir -p build
cd build

cmake ..
cmake --build . -j$(nproc)
```

## Price API

Prices are fetched from:

```text
https://poe.ninja/poe2/api/economy/exchange/current/overview?league=LEAGUE&type=TYPE
```

The cache is stored in:

```text
prices_dump.json
```

The dump is refreshed automatically every 15 minutes.

## Known Issues

* OCR may occasionally misread text even with high OCR Pass.
* Higher OCR pass counts may noticeably increase CPU usage.

## Disclaimer

This project:

* does **not** inject into the game;
* does **not** read game memory;
* only captures a user-selected screen region and performs OCR on the image.

## License

MIT License.
