#!/usr/bin/env bash
set -euo pipefail

IMAGE="${RUNEHELPER_OCR_IMAGE:-runehelper-deps:ci}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="${RUNEHELPER_ML:-$HOME/.cache/runehelper-ml}"
LANGUAGE="${RUNEHELPER_LANGUAGE:-en}"

if [ "$LANGUAGE" = "en" ]; then
    panels=tests/panels
    out=real
    model=/src/RuneHelper/resources/text_model.bin
else
    panels="tests/$LANGUAGE/panels"
    out="$LANGUAGE/real"
    model="/src/RuneHelper/resources/text_model_$LANGUAGE.bin"
fi

if [ -n "${RUNEHELPER_MODEL:-}" ]; then
    model="/work/$RUNEHELPER_MODEL"
fi

if ! docker image inspect "$IMAGE" > /dev/null 2>&1; then
    echo "$IMAGE is missing, run tools/ocr-test.sh once to build it"
    exit 1
fi

mkdir -p "$WORK/build" "$WORK/$out"

docker run --rm \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -v "$ROOT:/src:ro" \
    -v "$WORK:/work" \
    "$IMAGE" bash -lc "
        set -eo pipefail
        cmake -S /src -B /work/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DRUNEHELPER_BUILD_TESTS=ON > /work/build/configure.log 2>&1 ||
            { tail -20 /work/build/configure.log; exit 1; }
        cmake --build /work/build --target text_model_crops > /work/build/build.log 2>&1 ||
            { grep -E 'error:' /work/build/build.log | head -20; exit 1; }
        /work/build/tests/text_model_crops $model /src/$panels /work/$out"
