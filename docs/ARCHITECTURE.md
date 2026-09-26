# How RuneHelper works

This file explains what the code cannot say by itself: which thread runs what, how an action travels through the app, why the OCR heuristics use the numbers they use, and why a few platform pieces look odd. Names in `code` are the places to read next.

## Threads

* **Main thread.** `RuneHelperApp::MainLoop` runs about 30 times a second. It copies the OCR and price status into `UIState`, draws the ImGui window (`UIManager::Pump`), pumps the overlay window, handles the requests collected during the frame, lets `ConfigManager` save, and hands the newest overlay frame from `OcrService` to `OverlayWindow`.
* **OCR worker.** `OcrService::WorkerLoop` loads the text model for the game language first, then polls every 100 ms: capture the region, decide whether it needs reading, read it, parse and price the rows, run the features, and publish the overlay frame and the debug table. The two results cross to the main thread through mutex-guarded slots (`ConsumeOverlayFrame`, `ConsumeDebugData`).
* **Row readers.** `ReadRows` in `ocr/OCR.cpp` reads the rows of one panel on up to 8 threads (half the hardware threads). They share one `LineReader`; `Read` is const.
* **Network jobs.** `PriceCache` refreshes prices, `UpdateChecker` asks GitHub for the latest release and `RecipeUpdater` downloads a newer `combinations.json`, each on its own `std::jthread` that a stop token cancels.
* **PipeWire.** On Wayland desktops without wlr-screencopy, frames arrive on PipeWire's own loop thread inside `PortalScreenCast`.

Features run on two threads. `Feature::OnFrame` is called by the OCR worker, `DrawTab` and `DrawMainControls` by the main thread. That is why `ExpeditionSettings` holds atomics; the rest of a feature's state belongs to one thread only (`tiles_` and the cached marks to the OCR worker, the tab rows to the main thread).

## From a key press to the config

A hotkey sets a flag in `UIState::requests`: Windows through `RegisterHotKey`, X11 through `XGrabKey`, and Wayland through the control socket that `RuneHelper --toggle-ocr` and friends write to, because Wayland has no global key grabs. Clicks in the window set the same flags. Once per frame `RuneHelperApp::HandleRequests` takes all of them at once and acts: toggling OCR changes the config, a snapshot or a debug dump goes to `OcrService`, a region selection runs the platform selector.

A bug report crosses both threads. The main thread asks `OcrService` for a debug dump, then checks every frame whether `DebugDumpsWritten` has moved, giving up after 5 s when the region cannot be read. It then flushes the config and zips the dump, the last megabyte of each log, `config.json` and `DescribeSystem` into `reports/` (`WriteBugReport`), which takes a few milliseconds on the main thread.

Every config change goes through `ConfigManager::Update` or `SetFeatureSettings`, which mark the config as changed. The main loop writes `config.json` once nothing has changed for half a second (`SaveIfSettled`) and again on exit (`Flush`), so dragging a slider writes the file once.

Hotkeys are stored as Win32 virtual-key codes on Windows and as GLFW key codes on Linux. The Linux builds still accept the F-key codes of the Windows defaults, which older configs carry.

## When a frame is read

Before capturing, `OcrService::PauseForGame` asks which window is in front (`QueryGameFocus`). While it is neither Path of Exile nor RuneHelper itself, nothing is captured and the overlay is cleared; the first frame after the game comes back is read at once. A snapshot ignores the pause, and an answer of "unknown" never pauses.

`OcrService::NeedsOcr` keeps the recognizer idle while nothing moves. A frame is read when a snapshot forces it, or when it differs from the last frame that was read, at least 600 ms have passed since that read, and the image is either settled (the same as the previous capture) or 1.5 s have passed. Two frames count as the same when fewer than 0.2% of their pixels differ by more than 8 levels (`SimilarImages`). A snapshot (F9 by default) keeps reading for 2 s even with OCR switched off. The overlay clears after three empty frames in a row, so one bad capture does not make it flicker.

## The OCR pipeline

`OCR::RecognizeLoot` in `ocr/OCR.cpp` only orchestrates; each stage has its own file.

1. **Find the panel** (`FindPanel`, `ocr/PanelPreparation.cpp`). A region may be a loose selection around the loot panel, with game scene around it. The panel's right edge is the column where parchment turns into the dark frame in at least 30% of the rows, searched in the right 45% of the region; a left edge only counts when it is at least half as strong. Once found, the panel is kept while it moves by 2 px or less, so the row crops stay stable for the row cache.
2. **Prepare the image** (`PrepareGray`). A panel wider than 750 px (4K) is scaled to 680 px, because the row finder's thresholds were tuned on 720p to 1440p captures. Text brightness is measured in a window over the text column (x from 50% to 85%, y from 10% to 90%): the 25th, 50th and 95th percentiles. When the median or the highlights drift more than 12 levels from the reference (165 and 190), the panel is remapped so that the 25th percentile lands on 144 and the 95th on 190. HDR, dimmed and gamma-shifted captures otherwise lose rows. Captures of a normal panel drift less than 5 levels, so they are left untouched. The levels are kept while they stay within 6, so a steady panel keeps the same mapping and the row cache keeps hitting.
3. **Find the rows** (`FindLootRows`, `ocr/RowFinder.cpp`). Rows are bands of dark ink in the right half of the panel. Full-height vertical lines and the dark right frame are erased first: on real 4K captures the frame added ink to every row and chained whole rune rows into one band. The band is padded by 66% of the panel's median band height above and 33% below, because the baseline sits low in a band; an absolute padding reached into the bar borders at 720p.
4. **Find where the text starts** (`FindTextStartX`, `ocr/TextStart.cpp`). The rune tiles sit left of the item name. Usually the widest blank gap separates them from the text. Localized names are longer and can run into the last tile, so `FindTileGrid` first looks for evenly spaced tile frames (pitch 4% to 14% of the row, first gap within the left quarter) and `FindLastTile` snaps to the last tile's right frame. `ChooseTextStart` then decides:
   * no tile grid: the middle of the widest gap;
   * the widest gap lies inside the grid: start right after the last tile;
   * the widest gap is the one right after the grid: use it;
   * the widest gap lies between words of the name: start after the last tile, unless another tile frame follows (the grid goes on), the stretch before the gap is shorter than a tile gap, or its ink covers less than 8% of it, which is not text.
