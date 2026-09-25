from functools import partial

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader

from common import encode
from model import INPUT_HEIGHT, STRIDE, padded_width

BATCH_SIZE = 128
WEIGHT_DECAY = 1e-4
WARMUP_SHARE = 0.1
GRADIENT_CLIP = 5.0


def collate(batch, charset):
    width = padded_width(max(image.shape[1] for image, _ in batch))
    images = np.ones((len(batch), 1, INPUT_HEIGHT, width), np.float32)
    lengths, targets, target_lengths = [], [], []
    for i, (image, label) in enumerate(batch):
        images[i, 0, :, : image.shape[1]] = image
        lengths.append(image.shape[1] // STRIDE)
        encoded = encode(label, charset)
        targets.extend(encoded)
        target_lengths.append(len(encoded))
    return torch.from_numpy(images), torch.tensor(lengths), torch.tensor(targets), torch.tensor(target_lengths)


def batches(stream, charset, workers):
    return DataLoader(
        stream,
        batch_size=BATCH_SIZE,
        num_workers=workers,
        collate_fn=partial(collate, charset=charset),
        prefetch_factor=4,
        persistent_workers=True,
    )


class Optimiser:
    def __init__(self, model, learning_rate, steps):
        self.model = model
        self.optimizer = torch.optim.AdamW(model.parameters(), lr=learning_rate, weight_decay=WEIGHT_DECAY)
        self.schedule = torch.optim.lr_scheduler.OneCycleLR(self.optimizer, max_lr=learning_rate, total_steps=steps, pct_start=WARMUP_SHARE)
        self.ctc = nn.CTCLoss(blank=0, zero_infinity=True)

    def step(self, batch, device):
        images, lengths, targets, target_lengths = batch
        logits = self.model(images.to(device, non_blocking=True))
        loss = self.ctc(F.log_softmax(logits, dim=1).permute(2, 0, 1), targets, lengths, target_lengths)
        self.optimizer.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(self.model.parameters(), GRADIENT_CLIP)
        self.optimizer.step()
        self.schedule.step()
        return loss
