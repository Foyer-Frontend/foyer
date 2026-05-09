# Working in the foyer-workspace

Conventions for Claude Code sessions that span the foyer + foyer-cores
repos. Read this before making changes.

## What lives where

| Concern                                  | Repo / path                          |
|------------------------------------------|--------------------------------------|
| Browser UI (brls Activities, since 0.6.0)| `foyer/browser/src/{*_activity,settings_tab,*}.cpp` + `assets/romfs/xml/` |
| Borealis theming + widgets               | XITRIX/borealis (FetchContent, `moonlight_wiliwili` branch) |
| Per-core player binary scaffolding       | `foyer/player/`                      |
| Shared libretro frontend (audio/video/input/cheevos) | `foyer/shared/libretro/` |
| Library scanner, scrapers, settings, RA  | `foyer/shared/`                      |
| First-run wizard + manifest cache        | `foyer/browser/src/wizard_activity.cpp`, `manifest_cache.cpp` |
| Account avatars / nicknames (libnx accountsService) | `foyer/browser/src/hos_status.cpp` |
| Theme assets (alekfull-NX splashes + retrofix backgrounds) | `foyer/assets/romfs/themes/foyer/` |
| Per-system metadata + which cores apply  | `foyer/shared/library/system_db.cpp` |
| libretro core CMake recipes              | `foyer-cores/recipes/`               |
| `foyer_core_static_library()` helper     | `foyer-cores/cmake/core_recipe.cmake`|
| `rcheevos` build recipe (shared static)  | `foyer-cores/recipes/rcheevos.cmake` |
| In-repo C++ shims for problem cores      | `foyer-cores/recipes/swanstation_stubs/` |

### Browser UI carve-outs (0.6.0)

- **Compiled into `foyer.nro`**: every `*_activity.cpp` (Home, System,
  Game, Search, Settings, Power, Wizard), `settings_tab.cpp`, plus
  the small data/service modules (`launch.cpp`, `library_state.cpp`,
  `manifest_cache.cpp`, `first_run.cpp`, `hos_status.cpp`,
  `power_actions.cpp`, `self_update.cpp`).
- **On-disk-but-not-compiled** (kept for cherry-picking into Phase
  G cleanup): `views.cpp`, `theme.cpp`, `boot_splash.cpp`,
  `session.cpp`, `switch_titles.cpp`, `mtp.cpp`, `seed_assets.cpp`.
  See `HANDOFF-2026-05-09.md` for the migration status of each.

## Cross-repo edits — the typical patterns

### "Add a new core"
1. Author `foyer-cores/recipes/<name>.cmake` mirroring upstream
   `Makefile.libretro`.
2. (Optional) Add a `Player-<name>` preset to `foyer/CMakePresets.json` for
   convenient single-core builds.
3. Add a `CoreDef{ "<name>", "<Display>" }` entry in
   `foyer/shared/library/system_db.cpp` under the right `kCoresXxx[]`
   array — index 0 is the system default.
4. Update `foyer/README.md`'s "Supported systems / cores" table.

### "Add a new system"
1. Decide its folder name (e.g. `atari2600`), short name (`A2600`), and
   libretro-thumbnails db slug.
2. Add a `kSystems[]` entry in `foyer/shared/library/system_db.cpp`
   pointing at one or more `CoreDef`s already defined.
3. Make sure each referenced core has a `foyer-cores/recipes/<name>.cmake`
   recipe — if not, author it (see above).
4. Add splash + logo art under `foyer/assets/romfs/systems/`.

### "Bump a core to a new upstream tag"
- Single-repo change: edit `foyer-cores/recipes/<name>.cmake`'s `GIT_TAG`.
  Foyer doesn't need a touch unless the upgrade changes a public API the
  player calls (rare — rcheevos is the only one foyer has direct calls
  into).

## Build commands (run from inside `foyer/`)

```sh
source /etc/profile.d/devkit-env.sh   # devkitPro toolchain
cmake --preset Release                # browser
cmake --build --preset Release

cmake --preset Player-stella          # one player
cmake --build --preset Player-stella

cmake --preset Players-All            # every recipe'd core
cmake --build --preset Players-All
```

`FOYER_CORES_DIR` defaults to `${CMAKE_SOURCE_DIR}/../foyer-cores`. Override
when working off a forked recipes repo.

## Things to avoid

- **Don't put C++ source in `foyer-cores/`.** Recipes only — actual core
  source comes from `FetchContent`. The lone exception is
  `swanstation_stubs/`, which holds shims that replace upstream TUs that
  can't compile against libnx.
- **Don't reach into `foyer-cores` from foyer at runtime.** The split is
  that recipes live in foyer-cores and foyer consumes them only at build
  time. The runtime contract is "downloaded `foyer-<core>.nro` lives at
  `/foyer/cores/`" — see `foyer/browser/src/launch.cpp`.
- **Don't bump rcheevos's git tag without testing every player preset.**
  rcheevos's API is pinned to what `foyer/shared/libretro/cheevos.cpp`
  calls (`rc_client_*` family) — drift breaks every player.
- **Don't move `system_db.cpp` to `foyer-cores`.** Keeping it in foyer means
  the browser knows about every system regardless of which cores happen to
  be installed.

## Status — split complete (2026-05-02)

- `foyer-cores/` is its own git repo, sibling to `foyer/`. Foyer's CMake
  resolves recipes via `FOYER_CORES_DIR` (default: sibling).
- Legacy in-tree `foyer/cores/` and `foyer/cmake/core_recipe.cmake` are
  removed; the deletion is staged in `foyer/`'s working tree, ready to
  commit.
- Still TBD: publishing `foyer-cores` to a GitHub org with the matrix
  build workflow, and pointing `foyer/.github/CODEOWNERS` at it.

## Testing what you change

- **Browser-only edits**: rebuild `foyer.nro` and copy it to
  `sdmc:/switch/foyer/foyer.nro`.
- **Recipe-only edits**: rebuild the affected `foyer-<core>.nro` and copy
  it to `sdmc:/foyer/cores/`.
- **Cross-repo edits**: rebuild both, sync both files.

There's no harness for testing rendering or input on a desktop — every
change ultimately gets exercised on Switch hardware (or Ryujinx for
non-libretro paths). Type-check + link-check via the appropriate preset
is the cheapest signal in this workspace.
