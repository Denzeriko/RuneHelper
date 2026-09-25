import random

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

from common import CHARSET, FONT, LANGUAGE, LATIN_FONT, REAL, WORK, displayed, load_vocabulary, unquantified

SUPERSAMPLE = 4
LETTERS = LANGUAGE.letters or ''.join(c for c in CHARSET if c.isalpha())


def parchment_bank():
    bank = []
    folder = next((f for f in (REAL / 'clean', WORK / 'real' / 'clean') if f.exists()), REAL / 'clean')
    for path in sorted(folder.glob('*.png')):
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
        pieces = (piece for n in self.names for piece in n.replace('の', ' ').replace('・', ' ').split())
        self.words = sorted({w for w in pieces if w.isalpha() and len(w) > 1})
        self.bank = parchment_bank()
        self.fonts = {}

    def font(self, size, latin=False):
        key = (size, latin)
        if key not in self.fonts:
            path = LATIN_FONT if latin else FONT
            engine = ImageFont.Layout.RAQM if LATIN_FONT else ImageFont.Layout.BASIC
            self.fonts[key] = ImageFont.truetype(str(path), size, index=0 if latin else LANGUAGE.font_index, layout_engine=engine)
        return self.fonts[key]

    def runs(self, text, size):
        if LATIN_FONT is None:
            return [(text, self.font(size))]
        out = []
        for ch in text:
            latin = not ('\u0e00' <= ch <= '\u0e7f') and ch != ' '
            if out and (ch == ' ' or out[-1][2] == latin):
                out[-1] = (out[-1][0] + ch, out[-1][1], out[-1][2])
            else:
                out.append((ch, self.font(size, latin), latin))
        return [(t, f) for t, f, _ in out]

    def measure(self, draw, text, size):
        return sum(draw.textlength(t, font=f) for t, f in self.runs(text, size))

    def word_salad(self):
        count = self.rng.choice([1, 2, 2, 3, 3, 4])
        name = ' '.join(self.rng.choice(self.words) for _ in range(count))
        if self.rng.random() < 0.15:
            name += self.rng.choice(LANGUAGE.level).format(n=self.rng.randint(1, 21))
        return name

    def prefixed_name(self):
        tail = ' '.join(self.rng.choice(self.words) for _ in range(self.rng.choice([1, 2, 3])))
        return self.rng.choice(LANGUAGE.prefixed).format(tail=tail, n=self.rng.randint(1, 21))

    def random_letters(self):
        return ' '.join(''.join(self.rng.choice(LETTERS) for _ in range(self.rng.randint(2, 9))) for _ in range(self.rng.randint(1, 3)))

    def quantity(self):
        roll = self.rng.random()
        if roll < 0.35:
            return 1
        if roll < 0.85:
            return self.rng.randint(2, 9)
        return self.rng.randint(10, 20)

    def text(self):
        r = self.rng.random()
        if r < 0.55:
            name = self.rng.choice(self.names)
        elif r < 0.75:
            name = self.word_salad()
        elif r < 0.90:
            name = self.prefixed_name()
        else:
            name = self.random_letters()
        if unquantified(name) or self.rng.random() < 0.08:
            return name
        return displayed(self.quantity(), name)

    def line_height(self):
        roll = self.rng.random()
        if roll < 0.4:
            return self.rng.randint(10, 18)
        if roll < 0.7:
            return self.rng.randint(19, 28)
        return self.rng.randint(29, 46)

    def stroke_width(self):
        weight = self.rng.random()
        if weight < 0.1:
            return 2
        if weight < 0.7:
            return 1
        return 0

    def draw_text(self, draw, label, origin, size, advances, stroke):
        x, y = origin
        if advances:
            font = self.font(size)
            for ch, advance in zip(label, advances):
                draw.text((x, y), ch, font=font, fill=255, stroke_width=stroke, stroke_fill=255)
                x += advance
            return
        for text, run_font in self.runs(label, size):
            draw.text((x, y), text, font=run_font, fill=255, stroke_width=stroke, stroke_fill=255)
            x += draw.textlength(text, font=run_font)

    def underline(self, draw, label, origin, size, ascent, advances, tracking):
        word = next((w for w in LANGUAGE.underlined if w in label), None)
        if not word or self.rng.random() >= 0.8:
            return
        origin_x, origin_y = origin
        start = label.index(word)
        if advances:
            x0 = origin_x + sum(advances[:start])
            x1 = x0 + sum(advances[start:start + len(word)]) - tracking
        else:
            x0 = origin_x + self.measure(draw, label[:start], size)
            x1 = origin_x + self.measure(draw, label[:start + len(word)], size)
        y = origin_y + ascent + 0.12 * size
        draw.line([(x0, y), (x1, y)], fill=255, width=max(1, size // 14))

    def render(self, label, height):
        rng = self.rng
        em = (rng.uniform(0.8, 1.2) if height <= 18 else rng.uniform(0.6, 1.02)) * height
        size = max(6, int(round(em * SUPERSAMPLE)))
        font = self.font(size)
        probe = ImageDraw.Draw(Image.new('L', (1, 1)))
        tracking = rng.uniform(*LANGUAGE.tracking) * size
        advances = [probe.textlength(ch, font=font) + tracking for ch in label] if tracking > 0 else []
        text_width = (sum(advances) if advances else self.measure(probe, label, size)) / SUPERSAMPLE
        ascent, _ = font.getmetrics()
        left_margin = rng.uniform(0.2, 2.5) * height
        right_margin = rng.uniform(0.25, 1.2) * height
        width = int(left_margin + text_width + right_margin) + 1
        canvas = Image.new('L', (width * SUPERSAMPLE, height * SUPERSAMPLE), 0)
        draw = ImageDraw.Draw(canvas)
        cap_top = rng.uniform(0.0, 0.35) * height
        origin = (left_margin * SUPERSAMPLE, cap_top * SUPERSAMPLE - (ascent - LANGUAGE.cap * size))
        self.draw_text(draw, label, origin, size, advances, self.stroke_width())
        self.underline(draw, label, origin, size, ascent, advances, tracking)
        return cv2.resize(np.asarray(canvas, dtype=np.float32) / 255.0, (width, height), interpolation=cv2.INTER_AREA)

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

    def add_frame_strip(self, image, width, height):
        if self.rng.random() < 0.6:
            strip = max(1, int(self.rng.uniform(0.1, 0.5) * height))
            image[:, width - strip:] = self.rng.uniform(25, 80)
        return image

    def add_edge_band(self, image, height):
        if self.rng.random() >= 0.25:
            return image
        band = max(1, int(self.rng.uniform(0.05, 0.2) * height))
        level = self.rng.uniform(30, 90)
        if self.rng.random() < 0.5:
            image[:band, :] = image[:band, :] * 0.3 + level * 0.7
        else:
            image[height - band:, :] = image[height - band:, :] * 0.3 + level * 0.7
        return image

    def blur(self, image):
        if self.rng.random() < 0.5:
            image = cv2.GaussianBlur(image, (0, 0), self.rng.uniform(0.2, 0.7))
        return image

    def adjust_exposure(self, image):
        gain = self.rng.uniform(0.6, 1.2)
        offset = self.rng.uniform(-30, 30)
        gamma = self.rng.uniform(0.8, 1.25)
        image = np.clip(image, 0, 255) / 255.0
        image = 255.0 * np.power(image, gamma) * gain + offset
        noise = np.random.default_rng(self.rng.randint(0, 1 << 30))
        image += noise.normal(0, self.rng.uniform(0, 6), image.shape)
        return np.clip(image, 0, 255).astype(np.uint8)

    def sample(self):
        label = self.text()
        height = self.line_height()
        mask = self.render(label, height)
        width = mask.shape[1]
        background = self.background(width, height)
        ink = self.rng.uniform(10, 60)
        image = background * (1.0 - mask) + ink * mask
        image = self.add_frame_strip(image, width, height)
        image = self.add_edge_band(image, height)
        image = self.blur(image)
        return self.adjust_exposure(image), label


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
