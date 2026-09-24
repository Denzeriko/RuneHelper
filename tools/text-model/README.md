# Text model

RuneHelper reads every loot row with a small convolutional network, one per game client language: `RuneHelper/resources/text_model.bin` for English and `text_model_<code>.bin` for Russian (`ru`), German (`de`), French (`fr`), Spanish (`es`), Portuguese (`pt`), Korean (`ko`), Japanese (`ja`) and Thai (`th`), 0.5 to 0.6 MB each. `RuneHelper/ocr/LineReader.cpp` runs the one picked under **Game language** on the CPU. The scripts here rebuild those files.

## How the model is made

* The row crop is scaled to 24 px high and its contrast stretched. Convolutions reduce it to one column per 4 px, and a CTC head reads the language's symbols out of those columns: 73 for English, every character of the item names for Korean, Japanese and Thai. About 130k to 160k parameters.
* Pretraining uses synthetic lines drawn in the client's font: recipe outputs from `combinations.json` plus any local price dumps, random words and letter strings, quantities, parchment cut from real crops, frame strips, blur, gain, gamma and noise.
* Fine-tuning mixes those lines with the real row crops the OCR pipeline cuts out of the test panels in five scenes: as captured, dimmed to 70%, scaled to 4K, a loose crop over a busy game scene, and the same with a narrow margin. Labels come from the truth files next to the panels.
* Export folds the batch norms into the convolutions and writes the weights as float32, together with the symbol set as UTF-8.

## Setup

Everything that is not source code lives in a work folder, `~/.cache/runehelper-ml` unless `RUNEHELPER_ML` says otherwise.

```bash
python3 -m venv ~/.cache/runehelper-ml/venv
~/.cache/runehelper-ml/venv/bin/pip install torch --index-url https://download.pytorch.org/whl/cu128
~/.cache/runehelper-ml/venv/bin/pip install -r tools/text-model/requirements.txt
```

