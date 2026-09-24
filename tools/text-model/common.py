import glob
import json
import os
import re
from pathlib import Path

import cv2
import numpy as np

REPO = Path(__file__).resolve().parents[2]
WORK = Path(os.environ.get('RUNEHELPER_ML', '~/.cache/runehelper-ml')).expanduser()
LANGUAGE = os.environ.get('RUNEHELPER_LANGUAGE', 'en')

CYRILLIC = 'АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюя'
BASIC = " '()+,-.0123456789:"
LATIN = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz'
NOTO_CJK = '/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc'

LANGUAGES = {
    'en': {
        'charset': " '()+,-.0123456789:ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
        'font': 'Fontin-Regular.otf',
        'unprefixed': ('Skill', 'Support', 'Unique', 'Rare Unique'),
        'model': 'text_model.bin',
        'tests': REPO / 'tests',
        'work': WORK,
        'quantity': 'prefix',
    },
    'ru': {
        'charset': " '()+,-.0123456789:" + CYRILLIC,
        'font': 'FontinSans_Cyrillic_46b/FontinSans_Cyrillic_R_46b.otf',
        'unprefixed': ('Умение', 'Поддержка', 'Уровень умения', 'Уникальн', 'Редкий уникальный'),
        'model': 'text_model_ru.bin',
        'tests': REPO / 'tests' / 'ru',
        'work': WORK / 'ru',
        'quantity': 'suffix',
    },
    'de': {
        'charset': " '()+,-.0123456789:ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyzÄÖÜäöüß",
        'font': 'Fontin-Regular.otf',
        'unprefixed': ('Fertigkeit', 'Unterstützung', 'Einzigartig', 'Seltener einzigartiger'),
        'model': 'text_model_de.bin',
        'tests': REPO / 'tests' / 'de',
        'work': WORK / 'de',
        'quantity': 'prefix',
    },
    'fr': {
        'charset': BASIC + LATIN + 'àâäçéèêëîïôöùûüÿœæÀÂÄÇÉÈÊËÎÏÔÖÙÛÜŸŒÆ',
        'font': 'Fontin-Regular.otf',
        'unprefixed': ('Aptitude', 'Gemme de soutien'),
        'plain_words': ('Unique',),
        'quantity': 'prefix',
    },
    'es': {
        'charset': BASIC + LATIN + 'áéíóúüñÁÉÍÓÚÜÑ',
        'font': 'Fontin-Regular.otf',
        'unprefixed': ('Habilidad', 'Asistencia'),
        'plain_words': ('único', 'única'),
        'quantity': 'suffix_x',
    },
    'pt': {
        'charset': BASIC + LATIN + 'áàâãçéêíóôõúüÁÀÂÃÇÉÊÍÓÔÕÚÜ',
        'font': 'Fontin-Regular.otf',
        'unprefixed': ('Habilidade', 'Reforço'),
        'plain_words': ('Único', 'Única'),
        'quantity': 'bare',
    },
    'ko': {
        'charset': None,
        'extra': BASIC + LATIN + '스킬 레벨 고유 희귀한 아이템 미가공 젬 정신력 무작위 화폐 개',
        'font': NOTO_CJK,
        'font_index': 1,
        'unprefixed': ('스킬 레벨',),
        'plain_words': ('고유',),
        'quantity': 'prefix',
    },
    'ja': {
        'charset': None,
        'extra': BASIC + LATIN + 'スキルレベル ユニーク 貴重なアイテム 完全 上級 個 ランダムなカレンシー ・ー',
        'font': NOTO_CJK,
        'font_index': 0,
        'unprefixed': ('スキルレベル',),
        'plain_words': ('ユニーク',),
        'quantity': 'prefix',
    },
    'th': {
        'charset': None,
        'extra': BASIC + LATIN + 'สกิล เสริม ยูนิค ไอเทมยูนิคที่พบได้ยาก เลเวล ไร้ที่ติ ชั้นสูง',
        'font': '/usr/share/fonts/noto/NotoSansThai-Regular.ttf',
        'latin_font': '/usr/share/fonts/noto/NotoSans-Regular.ttf',
        'unprefixed': ('สกิล', 'เสริม'),
        'plain_words': ('ยูนิค',),
        'quantity': 'prefix',
    },
}

for _code, _settings in LANGUAGES.items():
    _settings.setdefault('model', f'text_model_{_code}.bin')
    _settings.setdefault('tests', REPO / 'tests' / _code)
    _settings.setdefault('work', WORK / _code)
    _settings.setdefault('plain_words', ())
    _settings.setdefault('font_index', 0)

SETTINGS = LANGUAGES[LANGUAGE]
UNPREFIXED = SETTINGS['unprefixed']
PLAIN_WORDS = SETTINGS['plain_words']
FONT = Path(SETTINGS['font']) if SETTINGS['font'].startswith('/') else WORK / 'fonts' / SETTINGS['font']
FONT_INDEX = SETTINGS['font_index']
LATIN_FONT = Path(SETTINGS['latin_font']) if 'latin_font' in SETTINGS else None
REAL = SETTINGS['work'] / 'real'
PANELS = SETTINGS['tests'] / 'panels'
TRUTH = SETTINGS['tests'] / 'truth'
MODEL = REPO / 'RuneHelper' / 'resources' / SETTINGS['model']

INPUT_HEIGHT = 24
STRIDE = 4
MIN_CONFIDENCE = 80.0
SCENES = ('clean', 'dim', 'uhd', 'busy', 'narrow')
QUANTITY = re.compile(r"^\s*[0-9iIl|!OoS]{1,2}[xXnw]\s+")
BARE_QUANTITY = re.compile(r"^\s*\d{1,3}\s+(?=\D)")
BRACKETED_QUANTITY = re.compile(r"\s*\(\d{1,3}\)\s*$")
TRAILING_QUANTITY = re.compile(r"\s+[xX]\d{1,3}\s*$")


def checkpoint(tag):
    return WORK / f'{tag}.pt'


def vocabulary_charset(settings):
    with open(REPO / 'RuneHelper' / 'resources' / 'combinations.json', encoding='utf-8') as handle:
        names = [entry.get('names', {}).get(LANGUAGE, '') for entry in json.load(handle)['combinations']]
    return ''.join(sorted(set(''.join(names) + settings['extra'])))


CHARSET = SETTINGS['charset'] or vocabulary_charset(SETTINGS)


def encode(text, charset=None):
    charset = charset or CHARSET
    return [charset.index(c) + 1 for c in text if c in charset]


def unquantified(name):
    return name.startswith(UNPREFIXED) or any(word in name.split() for word in PLAIN_WORDS)


def displayed(quantity, name):
    if unquantified(name):
        return name
    style = SETTINGS['quantity']
    if style == 'prefix':
        return f'{quantity}x {name}'
    if style == 'suffix':
        return f'{name} ({quantity})'
    if style == 'suffix_x':
        return f'{name} x{quantity}'
    return f'{quantity} {name}'


def load_vocabulary():
    names = set()
    with open(REPO / 'RuneHelper' / 'resources' / 'combinations.json', encoding='utf-8') as handle:
        for entry in json.load(handle)['combinations']:
            names.add(entry['output'] if LANGUAGE == 'en' else entry.get('names', {}).get(LANGUAGE, ''))
    if LANGUAGE == 'en':
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
