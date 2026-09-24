import struct
import sys
from pathlib import Path

import torch

from common import MODEL
from train import load

MAGIC = b'RHOCR2\0\0'


def folded(conv, norm):
    weight = conv.weight.detach().double()
    scale = norm.weight.detach().double() / torch.sqrt(norm.running_var.detach().double() + norm.eps)
    bias = norm.bias.detach().double() - norm.running_mean.detach().double() * scale
    shape = [-1] + [1] * (weight.dim() - 1)
    return (weight * scale.view(shape)).float(), bias.float()


def layers(model):
    f = model.features
    s = model.sequence
    pairs = [(f[0], f[1]), (f[4], f[5]), (f[8], f[9]), (f[12], f[13]), (f[15], f[16]), (s[0], s[1]), (s[3], s[4])]
    out = [folded(conv, norm) for conv, norm in pairs]
    out.append((model.classifier.weight.detach().float(), model.classifier.bias.detach().float()))
    return out


def main():
    tag = sys.argv[1]
    target = Path(sys.argv[2]) if len(sys.argv) > 2 else MODEL
    model, _ = load(tag, torch.device('cpu'))
    model.eval()
    with open(target, 'wb') as handle:
        handle.write(MAGIC)
        handle.write(struct.pack('<I', len(model.charset)))
        for symbol in model.charset:
            encoded = symbol.encode('utf-8')
            handle.write(struct.pack('<B', len(encoded)))
            handle.write(encoded)
        tensors = layers(model)
        handle.write(struct.pack('<I', len(tensors)))
        for weight, bias in tensors:
            if weight.dim() == 3:
                weight = weight.unsqueeze(2)
            handle.write(struct.pack('<4I', *weight.shape))
            handle.write(weight.contiguous().numpy().astype('<f4').tobytes())
            handle.write(bias.contiguous().numpy().astype('<f4').tobytes())
    print(target, target.stat().st_size, 'bytes')


if __name__ == '__main__':
    main()
