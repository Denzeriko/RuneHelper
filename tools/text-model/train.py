import math
import sys
import time

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader, IterableDataset, get_worker_info

from common import CHARSET, INPUT_HEIGHT, SCENES, STRIDE, Matcher, checkpoint, encode, expected_name, load_real, prepare
from synth import Synth


class Net(nn.Module):
    def __init__(self, classes, width=1.0):
        super().__init__()
        c = [max(8, int(round(v * width))) for v in (16, 32, 48, 64, 96)]

        def block(cin, cout, kernel=3, padding=1):
            return [nn.Conv2d(cin, cout, kernel, padding=padding, bias=False), nn.BatchNorm2d(cout), nn.ReLU(inplace=True)]

        self.features = nn.Sequential(
            *block(1, c[0]),
            nn.MaxPool2d(2, 2),
            *block(c[0], c[1]),
            nn.MaxPool2d(2, 2),
            *block(c[1], c[2]),
            nn.MaxPool2d((2, 1), (2, 1)),
            *block(c[2], c[3]),
            nn.Conv2d(c[3], c[4], (3, 1), bias=False),
            nn.BatchNorm2d(c[4]),
            nn.ReLU(inplace=True),
        )
        self.sequence = nn.Sequential(
            nn.Conv1d(c[4], c[4], 3, padding=1, bias=False),
            nn.BatchNorm1d(c[4]),
            nn.ReLU(inplace=True),
            nn.Conv1d(c[4], c[4], 3, padding=1, bias=False),
            nn.BatchNorm1d(c[4]),
            nn.ReLU(inplace=True),
        )
        self.classifier = nn.Conv1d(c[4], classes, 1)

    def forward(self, x):
        features = self.features(x).squeeze(2)
        return self.classifier(self.sequence(features))


def load(tag, device):
    saved = torch.load(checkpoint(tag), map_location=device)
    model = Net(len(CHARSET) + 1, saved['width']).to(device)
    model.load_state_dict(saved['state'])
    return model, saved['width']


def save(model, width, tag):
    torch.save({'state': model.state_dict(), 'width': width, 'charset': CHARSET}, checkpoint(tag))


class SynthStream(IterableDataset):
    def __init__(self, seed):
        self.seed = seed

    def __iter__(self):
        info = get_worker_info()
        synth = Synth(self.seed * 1000 + (info.id if info else 0))
        while True:
            gray, label = synth.sample()
            yield prepare(gray), label


def collate(batch):
    width = max(image.shape[1] for image, _ in batch)
    width = int(math.ceil(width / STRIDE) * STRIDE)
    images = np.ones((len(batch), 1, INPUT_HEIGHT, width), np.float32)
    lengths, targets, target_lengths = [], [], []
    for i, (image, label) in enumerate(batch):
        images[i, 0, :, : image.shape[1]] = image
        lengths.append(image.shape[1] // STRIDE)
        encoded = encode(label)
        targets.extend(encoded)
        target_lengths.append(len(encoded))
    return torch.from_numpy(images), torch.tensor(lengths), torch.tensor(targets), torch.tensor(target_lengths)


def read(model, device, gray):
    image = prepare(gray)
    width = int(math.ceil(image.shape[1] / STRIDE) * STRIDE)
    padded = np.ones((1, 1, INPUT_HEIGHT, width), np.float32)
    padded[0, 0, :, : image.shape[1]] = image
    with torch.no_grad():
        logits = model(torch.from_numpy(padded).to(device))[0, :, : image.shape[1] // STRIDE]
    best = torch.softmax(logits.double(), dim=0).max(0)
    text, total, previous = [], 0.0, 0
    for index, value in zip(best.indices.tolist(), best.values.tolist()):
        if index != 0 and index != previous:
            text.append(CHARSET[index - 1])
            total += value
        previous = index
    return ''.join(text), 100.0 * total / len(text) if text else 0.0


def evaluate(model, device, sets, matcher):
    model.eval()
    report = {}
    for scene, samples in sets.items():
        exact = named = 0
        for _, gray, label in samples:
            text, _ = read(model, device, gray)
            exact += text == label
            named += matcher.best(text) == expected_name(label)
        report[scene] = (exact, named, len(samples))
    model.train()
    return report


def main():
    steps = int(sys.argv[1]) if len(sys.argv) > 1 else 20000
    width = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
    tag = sys.argv[3] if len(sys.argv) > 3 else 'synthetic'
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    torch.manual_seed(0)
    model = Net(len(CHARSET) + 1, width).to(device)
    print(f'{tag}: {sum(p.numel() for p in model.parameters())} parameters, {steps} steps on {device}', flush=True)
    loader = DataLoader(SynthStream(1), batch_size=128, num_workers=12, collate_fn=collate, prefetch_factor=4, persistent_workers=True)
    optimizer = torch.optim.AdamW(model.parameters(), lr=3e-3, weight_decay=1e-4)
    schedule = torch.optim.lr_scheduler.OneCycleLR(optimizer, max_lr=3e-3, total_steps=steps, pct_start=0.1)
    ctc = nn.CTCLoss(blank=0, zero_infinity=True)
    sets = {scene: load_real(scene) for scene in SCENES}
    matcher = Matcher()
    best = -1.0
    started = time.time()
    running = 0.0
    for step, (images, lengths, targets, target_lengths) in enumerate(loader, 1):
        logits = model(images.to(device, non_blocking=True))
        loss = ctc(F.log_softmax(logits, dim=1).permute(2, 0, 1), targets, lengths, target_lengths)
        optimizer.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 5.0)
        optimizer.step()
        schedule.step()
        running = 0.98 * running + 0.02 * loss.item() if step > 1 else loss.item()
        if step % 1000 == 0 or step == steps:
            report = evaluate(model, device, sets, matcher)
            line = ' '.join(f'{s} named {r[1]}/{r[2]} exact {r[0]}' for s, r in report.items())
            print(f'step {step} loss {running:.3f} {time.time() - started:.0f}s | {line}', flush=True)
            score = sum(r[1] for r in report.values()) + sum(r[0] for r in report.values()) / 1000
            if score > best:
                best = score
                save(model, width, tag)
        if step >= steps:
            break
    print(f'{tag}: best checkpoint saved to {checkpoint(tag)}')


if __name__ == '__main__':
    main()
