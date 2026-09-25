import difflib

import cv2

from common import REAL, SCENES, TRUTH, displayed, load_truth, panel_of, read_debug_text, squash

MIN_LENGTH_RATIO = 0.65
MIN_WIDTH_PER_CHARACTER = 0.15
MIN_SIMILARITY = 0.5


def truth_labels(path):
    return [text if text is not None else displayed(quantity, name) for text, quantity, name in load_truth(path)]


def similarity(label, text):
    a, b = squash(label), squash(text)
    if not a or not b or len(b) < MIN_LENGTH_RATIO * len(a):
        return 0.0
    return difflib.SequenceMatcher(None, a, b).ratio()


def wide_enough(crop, label):
    height, width = cv2.imread(str(crop), cv2.IMREAD_GRAYSCALE).shape
    return width >= MIN_WIDTH_PER_CHARACTER * height * len(label)


def aligned_pairs(labels, texts):
    n, m = len(labels), len(texts)
    best = [[0.0] * (m + 1) for _ in range(n + 1)]
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            best[i][j] = max(best[i - 1][j], best[i][j - 1])
            score = similarity(labels[i - 1], texts[j - 1])
            if score >= MIN_SIMILARITY:
                best[i][j] = max(best[i][j], best[i - 1][j - 1] + score)
    pairs = []
    i, j = n, m
    while i > 0 and j > 0:
        score = similarity(labels[i - 1], texts[j - 1])
        if score >= MIN_SIMILARITY and abs(best[i][j] - (best[i - 1][j - 1] + score)) < 1e-9:
            pairs.append((i - 1, j - 1))
            i, j = i - 1, j - 1
        elif best[i - 1][j] >= best[i][j - 1]:
            i -= 1
        else:
            j -= 1
    pairs.reverse()
    return pairs


def align(labels, texts, crops):
    pairs = aligned_pairs(labels, texts)
    matched = {j: i for i, j in pairs}
    anchors = [(-1, -1)] + pairs + [(len(labels), len(texts))]
    for (i0, j0), (i1, j1) in zip(anchors, anchors[1:]):
        free_labels = list(range(i0 + 1, i1))
        free_crops = [j for j in range(j0 + 1, j1) if not texts[j]]
        if len(free_labels) == 1 and len(free_crops) == 1 and wide_enough(crops[free_crops[0]], labels[free_labels[0]]):
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
            labels = truth_labels(TRUTH / resolution / f'{test}.png.txt')
            texts = [read_debug_text(crop.with_suffix('.read.txt')) for crop in crops]
            matched = align(labels, texts, crops)
            for j, crop in enumerate(crops):
                label = labels[matched[j]] if j in matched else ''
                labelled += bool(label)
                lines.append(f'{crop.name}\t{label}\t{texts[j]}')
        (folder / 'labels.tsv').write_text('\n'.join(lines) + '\n', encoding='utf-8')
        print(f'{scene:6} {labelled} of {len(lines)} crops labelled')


if __name__ == '__main__':
    main()
