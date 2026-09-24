import glob
import json
import os
import re
from pathlib import Path

import cv2
import numpy as np

REPO = Path(__file__).resolve().parents[2]
WORK = Path(os.environ.get('RUNEHELPER_ML', '~/.cache/runehelper-ml')).expanduser()
FONT = WORK / 'fonts' / 'Fontin-Regular.otf'
REAL = WORK / 'real'
MODEL = REPO / 'RuneHelper' / 'resources' / 'text_model.bin'

INPUT_HEIGHT = 24
STRIDE = 4
MIN_CONFIDENCE = 80.0
CHARSET = " '()+,-.0123456789:ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
UNPREFIXED = ('Skill', 'Support', 'Unique', 'Rare Unique')
SCENES = ('clean', 'dim', 'uhd', 'busy', 'narrow')
QUANTITY = re.compile(r"^\s*[0-9iIl|!OoS]{1,2}[xXnw]\s+")


def checkpoint(tag):
    return WORK / f'{tag}.pt'


def encode(text):
    return [CHARSET.index(c) + 1 for c in text if c in CHARSET]


def decode(indices):
    out = []
    previous = 0
    for index in indices:
        if index != previous and index != 0:
            out.append(CHARSET[index - 1])
        previous = index
    return ''.join(out)


def load_vocabulary():
    names = set()
    with open(REPO / 'RuneHelper' / 'resources' / 'combinations.json') as handle:
        for entry in json.load(handle)['combinations']:
            names.add(entry['output'])
    for path in glob.glob(os.path.expanduser('~/.config/RuneHelper/prices_dump_*.json')):
        with open(path) as handle:
            names.update(json.load(handle).get('items', {}).keys())
    return sorted(n for n in names if n and all(c in CHARSET for c in n))


def prepare(gray):
    height, width = gray.shape
    scale = INPUT_HEIGHT / height
    target = (max(8, int(round(width * scale))), INPUT_HEIGHT)
    method = cv2.INTER_AREA if scale < 1.0 else cv2.INTER_CUBIC
    resized = cv2.resize(gray, target, interpolation=method).astype(np.float32)
    low, high = np.percentile(resized, 2), np.percentile(resized, 98)
    span = max(8.0, high - low)
    return np.clip((resized - low) / span, 0.0, 1.0)


def load_real(scene, labelled=True):
    folder = REAL / scene
    samples = []
    with open(folder / 'labels.tsv') as handle:
        for line in handle:
            name, label = (line.rstrip('\n').split('\t') + ['', ''])[:2]
            if labelled and not label:
                continue
            samples.append((name, cv2.imread(str(folder / name), cv2.IMREAD_GRAYSCALE), label))
    return samples


def panel_of(name):
    return name.rsplit('_row', 1)[0]


def squash(text):
    return re.sub(r'[^0-9a-z]', '', text.lower())


def expected_name(label):
    return QUANTITY.sub('', label).strip()


def levenshtein(a, b):
    previous = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        current = [i]
        for j, cb in enumerate(b, 1):
            current.append(min(previous[j] + 1, current[j - 1] + 1, previous[j - 1] + (ca != cb)))
        previous = current
    return previous[-1]


class Matcher:
    def __init__(self):
        self.names = load_vocabulary()
        self.squashed = [squash(n) for n in self.names]

    def best(self, text):
        target = squash(expected_name(text))
        if not target:
            return None
        best, best_distance = None, None
        for original, candidate in zip(self.names, self.squashed):
            limit = max(len(candidate), len(target)) * 18 // 100
            if abs(len(candidate) - len(target)) > limit:
                continue
            distance = levenshtein(target, candidate)
            if distance <= limit and (best_distance is None or distance < best_distance):
                best, best_distance = original, distance
        return best
