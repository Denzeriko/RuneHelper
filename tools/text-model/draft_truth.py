import difflib
import re

from common import LANGUAGE, REAL, TRUTH, combinations, displayed, load_vocabulary, panel_of, read_debug_text, squash

MIN_SCORE = 0.75
MAX_PREFIX_OFFSET = 6
LEVEL = re.compile(r'^\s*([0-9Oo]{1,2})(?=\s|$)')
CURRENCY_COUNT = re.compile(r'\s*\d+\s*\S*\s*$|^\s*\d+\s*\S*\s+')

QUANTITY = {
    'prefix': re.compile(r'^\s*(\d{1,3})\s*[xXх]\s+(.*)$'),
    'suffix': re.compile(r'^(.*?)\s*\((\d{1,3})\)\s*$'),
    'suffix_x': re.compile(r'^(.*?)\s+[xX](\d{1,3})\s*$'),
    'bare': re.compile(r'^\s*(\d{1,3})\s+(.*)$'),
}


def split_quantity(text):
    match = QUANTITY[LANGUAGE.quantity].match(text)
    if not match:
        return 1, text.strip()
    groups = match.groups()
    if LANGUAGE.quantity in ('prefix', 'bare'):
        return int(groups[0]), groups[1].strip()
    return int(groups[1]), groups[0].strip()


def closest(text, names):
    target = squash(text)
    scored = [(difflib.SequenceMatcher(None, target, squash(n)).ratio(), n) for n in names]
    return max(scored) if scored else (0.0, text)


def random_currency():
    for entry in combinations():
        if entry['output'] == '5x Random Currency':
            return entry.get('names', {}).get(LANGUAGE.code, '')
    return ''


def draft(text, names, currency):
    for prefix in LANGUAGE.unprefixed:
        start = text.find(prefix)
        if 0 <= start <= MAX_PREFIX_OFFSET:
            if ':' in text[start:]:
                head, tail = text[start:].split(':', 1)
            else:
                head, tail = prefix, text[start + len(prefix):]
                level = LEVEL.match(tail)
                if level:
                    head = f'{prefix} {level.group(1).replace("O", "0").replace("o", "0")}'
                    tail = tail[level.end():]
            score, name = closest(tail, names)
            separator = ' : ' if head.endswith(' ') or LANGUAGE.code == 'fr' else ': '
            return score, 1, f'{head.strip()}{separator}{name}', None
    if currency and closest(text, [currency])[0] >= 0.8:
        return 1.0, 5, CURRENCY_COUNT.sub('', currency).strip(), currency
    quantity, name = split_quantity(text)
    score, snapped = closest(name, names)
    return score, quantity, snapped, None


def main():
    names = load_vocabulary()
    currency = random_currency()
    panels = {}
    for crop in sorted((REAL / 'clean').glob('*.png')):
        panels.setdefault(panel_of(crop.name), []).append(crop)
    for panel, crops in sorted(panels.items()):
        resolution, test = panel.split('_', 1)
        lines = []
        print(f'== {resolution}/{test}')
        for crop in crops:
            text = read_debug_text(crop.with_suffix('.read.txt'))
            if not text.strip():
                continue
            score, quantity, name, shown = draft(text, names, currency)
            if score < MIN_SCORE:
                continue
            shown = shown or displayed(quantity, name)
            lines.append(f'text="{shown}" qty={quantity} name="{name}"')
            print(f'  {score:4.2f}  {text!r:50} -> {shown}')
        target = TRUTH / resolution / f'{test}.png.txt'
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('\n'.join([f'rows={len(lines)}'] + lines) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
