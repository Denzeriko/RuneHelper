import glob
import json
import os
import re
from pathlib import Path

import cv2

from languages import LANGUAGES

REPO = Path(__file__).resolve().parents[2]
WORK = Path(os.environ.get('RUNEHELPER_ML', '~/.cache/runehelper-ml')).expanduser()
LANGUAGE = LANGUAGES[os.environ.get('RUNEHELPER_LANGUAGE', 'en')]
ENGLISH = LANGUAGE.code == 'en'

COMBINATIONS = REPO / 'RuneHelper' / 'resources' / 'combinations.json'
MODEL = REPO / 'RuneHelper' / 'resources' / ('text_model.bin' if ENGLISH else f'text_model_{LANGUAGE.code}.bin')
TESTS = REPO / 'tests' if ENGLISH else REPO / 'tests' / LANGUAGE.code
PANELS = TESTS / 'panels'
TRUTH = TESTS / 'truth'
REAL = (WORK if ENGLISH else WORK / LANGUAGE.code) / 'real'
FONT = Path(LANGUAGE.font) if LANGUAGE.font.startswith('/') else WORK / 'fonts' / LANGUAGE.font
LATIN_FONT = Path(LANGUAGE.latin_font) if LANGUAGE.latin_font else None

MIN_CONFIDENCE = 80.0
SCENES = ('clean', 'dim', 'uhd', 'busy', 'narrow')
QUANTITY = re.compile(r"^\s*[0-9iIl|!OoS]{1,2}[xXnw]\s+")
BARE_QUANTITY = re.compile(r"^\s*\d{1,3}\s+(?=\D)")
BRACKETED_QUANTITY = re.compile(r"\s*\(\d{1,3}\)\s*$")
TRAILING_QUANTITY = re.compile(r"\s+[xX]\d{1,3}\s*$")
TRUTH_ROW = re.compile(r'(?:text="(.*?)" )?qty=(\d+) name="(.*)"')
DEBUG_READING = 'trimmed: '


def checkpoint(tag):
    return WORK / f'{tag}.pt'


def combinations():
    with open(COMBINATIONS, encoding='utf-8') as handle:
        return json.load(handle)['combinations']


def localized_names():
    return [entry.get('names', {}).get(LANGUAGE.code, '') for entry in combinations()]


def vocabulary_charset():
    return ''.join(sorted(set(''.join(localized_names()) + LANGUAGE.extra)))


CHARSET = LANGUAGE.charset or vocabulary_charset()


def encode(text, charset=None):
    charset = charset or CHARSET
    return [charset.index(c) + 1 for c in text if c in charset]


def unquantified(name):
    return name.startswith(LANGUAGE.unprefixed) or any(word in name.split() for word in LANGUAGE.plain_words)


def displayed(quantity, name):
    if unquantified(name):
        return name
    if LANGUAGE.quantity == 'prefix':
        return f'{quantity}x {name}'
    if LANGUAGE.quantity == 'suffix':
        return f'{name} ({quantity})'
    if LANGUAGE.quantity == 'suffix_x':
        return f'{name} x{quantity}'
    return f'{quantity} {name}'


def load_vocabulary():
    names = set(entry['output'] for entry in combinations()) if ENGLISH else set(localized_names())
    if ENGLISH:
        for path in glob.glob(os.path.expanduser('~/.config/RuneHelper/prices_dump_*.json')):
            with open(path) as handle:
                names.update(json.load(handle).get('items', {}).keys())
    return sorted(n for n in names if n and all(c in CHARSET for c in n))


def load_truth(path):
    rows = []
    with open(path, encoding='utf-8') as handle:
        for line in handle:
            match = TRUTH_ROW.search(line)
            if match:
                text, quantity, name = match.groups()
                rows.append((text, int(quantity), name))
    return rows


def read_debug_text(path):
    if not path.exists():
        return ''
    with open(path, encoding='utf-8') as handle:
        for line in handle:
            if line.startswith(DEBUG_READING):
                return line[len(DEBUG_READING):].rstrip('\n')
    return ''


def load_real(scene, labelled=True):
    folder = REAL / scene
    samples = []
    with open(folder / 'labels.tsv', encoding='utf-8') as handle:
        for line in handle:
            name, label = (line.rstrip('\n').split('\t') + ['', ''])[:2]
            if labelled and not label:
                continue
            samples.append((name, cv2.imread(str(folder / name), cv2.IMREAD_GRAYSCALE), label))
    return samples


def has_real():
    return all((REAL / scene / 'labels.tsv').exists() for scene in SCENES)


def panel_of(name):
    return name.rsplit('_row', 1)[0]


def squash(text):
    return ''.join(c for c in text.lower() if c.isalnum())


def expected_name(label):
    name = BARE_QUANTITY.sub('', QUANTITY.sub('', label))
    return TRAILING_QUANTITY.sub('', BRACKETED_QUANTITY.sub('', name)).strip()


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
