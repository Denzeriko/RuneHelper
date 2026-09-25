import argparse
import random
import time
from dataclasses import dataclass, fields

import cv2
import numpy as np
import torch
from torch.utils.data import IterableDataset, get_worker_info

from common import LANGUAGE, MIN_CONFIDENCE, SCENES, TRUTH, Matcher, load_real, load_truth, panel_of
from model import load, prepare, read, save
from synth import Synth
from training import Optimiser, batches

LEARNING_RATE = 1e-3
REAL_SHARE = 0.25
SEED = 11
FOLD_COUNT = 4

REFERENCE_FOLDS = {
    'en': [
        ['1280_test00', '1920_test00', '1920_test02', '2560_test00', '2560_test02', '2560_test06', '3840_test03'],
        ['1280_test02', '1920_test01', '1920_test06', '2560_test01', '2560_test04', '2560_test05', '3840_test01'],
        ['1280_test05', '1280_test06', '1920_test05', '1920_test07', '1920_test08', '2560_test07', '3840_test00'],
        ['1280_test01', '1280_test03', '1280_test04', '1920_test03', '1920_test04', '2560_test03', '3840_test02'],
    ],
    'ru': [
        ['1920_test00', '2560_test04', '2560_test07'],
        ['1920_test01', '1920_test06', '2560_test06'],
        ['1920_test02', '1920_test03', '2560_test00', '2560_test03'],
        ['1920_test04', '1920_test05', '2560_test01', '2560_test02', '2560_test05'],
    ],
    'de': [
        ['1920_test04', '1920_test06', '2560_test00'],
        ['1920_test00', '1920_test05', '2560_test01', '2560_test05'],
        ['1920_test01', '1920_test02', '2560_test02', '2560_test03'],
        ['1920_test03', '2560_test04', '2560_test06'],
    ],
}


@dataclass
class SceneScore:
    named: int = 0
    exact: int = 0
    labelled: int = 0
    phantom: int = 0
    unlabelled: int = 0


def truth_items():
    items = {}
    for path in sorted(TRUTH.glob('*/*.png.txt')):
        items[f'{path.parent.name}_{path.name.split(".")[0]}'] = {name for _, _, name in load_truth(path)}
    return items


def group_by_shared_items(items):
    groups = []
    for panel, names in items.items():
        touching = [group for group in groups if any(names & items[other] for other in group)]
        if not touching:
            groups.append([panel])
            continue
        first = touching[0]
        for group in touching[1:]:
            first.extend(group)
            groups.remove(group)
        first.append(panel)
    return groups


def balanced_folds(groups, count):
    folds = [[] for _ in range(count)]
    for group in sorted(groups, key=len, reverse=True):
        min(folds, key=len).extend(group)
    return [sorted(fold) for fold in folds]


FOLDS = REFERENCE_FOLDS.get(LANGUAGE.code) or balanced_folds(group_by_shared_items(truth_items()), FOLD_COUNT)


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


def fine_tune(base, held, steps, seed, device, workers):
    torch.manual_seed(0)
    model, width = load(base, device)
    real = [(gray, label) for scene in SCENES for name, gray, label in load_real(scene) if panel_of(name) not in held]
    loader = batches(MixedStream(real, REAL_SHARE, seed), model.charset, workers)
    optimiser = Optimiser(model, LEARNING_RATE, steps)
    model.train()
    for step, batch in enumerate(loader, 1):
        optimiser.step(batch, device)
        if step >= steps:
            break
    model.eval()
    return model, width, len(real)


def score(model, device, held, matcher, table):
    for scene in SCENES:
        result = table.setdefault(scene, SceneScore())
        for name, gray, label in load_real(scene, labelled=False):
            if panel_of(name) not in held:
                continue
            text, confidence = read(model, device, gray)
            accepted = bool(text.strip()) and confidence >= MIN_CONFIDENCE
            if label:
                target = matcher.best(label)
                result.named += accepted and target is not None and matcher.best(text) == target
                result.exact += accepted and text == label
                result.labelled += 1
            else:
                result.phantom += accepted
                result.unlabelled += 1


def total(scores):
    result = SceneScore()
    for scene_score in scores:
        for field in fields(SceneScore):
            setattr(result, field.name, getattr(result, field.name) + getattr(scene_score, field.name))
    return result


def table_row(label, result):
    return (
        f'{label:8} {result.named:4d}/{result.labelled:<4d} {result.exact:4d}/{result.labelled:<4d} '
        f'{result.phantom:4d}/{result.unlabelled:<4d}'
    )


def arguments():
    parser = argparse.ArgumentParser(description='Fine-tune a pretrained checkpoint on the real crops.')
    parser.add_argument('base', help='pretrained checkpoint in the work folder, for example synthetic')
    parser.add_argument('mode', help="'cv' scores every fold on its own held-out panels, a fold number scores one fold, 'all' trains on every crop")
    parser.add_argument('--steps', type=int, default=4000)
    parser.add_argument('--workers', type=int, default=12, help='processes that draw training lines')
    return parser.parse_args()


def main():
    args = arguments()
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    started = time.time()
    if args.mode == 'all':
        model, width, count = fine_tune(args.base, set(), args.steps, SEED, device, args.workers)
        save(model, width, f'{args.base}_all')
        print(f'{args.base}_all: fine-tuned on {count} real crops in {time.time() - started:.0f}s')
        return
    folds = range(len(FOLDS)) if args.mode == 'cv' else [int(args.mode)]
    matcher = Matcher()
    table = {}
    for fold in folds:
        held = set(FOLDS[fold])
        model, width, count = fine_tune(args.base, held, args.steps, SEED + fold, device, args.workers)
        save(model, width, f'{args.base}_fold{fold}')
        score(model, device, held, matcher, table)
        print(f'fold {fold}: fine-tuned on {count} real crops, {time.time() - started:.0f}s', flush=True)
    print(f'{"scene":8} {"named":>9} {"exact":>9} {"phantom":>9}')
    for scene, result in table.items():
        print(table_row(scene, result))
    print(table_row('total', total(table.values())))


if __name__ == '__main__':
    main()
