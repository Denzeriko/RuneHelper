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
        cmake -S /src -B /tmp/tidy -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Release > /tmp/configure.log 2>&1 ||
            { tail -20 /tmp/configure.log; exit 1; }

        if [ -n "$TIDY_TARGETS" ]; then
            echo "$TIDY_TARGETS" > /tmp/files.txt
        else
            python3 -c "
import json
db = json.load(open(\"/tmp/tidy/compile_commands.json\"))
files = sorted({e[\"file\"] for e in db if \"/src/RuneHelper/\" in e[\"file\"]})
open(\"/tmp/files.txt\", \"w\").write(\"\n\".join(files))
"
        fi

        extra=""
        [ "$TIDY_FIX" = "1" ] && extra="--fix"

        xargs -a /tmp/files.txt -P "$(nproc)" -I{} \
            clang-tidy -p /tmp/tidy --quiet $extra {} 2>/dev/null > /tmp/report.txt || true

        sed "s|/src/||" /tmp/report.txt

        count=$(grep -c "warning:" /tmp/report.txt || true)
        echo
        echo "$count warnings"
        [ "$count" = "0" ]'
