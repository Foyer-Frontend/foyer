# foyer

Native libretro frontend for Nintendo Switch (CFW only). Browser NRO
for picking systems and games; per-system player NROs that boot the
ROM directly via libretro cores.

> [!WARNING]
> **Early development.** foyer is under active development; the
> 0.7.x line is still alpha-quality, not every libretro core in the
> rotation has been fully exercised on hardware, and you should
> expect bugs and the occasional crash. File issues with the
> on-device log (Settings → About → Logs) and the Atmosphère
> crash report when something goes wrong — they make root-causing
> dramatically faster.

**Current line:** 0.7.x — browser is still
[borealis](https://github.com/XITRIX/borealis) (XITRIX/borealis,
`moonlight_wiliwili` branch); per-core players were ported off
borealis through ImGui in 0.6.124–0.6.130 and onto
[Plutonium](https://github.com/XorTroll/Plutonium) (SDL2 + the
XorTroll Plutonium UI framework) in 0.7.0. Latest tag: **v0.7.0**.
System + theme art ships as a separately-downloaded
`foyer-assets.zip` so `foyer.nro` itself stays ~8 MB. See
[`ROADMAP.md`](ROADMAP.md) for the phase-by-phase plan.

### Status snapshot

- **Browser** — stable. Update flow (Settings + boot-splash) +
  chain-launch back-into-foyer + asset-pack auto-download all
  working. SS scrape via `jeuInfos` (CRC+romnom) with a
  `jeuRecherche` → `jeuInfos gameid` fallback for the Switch
  virtual system. Re-running a system scrape now fills in
  missing media kinds per game (wheel art etc) without
  re-pulling already-present files.
- **Players (PLAYER_PLUTONIUM=ON, default since 0.7.0)** —
  launching works. The pause overlay (`L3 + R3`) honours the
  foyer/HOS Light/Dark theme, live-previews shader picks against
  the frozen pause frame, and preserves the highlight position
  across every picker rebuild. Cores get audio through SDL2's
  audio device fed by foyer's libretro AudioSink. MTP must stop
  before chain-launch — done automatically.
- **Switch native titles** (`__switch` virtual) — NACP enumeration
  via `nsListApplicationRecord` + launch via
  `appletRequestLaunchApplication`. SS coverage on the Switch
  system is sparse on indie titles; mainline first-party hits.
- **MTP** — `/foyer/roms` + `/foyer/data/logs` exposed via libhaze
  when the Settings toggles are on. Auto-stopped before every
  chain-launch.

## Screenshots

| | |
|---|---|
| ![Splash](docs/screenshots/splash.jpg) <br>Boot splash with per-step manifest progress | ![Home — virtual systems](docs/screenshots/home-virtual.jpg) <br>Home carousel — virtual systems (Switch / Favourites / Recent / All Games) |
| ![Home — systems](docs/screenshots/home-systems.jpg) <br>Home carousel — Dreamcast tile focused | ![Game Gear](docs/screenshots/system-gamegear.jpg) <br>System view — Game Gear box-art grid |
| ![Game detail](docs/screenshots/game-detail.jpg) <br>Game detail with ScreenScraper metadata + screenshot slideshow | ![Settings — General](docs/screenshots/settings-general.jpg) <br>Settings → General (theme / region / scrub extracted) |
| ![Settings — Cores](docs/screenshots/settings-cores.jpg) <br>Settings → Cores grouped by system | ![Settings — About](docs/screenshots/settings-about.jpg) <br>Settings → About |

## Features

### Browser

