# foyer roadmap

Living document. Past releases are kept brief; the active 0.6.0 cycle
gets phase-by-phase detail because it's a renderer-level rewrite and
ships incrementally as alpha tags. Issues / PRs / "please add X"
requests welcome at https://github.com/Foyer-Frontend/foyer/issues.

---

## Status snapshot

| Release | Status      | Headline                                              |
|---------|-------------|-------------------------------------------------------|
| 0.2.x   | shipped     | First public cores set + libretro frontend            |
| 0.3.0   | shipped     | Catalogue expansion + per-game overrides              |
| 0.4.0   | shipped     | Compile-time i18n catalogue (en / es / pt-BR)         |
| 0.5.x   | shipped     | HOS-launcher chrome refresh (custom nanovg)           |
| 0.6.0   | in progress | borealis cutover + first-run wizard                   |
| 0.7.0   | planned     | libretro Dolphin + JIT capability                     |
| 0.8.0   | planned     | netplay where cores support it                        |

---

## 0.2.x — first public cores (shipped)

Hardware-stable libretro frontend with the initial core set
(fceumm / snes9x / gambatte / mgba / genesis_plus_gx / mupen64plus_next /
ppsspp / swanstation / yabasanshiro / handy / prosystem). Per-system
chain-launch via dedicated player NROs, save plumbing through
retro_get_memory, bezel + cheat seed paths, libretro-thumbnails fallback
scraper.

Open from this line that follows us into 0.7.0:

- `gpsp` ships with the dynarec disabled — a write-side address
  translation in `gpsp_jit_switch.c` is the missing piece.
- Switch JIT NACP capability not yet declared. Blocks gpsp dynarec +
  any future core that needs runtime code generation. The
  Dolphin work in 0.7.0 depends on this.

## 0.3.0 — catalogue expansion (shipped)

Broaden the supported core matrix. Each core gates on a known-good ROM
boot on Switch hardware before it lands in the manifest. Final list at
tag time: ~28 cores covering NES through PSP, plus dosbox_pure, scummvm,
mame2003_plus, prboom and a long tail of less-trafficked systems.

Per-game overrides land here too — the user can pick a non-default core
for one game without disturbing the system-wide default.

## 0.4.0 — compile-time i18n (shipped)

Static enum-keyed catalogue (`StringId` -> array per language). Plain
C++ struct per language; no runtime parsing. Languages at tag: English,
Spanish, Portuguese (Brazilian). Player NROs stay English-only
intentionally — the libretro overlay is mostly core-internal text +
button hints.

Tooling: a scan that diffs `views.cpp`'s `_(SId::...)` references
against the catalogues so additions can't silently regress
non-English builds.

## 0.5.x — HOS-launcher chrome (shipped)

Move the home view from a generic carousel to one that mimics HOS's
launcher: profile cluster top-left, status cluster top-right, action
buttons row above a separator above a hint bar, alekfull-NX tile art,
per-system app backdrops, HOS-style power slide-in. Still
nanovg-rendered (the brls migration starts in 0.6.0).

Bundled the alekfull-NX theme from anthonycaccese/alekfull-nx-es-de
plus retrofix-revisited-es-de backgrounds (replaced alekfull's
backgrounds in alpha.6 of 0.6.0). i18n catalogue grew with all the
new HOS-chrome strings.

Key fix in 0.5.26: `apply_staged_update_if_present` was renaming
`foyer.nro.new` -> `foyer.nro` BEFORE `App` ran `romfsInit`, so the
romfs fd opened against a missing path → fatal at PC=0 (2354-0001).
Rename now happens after `App` is constructed.

---

## 0.6.0 — borealis cutover + first-run wizard (in progress)

Replace foyer's hand-rolled nanovg rendering with
[XITRIX/borealis](https://github.com/XITRIX/borealis) on the
`moonlight_wiliwili` branch (the active fork used by Moonlight Switch).
brls has the right HOS chrome out of the box, a Yoga flexbox layout
engine, and a deko3d backend; it removes ~6 KLOC of foyer-side
rendering code and gives us widgets we'd otherwise hand-build.

Ships incrementally as `v0.6.0-alpha.N` tags. Phase tasks track
internally; the user-facing milestones are below.

### Phase A — borealis build integration + boot stub  ✅ alpha.1

- `FetchContent` of XITRIX/borealis `moonlight_wiliwili`
  (`SOURCE_SUBDIR=library`).
- `PLATFORM_SWITCH=ON`, `BOREALIS_USE_DEKO3D=ON`, `BRLS_UNITY_BUILD=OFF`
  set explicitly because we bypass borealis's `toolchain.cmake` (our
  CMakePresets already loads `$DEVKITPRO/cmake/Switch.cmake`).
