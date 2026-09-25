#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

sizes=(16 20 24 32 40 48 64 96 128 256)

for size in "${sizes[@]}"; do
    source="$ROOT/assets/icon.svg"
    if [ "$size" -le 20 ]; then
        source="$ROOT/assets/icon-small.svg"
    fi
    rsvg-convert -w "$size" -h "$size" "$source" -o "$WORK/$size.png"
done

python3 - "$WORK" "$ROOT/RuneHelper/resources/runehelper.ico" "${sizes[@]}" <<'PY'
import struct
import sys
from pathlib import Path

work, target, sizes = Path(sys.argv[1]), Path(sys.argv[2]), [int(size) for size in sys.argv[3:]]
frames = [(size, (work / f'{size}.png').read_bytes()) for size in sizes]

offset = 6 + 16 * len(frames)
directory = struct.pack('<HHH', 0, 1, len(frames))
data = b''

for size, png in frames:
    side = 0 if size >= 256 else size
    directory += struct.pack('<BBBBHHII', side, side, 0, 0, 1, 32, len(png), offset + len(data))
    data += png

target.write_bytes(directory + data)
PY

echo "RuneHelper/resources/runehelper.ico: ${sizes[*]} px"
