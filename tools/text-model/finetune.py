import os
import random
import re
import sys
import time
from functools import partial

import cv2
import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader, IterableDataset, get_worker_info

from common import LANGUAGE, MIN_CONFIDENCE, SCENES, TRUTH, Matcher, load_real, panel_of, prepare
from synth import Synth
from train import collate, load, read, save

SPLITS = {
    'en': (
        [
            ['1280_test00', '1920_test00', '2560_test00', '3840_test03'],
            ['1280_test01', '3840_test02'],
            ['1280_test02', '1920_test01', '2560_test01', '3840_test01'],
            ['1280_test03', '1920_test03', '2560_test03'],
            ['1280_test04', '1920_test04'],
            ['1280_test05', '1920_test05', '3840_test00'],
            ['1280_test06', '1920_test08', '2560_test07'],
            ['1920_test02', '2560_test02'],
            ['1920_test06', '2560_test05'],
            ['1920_test07'],
            ['2560_test04'],
            ['2560_test06'],
        ],
        [[0, 7, 11], [2, 8, 10], [5, 6, 9], [1, 3, 4]],
    ),
    'ru': (
        [
            ['1920_test00', '2560_test07'],
            ['1920_test01', '2560_test06'],
            ['1920_test02', '2560_test03'],
            ['1920_test04', '2560_test01'],
            ['1920_test05', '2560_test02'],
            ['1920_test03', '2560_test00'],
            ['2560_test04'],
            ['1920_test06'],
            ['2560_test05'],
        ],
        [[0, 6], [1, 7], [2, 5], [3, 4, 8]],
    ),
    'de': (
        [
            ['1920_test06', '2560_test00'],
            ['1920_test00', '2560_test01'],
            ['1920_test01', '2560_test02'],
            ['1920_test02', '2560_test03'],
            ['1920_test03', '2560_test04', '2560_test06'],
            ['1920_test05', '2560_test05'],
            ['1920_test04'],
        ],
        [[0, 6], [1, 5], [2, 3], [4]],
    ),
}


def content_splits(folds=4):
    panels = {}
    for path in sorted(TRUTH.glob('*/*.png.txt')):
        panels[f'{path.parent.name}_{path.name.split(".")[0]}'] = set(re.findall(r'name="(.*)"', path.read_text(encoding='utf-8')))
    groups = []
    for panel, names in panels.items():
        group = next((g for g in groups if any(len(names & panels[p]) * 2 >= min(len(names), len(panels[p]), 1) for p in g)), None)
        if group is None:
            groups.append([panel])
        else:
            group.append(panel)
    groups.sort(key=len, reverse=True)
    assignment = [[] for _ in range(folds)]
    sizes = [0] * folds
    for index, group in enumerate(groups):
        target = sizes.index(min(sizes))
        assignment[target].append(index)
        sizes[target] += len(group)
    return groups, assignment


GROUPS, FOLDS = SPLITS[LANGUAGE] if LANGUAGE in SPLITS else content_splits()


def augment(gray, rng):
    image = gray.astype(np.float32)
    if rng.random() < 0.5:
        scale = rng.uniform(0.92, 1.08)
        image = cv2.resize(image, (max(8, int(image.shape[1] * scale)), image.shape[0]), interpolation=cv2.INTER_LINEAR)
    if rng.random() < 0.4:
        image = cv2.GaussianBlur(image, (0, 0), rng.uniform(0.2, 0.6))
    image = np.clip(image, 0, 255) / 255.0
    image = 255.0 * np.power(image, rng.uniform(0.85, 1.2)) * rng.uniform(0.7, 1.2) + rng.uniform(-20, 20)
    image += np.random.default_rng(rng.randint(0, 1 << 30)).normal(0, rng.uniform(0, 5), image.shape)
    return np.clip(image, 0, 255).astype(np.uint8)