5. **Reuse unchanged rows** (`OcrRowCache`). A row whose crop matches a stored crop reuses its text. The stored crop stays the one that produced the text; storing the newest matching crop instead lets a slowly changing row drift away from its text one small step at a time. A debug dump skips the cache so the dump is complete.
6. **Read the row** (`LineReader`). A small convolutional network with a CTC head, see below. Readings below confidence 80 are dropped.
7. **Parse** (`LootParser`). The clients write quantities as `3x`, `(3)`, `x3` or a bare `3 `, and OCR confuses `I`, `l`, `|` and `!` with 1, `O` with 0 and `S` with 5. A trailing one-letter token is noise, except one Hangul, kana, kanji or Thai character, which can be a whole word.
8. **Match names** (`NameMatcher`). Localized names are translated to English through the names in `combinations.json`, then matched against the price list. Matching tolerates about 18% of edits; candidates are pruned by length and by a letter histogram before the edit distance runs.

The measured effect of these numbers is pinned by the tests: `ocr_golden` per language, `ocr_robustness` for dim, dark, 4K and loose regions, `ocr_row_cache` and `ocr_rune_tiles`. A refactor that must not change behaviour is checked by comparing the full `ctest -V` output and the debug crops that `text_model_crops` writes, byte for byte, before and after.

## The text model

`tools/text-model/model.py` defines the network in one place, `FEATURES` and `SEQUENCE`. `export.py` walks the trained network and writes the model file: the magic `RHOCR3`, the input height, the symbol set as UTF-8, and for every layer its shape, padding, pooling, activation, weights and bias (batch norms folded in). `LineReader::Load` checks that the layers chain and that the input height comes down to one row, and `Read` runs the layers as the file describes them. The C++ side knows no architecture.

The preprocessing exists twice and must stay the same: `prepare` in `model.py` and `LineReader::Prepare` scale the crop to the input height, keep it at least 8 px wide, and stretch it between the 2nd and 98th percentile.

The training crops come from the OCR debug dump. `text_model_crops` runs `RecognizeLoot` with the dump on and copies `row_XX_text.png` and `row_XX_read.txt`; `labels.py` reads the `trimmed:` line of the latter. Renaming those files or that line breaks training.

## Platform notes

* **Commit hash.** `cmake/GitCommit.cmake` reads `.git` by hand instead of calling git, because the release build runs `docker build` with `.git` excluded from the context; `.dockerignore` lets only `HEAD` and the refs through.
* **Wayland layer shell.** The generated `wlr-layer-shell` header names a parameter `namespace`, a C++ keyword, so `WaylandSession.h` renames it with a `#define` around the include.
* **Wayland capture.** wlr-screencopy is used when the compositor offers it. Every capture waits for the compositor's next frame, so it costs one refresh period no matter how small the region is. Without it (KDE, GNOME) the xdg-desktop-portal ScreenCast streams one monitor through PipeWire, without the cursor, with a permission that lasts until the user revokes it; the restore token is kept in `screencast_token` next to the config. A denied or failed start is retried after 30 s, doubling up to 10 minutes.
* **Windows DPI.** `WinMain` makes the whole process per-monitor DPI aware, not just the UI thread, so the capture thread sees physical pixels too. Desktop Duplication maps the region into texture pixels whenever the texture size differs from the output's desktop coordinates.
* **Active window.** Windows matches the foreground window's class, `POEWindowClass`, or compares it with `FindWindowW` for the title `Path of Exile 2`; reading the title of whatever window is in front ten times a second is what window-logging spyware does, and Defender already flags the build. X11 reads `_NET_ACTIVE_WINDOW` from the window manager and matches the window class (`steam_app_2694490` under Proton, `pathofexile` under Wine, `gamescope`), the exact title, or RuneHelper's own `_NET_WM_PID`. Wayland does not tell clients which window has focus, so the check answers "unknown" there and the Debug tab greys the switch out.
* **X11.** Capture uses MIT-SHM, about 14 times faster than `XGetImage`. The overlay is click-through through an empty `ShapeInput` region, and the shape mask's GC needs an explicit foreground, or the mask comes out inverted.
* **GLFW on Wayland** starts with libdecor disabled; libdecor would load GTK and cost about 32 MB of memory.
* **Price league migration.** `ConfigManager::Normalize` moves the league name `Hardcore Runes of Aldur`, which older configs can hold, to `HC Runes of Aldur`, the name the price data uses.
