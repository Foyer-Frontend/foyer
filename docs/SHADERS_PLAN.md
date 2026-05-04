# Post-process shaders — design notes

Historical design doc. The shader pipeline shipped in v0.2.25 (single-pass
GLES3) and was extended through v0.2.27 (multi-pass + LUT textures +
per-pass parameters + foyer-shaders manifest installer). Kept around to
explain *why* the pipeline looks the way it does for future contributors.

## Motivation

Add CRT / scanline / NTSC / handheld-LCD post-process shaders to the
libretro player. Users on Switch want the look of CRT-Royale, Lottes,
GB-DMG palettes, etc. on top of every core's output.

## Architecture: mirror the cores model

A separate `foyer-frontend/foyer-shaders` repo, parallel to foyer-cores.
The reasoning is the same that drove the core split:

- Slang-shader upstreams move on their own cadence; foyer doesn't need a
  release every time.
- The full pack is too large to bundle (see "Sizing"). On-device install
  lets users pull only what they want.
- License story is cleaner than asset bundling — most slang-shaders are
  GPL/MIT and explicitly redistributable.

### foyer-shaders repo layout

```
foyer-shaders/
├── README.md
├── recipes/                    # one .cmake per preset (or a single
│                               # build.cmake that walks subdirs)
├── presets/<name>/
│   ├── preset.json             # passes + parameters + LUT manifest
│   ├── pass-0.slang            # checked-in source (or fetched
│   ├── pass-N.slang            #   from libretro/slang-shaders via
│   └── lut/*.png               #   FetchContent at recipe level)
└── .github/workflows/build-shaders.yml
```

### CI workflow

Matrix per preset. Each job:

1. Compile `pass-N.slang` → SPIRV (`glslangValidator`).
2. SPIRV → deko3d native binary (`uam`).
3. Bundle `preset.json` + `pass-N.dksh` + LUTs into a `.zip` per preset.
4. Upload as a release asset.
5. Aggregate `manifest.json` (name / version / size / sha256 / url /
   pass count) — same schema as foyer-cores.

Image: reuse `ghcr.io/foyer-frontend/foyer-cores-builder` (devkitPro
toolchain has `uam` already).

## foyer side

Lift-and-shift from the cores install pipeline.

- `Config::shaders_manifest_url` defaulting to
  `https://github.com/foyer-frontend/foyer-shaders/releases/latest/download/manifest.json`.
- `library/shader_installer.{hpp,cpp}` — copy of `core_installer.*`,
  rename `CoreManifest` → `ShaderManifest`, install path
  `/foyer/shaders/<name>/`.
- Settings → Display → "Shader presets" — mirrors the Settings →
  Emulator catalog: refresh manifest, per-preset install, "installed"
  vs "install" state by directory presence.
- Settings → Display → "Active shader" — cycle picker over installed
  presets. None / per-system / per-game overrides via the existing
  `general.jsonc` / `per_game.jsonc` fields the cores already use.

## Player-side renderer

The genuinely new code. Affects every player nro, so a re-tag of all
~21 cores when this lands.

- Read `/foyer/shaders/<active>/preset.json` at game launch.
- For each pass:
  - Allocate intermediate framebuffer at pass-specified scale (1x,
    viewport, source, etc.).
  - Bind `pass-N.dksh`, source LUT samplers, parameter UBO.
  - Draw a fullscreen quad sampling from previous pass.
- Final pass writes to the swapchain image (replaces the current
  single-pass blit in `shared/libretro/video.cpp`).

Estimated 600–1000 lines of player video code. Touches the path that
every core's nro links — coordinate the rebuild.

## Sizing

Bundling everything in romfs is not viable.

| Bundle scope                           | Approx size   |
|----------------------------------------|---------------|
| 1 simple scanline preset               | 20–80 KB      |
| 1 mid CRT (Lottes / Geom / Easymode)   | 100–300 KB    |
| 1 heavy CRT-Royale variant (+ LUTs)    | 400–800 KB    |
| 1 NTSC composite chain                 | 200–600 KB    |
| Curated 10–20 preset default pack      | ~3–5 MB       |
| Full slang-shaders catalog (deduped)   | 30–80 MB      |
| Full slang-shaders catalog (no dedupe) | >100 MB       |

Plan: optional small default pack in romfs (~5 MB — Lottes, Royale,
Easymode, a couple scanlines, NTSC, GB-DMG, GBA-color), everything
else via the foyer-shaders manifest.

## License

Most libretro/slang-shaders entries are GPL or MIT. The bundled
default pack and any redistributed presets need:

- Per-preset LICENSE preserved alongside the .slang sources.
- Aggregated `LICENSE-SHADERS.md` similar to `LICENSE-ART.md`.
- README acknowledging upstream attributions.

## Suggested PR sequence

1. `foyer-shaders` repo: README + matrix workflow + 1–2 starter
   presets (Lottes + scanlines). Ships its first manifest.
2. Foyer downloader: `library/shader_installer`, Settings UI rows
   under Display. Foyer can install presets but doesn't render
   them yet. Lets us debug the catalog flow without touching the
   video pipeline.
3. Player renderer: multi-pass pipeline in
   `shared/libretro/video.cpp` + per-game/system shader resolution.
   This is the rebuild-everything change.
4. (Optional) Bundle the default pack in romfs.

## Open questions

- **Source fetch in CI** — do recipes `FetchContent` the full
  `libretro/slang-shaders` repo and pull specific files, or do we
  vendor sources per preset? Vendoring is bigger but stable;
  FetchContent is smaller but moves with upstream.
- **Parameter UI** — most presets expose tunables (mask strength,
  bloom amount, etc.). Do we expose them in Settings, or just ship
  curated presets with fixed parameters?
- **Per-preset thumbnails** — would help users browse the catalog.
  Lives where: `presets/<name>/preview.png` in foyer-shaders, served
  to the foyer installer UI? Bigger downloads.
- **LUT format** — most slang-shaders use `.png` LUTs sampled as
  3D textures via slice. Need to confirm deko3d / nanovg-deko3d
  pipeline can sample them efficiently.
