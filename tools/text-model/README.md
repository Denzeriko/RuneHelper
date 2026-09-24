# Text model

RuneHelper reads every loot row with a small convolutional network, `RuneHelper/resources/text_model.bin` (0.5 MB), which `RuneHelper/ocr/LineReader.cpp` runs on the CPU. The scripts here rebuild that file.

## How the model is made

* The row crop is scaled to 24 px high and its contrast stretched. Convolutions reduce it to one column per 4 px, and a CTC head reads 73 characters out of those columns. About 128k parameters.
* Pretraining uses synthetic lines drawn with Fontin, the game font: recipe outputs from `combinations.json` plus any local price dumps, random words and letter strings, quantities, parchment cut from real crops, frame strips, blur, gain, gamma and noise.
* Fine-tuning mixes those lines with the real row crops the OCR pipeline cuts out of `tests/panels` in five scenes: as captured, dimmed to 70%, scaled to 4K, a loose crop over a busy game scene, and the same with a narrow margin. Labels come from `tests/truth`.
* Export folds the batch norms into the convolutions and writes the weights as float32.

## Setup

Everything that is not source code lives in a work folder, `~/.cache/runehelper-ml` unless `RUNEHELPER_ML` says otherwise.

```bash
python3 -m venv ~/.cache/runehelper-ml/venv
~/.cache/runehelper-ml/venv/bin/pip install torch --index-url https://download.pytorch.org/whl/cu128
~/.cache/runehelper-ml/venv/bin/pip install -r tools/text-model/requirements.txt
```

Download Fontin from [exljbris](https://www.exljbris.com/fontin.html) and put `Fontin-Regular.otf` into `~/.cache/runehelper-ml/fonts/`. Its license does not allow redistribution, which is why it is not in the repository.

Pretraining takes about 11 minutes on a CUDA GPU and several hours on a CPU. Fine-tuning takes about a minute per run on a GPU.

## Steps

Run these from `tools/text-model` with the venv's `python`:

1. `./crops.sh` builds `text_model_crops` in the OCR test image and writes the row crops to `real/<scene>/` in the work folder. It needs the image that `tools/ocr-test.sh` builds.
2. `python labels.py` matches every crop to its row in `tests/truth`, using what the current model reads as the guide.
3. `python train.py 20000 1.0 synthetic` pretrains on synthetic lines and keeps the checkpoint that reads the real crops best.
4. `python finetune.py synthetic cv` is the accuracy check: the panels are split into four folds, and each fold is fine-tuned without its own panels and scored on them.
5. `python finetune.py synthetic all` fine-tunes on every crop.
6. `python export.py synthetic_all` overwrites `RuneHelper/resources/text_model.bin`.
7. `./tools/ocr-test.sh` from the repository root, then `--bless` once the golden changes have been checked.

## Judging a new model

The tests under `tests/` read the same panels the model was fine-tuned on, so they catch regressions but flatter accuracy. Step 4 is the honest number. The shipped model scores:

| scene | named | exact | phantom |
| --- | --- | --- | --- |
| clean | 234/256 | 249/256 | 2/25 |
| dim | 232/254 | 245/254 | 1/51 |
| uhd | 234/256 | 253/256 | 2/22 |
| busy | 233/255 | 248/255 | 5/32 |
| narrow | 234/256 | 250/256 | 4/30 |
| total | 1167/1277 | 1245/1277 | 14/160 |

*named* is a row the model accepts (confidence 80 or more) and the fuzzy matcher turns into the right item; *exact* is an accepted row whose text is exactly right; *phantom* is a crop with no row in `tests/truth` that the model still accepts. Rows whose item is in neither `combinations.json` nor a price dump can never count as named; every scene has 22 of them, so named tops out 22 below the row count, and the shipped model reaches that ceiling in every scene. Fine-tuning is not bit-for-bit repeatable, so a rerun moves by a row or two; a replacement should match or beat this table beyond that.