- HOS-style launcher chrome (profile cluster, status cluster, action
  buttons, tile carousel) on
  [borealis](https://github.com/XITRIX/borealis).
- Home carousel mixes real systems with **virtual systems** —
  Favourites, Recent, All Games, plus a Switch tile that defers to
  HOS for native titles. Per-tile box art comes from `auto-*`
  splashes bundled in the romfs theme.
- Per-system game list (`SystemActivity`) — sort cycle via `−`
  (Name → Recent → Playtime → Favorites). Round action buttons:
  Scrape, Scan, Search, Settings.
- Per-game detail (`GameActivity`) — ScreenScraper metadata, fanart
  backdrop, screenshot slideshow (Title / Gameplay / Marquee), red
  metadata key labels, in-place refresh when a rescrape finishes.
- Cross-system search activity (`SearchActivity`).
- Per-game settings (`PerGameActivity`) — core override, shader,
  runahead, favourite. Per-system settings (`PerSystemActivity`) —
  default core.
- First-run wizard: pick initial cores / bezel packs / shader packs,
  enter ScreenScraper + SteamGridDB credentials. Downloads run on
  the global install queue while the wizard hands off to Home.
- **Install queue** — one global FIFO. Per-cell "Tap to install" /
  "Tap to re-install" / "Tap to update" labels live-refresh on
  completion. Y from any Settings tab opens the queue overlay with
  progress + pending list.
- **Splash** — pixel-art backdrop, theme-aware overlay, manual
  progress bar that fills as the worker advances through each
  manifest fetch ("Fetching cores manifest…" …).
- Settings tabs: General · Online accounts · Library · Cores ·
  Emulators (default core per system) · Bezels · Shaders · Cheats ·
  Updates · About. The Updates tab splits into four checks —
  foyer self-update / cores / bezels / cheats — each one polls just
  its bucket.
- **Carousel wrap** — left/right at the edge wraps to the opposite
  end on every horizontal Box (in-place patch to brls
  `Box::getNextFocus`).
- libhaze MTP server scoped to `/foyer/roms` only (drop ROMs over
  USB without exposing the rest of the SD card). Settings toggle.
- Self-update: boot-time check fetches `foyer-manifest.json`, modal
  prompt, chain-launches the staged `.new` directly on user-
  confirmed restart; next boot atomically promotes it.
- HOS power slide-in (Sleep / Restart / Power off / Reboot to
  Hekate).
- Profile avatar + nickname pulled from the active libnx account
  (`accountsService`).
- i18n catalogue (en-US / es / pt-BR) — both brls's UI strings and
  foyer's own (`romfs:/i18n/<locale>/foyer.json`,
  `hints.json`).
- Live HOS Light/Dark theme tracking (`setsysGetColorSetId`
  polled once a second).
- Scrub extracted .zip games — off by default; configurable
  threshold (3 / 7 / 10 / 14 / 30 / 60 days).
- Box-art scrapers: libretro-thumbnails (free), ScreenScraper (auth),
  SteamGridDB (auth) — picked by `general.jsonc`.

### Player (per-core NRO)

- Built on Plutonium since 0.7.0 (`PLAYER_PLUTONIUM=ON` default,
  SDL2 + XorTroll/Plutonium). Boots the ROM directly, no browser
  overhead. Brief stop on ImGui in 0.6.124–0.6.130; borealis
  player retired alongside it. The shared `foyer_libretro_frontend`
  + `ShaderPipeline` survived every framework swap.
- Audio via SDL2 (`SDL_AUDIO_DEVICE` + the libretro AudioSink) at
  the core's reported sample rate.
- **Pause overlay** rendered as a Plutonium Layout on top of the
  frozen frame + bezel. `L3 + R3` opens it. Theme-aware palette —
  dark UI → dark panel + light text, light UI → light panel + dark
  text. Highlight survives rebuilds.
- From the overlay: Restart rom, Save / Load state (slot picker
  with timestamps, Quick + 1–9), Shaders (preset picker with live
  preview against the paused frame, "none" restores the unshaded
  frame), Display (Aspect submenu, Bezel ON/OFF, Pick bezel),
  Core options (cycle on A per `CoreOption`), Cheats (boolean
  toggle per cheat — `file_idx`-keyed so empty-code rows in the
  `.cht` don't shift toggles off by one), Quit (chain-launches
  back to `foyer.nro`).
- **Bezels** rendered from the ScreenScraper bundle
  (`/foyer/assets/system/<sys>/<stem>/bezel-16-9(*).png`); fall
  back to a per-system override under `/foyer/bezels/`.
- SRAM persistence across .zip extract via the rom-basis sidecar
  path (`fe.set_sram_basis_path`).
