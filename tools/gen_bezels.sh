#!/bin/bash
# Generates the bundled bezel set for foyer's browser romfs.
#
# Pulls real CC-BY 4.0 source art from libretro/common-overlays — the
# Game Boy console-shape, the GBA console-shape, NES Super Mario 2
# integer border, SNES Link to the Past border, and the generic CRT
# TV bezel — and adapts them to foyer's 1280×720 framebuffer with
# per-system color variations so every supported console gets a
# differentiated bezel out of the box.
#
# Output: assets/romfs/bezels/<foyer_system_folder>.png
#
# Per-system bezels are indexed PNGs (32-color palette) for size; the
# generic tv-integer base lands at ~150 KiB each instead of ~880 KiB
# truecolor. Total payload ~5 MiB across all 24 systems.
#
# Source attribution lives in assets/romfs/bezels/LICENSE.
#
# Requires: git, ImageMagick `convert`.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/assets/romfs/bezels"
SRC="$(mktemp -d)"
trap 'rm -rf "$SRC"' EXIT

echo "[1/3] cloning libretro/common-overlays ..."
git clone --depth 1 --filter=blob:none --quiet \
    https://github.com/libretro/common-overlays.git "$SRC"

mkdir -p "$OUT"
W=1280
H=720

# Render <foyer_folder>.png directly from a source path. Used when we
# have system-specific art that already looks right.
import_direct() {
    local folder="$1"; local rel="$2"; local quality="${3:-32}"
    local src="$SRC/$rel"
    if [ ! -f "$src" ]; then echo "  ! missing $rel" >&2; return; fi
    convert "$src" \
        -resize ${W}x${H} \
        -background 'rgba(0,0,0,0)' -gravity center -extent ${W}x${H} \
        -colors "$quality" -strip "PNG8:$OUT/${folder}.png"
    printf '  + %-15s  <- %s\n' "$folder" "$rel"
}

# Tint the generic TV bezel with a system-accent color and burn the
# system label in the bottom-right. The tint nudges the metallic frame
# toward the brand color without obliterating the underlying CRT-TV
# look, so each console reads as "the TV with a little bit of
# nintendo/sega/atari character".
#
# args: folder display_name tint_hex
import_tinted_tv() {
    local folder="$1"; local label="$2"; local tint="$3"
    convert "$SRC/borders/img/tv-integer.png" \
        -resize ${W}x${H} \
        -background 'rgba(0,0,0,0)' -gravity center -extent ${W}x${H} \
        \( -clone 0 -fill "$tint" -colorize 35% \) \
        -compose Over -composite \
        -fill 'rgba(220,220,220,0.55)' -font DejaVu-Sans -pointsize 22 \
        -gravity SouthEast -annotate +24+16 "$label" \
        -colors 64 -strip "PNG8:$OUT/${folder}.png"
    printf '  + %-15s  (tinted, %s)\n' "$folder" "$tint"
}

echo "[2/3] generating per-system bezels ..."

# Real system-themed art from common-overlays.
import_direct  nes          borders/img/nes-smb2-integer.png       64
import_direct  snes         borders/img/snes-lttp.png              64
import_direct  gb           borders/img/gb.png                     64
import_direct  gbc          borders/img/gb.png                     64
import_direct  gba          borders/img/gba-4k.png                 64

# Animated-border main panels — these are real, themed full-screen
# backgrounds rather than the generic TV.
import_direct  genesis      borders/animated-border/img-md/main_retroarch.png   64
import_direct  megadrive    borders/animated-border/img-md/main_retroarch.png   64
import_direct  psx          borders/animated-border/img-psx/main_body_lights.png 64

# Tinted CRT-TV variations for systems where common-overlays doesn't
# carry dedicated art. Tints are deliberately subtle so the CRT
# remains the dominant visual and labels stay readable.
import_tinted_tv  n64          "Nintendo 64"          "#3b6"
import_tinted_tv  nds          "Nintendo DS"          "#88f"
import_tinted_tv  gc           "GameCube"             "#638"
import_tinted_tv  mastersystem "Master System"        "#a22"
import_tinted_tv  gamegear     "Game Gear"            "#a22"
import_tinted_tv  saturn       "Sega Saturn"          "#226"
import_tinted_tv  dc           "Dreamcast"            "#e85"
import_tinted_tv  psp          "PSP"                  "#445"
import_tinted_tv  ngp          "Neo Geo Pocket"       "#67c"
import_tinted_tv  ngpc         "Neo Geo Pocket Color" "#67c"
import_tinted_tv  atari2600    "Atari 2600"           "#742"
import_tinted_tv  atari7800    "Atari 7800"           "#888"
import_tinted_tv  atarilynx    "Atari Lynx"           "#a64"
import_tinted_tv  32x          "Sega 32X"             "#911"
import_tinted_tv  segacd       "Sega CD"              "#911"

# Catch-all for anything bezel.cpp can't resolve (unknown system,
# missing per-system file). Stays as the untinted CRT TV so user-
# folded packs continue to work uniformly.
import_direct    default      borders/img/tv-integer.png           64

echo "[3/3] writing LICENSE ..."
cat > "$OUT/LICENSE" <<'EOF'
foyer bundles bezel art adapted from libretro/common-overlays
(https://github.com/libretro/common-overlays), licensed under the
Creative Commons Attribution 4.0 International license:

  https://creativecommons.org/licenses/by/4.0/

Source files used:
  - borders/img/nes-smb2-integer.png
  - borders/img/snes-lttp.png
  - borders/img/gb.png  (gb, gbc)
  - borders/img/gba-4k.png  (gba)
  - borders/img/tv-integer.png  (default, plus tinted variations)
  - borders/animated-border/img-md/main_retroarch.png  (genesis, megadrive)
  - borders/animated-border/img-psx/main_body_lights.png  (psx)

Attribution: libretro contributors. Foyer's adaptations (resize +
per-system tints + system labels) are released under the same
CC-BY-4.0 license.
EOF

echo
echo "wrote $(ls "$OUT"/*.png | wc -l) bezels under $OUT  ($(du -sh "$OUT" | cut -f1) total)"
