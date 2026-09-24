#!/usr/bin/env bash
set -euo pipefail

IMAGE="${RUNEHELPER_FORMAT_IMAGE:-runehelper-format:ci}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage()
{
    cat <<USAGE
usage: $(basename "$0") [--fix] [file ...]

  (no flags)  report which files clang-format would change, and how
  --fix       rewrite the files in place
  file ...    limit the run to these paths, relative to the repository root

The style lives in .clang-format. The image is a small Ubuntu with
clang-format-15 and is built on first use.
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
    docker build -t "$IMAGE" - <<DOCKERFILE
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y --no-install-recommends clang-format-15 \
    && ln -sf /usr/bin/clang-format-15 /usr/bin/clang-format \
    && rm -rf /var/lib/apt/lists/*
DOCKERFILE
fi

selector=""

if [ "${#targets[@]}" -gt 0 ]; then
    selector="$(printf '%s\n' "${targets[@]}")"
fi

mount_mode=ro
[ "$fix" -eq 1 ] && mount_mode=rw

docker run --rm \
    --user "$(id -u):$(id -g)" \
    -v "$ROOT:/src:$mount_mode" \
    -e FORMAT_FIX="$fix" \
    -e FORMAT_TARGETS="$selector" \
    "$IMAGE" bash -lc '
        set -e
        cd /src

        if [ -n "$FORMAT_TARGETS" ]; then
            echo "$FORMAT_TARGETS" > /tmp/files.txt
        else
            find RuneHelper tests tools -name "*.cpp" -o -name "*.h" | sort > /tmp/files.txt
        fi

        if [ "$FORMAT_FIX" = "1" ]; then
            xargs -a /tmp/files.txt clang-format -i
            echo "formatted $(wc -l < /tmp/files.txt) files"
            exit 0
        fi

        dirty=0

        while read -r file; do
            [ -n "$file" ] || continue

            if ! diff -q "$file" <(clang-format "$file") > /dev/null; then
                added=$(diff "$file" <(clang-format "$file") | grep -c "^>" || true)
                removed=$(diff "$file" <(clang-format "$file") | grep -c "^<" || true)
                printf "  %-60s %s\n" "$file" "+$added/-$removed"
                dirty=$((dirty + 1))
            fi
        done < /tmp/files.txt

        echo
        echo "$dirty of $(wc -l < /tmp/files.txt) files need formatting"

        [ "$dirty" = "0" ]'
