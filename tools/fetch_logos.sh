#!/usr/bin/env bash
#
# fetch_logos.sh — download per-system logo SVGs from
# anthonycaccese/art-book-next-es-de and render them to PNG so the
# brls system view can paint them as a tinted Image (TintableImage
# multiplies the texture by the theme's text colour at runtime —
# single white asset works for both light and dark themes).
#
# Run once after pulling new systems into system_db.cpp. Output PNGs
# are committed to assets/romfs/themes/foyer/systems/<folder>/logo.png
# so the build doesn't need rsvg-convert in CI.
#
# Requires: curl, rsvg-convert (librsvg2-tools).

set -euo pipefail

REPO_RAW="https://raw.githubusercontent.com/HVR88/Monochrome-Gaming-Logos/main/png"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASSETS_DIR="${SCRIPT_DIR}/../assets/romfs/themes/foyer/systems"
LOGO_W=480
LOGO_H=112

# foyer-folder → HVR88-svg-name (without .svg). HVR88's logos are
# monochrome white-on-transparent across the board so they downscale
# cleanly to the 240×56 we paint at — much sharper than the prior
# art-book-next set, which used decorative wordmarks that aliased
# into "weird stripes" on hardware.
#
# Systems with no good HVR88 match (pokemini, doom, quake) are
# omitted — fetch_logos.sh skips them with a warning and the
# system view falls back to no logo for those tiles.
declare -A REMAP=(
    [nes]=nintendo_nes
    [snes]=nintendo_snes
    [gb]=nintendo_gameboy
    [gbc]=nintendo_gameboy_color
    [gba]=nintendo_gameboy_advance
    [n64]=nintendo_64
    [nds]=nintendo_ds
    [gc]=nintendo_gamecube
    [genesis]=sega_genesis
    [megadrive]=sega_megadrive
    [mastersystem]=sega_master_system
    [gamegear]=sega_gamegear
    [saturn]=sega_saturn
    [dc]=sega_dreamcast
    [psx]=playstation_flat
    [psp]=playstation_psp
    [ngp]=snk_neogeo_pocket
    [ngpc]=snk_neogeo_pocket_color
    [atari2600]=atari_2600
    [atari7800]=atari_7800
    [atarilynx]=atari_lynx
    [32x]=sega_32x
    [segacd]=sega_cd
    [pcengine]=nec_pcengine
    [pcenginecd]=nec_pcengine_cdrom
    [supergrafx]=nec_pcengine_supergrafx
    [pcfx]=nec_pcfx
    [wonderswan]=bandai_wonderswan
    [wonderswancolor]=bandai_wonderswan_color
    [virtualboy]=nintendo_virtualboy
    [atarijaguar]=atari_jaguar
    [intellivision]=mattel_intellivision
    [gameandwatch]=handheld_game_and_watch
    [pico8]=pico-8
    [dos]=ms-dos
    [amstradcpc]=amstrad_cpc
    [c64]=commodore_64
    [atari800]=atari_800
    [atari5200]=atari_5200
    [msx]=msx
    [msx2]=msx2
    [3do]=3do
    [arcade]=arcade
    [scummvm]=scummvm
    [amiga]=commodore_amiga
    [amiga600]=commodore_amiga
    [amiga1200]=commodore_amiga
    [amigacd32]=commodore_amiga_cd32
    [cdtv]=commodore_cdtv
)

# Foyer's full kSystems[] folder list. Keep in sync with
# shared/library/system_db.cpp. Anything not present in art-book is
# silently skipped with a warning.
SYSTEMS=(
    nes snes gb gbc gba n64 nds gc
    genesis megadrive mastersystem gamegear
    saturn dc psx psp
    ngp ngpc
    atari2600 atari7800 atarilynx
    32x segacd
    pcengine pcenginecd supergrafx pcfx
    wonderswan wonderswancolor virtualboy
    atarijaguar pokemini intellivision gameandwatch
    doom quake pico8 dos
    amstradcpc c64 atari800 atari5200
    msx msx2
    3do arcade scummvm
    amiga amiga600 amiga1200 amigacd32 cdtv
)

command -v curl >/dev/null    || { echo "curl missing" >&2; exit 1; }
command -v magick >/dev/null  || { echo "magick missing (install ImageMagick)" >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

mkdir -p "${ASSETS_DIR}"

for sys in "${SYSTEMS[@]}"; do
    src="${REMAP[$sys]:-$sys}"
    url="${REPO_RAW}/${src}.png"
    src_png="${TMP}/${sys}.png"
    out_dir="${ASSETS_DIR}/${sys}"

    if ! curl -fsSL "$url" -o "$src_png"; then
        echo "warn: ${sys} (${src}.png) not found upstream — skipped" >&2
        continue
    fi

    mkdir -p "${out_dir}"

    # Source PNGs are white-on-transparent at ~600×226. Resize with
    # Lanczos to preserve sharp edges, preserving the alpha channel
    # so the backdrop shows through.
    #
    # Dark variant = source as-is (white pixels for dark theme).
    # Light variant = RGB-channel inverted (white → black, alpha
    # preserved) for paint on light backgrounds.
    magick "$src_png" \
        -resize "${LOGO_W}x${LOGO_H}" \
        -background none \
        "${out_dir}/logo_dark.png"

    magick "${out_dir}/logo_dark.png" \
        -channel RGB -negate +channel \
        "${out_dir}/logo_light.png"

    rm -f "${out_dir}/logo.png"

    echo "wrote ${sys}/logo_{dark,light}.png"
done

echo "done."