- RetroAchievements via
  [rcheevos](https://github.com/RetroAchievements/rcheevos) —
  login from `accounts.jsonc`, unlock toasts, server-side
  reporting.
- Cancellation-safe libretro frontend
  (`shared/libretro/frontend.{cpp,hpp}`) — set_video_sink /
  set_audio_sink / poll_input wiring is shared with the legacy
  player path.

## Layout on SD

```
/switch/foyer/foyer.nro                  # browser (install here)
/foyer/
├── content/                             # everything the install queue writes
│   ├── cores/foyer-<core>.nro           # downloaded core players
│   ├── bezels/<sys>.png                 # per-system bezel fallback
│   ├── shaders/<preset>/                # libretro shader presets
│   └── cheats/<sys>/                    # cheat packs
├── data/
│   ├── first_run_complete               # marker — written by wizard's Finish
│   ├── session.json                     # last-played, recent list
│   ├── skipped_versions.json            # "skip this version" picks
│   ├── switch_titles.cache              # NACP icon cache (legacy)
│   ├── extract/                         # extracted-zip cache (LRU-scrubbed)
│   ├── cache/
│   │   ├── library.cache.json           # scanner cache (delta-rescan fast path)
│   │   └── libretro_index/v2/           # libretro-thumbnails index cache
│   ├── config/
│   │   ├── general.jsonc                # rom_root, language, theme, region, …
│   │   ├── accounts.jsonc               # ScreenScraper / SteamGridDB / RA creds
│   │   └── per_game.jsonc               # per-rom core / shader / favourite
│   └── logs/<YYYY-MM-DD_HH-MM-SS>.log   # per-run log files
├── assets/
│   ├── system/<sys>/<stem>/             # per-game ScreenScraper bundle:
│   │   ├── metadata.json                #   developer / publisher / synopsis / …
│   │   ├── sstitle(<region>).png        #   title screen
│   │   ├── ss(<region>).png             #   gameplay shots
│   │   ├── box-2D(<region>).png         #   box art
│   │   ├── bezel-16-9(<region>).png     #   per-game bezel (consumed by player)
│   │   ├── fanart.jpg                   #   detail-view backdrop
│   │   └── screenmarquee(<region>).png  #   marquee
│   ├── covers/<sys>/<stem>.png          # legacy cover path (still read)
│   ├── backgrounds/<sys>/<stem>.jpg     # legacy detail backdrop
│   └── systems/<sys>.{png,jpg}          # per-system splash override
├── roms/<system>/<file.ext>             # rom root (configurable)
├── saves/<system>/                      # libretro SRAM
├── states/<system>/<stem>.<slot>.state  # save states (slots 1–9 + quick)
└── system/<system>/                     # BIOS / firmware
```

Bundled inside `foyer.nro` (`romfs:/`):

```
romfs:/
├── splash_bg.png                            # pixel-art splash backdrop
├── splash.jpg                               # legacy splash (still bundled)
├── themes/foyer/systems/<folder>/
│   ├── splash.png                           # alekfull-NX tile art
│   ├── background.jpg                       # retrofix-revisited app backdrop
│   ├── logo_light.png / logo_dark.png
│   └── auto-{favorites,lastplayed,allgames}/  # virtual-system splashes
│       └── __switch/                          # native Switch tile art
├── shaders/{fill_vsh,fill_aa_fsh,fill_fsh}.dksh   # brls renderer shaders
├── i18n/<locale>/{foyer,brls,hints}.json
├── img/actions/{news,eshop,gallery,search,settings,power}.png
├── xml/activity/{home,system,game,foyer_settings,splash,…}.xml
├── xml/tabs/foyer_settings.xml
└── material/MaterialIcons-Regular.ttf
```

## Theme

0.7.x ships a single bundled theme tree
(`romfs:/themes/foyer/`) combining alekfull-NX tile splashes with
retrofix-revisited per-system app backdrops. brls owns the rest of
the browser chrome (Light or Dark, picked from the Switch system
theme automatically via `setsysGetColorSetId`); the Plutonium
player overlay reads the same `ColorSetId` so its panel + text
colours follow the system theme in lockstep with the browser.

Settings → General → Theme overrides the system default (Auto /
Light / Dark). `theme_watcher` polls once per second so toggling
the system theme takes effect without relaunching foyer.

The 0.5.x JSONC theme system + the multi-palette picker are gone;
brls's HOS-faithful theme is the source of truth.

## Cores

A system can declare multiple cores in
`shared/library/system_db.cpp`; the first entry is the default.
Resolution at launch:

1. Per-game override in `/foyer/data/config/per_game.jsonc`
   (`{ "<rom path>": { "core": "<name>" } }`)
2. Per-system override in `general.jsonc`
   (`default_core_per_system: { "<folder>": "<name>" }`)
3. The system's first declared core

Player NROs are downloaded from
[`foyer-cores`](https://github.com/foyer-frontend/foyer-cores)'s
release manifest. The first-run wizard offers checkboxes for the
manifest's cores; Settings → Cores groups them by system and
exposes per-row "Tap to install / re-install / update".

```sh
# Build a single player binary:
cmake --preset Player-fceumm
cmake --build --preset Player-fceumm
# → build/Player-fceumm/foyer-fceumm.nro

# Build the whole rotation locally:
cmake --preset Players-All
cmake --build --preset Players-All
```

The current live core list is in
[`foyer-cores/manifest.json`](https://github.com/foyer-frontend/foyer-cores/releases/latest)
(~55 cores at the latest tag). Foyer's `system_db.cpp` knows about
every system regardless of which cores happen to be installed.

## Compatibility matrix — cores

Two-tier layout: a slim **summary table** with grouped columns
(Runs = boots+audio, Saves = sram+save state, Visuals = bezel+
shader), then a **detail section per exercised core** with the
per-feature breakdown + notes (perf quirks, RA hash quirks,
known issues). Un-exercised cores live in the summary only —
the moment one gets a hardware pass it earns its own subsection.

`✅` = working, `🟡` = partially / quirky (slow, missing one
sub-feature), `❌` = broken / crashes, `⬜` = not yet tested. A
group cell shows the worst state across its sub-features; the
detail section under the heading breaks it down.

### Summary

| Core | System(s) | Runs | Saves | Visuals | Cheats |
|---|---|---|---|---|---|
| [`fceumm`](#fceumm) | NES | ✅ | ✅ | ✅ | ✅ |
| [`nestopia`](#nestopia) | NES | ✅ | ✅ | ✅ | ✅ |
| [`mesen`](#mesen) | NES | 🟡 | ✅ | ✅ | ❌ |
| [`snes9x`](#snes9x) | SNES | ✅ | ✅ | ✅ | ✅ |
| [`snes9x2010`](#snes9x2010) | SNES | ✅ | ✅ | ✅ | ❌ |
| `bsnes_hd_beta` | SNES | ⬜ | ⬜ | ⬜ | ⬜ |
| `gambatte` | GB / GBC | ⬜ | ⬜ | ⬜ | ⬜ |
| `sameboy` | GB / GBC | ⬜ | ⬜ | ⬜ | ⬜ |
| `tgbdual` | GB / GBC | ⬜ | ⬜ | ⬜ | ⬜ |
| `mgba` | GBA | ⬜ | ⬜ | ⬜ | ⬜ |
| `gpsp` | GBA | ⬜ | ⬜ | ⬜ | ⬜ |
| `vba_next` | GBA | ⬜ | ⬜ | ⬜ | ⬜ |
| `melonds` | NDS | ⬜ | ⬜ | ⬜ | ⬜ |
| `mupen64plus` | N64 | ⬜ | ⬜ | ⬜ | ⬜ |
| `pokemini` | Pokemon Mini | ⬜ | ⬜ | ⬜ | ⬜ |
| `mednafen_vb` | Virtual Boy | ⬜ | ⬜ | ⬜ | ⬜ |
| `genesisplusgx` | Genesis / SMS / GG | ⬜ | ⬜ | ⬜ | ⬜ |
| `genesis_plus_gx_wide` | Genesis | ⬜ | ⬜ | ⬜ | ⬜ |
| `picodrive` | 32X / SegaCD | ⬜ | ⬜ | ⬜ | ⬜ |
| `yabasanshiro` | Saturn | ⬜ | ⬜ | ⬜ | ⬜ |
| `flycast` | Dreamcast | ⬜ | ⬜ | ⬜ | ⬜ |
| `pcsx_rearmed` | PSX | ⬜ | ⬜ | ⬜ | ⬜ |
| `swanstation` | PSX | ⬜ | ⬜ | ⬜ | ⬜ |
| `mednafen_psx_hw` | PSX | ⬜ | ⬜ | ⬜ | ⬜ |
| [`ppsspp`](#ppsspp) | PSP | ❌ | — | — | — |
| `race` | NGP / NGPC | ⬜ | ⬜ | ⬜ | ⬜ |
| `mednafen_ngp` | NGP / NGPC | ⬜ | ⬜ | ⬜ | ⬜ |
| `stella` | Atari 2600 | ⬜ | ⬜ | ⬜ | ⬜ |
| `stella2014` | Atari 2600 | ⬜ | ⬜ | ⬜ | ⬜ |
| `prosystem` | Atari 7800 | ⬜ | ⬜ | ⬜ | ⬜ |
| `handy` | Atari Lynx | ⬜ | ⬜ | ⬜ | ⬜ |
| `mednafen_lynx` | Atari Lynx | ⬜ | ⬜ | ⬜ | ⬜ |
| `atari800` | Atari 800 / 5200 | ⬜ | ⬜ | ⬜ | ⬜ |
| `virtualjaguar` | Atari Jaguar | ⬜ | ⬜ | ⬜ | ⬜ |
| `mednafen_pce_fast` | PCE | ⬜ | ⬜ | ⬜ | ⬜ |
| `beetle_pce` | PCE / PCE-CD | ⬜ | ⬜ | ⬜ | ⬜ |
| `beetle_supergrafx` | SuperGrafx | ⬜ | ⬜ | ⬜ | ⬜ |
| `beetle_pcfx` | PC-FX | ⬜ | ⬜ | ⬜ | ⬜ |
| `beetle_wswan` | WonderSwan | ⬜ | ⬜ | ⬜ | ⬜ |
| `beetle_vb` | Virtual Boy | ⬜ | ⬜ | ⬜ | ⬜ |
| `freeintv` | Intellivision | ⬜ | ⬜ | ⬜ | ⬜ |
| `gw` | Game & Watch | ⬜ | ⬜ | ⬜ | ⬜ |
| `prboom` | Doom | ⬜ | ⬜ | ⬜ | ⬜ |
| `tyrquake` | Quake | ⬜ | ⬜ | ⬜ | ⬜ |
| `retro8` | Pico-8 | ⬜ | ⬜ | ⬜ | ⬜ |
| `dosbox_pure` | DOS | ⬜ | ⬜ | ⬜ | ⬜ |
| `caprice32` | Amstrad CPC | ⬜ | ⬜ | ⬜ | ⬜ |
| `frodo` | C64 | ⬜ | ⬜ | ⬜ | ⬜ |
| `fmsx` | MSX / MSX2 | ⬜ | ⬜ | ⬜ | ⬜ |
| `opera` | 3DO | ⬜ | ⬜ | ⬜ | ⬜ |
| `mame2003_plus` | Arcade | ⬜ | ⬜ | ⬜ | ⬜ |
| `nxengine` | Cave Story | ⬜ | ⬜ | ⬜ | ⬜ |
| `reminiscence` | Flashback | ⬜ | ⬜ | ⬜ | ⬜ |
| `puae` | Amiga | ⬜ | ⬜ | ⬜ | ⬜ |
| `scummvm` | ScummVM | ❌ | — | — | — |
| `parallel_n64` | N64 | ❌ | — | — | — |

`scummvm` is currently disabled in the foyer-cores matrix (build
time); `parallel_n64` has an upstream link error chain that needs
recipe surgery — see `foyer-cores/.github/workflows/build-cores.yml`.

### Per-core details

#### `fceumm`

| Feature | Status | Notes |
|---|---|---|
| Boots          | ✅ | NES roms boot directly into gameplay |
| Audio          | ✅ | 48 kHz via SDL2 audio sink |
| SRAM           | ✅ | .srm round-trip across chain-launch |
| Save state     | ✅ | slot picker (Quick + 1–9) round-trips |
| Bezel          | ✅ | per-system + per-game bezels render |
| Shader         | ✅ | live preview against frozen pause frame |
| Cheats         | ✅ | retroarch .cht file_idx-keyed round-trip |
| RetroAchievements | ✅ | confirmed hardcore-off unlock + toast on 0.7.5 |

Recommended default for NES — fast, accurate enough for the
core RA achievement set, low memory footprint.

#### `nestopia`

| Feature | Status | Notes |
|---|---|---|
| Boots          | ✅ | NES roms boot directly into gameplay |
| Audio          | ✅ | 48 kHz via SDL2 audio sink |
| SRAM           | ✅ | .srm round-trip across chain-launch |
| Save state     | ✅ | slot picker round-trips |
| Bezel          | ✅ | per-system + per-game bezels render |
| Shader         | ✅ | live preview against frozen pause frame |
| Cheats         | ✅ | retroarch .cht round-trip working |
| RetroAchievements | ⬜ | not yet exercised |

Higher-accuracy NES alternative to fceumm; modest overhead.
Recommended when you want sub-frame audio + ppu accuracy.

#### `mesen`

| Feature | Status | Notes |
|---|---|---|
| Boots          | 🟡 | runs but performance-bound — drops below 60 fps in busy scenes |
| Audio          | ✅ | 48 kHz via SDL2 audio sink |
| SRAM           | ✅ | .srm round-trip across chain-launch |
| Save state     | ✅ | slot picker round-trips |
| Bezel          | ✅ | per-system + per-game bezels render |
| Shader         | ✅ | live preview against frozen pause frame |
| Cheats         | ❌ | retro_cheat_set call lands but no in-game effect — separate from foyer's loader (fceumm + nestopia consume the same files cleanly) |
| RetroAchievements | ⬜ | not yet exercised |

Accuracy-leaning NES core. Use when fceumm/nestopia can't run a
specific homebrew or mapper edge case; expect lower framerate
than the other two on Switch.

#### `snes9x`

| Feature | Status | Notes |
|---|---|---|
| Boots          | ✅ | SNES roms boot directly into gameplay |
| Audio          | ✅ | 48 kHz via SDL2 audio sink |
| SRAM           | ✅ | .srm round-trip across chain-launch |
| Save state     | ✅ | slot picker round-trips |
| Bezel          | ✅ | per-system + per-game bezels render |
| Shader         | ✅ | live preview against frozen pause frame |
| Cheats         | ✅ | retroarch .cht round-trip working |
| RetroAchievements | ⬜ | not yet exercised |

Recommended default for SNES. Full feature row green on 0.7.5
hardware.

#### `snes9x2010`

| Feature | Status | Notes |
|---|---|---|
| Boots          | ✅ | SNES roms boot directly into gameplay |
| Audio          | ✅ | 48 kHz via SDL2 audio sink |
| SRAM           | ✅ | .srm round-trip across chain-launch |
| Save state     | ✅ | slot picker round-trips |
| Bezel          | ✅ | per-system + per-game bezels render |
| Shader         | ✅ | live preview against frozen pause frame |
| Cheats         | ❌ | retro_cheat_set call lands but no in-game effect — same symptom mesen has on NES; needs a per-core dig |
| RetroAchievements | ⬜ | not yet exercised |

Older snes9x port, kept for perf-constrained roms. Use snes9x
when you need full feature coverage; reach for snes9x2010 only
when a specific game runs better here.

#### `ppsspp`

| Feature | Status | Notes |
|---|---|---|
| Boots          | ❌ | rom launch fails before retro_run — needs deeper investigation |
| Audio          | — | gated on Boots |
| SRAM           | — | — |
| Save state     | — | — |
| Bezel          | — | — |
| Shader         | — | — |
| Cheats         | — | — |

PSP core ships its own embedded rcheevos + libpng17; foyer
links against system libpng for SDL2_image. Linker conflict
worked around in 0.7.0 with `--allow-multiple-definition`, but
boot still fails — likely runtime ABI mismatch on the libpng
side (different `png_struct` layouts). Unblock plan: strip
libpng17/*.o from ppsspp's libretro.a so the system libpng wins
both compile + link, or fork in a unified png struct shim.

**Note on SRAM**: cartridge battery saves (.srm files at
`/foyer/saves/<rom>.srm`) require v0.6.116+ in both foyer
browser AND the player nros — earlier versions skipped the
on-quit flush so saves were lost on chain-launch. The 🟡
markers above reflect that the fix is in v0.6.116 but the
matching player nros need a foyer-cores rebuild to ship.

## Compatibility matrix — systems

Per-system scrape + theme art status. `Scrape` covers ScreenScraper
box-2D + wheel + fanart fetch via foyer's system-scrape action.

| Folder | Display | SS coverage | foyer-assets art | Notes |
|---|---|---|---|---|
| `nes` | Nintendo Entertainment System | ⬜ | ✅ | |
| `snes` | Super Nintendo | ⬜ | ✅ | |
| `n64` | Nintendo 64 | ⬜ | ✅ | |
| `gb` | Game Boy | ⬜ | ✅ | |
| `gbc` | Game Boy Color | ⬜ | ✅ | |
| `gba` | Game Boy Advance | ⬜ | ✅ | |
| `nds` | Nintendo DS | ⬜ | ✅ | |
| `gc` | GameCube | ⬜ | ✅ | external nro (Dolphin) |
| `virtualboy` | Virtual Boy | ⬜ | ✅ | |
| `pokemini` | Pokemon Mini | ⬜ | ✅ | |
| `__switch` | Nintendo Switch (installed titles) | 🟡 | ✅ | indie title coverage thin |
| `genesis` / `megadrive` | Mega Drive / Genesis | ⬜ | ✅ | |
| `mastersystem` | Master System | ⬜ | ✅ | |
| `gamegear` | Game Gear | ⬜ | ✅ | |
| `32x` | Sega 32X | ⬜ | ✅ | |
| `segacd` | Sega CD | ⬜ | ✅ | |
| `saturn` | Sega Saturn | ⬜ | ✅ | |
| `dc` | Dreamcast | ⬜ | ✅ | |
| `psx` | PlayStation | ⬜ | ✅ | |
| `psp` | PlayStation Portable | ⬜ | ✅ | |
| `ngp` / `ngpc` | Neo Geo Pocket / Color | ⬜ | ✅ | |
| `atari2600` | Atari 2600 | ⬜ | ✅ | |
| `atari5200` | Atari 5200 | ⬜ | ✅ | |
| `atari7800` | Atari 7800 | ⬜ | ✅ | |
| `atari800` | Atari 800 | ⬜ | ✅ | |
| `atarilynx` | Atari Lynx | ⬜ | ✅ | |
| `atarijaguar` | Atari Jaguar | ⬜ | ✅ | |
| `pcengine` / `pcenginecd` | PC Engine / CD | ⬜ | ✅ | |
| `supergrafx` | SuperGrafx | ⬜ | ✅ | |
| `pcfx` | PC-FX | ⬜ | ✅ | |
| `wonderswan` / `wonderswancolor` | WonderSwan / Color | ⬜ | ✅ | |
| `intellivision` | Intellivision | ⬜ | ✅ | |
| `gameandwatch` | Game & Watch | ⬜ | ✅ | |
| `3do` | 3DO | ⬜ | ✅ | |
| `arcade` | Arcade (MAME 2003+) | ⬜ | ✅ | |
| `doom` | Doom (PrBoom) | ⬜ | ✅ | |
| `quake` | Quake (TyrQuake) | ⬜ | ✅ | |
| `pico8` | Pico-8 | ⬜ | ✅ | |
| `dos` | DOS (DOSBox-pure) | ⬜ | ✅ | |
| `amstradcpc` | Amstrad CPC | ⬜ | ✅ | |
| `c64` | Commodore 64 | ⬜ | ✅ | |
| `msx` / `msx2` | MSX / MSX2 | ⬜ | ✅ | |
| `amiga` / `amiga600` / `amiga1200` / `amigacd32` / `cdtv` | Amiga family | ⬜ | ✅ | |
| `scummvm` | ScummVM | ⬜ | ✅ | core disabled in matrix |

## Compatibility matrix — features

| Feature | Status | Notes |
|---|---|---|
| Library scan + cache | ✅ | `/foyer/data/cache/library.cache.json` |
| Switch title enumeration | ✅ | NACP cache at `switch_titles.cache` |
| SS scrape (box-2D, wheel, fanart, sstitle, ss, bezel, video) | ✅ | Re-scrape fills only missing kinds |
| libretro-thumbnails scraper | ⬜ | wire-in for any system on the LR db |
| SteamGridDB scraper | ⬜ | creds wired in wizard; query path not yet exhaustive |
| Per-game state slots (10) | 🟡 | Save works; load refused on size mismatch (v0.6.96 guard) |
| Per-game settings (core / shader / runahead / favourite) | ✅ | `PerGameActivity` |
| Per-system settings (default core) | ✅ | `PerSystemActivity` |
| Cheats (libretro-database packs) | ⬜ | |
| Bezels (libretro/common-overlays packs) | ⬜ | install + pause-overlay toggle wired |
| Shaders (libretro presets) | ⬜ | install + per-rom override wired |
| RetroAchievements | ⬜ | rcheevos linked; login + Settings switch pending |
| Run-ahead | ⬜ | per-game override exists; not validated |
| Self-update (Settings) | ✅ | Chain-launches new nro |
| Self-update (boot splash) | ✅ | Modal Yes / Later dialog before Home |
| Asset pack auto-download / versioning | ✅ | sidecar `/foyer/data/assets/.version` |
| MTP (`/foyer/roms`) | ✅ | libhaze; auto-stops before chain-launch |
| MTP (`/foyer/data/logs`) | ✅ | |
| Crash log viewer (Atmosphère + foyer) | ✅ | Settings → About → Logs |
| In-app sleep / restart / shutdown | ✅ | `PowerActivity` |
| Theme follow HOS Light / Dark | ✅ | `theme_watcher` polls once/sec |
| Wizard (first-run) | ✅ | scraper creds + initial cores / bezels / shaders |

### Release cadence

foyer-cores uses **CalVer** tags (`YYYY.MM.DD[.NN]`):

- **Nightly cron @ 03:00 UTC** — smart-matrix build. Each core's
  upstream is probed via `git ls-remote`; only cores whose
  upstream or recipe file changed actually rebuild. The carry-
  forward manifest aggregator inherits the prior release's URL
  for skipped cores, so every published manifest still lists
  every core.
- **Manual tag push** (e.g. `2026.05.20`) — force-rebuilds the
  whole matrix.
- **Cross-repo trigger** — any commit on `foyer/main` that
  touches `shared/**` or `player/**` fires a
  `repository_dispatch` at foyer-cores, which then cuts a fresh
  CalVer tag and force-rebuilds.

## Build

Requires devkitPro with `switch-dev`, `switch-curl`, `switch-glm`,
`switch-glfw`, `switch-zlib`, `switch-mbedtls`, `deko3d`,
`switch-mesa`, `switch-libdrm_nouveau`, `switch-glad`,
`switch-ffmpeg`, `switch-sdl2`, `switch-sdl2_image`,
`switch-sdl2_ttf`. CMake ≥ 3.21 (FetchContent's `SOURCE_SUBDIR`).

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=/opt/devkitpro/devkitA64
export PATH=$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH

# Browser (borealis)
cmake --preset Release
cmake --build build/Release -j$(nproc)
# → build/Release/foyer.nro

# Player (Plutonium — one core per build)
cmake --preset Player-fceumm
cmake --build --preset Player-fceumm
# → build/Player-fceumm/foyer-fceumm.nro
```

`FOYER_CORES_DIR` defaults to `${CMAKE_SOURCE_DIR}/../foyer-cores`
(sibling clone of the recipes repo). Override when working off a
fork.

First configure clones
[`XITRIX/borealis`](https://github.com/XITRIX/borealis) on the
`moonlight_wiliwili` branch (~50 MB) plus brls's vendored fmt /
yoga / tweeny / tinyxml2 / libretro-common subset, then applies a
small in-place patch to `Box::getNextFocus` for the carousel-wrap
behaviour. Subsequent configures are fast.

## License

GPLv3. See [`LICENSE`](LICENSE).

Theme art:
[`alekfull-nx`](https://github.com/anthonycaccese/alekfull-nx-es-de)
splashes and
[`retrofix-revisited`](https://github.com/anthonycaccese/retrofix-revisited-es-de)
backgrounds, both **CC BY-NC-SA 4.0**.
