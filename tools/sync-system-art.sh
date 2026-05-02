#!/usr/bin/env bash
# Sync per-system logo + splash artwork from a local clone of
# anthonycaccese/art-book-next-es-de into foyer's romfs.
#
# Usage:
#   tools/sync-system-art.sh                # all systems
#   tools/sync-system-art.sh nes snes gb    # only the listed system folders
#
# Requires: librsvg2-bin (rsvg-convert).
#
# Source license: CC-BY-NC-SA 2.0 — see assets/romfs/systems/LICENSE-ART.md.

set -euo pipefail

ABN_REPO="${ABN_REPO:-$HOME/art-book-next-es-de}"
DST="$(cd "$(dirname "$0")/.." && pwd)/assets/romfs/systems"
SRC="$ABN_REPO/_inc/systems"

if [ ! -d "$SRC/artwork" ] || [ ! -d "$SRC/logos" ]; then
    echo "Set ABN_REPO to a clone of art-book-next-es-de (current: $ABN_REPO)." >&2
    echo "  git clone --depth 1 https://github.com/anthonycaccese/art-book-next-es-de \\" >&2
    echo "      \"$ABN_REPO\"" >&2
    exit 1
fi
command -v rsvg-convert >/dev/null || {
    echo "rsvg-convert not found — install librsvg2-bin." >&2
    exit 1
}

mkdir -p "$DST"

# Build the set of system folders to sync. Default: every artwork PNG and
# logo SVG that has a name match (skipping leading-underscore meta files).
if [ "$#" -gt 0 ]; then
    SYSTEMS=("$@")
else
    SYSTEMS=()
    while IFS= read -r f; do
        n=$(basename "$f" .png)
        [[ "$n" == _* ]] && continue
        SYSTEMS+=("$n")
    done < <(find "$SRC/artwork" -maxdepth 1 -name '*.png')
fi

n_splash=0
n_logo=0
n_skip=0
for sys in "${SYSTEMS[@]}"; do
    splash_src="$SRC/artwork/$sys.png"
    logo_src="$SRC/logos/$sys.svg"
    have_any=0
    if [ -f "$splash_src" ]; then
        cp "$splash_src" "$DST/$sys-splash.png"
        n_splash=$((n_splash + 1))
        have_any=1
    fi
    if [ -f "$logo_src" ]; then
        rsvg-convert -w 755 -h 240 -a "$logo_src" -o "$DST/$sys.png"
        n_logo=$((n_logo + 1))
        have_any=1
    fi
    [ "$have_any" -eq 0 ] && {
        echo "  skip $sys — no upstream artwork or logo" >&2
        n_skip=$((n_skip + 1))
    }
done

echo "Synced: $n_splash splash, $n_logo logos. Skipped: $n_skip."
echo "Don't forget to commit assets/romfs/systems/ + LICENSE-ART.md."
