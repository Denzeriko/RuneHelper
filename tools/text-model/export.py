import argparse
import struct
from pathlib import Path

import torch
import torch.nn as nn

from common import MODEL
from model import INPUT_HEIGHT, load

MAGIC = b'RHOCR3\0\0'


def folded(conv, norm):
    weight = conv.weight.detach().double()
    scale = norm.weight.detach().double() / torch.sqrt(norm.running_var.detach().double() + norm.eps)
    bias = norm.bias.detach().double() - norm.running_mean.detach().double() * scale
    shape = [-1] + [1] * (weight.dim() - 1)
    return (weight * scale.view(shape)).float(), bias.float()


def pair(value):
    return (value, value) if isinstance(value, int) else tuple(value)


def exported_layers(model):
    modules = [*model.features, *model.sequence]
    layers = []
    for index, conv in enumerate(modules):
        if not isinstance(conv, (nn.Conv1d, nn.Conv2d)):
            continue
        weight, bias = folded(conv, modules[index + 1])
        pool = modules[index + 3] if index + 3 < len(modules) and isinstance(modules[index + 3], nn.MaxPool2d) else None
        padding = pair(conv.padding) if isinstance(conv, nn.Conv2d) else (0, conv.padding[0])
        layers.append((weight, bias, padding, pair(pool.kernel_size) if pool is not None else (1, 1), True))
    classifier = model.classifier
    layers.append((classifier.weight.detach().float(), classifier.bias.detach().float(), (0, 0), (1, 1), False))
    return layers


def write(model, target):
    with open(target, 'wb') as handle:
        handle.write(MAGIC)
        handle.write(struct.pack('<I', INPUT_HEIGHT))
        handle.write(struct.pack('<I', len(model.charset)))
        for symbol in model.charset:
            encoded = symbol.encode('utf-8')
            handle.write(struct.pack('<B', len(encoded)))
            handle.write(encoded)
        layers = exported_layers(model)
        handle.write(struct.pack('<I', len(layers)))
        for weight, bias, padding, pool, relu in layers:
            if weight.dim() == 3:
                weight = weight.unsqueeze(2)
            handle.write(struct.pack('<4I', *weight.shape))
            handle.write(struct.pack('<5I', *padding, *pool, int(relu)))
            handle.write(weight.contiguous().numpy().astype('<f4').tobytes())
            handle.write(bias.contiguous().numpy().astype('<f4').tobytes())


def main():
    parser = argparse.ArgumentParser(description='Write a checkpoint as the model file LineReader loads.')
    parser.add_argument('tag', help='checkpoint name in the work folder, for example synthetic_all')
    parser.add_argument('output', nargs='?', type=Path, default=MODEL, help='defaults to the language model file in RuneHelper/resources')
    args = parser.parse_args()
    model, _ = load(args.tag, torch.device('cpu'))
    model.eval()
    write(model, args.output)
    print(args.output, args.output.stat().st_size, 'bytes')


if __name__ == '__main__':
    main()