- `$DEVKITPRO/portlibs/switch/include` exposed to the borealis target
  so its `<glm/vec2.hpp>` resolves.
- Split `foyer_shared` (services) from `foyer_render` (legacy nanovg
  App, players-only). Browser drops the nanovg link.
- Stub `HomeActivity` boots into a centred "foyer 0.6.0" label.

Crash fix in alpha.2: brls's nanovg-deko3d backend reads its renderer
shaders from `romfs:/shaders/{fill_vsh,fill_aa_fsh,fill_fsh}.dksh`.
We bundle them via a configure-time `uam` invocation (mirrors borealis's
`gen_dksh()` macro inline since we don't include their toolchain).

### Phase B — Settings  ✅ alpha.4

- `FoyerSettingsTab` with brls `SelectorCell`s for Language. Theme
  picker dropped — brls reads the system Light/Dark via
  `setsysGetColorSetId` automatically, no foyer-side palette needed.
- All strings i18n-keyed (en-US / es / pt-BR JSON catalogues).
- `SettingsActivity` wraps the tab in an `AppletFrame`. Reachable
  from the Home action row.

### Phase C — Home carousel  ✅ alpha.4 → alpha.10

Per user direction "use whatever brls has, no custom":

- `HScrollingFrame` containing a row Box of `SystemTile`s.
- Each tile is a 280-px Box with the alekfull splash absolute-
  positioned at 100%/100% FILL.
- Clock + Wireless + Battery cluster at the top right (custom layout
  — brls's stock `BottomBar` puts them at the bottom-left).
- Per-system app backdrop swaps on tile focus
  (`HomeActivity::onSystemFocused`).
- Focused-system display name above the carousel.

Layout fixes shipped: `HScrollingFrame` height bumped to 320 so the
focus highlight doesn't clip the bottom of every tile, equal-grow
column wrappers so the action row centres between tile-bottom and
the separator.

### Phase D — System / Game / Search  ✅ alpha.16

- `library_state` module owns the cached scan. `main()` calls
  `rescan()` after i18n init; activities pull from
  `library_state::find_system()`.
- `SystemActivity` renders the system's games as `DetailCell`s. Click
  pushes `GameActivity`.
- `GameActivity` shows title + path + Play button. Play calls
  `launch_game()` and quits brls so libnx drains the chain-launch.
- `SearchActivity` filters across every scanned system via
  case-insensitive substring match. Reachable from a 6th round
  button on the action row.

### First-run wizard  🔧 alpha.19 → alpha.21+

Sequenced ahead of finishing E/F polish per user request.

`WizardActivity` runs the first time foyer launches (gated on
`/foyer/data/first_run_complete`). Steps:

  0. Welcome
  1. Initial cores         — alpha.20 ships the real selection +
                              background `CoreInstallJob` kickoff.
  2. Bezel packs           — alpha.21 wires `install_bezels` from
                              the BezelManifest.
  3. Shader packs          — alpha.21 wires `install_shaders` from
                              the ShaderManifest.
  4. ScreenScraper account — alpha.21 ships username + password
                              `InputCell`s writing through
                              `scrapers::set_account_field`.
  5. SteamGridDB API key   — alpha.21 ships `InputCell` writing
                              through `set_account_field`.
  6. Done — writes the marker, kicks queued install workers, pops
            to Home. The launcher is usable immediately; downloads
            keep running in the background.

Boot path: pushes `HomeActivity` first, then `WizardActivity` on top
when the marker is missing. Pop on Finish lands cleanly on the
running Home with no empty-stack flicker. B intentionally not bound
on the wizard so the marker writes consistently.

Per-run logs at `/foyer/data/logs/<YYYY-MM-DD_HH-MM-SS>.log` so
crash reports always have a fresh, scoped paper trail.

### Phase E — modals (Power, profile dialogs, banners)  🔧 alphas 17 / 18 — finish after wizard

- ✅ Power slide-in (`PowerActivity`) with Sleep / Restart /
   Power off / Reboot to Hekate. Translucent scrim + right-anchored
   panel; B or tap-outside dismisses.
- ✅ News / eShop / Gallery action buttons swap silent log no-ops
   for "coming soon" `brls::Dialog` banners.
- ⏳ Profile-switch picker (waits on Phase F secondary roster).
- ⏳ Boot / scrape / install progress hints via `brls::Hint`.

### Phase F — accountsService + i18n bridge  🔧 alpha.18 — finish after wizard

Reduced scope: theme injection dropped (system theme is the source of
truth). Remaining work is `hos_status`:

- ✅ Active user avatar + nickname pulled from libnx
   `accountsService`. Avatar JPEG bytes retained alongside the nvg
   handle so `brls::Image::setImageFromMem` can paint them
   (`Image` doesn't wrap an existing nvg handle).
- ⏳ Secondary profile roster + tap-to-switch flow.
- ⏳ Top-bar nickname display.

### Phase G — cutover release  ⏳ pending

- Drop legacy `views.cpp` / `theme.cpp` / `boot_splash.cpp` /
  `session.cpp` / `switch_titles.cpp` from the build. The browser
  becomes pure brls-driven.
- Final `v0.6.0` tag. Migration notes for existing 0.5.x users.

### NRO size budget

| Stage     | Size  | Notes                                       |
|-----------|-------|---------------------------------------------|
| 0.5.26    | 45 MB | nanovg only, retrofix backgrounds at 1280   |
| 0.6.0-α.1 | 49 MB | + borealis library                          |
| 0.6.0-α.6 | 70 MB | retrofix bg restored to 1280                |
| 0.6.0-α.20| 71 MB | + scanner / launch / search / wizard        |

NSP-forwarder load context handles 70 MB fine; users on hbloader
chain-launch may want a future shrink pass.

---

## 0.7.0 — libretro Dolphin (planned)

GameCube + Wii via foyer's libretro frontend. The standalone-launcher
fallback (`/switch/dolphin/dolphin.nro` chain-launch) stays as a path
for users who already have it working, but the goal is libretro-native
so saves / cheats / overlay / chain-back are uniform with the rest of
the catalogue.

Scope:

- libretro `dolphin` recipe in foyer-cores: build against tico-dolphin
  or upstream libretro-dolphin (whichever's more current on libnx),
  bundle the asset directories Dolphin needs, patch for the Switch's
  GLES3-only HwContext.
- Per-rom save plumbing through `retro_get_memory` + savestate.
- Verified working list: 5+ commercial titles + at least one Wii
  title hitting 30 fps on docked Switch.

Depends on the JIT NACP capability and dynarec memory plumbing left
over from 0.2.x. Dolphin's PowerPC interpreter is too slow for real
games — JIT is the gate.

## 0.8.0 — netplay (planned)

Wire libretro's `RETRO_ENVIRONMENT_NETPACKET_*` callbacks for the
cores that support it (snes9x, fceumm, gambatte, mgba, beetle_psx_hw,
…). Switch's network stack via libnx covers the UDP socket and the
few-hundred-KB/s state-sync traffic.

Scope:

- Netplay callbacks in the shared frontend.
- Browser UI: a "Host" / "Join code" pair on `GameActivity`.
- Tested with at least 2-player NES + SNES on a LAN.

Depends on the Phase G brls cutover landing first — the netplay UI
is built natively in brls, not retrofitted into legacy view code.

## Phase H (post-0.6.0) — players on borealis

Apply the same brls shell to the per-system player NROs: pause menu,
achievement toasts, boot splash, save state browser, error dialogs.
Game viewport becomes a `brls::View` driving `retro_run()` and
blitting libretro's output texture (Moonlight Switch is the reference
implementation for this pattern).

Defer-until-stable: each player NRO would FetchContent borealis on
first configure (~50 MB clone) and link ~5 MB of brls code, so we
land it once browser is rock-solid and the integration is debugged.
Tagged 0.7.0 alpha at earliest.

---

Drafts; cadence depends on hardware test cycles. Issues / PRs / "please
add X" requests welcome at
https://github.com/Foyer-Frontend/foyer/issues.
