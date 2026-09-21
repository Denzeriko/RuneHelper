#!/usr/bin/env bash
set -euo pipefail

IMAGE="${RUNEHELPER_OCR_IMAGE:-runehelper-deps:ci}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage()
{
    cat <<EOF
usage: $(basename "$0") [--bless] [--tsan]

  (no flags)  compare OCR output on tests/panels against tests/golden and
              report accuracy against the labels in tests/truth
  --bless     overwrite tests/golden with the current output
  --tsan      run the comparison under ThreadSanitizer

The pinned dependency image is built from the Dockerfile deps stage on first
use, which compiles OpenCV, Leptonica and Tesseract and takes a while. Every
run after that takes a few seconds.
EOF
    exit 1
}

bless=0
tsan=0

for arg in "$@"; do
    case "$arg" in
        --bless) bless=1 ;;
        --tsan)  tsan=1 ;;
        *)       usage ;;
    esac
done

if ! docker image inspect "$IMAGE" > /dev/null 2>&1; then
    echo "building $IMAGE from the Dockerfile deps stage, this only happens once"
    docker build --target deps -t "$IMAGE" "$ROOT"
fi

if [ "$tsan" -eq 1 ]; then
    build_dir=/tmp/ocr-tsan
    configure="-DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS='-fsanitize=thread -g' -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=thread'"
else
    build_dir=/tmp/ocr-build
    configure="-DCMAKE_BUILD_TYPE=Release"
fi

if [ "$bless" -eq 1 ]; then
    mount_mode=rw
    action="cmake --build $build_dir --target bless_ocr_golden"
else
    mount_mode=ro
    action="ctest --test-dir $build_dir -V | sed -e 's/^[0-9]*: //' -e '/^Test command:/d' -e '/^Test timeout/d'"
fi

docker run --rm \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -e TSAN_OPTIONS=halt_on_error=1 \
    -v "$ROOT:/src:$mount_mode" \
    "$IMAGE" bash -lc "
        set -eo pipefail
        cmake -S /src -B $build_dir -G Ninja $configure -DRUNEHELPER_BUILD_TESTS=ON > /tmp/configure.log 2>&1 ||
            { tail -20 /tmp/configure.log; exit 1; }
        cmake --build $build_dir --target ocr_golden > /tmp/build.log 2>&1 ||
            { grep -E 'error:' /tmp/build.log | head -20; exit 1; }
        $action"
