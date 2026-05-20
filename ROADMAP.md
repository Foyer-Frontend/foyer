# foyer roadmap

Living document. Past releases are kept brief. 0.6.0 gets phase-by-phase
detail because it was a renderer-level rewrite shipped incrementally as
alphas; the active 0.7.x cycle is core-stabilisation polish, surfaced
here as a per-core compat matrix instead of phases. Issues / PRs /
"please add X" requests welcome at
https://github.com/Foyer-Frontend/foyer/issues.

---

## Status snapshot

| Release | Status   | Headline                                                |
|---------|----------|---------------------------------------------------------|
| 0.2.x   | shipped  | First public cores set + libretro frontend              |
| 0.3.0   | shipped  | Catalogue expansion + per-game overrides                |
| 0.4.0   | shipped  | Compile-time i18n catalogue (en / es / pt-BR)           |
| 0.5.x   | shipped  | HOS-launcher chrome refresh (custom nanovg)             |
| 0.6.0   | shipped  | borealis cutover + first-run wizard                     |
| 0.7.x   | active   | Core stabilisation — every shipping core at 100%        |
| 0.8.0   | planned  | libretro Dolphin (gated on JIT NACP cap + gpsp dynarec) |
| 0.9.0   | planned  | netplay (Plutonium pause-menu UI, gated on Dolphin)     |

---

## 0.2.x — first public cores (shipped)

Hardware-stable libretro frontend with the initial core set
(fceumm / snes9x / gambatte / mgba / genesis_plus_gx / mupen64plus_next /
ppsspp / swanstation / yabasanshiro / handy / prosystem). Per-system
chain-launch via dedicated player NROs, save plumbing through
retro_get_memory, bezel + cheat seed paths, libretro-thumbnails fallback
scraper.

Open from this line that follows us into 0.8.0:

- `gpsp` ships with the dynarec disabled — a write-side address
  translation in `gpsp_jit_switch.c` is the missing piece. No
  investigation done yet.
- Switch JIT NACP capability not yet declared. Blocks gpsp dynarec +
  any future core that needs runtime code generation. The
  Dolphin work in 0.8.0 depends on this.

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

## 0.6.0 — borealis cutover + first-run wizard (shipped)

