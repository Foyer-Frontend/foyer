# foyer

Native libretro frontend for Nintendo Switch (CFW only). Browser NRO
for picking systems and games; per-system player NROs that boot the
ROM directly via libretro cores.

**Current line:** 0.6.0 alphas (the brls / borealis cutover). The
last shipped stable is **v0.5.26**. See [`ROADMAP.md`](ROADMAP.md) for
the phase-by-phase 0.6.0 plan.

## Features

- HOS-style launcher chrome (profile cluster, status cluster, action
  buttons, tile carousel) — built on
  [borealis](https://github.com/XITRIX/borealis) since 0.6.0.
- Per-system game list (`SystemActivity`) + game detail
  (`GameActivity`) + cross-system search.
- Per-system player binaries (`foyer-<core>.nro`) — chain-launched
  by `launch.cpp` via `envSetNextLoad`.
- Pause overlay (player-side, **not yet on borealis** — see
  ROADMAP Phase H): 10 timestamped save-state slots, aspect ratio
  picker, per-core libretro options, Quit. `− +` shoulder combo.
- Audio via `audrv` at the core's sample rate.
- Box-art scrapers: libretro-thumbnails (free), ScreenScraper (auth),
  SteamGridDB (auth) — picked by `general.jsonc`.
- RetroAchievements via [rcheevos](https://github.com/RetroAchievements/rcheevos)
  — login from `accounts.jsonc`, unlock toasts, server-side reporting.
- First-run wizard: pick initial cores / bezel packs / shader packs,
  enter ScreenScraper + SteamGridDB credentials. Downloads run in the
  background while the user lands on the home screen.
- libhaze MTP server scoped to `/foyer/roms` only (drop ROMs over USB
  without exposing the rest of the SD card). Settings toggle.
- Self-update: boot-time check fetches `foyer-manifest.json`, yes/no
  modal, atomic rename of `foyer.nro.new` after the next boot.
- i18n catalogue (en-US / es / pt-BR) — both brls's UI strings and
  foyer's own (`romfs:/i18n/<locale>/foyer.json`).
- Profile avatar + nickname pulled from the active libnx account.
- HOS power slide-in (Sleep / Restart / Power off / Reboot to Hekate).

## Layout on SD

```
/switch/foyer/foyer.nro              # browser (install here)
/foyer/
├── cores/
│   └── foyer-<core>.nro             # downloaded via the wizard / Settings
├── config/
│   ├── general.jsonc                # rom_root, preferred_scraper, language, …
│   ├── accounts.jsonc               # ScreenScraper / SteamGridDB / RA creds
│   ├── per_game.jsonc               # per-rom core overrides
│   └── cores/<core>.jsonc           # per-core libretro variables
├── data/
│   ├── first_run_complete           # marker — written by wizard's Finish
│   ├── library.cache.json           # scanner cache (delta-rescan fast path)
│   ├── switch_titles.cache          # NACP icon cache (legacy, not yet on brls)
│   └── logs/<YYYY-MM-DD_HH-MM-SS>.log   # per-run log files
├── assets/
│   ├── covers/<system>/<stem>.png       # box art on Game Detail
│   └── backgrounds/<system>/<stem>.jpg  # backdrop behind Game Detail
├── bezels/<system>.png              # libretro-overlay PNGs
├── shaders/<preset>/                # libretro shader presets
├── roms/<system>/<file.ext>         # rom root (configurable)
├── saves/<system>/                  # libretro SRAM
├── states/<system>/<stem>.<slot>.state
└── system/<system>/                 # BIOS / firmware
```

Bundled inside the NRO (`romfs:/`):

```
romfs:/themes/foyer/
├── systems/<folder>/
│   ├── splash.png                   # alekfull-NX tile art
│   └── background.jpg               # retrofix-revisited app backdrop
├── shaders/{fill_vsh,fill_aa_fsh,fill_fsh}.dksh   # brls renderer shaders
├── i18n/<locale>/{foyer,...}.json   # foyer + brls catalogues
├── img/actions/{news,eshop,gallery,search,settings,power}.png
├── xml/activity/{home,foyer_settings}.xml
├── xml/tabs/foyer_settings.xml
└── material/MaterialIcons-Regular.ttf
```

## Theme

0.6.0 ships a single bundled theme tree (`romfs:/themes/foyer/`)
combining alekfull-NX tile splashes with retrofix-revisited
per-system app backdrops. brls owns the rest of the chrome (Light
or Dark, picked from the Switch system theme automatically via
`setsysGetColorSetId`).

The 0.5.x JSONC theme system + the multi-palette picker are gone;
brls's HOS-faithful theme is the source of truth.

## Cores

A system can declare multiple cores in
`shared/library/system_db.cpp`; the first entry is the default.
Resolution at launch:

1. Per-game override in `/foyer/config/per_game.jsonc`
   (`{ "<rom path>": { "core": "<name>" } }`)
2. Per-system override in `general.jsonc`
   (`default_core_per_system: { "<folder>": "<name>" }`)
3. The system's first declared core

Player NROs are downloaded from
[`foyer-cores`](https://github.com/foyer-frontend/foyer-cores)'s
release manifest. The first-run wizard offers checkboxes for the
manifest's cores; Settings → Updates also exposes the install/update
flow for users who skipped the wizard.

```sh
# Build a single player binary:
cmake --preset Player-fceumm
cmake --build --preset Player-fceumm
# → build/Player-fceumm/foyer-fceumm.nro
```

The current live core list is in
[`foyer-cores/manifest.json`](https://github.com/foyer-frontend/foyer-cores/releases/latest)
(~28 cores at the latest tag). Foyer's `system_db.cpp` knows about
every system regardless of which cores happen to be installed.

## Build

Requires devkitPro with `switch-dev`, `switch-curl`, `switch-glm`,
`switch-glfw`, `switch-zlib`, `switch-mbedtls`, `deko3d`,
`switch-mesa`, `switch-libdrm_nouveau`, `switch-glad`,
`switch-ffmpeg`. CMake ≥ 3.21 (FetchContent's `SOURCE_SUBDIR`).

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=/opt/devkitpro/devkitA64
export PATH=$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH

# Browser
cmake --preset Release
cmake --build build/Release -j$(nproc)
# → build/Release/foyer.nro

# Player (one core per build)
cmake --preset Player-fceumm
cmake --build --preset Player-fceumm
# → build/Player-fceumm/foyer-fceumm.nro
```

First configure clones [`XITRIX/borealis`](https://github.com/XITRIX/borealis)
on the `moonlight_wiliwili` branch (~50 MB) plus brls's vendored
fmt / yoga / tweeny / tinyxml2 / libretro-common subset. Subsequent
configures are fast.

## License

GPLv3. See [`LICENSE`](LICENSE).

Theme art:
[`alekfull-nx`](https://github.com/anthonycaccese/alekfull-nx-es-de)
splashes and
[`retrofix-revisited`](https://github.com/anthonycaccese/retrofix-revisited-es-de)
backgrounds, both **CC BY-NC-SA 4.0**.
