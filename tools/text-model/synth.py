import random

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

from common import FONT, REAL, UNPREFIXED, WORK, load_vocabulary

SUPERSAMPLE = 4


def parchment_bank():
    bank = []
    for path in sorted((REAL / 'clean').glob('*.png')):
        gray = cv2.imread(str(path), cv2.IMREAD_GRAYSCALE)
        _, ink = cv2.threshold(gray, 0, 255, cv2.THRESH_BINARY_INV | cv2.THRESH_OTSU)
        columns = (ink > 0).mean(axis=0)
        busy = np.where(columns > 0.12)[0]
        first = busy[0] if len(busy) else gray.shape[1]
        if first - 4 >= 12:
            bank.append(gray[:, : first - 4].copy())
    return bank


class Synth:
    def __init__(self, seed=0):
        self.rng = random.Random(seed)
        self.names = load_vocabulary()
        self.words = sorted({w for n in self.names for w in n.split() if w.isalpha() and len(w) > 1})
        self.bank = parchment_bank()
        self.fonts = {}

    def font(self, size):
        if size not in self.fonts:
            self.fonts[size] = ImageFont.truetype(str(FONT), size)
        return self.fonts[size]

    def text(self):
        r = self.rng.random()
        if r < 0.55:
            name = self.rng.choice(self.names)
        elif r < 0.75:
            count = self.rng.choice([1, 2, 2, 3, 3, 4])
            name = ' '.join(self.rng.choice(self.words) for _ in range(count))
            if self.rng.random() < 0.15:
                name += f' (Level {self.rng.randint(1, 21)})'
        elif r < 0.90:
            tail = ' '.join(self.rng.choice(self.words) for _ in range(self.rng.choice([1, 2, 3])))
            name = self.rng.choice([f'Skill: {tail}', f'Support: {tail}', f'Unique {tail}', 'Rare Unique Item', f'Skill Level {self.rng.randint(1, 21)}: {tail}'])
        else:
            letters = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ'
            name = ' '.join(''.join(self.rng.choice(letters) for _ in range(self.rng.randint(2, 9))) for _ in range(self.rng.randint(1, 3)))
        if name.startswith(UNPREFIXED) or self.rng.random() < 0.08:
            return name
        roll = self.rng.random()
        quantity = 1 if roll < 0.35 else self.rng.randint(2, 9) if roll < 0.85 else self.rng.randint(10, 20)
        return f'{quantity}x {name}'

    def background(self, width, height):
        if self.bank and self.rng.random() < 0.7:
            tiles = []
            total = 0
            while total < width:
                patch = self.rng.choice(self.bank)
                scaled = cv2.resize(patch, (max(4, int(patch.shape[1] * height / patch.shape[0])), height), interpolation=cv2.INTER_LINEAR)
                if self.rng.random() < 0.5:
                    scaled = cv2.flip(scaled, 1)
                tiles.append(scaled)
                total += scaled.shape[1]
            canvas = np.concatenate(tiles, axis=1)[:, :width].astype(np.float32)
            canvas += self.rng.uniform(-12, 12)
        else:
            base = self.rng.uniform(150, 195)
            coarse = np.random.default_rng(self.rng.randint(0, 1 << 30)).normal(0, 1, (max(2, height // 6), max(2, width // 6))).astype(np.float32)
            canvas = base + 9 * cv2.resize(coarse, (width, height), interpolation=cv2.INTER_CUBIC)
            canvas += np.random.default_rng(self.rng.randint(0, 1 << 30)).normal(0, 4, (height, width)).astype(np.float32)
        return canvas

    def sample(self):
        rng = self.rng
        label = self.text()
        roll = rng.random()
        height = rng.randint(10, 18) if roll < 0.4 else rng.randint(19, 28) if roll < 0.7 else rng.randint(29, 46)
        em = (rng.uniform(0.8, 1.2) if height <= 18 else rng.uniform(0.6, 1.02)) * height
        size = max(6, int(round(em * SUPERSAMPLE)))
        font = self.font(size)
        probe = ImageDraw.Draw(Image.new('L', (1, 1)))
        text_width = probe.textlength(label, font=font) / SUPERSAMPLE
        ascent, descent = font.getmetrics()
        left_margin = rng.uniform(0.2, 2.5) * height
        right_margin = rng.uniform(0.25, 1.2) * height
        width = int(left_margin + text_width + right_margin) + 1
        canvas = Image.new('L', (width * SUPERSAMPLE, height * SUPERSAMPLE), 0)
        draw = ImageDraw.Draw(canvas)
        cap_top = rng.uniform(0.0, 0.35) * height
        origin_y = cap_top * SUPERSAMPLE - (ascent - 0.72 * size)
        origin_x = left_margin * SUPERSAMPLE
        weight = rng.random()
        stroke = 2 if weight < 0.1 else 1 if weight < 0.7 else 0
        draw.text((origin_x, origin_y), label, font=font, fill=255, stroke_width=stroke, stroke_fill=255)
        if 'Unique' in label and rng.random() < 0.8:
            start = label.index('Unique')
            x0 = origin_x + draw.textlength(label[:start], font=font)
            x1 = x0 + draw.textlength('Unique', font=font)
            y = origin_y + ascent + 0.12 * size
            draw.line([(x0, y), (x1, y)], fill=255, width=max(1, size // 14))
        mask = cv2.resize(np.asarray(canvas, dtype=np.float32) / 255.0, (width, height), interpolation=cv2.INTER_AREA)
        background = self.background(width, height)
        ink = rng.uniform(10, 60)
        image = background * (1.0 - mask) + ink * mask
        if rng.random() < 0.6:
            strip = max(1, int(rng.uniform(0.1, 0.5) * height))
            image[:, width - strip:] = rng.uniform(25, 80)
        if rng.random() < 0.25:
            band = max(1, int(rng.uniform(0.05, 0.2) * height))
            level = rng.uniform(30, 90)
            if rng.random() < 0.5:
                image[:band, :] = image[:band, :] * 0.3 + level * 0.7
            else:
                image[height - band:, :] = image[height - band:, :] * 0.3 + level * 0.7
        if rng.random() < 0.5:
            sigma = rng.uniform(0.2, 0.7)
            image = cv2.GaussianBlur(image, (0, 0), sigma)
        gain = rng.uniform(0.6, 1.2)
        offset = rng.uniform(-30, 30)
        gamma = rng.uniform(0.8, 1.25)
        image = np.clip(image, 0, 255) / 255.0
        image = 255.0 * np.power(image, gamma) * gain + offset
        image += np.random.default_rng(rng.randint(0, 1 << 30)).normal(0, rng.uniform(0, 6), image.shape)
        return np.clip(image, 0, 255).astype(np.uint8), label


if __name__ == '__main__':
    synth = Synth(1)
    print(len(synth.names), 'names', len(synth.words), 'words', len(synth.bank), 'parchment patches')
    rows = []
    for _ in range(16):
        gray, label = synth.sample()
        scaled = cv2.resize(gray, (int(gray.shape[1] * 40 / gray.shape[0]), 40), interpolation=cv2.INTER_CUBIC)
        rows.append((scaled, label))
        print(gray.shape, label)
    width = max(r[0].shape[1] for r in rows)
    sheet = np.full((len(rows) * 44, width), 255, np.uint8)
    for i, (r, _) in enumerate(rows):
        sheet[i * 44:i * 44 + 40, :r.shape[1]] = r
    (WORK / 'check').mkdir(parents=True, exist_ok=True)
    cv2.imwrite(str(WORK / 'check' / 'synth.png'), sheet)
