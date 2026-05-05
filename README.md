# foyer

Native libretro frontend for Nintendo Switch (CFW only). Inspired by
EmulationStation-DE and Tico. Runs libretro cores in-process via per-system
player nros, with a browser nro for picking systems and games.

Features delivered so far:

- ES-DE-style browser: home carousel + per-system game list + metadata sidebar,
  faded game backdrops, console logo tiles, persistent topbar/bottombar with
  button glyph hints, modal `+ Menu` popup (Rescan / Settings / Exit)
- Per-system player binaries that boot the rom directly (`foyer-<core>.nro`)
- Pause overlay (`− +` together): 10 timestamped save-state slots, load-state
  picker, aspect ratio (4:3, 16:9, Core, Stretch, Integer 1×/2×/Auto), per-core
  libretro options menu (persists to `/foyer/config/cores/<core>.jsonc`), Quit
- Audio (audrv stream at the core's sample rate)
- "Continue" row on Game Detail auto-resumes the most recent save state
- Box-art scrapers: libretro-thumbnails (free), ScreenScraper (auth),
  SteamGridDB (auth) — picked by the value in `general.jsonc`
- RetroAchievements via [rcheevos](https://github.com/RetroAchievements/rcheevos)
  — login from `accounts.jsonc`, unlock toasts, server-side reporting
- Tico-style two-column Settings (sidebar + content) with eight categories:
  General, Display, Audio, Library, Emulator, Accounts, Updates, Experimental
- Pluggable themes loaded from `/foyer/config/themes/<name>.jsonc`
- libhaze MTP server scoped to `/foyer/roms` only — drop roms over USB without
  exposing the rest of the SD card. Auto-start toggle in Experimental.
- Multi-core architecture: per-game override + per-system default + system_db
  ordering, all editable from the in-app Game Detail and Settings views

## Layout on SD

```
/switch/foyer/foyer.nro              # browser (install here)
/foyer/
├── cores/
│   └── foyer-<core>.nro             # downloaded via Settings → Install Cores
├── themes/<name>/                   # theme packs (ES-DE-style — full art + palette)
│   ├── theme.jsonc                  # palette + metrics
│   ├── wallpaper.jpg                # global background
│   └── systems/<folder>/
│       ├── splash.jpg               # per-system fullscreen splash
│       └── logo.png                 # console logo
├── config/
│   ├── general.jsonc                # rom_root, preferred_scraper, theme, …
│   ├── accounts.jsonc               # ScreenScraper / SteamGridDB / RA creds
│   ├── per_game.jsonc               # per-rom core overrides
│   ├── themes/<name>.jsonc          # single-file palette overrides
│   └── cores/<core>.jsonc           # per-core libretro variables
├── data/
│   └── log.txt
├── assets/
│   ├── covers/<system>/<stem>.png      # box art shown in System view sidebar / Game Detail
│   ├── backgrounds/<system>/<stem>.jpg # full-screen backdrop behind System + Game Detail
│   └── systems/<system>.png            # console logo on the Home carousel tiles
├── roms/<system>/<file.ext>         # rom root (configurable)
├── saves/<system>/                  # libretro SRAM
├── states/<system>/<stem>.<slot>.state
└── system/<system>/                 # BIOS / firmware
```

## Supported systems / cores

| Folder         | Display                       | Cores (default first)            |
|----------------|-------------------------------|----------------------------------|
| `nes`          | Nintendo Entertainment System | `fceumm` / `nestopia` / `mesen`  |
| `snes`         | Super Nintendo                | `snes9x`                         |
| `gb`           | Game Boy                      | `gambatte` / `sameboy`           |
| `gbc`          | Game Boy Color                | `gambatte` / `sameboy`           |
| `gba`          | Game Boy Advance              | `mgba` / `vba_next`              |
| `n64`          | Nintendo 64                   | `mupen64plus`                    |
| `nds`          | Nintendo DS                   | `melonds`                        |
| `gc`           | GameCube                      | `dolphin` (recipe TBD)           |
| `genesis`      | Sega Genesis                  | `genesisplusgx` / `genesis_plus_gx_wide` / `picodrive` |
| `megadrive`    | Sega Mega Drive               | `genesisplusgx` / `genesis_plus_gx_wide` / `picodrive` |
| `mastersystem` | Sega Master System            | `genesisplusgx` / `genesis_plus_gx_wide` / `picodrive` |
| `gamegear`     | Sega Game Gear                | `genesisplusgx` / `genesis_plus_gx_wide` / `picodrive` |
| `32x`          | Sega 32X                      | `picodrive`                      |
| `segacd`       | Sega CD                       | `picodrive`                      |
| `saturn`       | Sega Saturn                   | `yabasanshiro`                   |
| `dc`           | Dreamcast                     | `flycast`                        |
| `psx`          | PlayStation                   | `pcsx_rearmed` / `swanstation`   |
| `psp`          | PlayStation Portable          | `ppsspp` (recipe TBD)            |
| `ngp`          | Neo Geo Pocket                | `race` / `mednafen_ngp`          |
| `ngpc`         | Neo Geo Pocket Color          | `race` / `mednafen_ngp`          |
| `atari2600`    | Atari 2600                    | `stella`                         |
| `atari7800`    | Atari 7800                    | `prosystem`                      |
| `atarilynx`    | Atari Lynx                    | `handy`                          |

## Themes

Three flavors, in resolution priority order:

1. **Theme pack** — drop a directory at `/foyer/themes/<name>/` with at least
   `theme.jsonc` (palette) and optionally `wallpaper.jpg`,
   `systems/<folder>/splash.jpg` (per-console fullscreen art) and
   `systems/<folder>/logo.png`. Settings → Display → Theme lists every pack
   alongside the single-file themes. Build one yourself or repackage an
   ES-DE theme by flattening it into this layout — only theme.jsonc is
   strictly required, every art asset is optional and falls back through
   the next layers.
2. **Single-file SD theme** — `/foyer/config/themes/<name>.jsonc`, palette
   only. Useful for tweaking colors without bundling art.
3. **Bundled themes** — `dark`, `light`, `midnight`, `forest`, `snow`. Ship
   inside the nro at `romfs:/themes/<name>.{jsonc,jpg}`. Per-system splash
   placeholders (`romfs:/systems/<folder>.jpg`) cover every declared
   system out of the box, so the carousel never sits on a flat colour.

Settings → Display → Theme cycles through them all and persists the choice
in `general.jsonc`.

## Cores

A system can declare multiple cores in `shared/library/system_db.cpp`; the
first entry is the default. Resolution at launch:

1. Per-game override in `/foyer/config/per_game.jsonc`
   (`{ "<rom path>": { "core": "<name>" } }`)
2. Per-system override in `general.jsonc`
   (`default_core_per_system: { "<folder>": "<name>" }`)
3. The system's first declared core

Each core has a `cores/<name>.cmake` recipe. Build a single core or every
recipe'd core in one configure:

```sh
# Single core
cmake --preset Player-fceumm
cmake --build --preset Player-fceumm

# All recipe'd cores in one go (currently 13)
cmake --preset Players-All
cmake --build --preset Players-All
```

Cores currently recipe'd: `fceumm`, `nestopia`, `gambatte`, `snes9x`,
`genesisplusgx`, `mgba`, `melonds`, `pcsx_rearmed`, `swanstation`,
`yabasanshiro`, `race`, `mupen64plus`, `flycast`.

### External standalone emulators (PSP, GameCube)

PPSSPP and Dolphin don't have working libretro cores on Switch upstream,
but their **standalone** Switch nros do exist and run well. Foyer
chain-launches whichever standalone you have installed — its launcher
recognises a per-system entry in `general.jsonc`'s `external_cores`
map, looks for the configured nro on disk, and `envSetNextLoad`s it
with the rom path as `argv[1]`. Defaults match the canonical install
paths:

```jsonc
"external_cores": {
    "psp": "/switch/PPSSPP/PPSSPP.nro",
    "gc":  "/switch/dolphin-emu/dolphin-emu.nro"
}
```

To enable PSP support: install [PPSSPP for Switch](https://www.ppsspp.org/downloads/)
to `/switch/PPSSPP/PPSSPP.nro` and drop PSP roms into `/foyer/roms/psp/`.
For GameCube, install Dolphin's Switch build to
`/switch/dolphin-emu/dolphin-emu.nro` and put roms under `/foyer/roms/gc/`.

Foyer's pause overlay, save-state slots, run-ahead, and bezel pipeline
don't apply to externally-launched standalones — those emulators ship
their own UIs. The standalone owns the experience once foyer hands off.

Settings → Emulator shows the install status of every configured
external-launcher entry so it's obvious whether the chain-launch will
succeed.

## Build

Requires devkitPro with `switch-dev`, `switch-curl`, `switch-glm`,
`switch-zlib`, `switch-mbedtls`, `deko3d`, `switch-mesa`,
`switch-libdrm_nouveau`, `switch-glad`, `switch-ffmpeg` (the last four are
needed by the heavier cores — `mupen64plus`, `flycast`, eventually `ppsspp`
and `dolphin`).

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=/opt/devkitpro/devkitA64
export PATH=$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH

# Browser
cmake --preset Release
cmake --build --preset Release           # → build/Release/foyer.nro

# Player (one core per build)
cmake --preset Player-fceumm
cmake --build --preset Player-fceumm     # → build/Player-fceumm/foyer-fceumm.nro
```

## License

GPLv3. See [`LICENSE`](LICENSE).
