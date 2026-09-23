#!/usr/bin/env bash
set -euo pipefail

IMAGE="${RUNEHELPER_TIDY_IMAGE:-runehelper-tidy:ci}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage()
{
    cat <<USAGE
usage: $(basename "$0") [--fix] [file ...]

  (no flags)  report clang-tidy findings for the RuneHelper sources
  --fix       apply the fixes clang-tidy can make on its own
  file ...    limit the run to these paths, relative to the repository root

The checks live in .clang-tidy. The image is built from the Dockerfile deps
stage plus clang-tidy on first use; every run after that takes about a minute.
USAGE
    exit 1
}

fix=0
targets=()

for arg in "$@"; do
    case "$arg" in
        --fix) fix=1 ;;
        -*)    usage ;;
        *)     targets+=("$arg") ;;
    esac
done

if ! docker image inspect "$IMAGE" > /dev/null 2>&1; then
    echo "building $IMAGE, this only happens once"
    docker build --target deps -t runehelper-tidy:deps "$ROOT"
    docker build -t "$IMAGE" - <<DOCKERFILE
FROM runehelper-tidy:deps
RUN apt-get update && apt-get install -y --no-install-recommends clang-tidy-15 libxext-dev \
    && ln -sf /usr/bin/clang-tidy-15 /usr/bin/clang-tidy \
    && rm -rf /var/lib/apt/lists/*
DOCKERFILE
fi

selector=""

if [ "${#targets[@]}" -gt 0 ]; then
    selector="$(printf '/src/%s\n' "${targets[@]}")"
fi

docker run --rm \
    -v "$ROOT:/src" \
    -e TIDY_FIX="$fix" \
    -e TIDY_TARGETS="$selector" \
    "$IMAGE" bash -lc '
        set -e

        for backend in x11 wayland; do
            cmake -S /src -B "/tmp/tidy-$backend" -G Ninja \
                -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
                -DCMAKE_BUILD_TYPE=Release \
                -DRUNEHELPER_LINUX_BACKEND="$backend" > "/tmp/configure-$backend.log" 2>&1 ||
                { echo "configure failed for $backend"; tail -20 "/tmp/configure-$backend.log"; exit 1; }
        done

        ninja -C /tmp/tidy-wayland \
            wayland-protocols/wlr-screencopy-unstable-v1-client-protocol.h \
            wayland-protocols/wlr-layer-shell-unstable-v1-client-protocol.h \
            wayland-protocols/xdg-output-unstable-v1-client-protocol.h \
            wayland-protocols/xdg-shell-client-protocol.h > /tmp/protocols.log 2>&1 ||
            { echo "could not generate the wayland protocol headers"; tail -20 /tmp/protocols.log; exit 1; }

        cat > /tmp/pick.py <<"PY"
import json, os, sys

def files_in(tree):
    path = os.path.join(tree, "compile_commands.json")
    return {e["file"] for e in json.load(open(path))}

x11 = files_in("/tmp/tidy-x11")
wayland = files_in("/tmp/tidy-wayland")

requested = [line.strip() for line in sys.stdin if line.strip()]

if not requested:
    requested = sorted(x11 | wayland)

requested = [f for f in requested if "/src/RuneHelper/" in f]

buckets = {"x11": [], "wayland": [], "": []}

for f in requested:
    if f in x11:
        buckets["x11"].append(f)
    elif f in wayland:
        buckets["wayland"].append(f)
    else:
        buckets[""].append(f)

for backend in ("x11", "wayland"):
    with open("/tmp/files-%s.txt" % backend, "w") as out:
        out.write("\n".join(buckets[backend]))

if buckets[""]:
    print("not in any compilation database:", file=sys.stderr)
    for f in buckets[""]:
        print("  " + f.replace("/src/", ""), file=sys.stderr)
    sys.exit(1)
PY

        printf "%s" "$TIDY_TARGETS" | python3 /tmp/pick.py

        extra=""
        [ "$TIDY_FIX" = "1" ] && extra="--fix"

        : > /tmp/report.txt

        for backend in x11 wayland; do
            [ -s "/tmp/files-$backend.txt" ] || continue

            xargs -a "/tmp/files-$backend.txt" -P "$(nproc)" -I{} \
                clang-tidy -p "/tmp/tidy-$backend" --quiet $extra {} 2>/dev/null >> /tmp/report.txt || true
        done

        sed "s|/src/||" /tmp/report.txt

        warnings=$(grep -c "warning:" /tmp/report.txt || true)
        errors=$(grep -c "error:" /tmp/report.txt || true)

        echo
        echo "$warnings warnings, $errors errors"

        [ "$warnings" = "0" ] && [ "$errors" = "0" ]'
