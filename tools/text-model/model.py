import math
from dataclasses import dataclass

import cv2
import numpy as np
import torch
import torch.nn as nn

from common import CHARSET, checkpoint

INPUT_HEIGHT = 24
MIN_INPUT_WIDTH = 8


@dataclass(frozen=True)
class Layer:
    channels: int
    kernel: tuple[int, int]
    padding: tuple[int, int]
    pool: tuple[int, int] = (1, 1)


FEATURES = (
    Layer(16, (3, 3), (1, 1), pool=(2, 2)),
    Layer(32, (3, 3), (1, 1), pool=(2, 2)),
    Layer(48, (3, 3), (1, 1), pool=(2, 1)),
    Layer(64, (3, 3), (1, 1)),
    Layer(96, (3, 1), (0, 0)),
)
SEQUENCE = (
    Layer(96, (1, 3), (0, 1)),
    Layer(96, (1, 3), (0, 1)),
)
STRIDE = math.prod(layer.pool[1] for layer in FEATURES + SEQUENCE)


def scaled(channels, width):
    return max(8, int(round(channels * width)))


class Net(nn.Module):
    def __init__(self, classes, width=1.0):
        super().__init__()
        features = []
        channels = 1
        for layer in FEATURES:
            out = scaled(layer.channels, width)
            features += [nn.Conv2d(channels, out, layer.kernel, padding=layer.padding, bias=False), nn.BatchNorm2d(out), nn.ReLU(inplace=True)]
            if layer.pool != (1, 1):
                features.append(nn.MaxPool2d(layer.pool, layer.pool))
            channels = out
        sequence = []
        for layer in SEQUENCE:
            out = scaled(layer.channels, width)
            sequence += [nn.Conv1d(channels, out, layer.kernel[1], padding=layer.padding[1], bias=False), nn.BatchNorm1d(out), nn.ReLU(inplace=True)]
            channels = out
        self.features = nn.Sequential(*features)
        self.sequence = nn.Sequential(*sequence)
        self.classifier = nn.Conv1d(channels, classes, 1)

    def forward(self, x):
        features = self.features(x).squeeze(2)
        return self.classifier(self.sequence(features))


def load(tag, device):
    saved = torch.load(checkpoint(tag), map_location=device)
    charset = saved.get('charset', CHARSET)
    model = Net(len(charset) + 1, saved['width']).to(device)
    model.load_state_dict(saved['state'])
    model.charset = charset
    return model, saved['width']


def transfer(model, tag, device):
    saved = torch.load(checkpoint(tag), map_location=device)
    state = model.state_dict()
    for key, value in saved['state'].items():
        if not key.startswith('classifier.') and state[key].shape == value.shape:
            state[key] = value
    source = {symbol: index + 1 for index, symbol in enumerate(saved['charset'])}
    for key in ('classifier.weight', 'classifier.bias'):
        state[key][0] = saved['state'][key][0]
        for index, symbol in enumerate(model.charset, 1):
            if symbol in source:
                state[key][index] = saved['state'][key][source[symbol]]
    model.load_state_dict(state)


def save(model, width, tag):
    torch.save({'state': model.state_dict(), 'width': width, 'charset': model.charset}, checkpoint(tag))


def prepare(gray):
    height, width = gray.shape
    scale = INPUT_HEIGHT / height
    target = (max(MIN_INPUT_WIDTH, int(round(width * scale))), INPUT_HEIGHT)
    method = cv2.INTER_AREA if scale < 1.0 else cv2.INTER_CUBIC
    resized = cv2.resize(gray, target, interpolation=method).astype(np.float32)
    low, high = np.percentile(resized, 2), np.percentile(resized, 98)
    span = max(8.0, high - low)
    return np.clip((resized - low) / span, 0.0, 1.0)


def padded_width(width):
    return int(math.ceil(width / STRIDE) * STRIDE)


def read(model, device, gray):
    image = prepare(gray)
    padded = np.ones((1, 1, INPUT_HEIGHT, padded_width(image.shape[1])), np.float32)
    padded[0, 0, :, : image.shape[1]] = image
    with torch.no_grad():
        logits = model(torch.from_numpy(padded).to(device))[0, :, : image.shape[1] // STRIDE]
    best = torch.softmax(logits.double(), dim=0).max(0)
    text, total, previous = [], 0.0, 0
    for index, value in zip(best.indices.tolist(), best.values.tolist()):
        if index != 0 and index != previous:
            text.append(model.charset[index - 1])
            total += value
        previous = index
    return ''.join(text), 100.0 * total / len(text) if text else 0.0
