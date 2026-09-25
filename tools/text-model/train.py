import argparse
import time
from dataclasses import dataclass

import torch
from torch.utils.data import IterableDataset, get_worker_info

from common import CHARSET, SCENES, Matcher, checkpoint, has_real, load_real
from model import Net, prepare, read, save, transfer
from synth import Synth
from training import Optimiser, batches

LEARNING_RATE = 3e-3
REPORT_EVERY = 1000


@dataclass
class SceneResult:
    exact: int
    named: int
    total: int


class SynthStream(IterableDataset):
    def __init__(self, seed):
        self.seed = seed

    def __iter__(self):
        info = get_worker_info()
        synth = Synth(self.seed * 1000 + (info.id if info else 0))
        while True:
            gray, label = synth.sample()
            yield prepare(gray), label


def evaluate(model, device, sets, matcher):
    model.eval()
    results = {}
    for scene, samples in sets.items():
        exact = named = 0
        for _, gray, label in samples:
            text, _ = read(model, device, gray)
            target = matcher.best(label)
            exact += text == label
            named += target is not None and matcher.best(text) == target
        results[scene] = SceneResult(exact, named, len(samples))
    model.train()
    return results


def checkpoint_score(results):
    named = sum(result.named for result in results.values())
    exact = sum(result.exact for result in results.values())
    return named + exact / 1000


def arguments():
    parser = argparse.ArgumentParser(description='Pretrain the text model on synthetic lines.')
    parser.add_argument('tag', nargs='?', default='synthetic', help='checkpoint name in the work folder')
    parser.add_argument('--steps', type=int, default=20000)
    parser.add_argument('--width', type=float, default=1.0, help='channel count multiplier')
    parser.add_argument('--seed', type=int, default=0)
    parser.add_argument('--init', help='checkpoint to start from; its layers and the symbols both charsets share are copied')
    parser.add_argument('--workers', type=int, default=12, help='processes that draw synthetic lines')
    return parser.parse_args()


def main():
    args = arguments()
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    torch.manual_seed(args.seed)
    model = Net(len(CHARSET) + 1, args.width).to(device)
    model.charset = CHARSET
    if args.init:
        transfer(model, args.init, device)
    print(f'{args.tag}: {sum(p.numel() for p in model.parameters())} parameters, {args.steps} steps on {device}', flush=True)
    loader = batches(SynthStream(1 + args.seed), model.charset, args.workers)
    optimiser = Optimiser(model, LEARNING_RATE, args.steps)
    sets = {scene: load_real(scene) for scene in SCENES} if has_real() else {}
    matcher = Matcher()
    best = -1.0
    started = time.time()
    running = 0.0
    for step, batch in enumerate(loader, 1):
        loss = optimiser.step(batch, device).item()
        running = 0.98 * running + 0.02 * loss if step > 1 else loss
        if step % REPORT_EVERY == 0 or step == args.steps:
            progress = f'step {step} loss {running:.3f} {time.time() - started:.0f}s'
            if not sets:
                print(f'{progress} | no labelled real crops yet, keeping the latest', flush=True)
                save(model, args.width, args.tag)
            else:
                results = evaluate(model, device, sets, matcher)
                scenes = ' '.join(f'{scene} named {r.named}/{r.total} exact {r.exact}' for scene, r in results.items())
                print(f'{progress} | {scenes}', flush=True)
                score = checkpoint_score(results)
                if score > best:
                    best = score
                    save(model, args.width, args.tag)
        if step >= args.steps:
            break
    print(f'{args.tag}: best checkpoint saved to {checkpoint(args.tag)}')


if __name__ == '__main__':
    main()