Replaced foyer's hand-rolled nanovg rendering with
[XITRIX/borealis](https://github.com/XITRIX/borealis) on the
`moonlight_wiliwili` branch (the active fork used by Moonlight Switch).
brls brought the right HOS chrome out of the box, a Yoga flexbox layout
engine, and a deko3d backend; the cutover removed ~6 KLOC of foyer-side
rendering code and gave us widgets we'd otherwise hand-build.

Shipped incrementally as `v0.6.0-alpha.N` tags through `v0.6.0`
final, then continued as `0.6.x` point releases. The user-facing
milestones are below — every Phase closed.

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

### First-run wizard  ✅ shipped rough; polish queued to 0.7.x

Sequenced ahead of finishing E/F polish per user request. Shipped
in 0.6.0 with the step skeletons + persistence wiring; UX polish
(credential round-trip checks, install progress/failure UI, core
selection sanity) is queued under the **0.7.x polish bucket** below.

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

### Phase E — modals (Power, profile dialogs, banners)  ✅ shipped

- ✅ Power slide-in (`PowerActivity`) with Sleep / Restart /
   Power off / Reboot to Hekate. Translucent scrim + right-anchored
   panel; B or tap-outside dismisses.
- ✅ News / eShop / Gallery action buttons swap silent log no-ops
   for "coming soon" `brls::Dialog` banners.
- ✅ Profile-switch picker (`HomeActivity::openProfilePicker` —
   vertical list of avatar+nickname rows, `hos_status::switch_active`
   swaps the active user, dialog closes on selection).
- ⏭️ Boot / scrape / install progress hints via `brls::Hint` —
   never wired into the brls activities in 0.6.0. Moved to the
   **0.7.x polish bucket** below; it's a quality-of-life item, not
   a 0.6.0 closure blocker.

### Phase F — accountsService + i18n bridge  ✅ shipped

Reduced scope: theme injection dropped (system theme is the source of
truth). Final `hos_status` work:

- ✅ Active user avatar + nickname pulled from libnx
   `accountsService`. Avatar JPEG bytes retained alongside the nvg
   handle so `brls::Image::setImageFromMem` can paint them
   (`Image` doesn't wrap an existing nvg handle).
- ✅ Secondary profile roster + tap-to-switch flow
   (`hos_status::other_avatar_count` / `other_avatar_jpeg` /
   `other_nickname` feed the picker; `switch_active(idx, vg)`
   swaps the active user and reloads avatar state).
- ✅ Top-bar nickname display
   (`HomeActivity` renders `hos_status::nickname()` inline with the
   avatar at the top-left of the home view).

### Phase G — cutover release  ✅ shipped

- Dropped legacy `views.cpp` / `views.hpp` / `theme.cpp` /
  `boot_splash.cpp` / `session.cpp` / `switch_titles.cpp` /
  `seed_assets.cpp` from disk. The browser is pure brls-driven.
- `mtp.cpp` was refactored off the legacy view layer (not dropped)
  and is wired into the brls activities — last touched in
  `0.6.117: drop MTP logs mount`.
- Tagged `v0.6.0`. Migration notes for existing 0.5.x users shipped
  alongside the tag.

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

## 0.7.x — Core stabilisation (active)

The 0.7.x cycle's job is to bring every shipping core to **100%
working** on Switch hardware. No new cores, no new headline features —
this is the polish line. Closes when:

1. Every core in the compat matrix below is **✅** (tested PASS on
   hardware) or **❌** with an explicit reason for exclusion from the
   manifest.
2. The polish bucket (below) is empty.

Cadence: ships as continuous **0.7.N** point releases (matching the
existing `0.7.13:` / `0.7.14:` commit-prefix pattern) as each core
promotion or polish item lands. There is no explicit `0.7.0-final`
tag — the cycle closes when both criteria above are met, and the
next tag is `0.8.0-alpha.1` when Dolphin opens.

### Per-core compat matrix

Hardware testing is in progress, one core at a time. The status
column updates as each is exercised on hardware.

#### ❌ Excluded from the CI matrix

| Core | Reason |
|------|--------|
| `parallel_n64` | Upstream link errors (`memory_layout`, `g_frameCheatStatus`, `g_cheatStatus` undefined). Recipe needs a broader source-file audit or upstream rollback before re-enabling. |
| `scummvm` | Build time ~45 min on the matrix runner with no ccache hit across runs. Re-enable once cross-run object caching exists. |

#### ⚠️ Building, but with known feature gaps

| Core | Gap |
|------|-----|
| `gpsp` | Dynarec disabled — write-side address translation in `gpsp_jit_switch.c` missing. Pure-interpreter GBA. (See 0.2.x carry-over.) |
| `melonds` | ARMJIT disabled on libnx (upstream memory issues). Pure interpreter. |
| `mgba` | GL renderer excluded (`src/gba/renderers/gl.c`); software renderer only. |
| `swanstation` | Hardware GPU backends (OpenGL/Vulkan/D3D11/D3D12) stubbed for libnx. Software renderer only. |
| `mesen` | HD packs flipped from upstream-default ON to default OFF — upstream's HD-pack init path crashed on Switch (atmosphère report `01778012390`). |
| `puae` | libpng stripped from the build (any screenshot path absent). |

#### 🔧 Building, hardware test pending or in progress

`atari800`, `beetle_pce`, `beetle_pcfx`, `beetle_supergrafx`,
`beetle_vb`, `beetle_wswan`, `bsnes_hd_beta`, `caprice32`,
`dosbox_pure`, `fceumm`, `flycast`, `fmsx`, `freeintv`, `frodo`,
`gambatte`, `genesisplusgx`, `genesis_plus_gx_wide`, `gw`, `handy`,
`mame2003_plus`, `mednafen_lynx`, `mednafen_ngp`,
`mednafen_pce_fast`, `mednafen_psx_hw`, `mupen64plus`, `nestopia`,
`nxengine`, `opera`, `pcsx_rearmed`, `picodrive`, `pokemini`,
`ppsspp`, `prboom`, `prosystem`, `race`, `reminiscence`, `retro8`,
`sameboy`, `snes9x`, `snes9x2010`, `stella`, `stella2014`,
`tgbdual`, `tyrquake`, `vba_next`, `virtualjaguar`, `yabasanshiro`.

Each gets promoted to ✅ on first known-good ROM boot on hardware,
or moved to ⚠️ / ❌ if the test surfaces a real issue.

### Polish bucket

Quality-of-life items folded into 0.7.x rather than carried as a
separate release line:

- **brls::Hint progress hints** — wire boot / scrape / install
  progress into the brls Hint slot. Skipped in 0.6.0; remained the
  only Phase E item that didn't land.
- **Wizard: ScreenScraper credentials round-trip check** — attempt
  a token fetch on Next; block save on auth failure with a clear
  error.
- **Wizard: SteamGridDB API key validation** — ping the API with
  the supplied key before persisting.
- **Wizard: Bezel / shader pack install progress + failure UI** —
  surface download progress, retry on failure, no silent hangs.
- **Wizard: Core selection state validation** — catch core picks
  the device can't actually run; explain why before saving.
- **Player → browser per-game writeback on clean exit**
  (`per-game-writeback-on-clean-exit`). The plutonium player today
  doesn't persist anything it learns during a session;
  `shared/library/per_game.hpp:45` already flags playtime as the
  visible gap (sort-by-playtime UI is wired but the field is always
  zero). Scope:
  - **Transport:** player links the existing per-game write path
    from `shared/library/per_game.cpp` and rewrites
    `/foyer/data/config/per_game.jsonc` directly (no sidecar / log
    files; reuses `save_locked()`'s atomic write).
  - **Measurement:** wallclock seconds excluding pause — the
    counter ticks only while `retro_run` is actively being called in
    the inner loop. Pause-menu-open and applet-focus-loss states
    stop the ticker.
  - **Trigger:** **only** the Quit cell in the pause menu. HOS HOME,
    sleep, crash, and power-off paths intentionally lose the
    session (conservative — no half-written state when something
    unusual happens).
  - **Fields written:** playtime (added to the existing total),
    last_played (refreshed to end-of-session timestamp so "Recent"
    reflects actual play not just launch), per-game runahead (if
    changed via pause overlay), per-game shader (if changed via
    pause overlay).

## 0.8.0 — libretro Dolphin (planned)

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
over from 0.2.x — both still open at the start of 0.7.x. Dolphin's
PowerPC interpreter is too slow for real games; JIT is the gate.
0.8.0 cannot open until both prereqs land.

## 0.9.0 — netplay (planned)

Wire libretro's `RETRO_ENVIRONMENT_NETPACKET_*` callbacks for the
cores that support it (snes9x, fceumm, gambatte, mgba,
mednafen_psx_hw, …). Switch's network stack via libnx covers the
UDP socket and the few-hundred-KB/s state-sync traffic.

Scope:

- Netplay callbacks in the shared frontend.
- **Plutonium pause-menu UI**: Host / Join code modals, peer list,
  latency hints. The player NROs stay on Plutonium, so the in-game
  networking UX lives in the Plutonium pause menu — not in the brls
  browser. (The browser side, if any, is limited to the Join-code
  share affordance.)
- Tested with at least 2-player NES + SNES on a LAN.

Depends on 0.8.0 Dolphin landing first — netplay is the next-line
work after the catalogue is complete and the cycle has successfully
shipped a new core.

---

Drafts; cadence depends on hardware test cycles. Issues / PRs / "please
add X" requests welcome at
https://github.com/Foyer-Frontend/foyer/issues.