The fonts go into `~/.cache/runehelper-ml/fonts/`: `Fontin-Regular.otf` from [exljbris](https://www.exljbris.com/fontin.html) for English, German, French, Spanish and Portuguese, and Fontin Sans CR (`FontinSans_Cyrillic_46b.zip` from the same site) for Russian. Their license does not allow redistribution, which is why they are not in the repository. Korean and Japanese are drawn with Noto Sans CJK and Thai with Noto Sans Thai from `/usr/share/fonts`, standing in for the client's fonts.

Pretraining takes 10 to 30 minutes per language on a CUDA GPU and several hours on a CPU. Fine-tuning takes one to three minutes per run on a GPU.

## Steps

Run these from `tools/text-model` with the venv's `python`. `RUNEHELPER_LANGUAGE=<code>` switches every step to that language: its panels and truth in `tests/<code>` (English uses `tests/panels` and `tests/truth`), its crops in the work folder, its checkpoints and its model file. The examples use English and its tag `synthetic`; other languages use `<code>_synthetic`, since the checkpoints share the work folder.

1. `./crops.sh` builds `text_model_crops` in the OCR test image and writes the row crops to `real/<scene>/` (`<code>/real/<scene>/` for other languages) in the work folder. It needs the image that `tools/ocr-test.sh` builds.
2. `python labels.py` matches every crop to its row in the truth, using what the current model reads as the guide. A reading much shorter than its row comes from a crop that misses part of the text and stays unlabelled, so the model is never taught to invent the missing words.
3. `python train.py 20000 1.0 synthetic` pretrains on synthetic lines and keeps the checkpoint that reads the real crops best.
4. `python finetune.py synthetic cv` is the accuracy check: the panels are split into four folds, and each fold is fine-tuned without its own panels and scored on them.
5. `python finetune.py synthetic all` fine-tunes on every crop.
6. `python export.py synthetic_all` overwrites the model file in `RuneHelper/resources`.
7. `./tools/ocr-test.sh` from the repository root, then `--bless` once the golden changes have been checked.

A language with no model yet starts at step 3: without labelled crops `train.py` keeps the last checkpoint. Japanese and Thai pretrained from scratch never learn to read the leading `3x` and emit a bare `x` instead, so start them from the Korean checkpoint: `RUNEHELPER_INIT=ko_synthetic` copies its layers and the rows of every symbol both languages share. Export the result into the model file so the build finds it, run step 1 with `RUNEHELPER_MODEL=<file in the work folder>`, then `python draft_truth.py` writes the truth from what that model reads, snapped to the names in `combinations.json`. Check every draft against the screenshots: early models drop quantity digits and levels, and rows they cannot read at all are missing.

## Judging a new model

The tests under `tests/` read the same panels the model was fine-tuned on, so they catch regressions but flatter accuracy. Step 4 is the honest number. The English model scores:

| scene | named | exact | phantom |
| --- | --- | --- | --- |
| clean | 234/256 | 249/256 | 2/25 |
| dim | 232/254 | 245/254 | 1/51 |
| uhd | 234/256 | 253/256 | 2/22 |
| busy | 233/255 | 248/255 | 5/32 |
| narrow | 234/256 | 250/256 | 4/30 |
| total | 1167/1277 | 1245/1277 | 14/160 |

*named* is a row the model accepts (confidence 80 or more) and the fuzzy matcher turns into the right item; *exact* is an accepted row whose text is exactly right; *phantom* is a crop with no row in the truth that the model still accepts. Rows whose item is in neither `combinations.json` nor a price dump can never count as named; every scene has 22 of them, so named tops out 22 below the row count, and the English model reaches that ceiling in every scene. Fine-tuning is not bit-for-bit repeatable, so a rerun moves by a row or two; a replacement should match or beat its table beyond that.

The other languages, checked the same way on their 14 or 15 panels (1920 and 2560 captures, split into four folds so that panels sharing an item stay together), with *named* counted against the rows that can be named at all:

| language | named | exact | phantom |
| --- | --- | --- | --- |
| Russian | 445/460 | 367/510 | 4/110 |
| German | 583/585 | 667/685 | 1/89 |
| French | 548/548 | 612/646 | 6/73 |
| Spanish | 576/576 | 641/656 | 6/73 |
| Portuguese | 567/567 | 647/652 | 5/79 |
| Korean | 550/554 | 543/594 | 14/155 |
| Japanese | 537/547 | 551/613 | 7/88 |
| Thai | 570/573 | 526/643 | 2/78 |

Gem rows and random currency have no item behind them. More panels, especially at 4K, are the quickest way to lift a language; Russian gains the most.

## Game languages

The app reads one language at a time, picked under **Game language**; the list shows the languages whose model is built into the binary. Item names are translated back to English before prices and recipes are looked up. The translations come from `combinations.json`, where `tools/scrape_poe2db.py` stores every output under `names` for each language: names the official trade site knows come from its static item data, matched to English through the item id, and poe2db's localized pages fill in gems and uniques.

What the clients write:

| language | font | quantity | gem rows |
| --- | --- | --- | --- |
| Russian | Fontin Sans CR, about 0.08 em extra spacing | `Рунный сплав (2)` | `Умение: …`, `Поддержка: …` |
| German | Fontin | `2x Runenlegierung` | `Fertigkeit: …`, `Unterstützung: …` |
| French | Fontin | `2x Alliage runique` | `Aptitude : …`, `Gemme de soutien : …` |
| Spanish | Fontin | `Aleación rúnica x2` | `Habilidad: …`, `Asistencia: …` |
| Portuguese | Fontin | `2 Liga Rúnica` | `Habilidade: …`, `Reforço: …` |
| Korean | sans-serif | `2x 룬 합금` | `스킬 레벨 20: …` |
| Japanese | sans-serif | `2x ルーンの合金` | `スキルレベル 20: …` |
| Thai | sans-serif | `2x โลหะเจืออักขระ` | `สกิล: …`, `เสริม: …` |

`LootParser` accepts every quantity format in every language, and unique items carry no count. Japanese and Thai draw unique names without the space the scraped names have (`ユニーク指輪`), and the Thai client keeps English names for most orbs (`Exalted Orb ไร้ที่ติ`). The game shrinks a name that does not fit into a smaller font, and a long name can run into the last rune tile; `FindTextStartX` in `OCR.cpp` then starts the text after the tile grid.

A new language needs:

* its font and an entry in `LANGUAGES` in `common.py` and `STYLES` in `synth.py`;
* panels from that client under `tests/<code>/panels` and checked truth in `tests/<code>/truth`;
* its code in `RUNEHELPER_TEXT_MODEL_LANGUAGES` in `cmake/EmbedResources.cmake`, which embeds `text_model_<code>.bin` and adds its `ocr_golden_<code>` test, in `kGameLanguages` in `core/Config.h` and in the scraper's `LANGUAGES` and `TRADE_HOSTS`, and on Windows in `RuneHelper.rc`, `resource.h` and `kTextModels` in `platform/windows/ResourceHelper.cpp`.

The model file stores its own symbol set as UTF-8, so a script with thousands of symbols fits the same format.
