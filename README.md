# foyer

Native libretro frontend for Nintendo Switch (CFW only). Inspired by
EmulationStation-DE and Tico. Runs libretro cores in-process via per-system
player nros, with a browser nro for picking systems and games.

Features delivered so far:

- ES-DE-style browser: home carousel + per-system game list + metadata sidebar
- Per-system player binaries that boot the rom directly (`foyer-<core>.nro`)
- Pause overlay (`− +` together): 10 timestamped save-state slots, load-state
  picker, aspect ratio (4:3, 16:9, Core, Stretch, Integer 1×/2×/Auto), Quit
- Audio (audrv stream at the core's sample rate)
- Box-art scrapers: libretro-thumbnails (free), ScreenScraper (auth),
  SteamGridDB (auth) — picked by the value in `general.jsonc`
- RetroAchievements via [rcheevos](https://github.com/RetroAchievements/rcheevos)
  — login from `accounts.jsonc`, unlock toasts, server-side reporting
- MTP server (libhaze, Phase 8) so the user can drop roms onto the SD without
  unmounting

## Layout on SD

```
/switch/foyer/foyer.nro              # browser (install here)
/foyer/
├── cores/
│   └── foyer-<core>.nro             # downloaded via Settings → Install Cores
├── config/
│   ├── general.jsonc                # rom_root, preferred_scraper, theme, …
│   ├── accounts.jsonc               # ScreenScraper / SteamGridDB / RA creds
│   ├── per_game.jsonc               # per-rom core overrides
│   ├── themes/<name>.jsonc          # palette + metric overrides (Settings → Theme)
│   └── cores/<core>.jsonc           # per-core libretro variables (Phase 8)
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

| Folder         | Display                       | Core           |
|----------------|-------------------------------|----------------|
| `nes`          | Nintendo Entertainment System | `fceumm`       |
| `snes`         | Super Nintendo                | `snes9x`       |
| `gb`           | Game Boy                      | `gambatte`     |
| `gbc`          | Game Boy Color                | `gambatte`     |
| `gba`          | Game Boy Advance              | `mgba`         |
| `n64`          | Nintendo 64                   | `mupen64plus`  |
| `nds`          | Nintendo DS                   | `melonds`      |
| `gc`           | GameCube                      | `dolphin`      |
| `genesis`      | Sega Genesis                  | `genesisplusgx`|
| `megadrive`    | Sega Mega Drive               | `genesisplusgx`|
| `mastersystem` | Sega Master System            | `genesisplusgx`|
| `gamegear`     | Sega Game Gear                | `genesisplusgx`|
| `saturn`       | Sega Saturn                   | `yabasanshiro` |
| `dc`           | Dreamcast                     | `flycast`      |
| `psx`          | PlayStation                   | `swanstation`  |
| `psp`          | PlayStation Portable          | `ppsspp`       |
| `ngp`          | Neo Geo Pocket                | `race`         |
| `ngpc`         | Neo Geo Pocket Color          | `race`         |

A system can declare multiple cores in `shared/library/system_db.cpp`; the
first entry is the default. Resolution at launch:

1. Per-game override in `/foyer/config/per_game.jsonc`
   (`{ "<rom path>": { "core": "<name>" } }`)
2. Per-system override in `general.jsonc`
   (`default_core_per_system: { "<folder>": "<name>" }`)
3. The system's first declared core

Phase 7 expands the player builds beyond `fceumm`. Each new core gets a
`cores/<core>.cmake` recipe. Build a single core or every recipe'd core in one
configure:

```sh
# Single core (legacy)
cmake --preset Player-fceumm
cmake --build --preset Player-fceumm

# All recipe'd cores in one go (semicolon list, edit in CMakePresets.json)
cmake --preset Players-All
cmake --build --preset Players-All
```

## Build

Requires devkitPro with `switch-dev`, `switch-curl`, `switch-glm`,
`switch-zlib`, `switch-mbedtls`, `deko3d`.

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
