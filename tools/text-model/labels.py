import difflib
import re

from common import REAL, REPO, SCENES, UNPREFIXED, panel_of, squash

TRUTH = REPO / 'tests' / 'truth'


def displayed(quantity, name):
    return name if name.startswith(UNPREFIXED) else f'{quantity}x {name}'


def load_truth(path):
    rows = []
    with open(path) as handle:
        for line in handle:
            match = re.search(r'qty=(\d+) name="(.*)"', line)
            if match:
                rows.append(displayed(int(match.group(1)), match.group(2)))
    return rows


def reading(path):
    if not path.exists():
        return ''
    with open(path) as handle:
        for line in handle:
            if line.startswith('trimmed: '):
                return line[len('trimmed: '):].rstrip('\n')
    return ''


def similarity(label, text):
    a, b = squash(label), squash(text)
    if not a or not b:
        return 0.0
    return difflib.SequenceMatcher(None, a, b).ratio()


def align(labels, texts):
    n, m = len(labels), len(texts)
    best = [[0.0] * (m + 1) for _ in range(n + 1)]
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            best[i][j] = max(best[i - 1][j], best[i][j - 1])
            score = similarity(labels[i - 1], texts[j - 1])
            if score >= 0.5:
                best[i][j] = max(best[i][j], best[i - 1][j - 1] + score)
    pairs = []
    i, j = n, m
    while i > 0 and j > 0:
        score = similarity(labels[i - 1], texts[j - 1])
        if score >= 0.5 and abs(best[i][j] - (best[i - 1][j - 1] + score)) < 1e-9:
            pairs.append((i - 1, j - 1))
            i, j = i - 1, j - 1
        elif best[i - 1][j] >= best[i][j - 1]:
            i -= 1
        else:
            j -= 1
    pairs.reverse()
    matched = {j: i for i, j in pairs}
    anchors = [(-1, -1)] + pairs + [(n, m)]
    for (i0, j0), (i1, j1) in zip(anchors, anchors[1:]):
        free_labels = list(range(i0 + 1, i1))
        free_crops = [j for j in range(j0 + 1, j1) if not texts[j]]
        if len(free_labels) == 1 and len(free_crops) == 1:
            matched[free_crops[0]] = free_labels[0]
    return matched


def main():
    for scene in SCENES:
        folder = REAL / scene
        panels = {}
        for crop in sorted(folder.glob('*.png')):
            panels.setdefault(panel_of(crop.name), []).append(crop)
        lines = []
        labelled = 0
        for panel, crops in sorted(panels.items()):
            resolution, test = panel.split('_', 1)
            labels = load_truth(TRUTH / resolution / f'{test}.png.txt')
            texts = [reading(crop.with_suffix('.read.txt')) for crop in crops]
            matched = align(labels, texts)
            for j, crop in enumerate(crops):
                label = labels[matched[j]] if j in matched else ''
                labelled += bool(label)
                lines.append(f'{crop.name}\t{label}\t{texts[j]}')
        (folder / 'labels.tsv').write_text('\n'.join(lines) + '\n')
        print(f'{scene:6} {labelled} of {len(lines)} crops labelled')


if __name__ == '__main__':
    main()