class MixedStream(IterableDataset):
    def __init__(self, real, real_share, seed):
        self.real = real
        self.real_share = real_share
        self.seed = seed

    def __iter__(self):
        info = get_worker_info()
        worker = info.id if info else 0
        synth = Synth(self.seed * 1000 + worker)
        rng = random.Random(self.seed * 7919 + worker)
        while True:
            if self.real and rng.random() < self.real_share:
                gray, label = rng.choice(self.real)
                yield prepare(augment(gray, rng)), label
            else:
                gray, label = synth.sample()
                yield prepare(gray), label


def fine_tune(base, held, steps, seed, device):
    torch.manual_seed(0)
    model, width = load(base, device)
    real = [(gray, label) for scene in SCENES for name, gray, label in load_real(scene) if panel_of(name) not in held]
    loader = DataLoader(MixedStream(real, 0.25, seed), batch_size=128, num_workers=int(os.environ.get('RUNEHELPER_WORKERS', '12')), collate_fn=partial(collate, charset=model.charset), prefetch_factor=4, persistent_workers=True)
    optimizer = torch.optim.AdamW(model.parameters(), lr=1e-3, weight_decay=1e-4)
    schedule = torch.optim.lr_scheduler.OneCycleLR(optimizer, max_lr=1e-3, total_steps=steps, pct_start=0.1)
    ctc = nn.CTCLoss(blank=0, zero_infinity=True)
    model.train()
    for step, (images, lengths, targets, target_lengths) in enumerate(loader, 1):
        logits = model(images.to(device, non_blocking=True))
        loss = ctc(F.log_softmax(logits, dim=1).permute(2, 0, 1), targets, lengths, target_lengths)
        optimizer.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 5.0)
        optimizer.step()
        schedule.step()
        if step >= steps:
            break
    model.eval()
    return model, width, len(real)


def score(model, device, held, matcher, table):
    for scene in SCENES:
        row = table.setdefault(scene, [0, 0, 0, 0, 0])
        for name, gray, label in load_real(scene, labelled=False):
            if panel_of(name) not in held:
                continue
            text, confidence = read(model, device, gray)
            accepted = bool(text.strip()) and confidence >= MIN_CONFIDENCE
            if label:
                target = matcher.best(label)
                row[0] += accepted and target is not None and matcher.best(text) == target
                row[1] += accepted and text == label
                row[2] += 1
            else:
                row[3] += accepted
                row[4] += 1


def main():
    base = sys.argv[1]
    mode = sys.argv[2]
    steps = int(sys.argv[3]) if len(sys.argv) > 3 else 4000
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    started = time.time()
    if mode == 'all':
        model, width, count = fine_tune(base, set(), steps, 11, device)
        save(model, width, f'{base}_all')
        print(f'{base}_all: fine-tuned on {count} real crops in {time.time() - started:.0f}s')
        return
    folds = range(len(FOLDS)) if mode == 'cv' else [int(mode)]
    matcher = Matcher()
    table = {}
    for fold in folds:
        held = {panel for group in FOLDS[fold] for panel in GROUPS[group]}
        model, width, count = fine_tune(base, held, steps, 11 + fold, device)
        save(model, width, f'{base}_fold{fold}')
        score(model, device, held, matcher, table)
        print(f'fold {fold}: fine-tuned on {count} real crops, {time.time() - started:.0f}s', flush=True)
    print(f'{"scene":8} {"named":>9} {"exact":>9} {"phantom":>9}')
    for scene, (named, exact, labelled, phantom, unlabelled) in table.items():
        print(f'{scene:8} {named:4d}/{labelled:<4d} {exact:4d}/{labelled:<4d} {phantom:4d}/{unlabelled:<4d}')
    totals = [sum(v[i] for v in table.values()) for i in range(5)]
    print(f'{"total":8} {totals[0]:4d}/{totals[2]:<4d} {totals[1]:4d}/{totals[2]:<4d} {totals[3]:4d}/{totals[4]:<4d}')


if __name__ == '__main__':
    main()
