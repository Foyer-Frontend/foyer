# Player migration — brls/deko3d → Dear ImGui / libnx-GLES

## Why

The per-core player binary (`foyer-<core>.nro`) currently shares the GPU
between **two renderers** on Switch:

- **brls (XITRIX/borealis fork)** — uses **deko3d** through libnx for every
  pause overlay + picker activity, plus the running game frame which it
  draws as a `nvgUpdateImage`'d nanovg quad.
- **mesa-on-Switch GLES** — needed by `ShaderPipeline` (post-process
  fragment passes) and by HW-render libretro cores (PSX HW, PPSSPP) via
  `video_hw.cpp`.

These two share the same nvhost channel under libnx. As soon as the
GLES side does sustained work (~1 second of frames once a shader is
enabled), mesa stomps on deko3d's command-buffer pool and the core
crashes with `PC=0` in heap-corruption territory. This was confirmed on
v0.6.119–v0.6.121 across fceumm, mesen, and nestopia.

RetroArch on Switch does not hit this because **GLES is its only
renderer** — its menu, video, and shaders all share one GL context.

Migrating the player off brls onto Dear ImGui + GLES achieves the same
"one renderer" model and makes the shader pipeline a zero-cost in-line
pass instead of a separate context.

**Browser (`foyer.nro`) stays on brls. None of this plan touches the
browser binary.**

---

## Target architecture

```
+-----------------------------------------------------------+
| foyer-<core>.nro (player) — single GLES3 context          |
|                                                           |
|  libnx EGL window surface  (nwindowGetDefault)            |
|     |                                                     |
|     +-- libretro core run thread --------+                |
|     |       Frontend::run_frame          |                |
|     |       video_cb -> CPU RGBA buffer  |                |
|     |       glTexSubImage2D -> in_tex    |                |
|     |       ShaderPipeline::process_tex  |                |
|     |          -> shader_out_tex (FBO)   |                |
|     |       video_gl::draw quad          |                |
|     |       bezel_gl::draw quad (if any) |                |
|     |                                    |                |
|     +-- ImGui frame ---------------------+                |
|             NewFrame                     |                |
|             pause modal (when open)      |                |
|             picker modals (when open)    |                |
|             Render -> OpenGL3 backend    |                |
|     +-- eglSwapBuffers ------------------+                |
|                                                           |
|  Audio: existing AudioSink (audrv) — separate thread,     |
|  unaffected by render swap.                               |
|  Input: existing libnx pad poll, but gated on             |
|  ImGui::GetIO().WantCaptureKeyboard so the core doesn't   |
|  consume UI keys.                                         |
+-----------------------------------------------------------+
```

Single EGL context owns the swapchain, every libretro frame upload,
every shader pass, every UI pixel. No deko3d in the binary.

---

## Phase plan

Every phase ends with a buildable `foyer-fceumm.nro` AND a buildable
`foyer.nro`. fceumm is the migration vehicle until phase 6 starts
rolling other cores.

### Phase 1 — libnx-EGL + GLES3 + ImGui skeleton (~2 days)

Stand up the new shell behind a CMake option. No libretro yet.

**New files:**
- `player/cmake/imgui.cmake`
- `player/src/imgui/gl_context.{hpp,cpp}` — libnx EGL window surface on
  `nwindowGetDefault()`. Reuses the probe ladder from `video_hw.cpp`
  but adds an `EGL_WINDOW_BIT` slot in front:
  `{ ES3, WINDOW, RGBA8, depth=0, stencil=0 }`, then falls back to
  surfaceless ES3 (matches today's `video_hw.cpp` behavior).
- `player/src/imgui/imgui_switch_input.cpp` — libnx `padGetButtons` →
  `ImGuiKey_GamepadFaceDown / Right / Up / Down / L1 / R1 / Start /
  Back`, plus `hidGetTouchScreenStates` → `ImGuiIO::AddMousePosEvent`.
- `player/src/imgui/imgui_theme.cpp` — reads
  `foyer::library::config().theme_override` ("light" / "dark" / empty),
  empty falls back to `setsysGetColorSetId(&id) == ColorSetId_Dark`,
  calls `ImGui::StyleColorsDark()` or `Light()`. 1 Hz live-refresh in
  the main loop so HOS theme flips at runtime.
- `player/src/imgui/main_imgui.cpp` — main loop, draws a centered
  "Hello foyer" label, exits on `+`.

**Edits:**
- `CMakeLists.txt` — `option(PLAYER_IMGUI ...)` default OFF.
  `FetchContent_Declare(imgui …)` at v1.91.5 docking branch. Builds
  `imgui.cpp + imgui_draw.cpp + imgui_tables.cpp + imgui_widgets.cpp +
  backends/imgui_impl_opengl3.cpp` into a `foyer_imgui` static lib.
- `player/CMakeLists.txt` — 3-way branch: `PLAYER_IMGUI > PLAYER_BRLS
  > foyer_render`. When `PLAYER_IMGUI` is ON, drop the borealis link
  on the player target.

**Done when:**
- `cmake --preset Player-fceumm -DPLAYER_IMGUI=ON && cmake --build`
  produces an nro.
- nro boots on hardware → black screen + "Hello foyer" → `+` exits.
- `PLAYER_IMGUI=OFF` build still produces the brls fceumm nro.
- Browser preset still produces `foyer.nro`.

---

### Phase 2 — emulator surface + bezel as GL quads (~3 days)

Get a libretro core actually rendering through the ImGui shell.

**New files:**
- `shared/libretro/video_gl.{hpp,cpp}` — sibling of `video.cpp`.
  Receives RGBA8 buffer from `Frontend::on_frame`, uploads via
  `glTexImage2D` / `glTexSubImage2D`, draws an aspect-fit fullscreen
  quad. Implements the same `AspectMode` switch as `video.cpp::draw`
  (4:3, 16:9, Stretch, Integer1x, Integer2x, IntegerAuto).
- `shared/libretro/bezel_gl.{hpp,cpp}` — stb_image decode (already
  vendored in shader.cpp) → GL texture → fullscreen quad over the
  game frame. Reuses `bezel.cpp::resolve_path()` verbatim.
- `player/src/imgui/emulator_loop.{hpp,cpp}` — owns the main loop
  body during in-game: calls `Frontend::run_frame`, then
  `video_gl::draw`, then `bezel_gl::draw`, then ImGui NewFrame +
  Render, then `eglSwapBuffers`.

**Edits:**
- `shared/libretro/video_hw.cpp` — add
  `HwContext::attach_external(EGLDisplay, EGLContext, EGLSurface)`.
  When called, `ensure_context` short-circuits its own EGL setup and
  treats the supplied context as borrowed. HW-render cores (PSX HW,
  PPSSPP) then run on the player's single shared context.
- `player/CMakeLists.txt` — compile `video_gl.cpp` and `bezel_gl.cpp`
  only when `PLAYER_IMGUI` is ON.
- `shared/libretro/frontend.cpp` — no change to public API; the GL
  shell calls the same `run_frame` / `set_video_sink` paths.

**Done when:**
- fceumm + Super Mario Bros plays through the ImGui nro — picture +
  sound + Joy-Cons + bezel.
- ImGui FPS overlay reads ~60.
- brls build of fceumm still works (off-by-default switch).

---

### Phase 3 — ShaderPipeline native GL, zero-copy (~1.5 days)

Replace the v0.6.123 CPU shader fallback with the live GL pipeline now
that we own the context.

**Edits to `shared/libretro/shader.{hpp,cpp}`:**
- Add `init_borrowed(EGLDisplay, EGLContext, EGLSurface)` so the
  pipeline does NOT stand up its own EGL context.
- Rename `process_gles_unused` back to `process` and switch the API
  shape to:
  `GLuint process_texture(GLuint src_tex, unsigned w, unsigned h);`
  Returns the final FBO color attachment. Zero CPU readback.
- Delete the CPU implementations (`cpu_scanlines`, `cpu_crt_simple`,
  `cpu_lcd_grid`, `cpu_gb_dmg`, `cpu_gba_correct`) added in v0.6.123.
- Keep the queue-from-any-thread `set_preset` from v0.6.123 — the
  drain still runs inside `process_texture` on the GL thread.

**Edits to `shared/libretro/video_gl.cpp`:**
- When `shader_pipeline().active()` is non-empty, sample from
  `process_texture(in_tex, w, h)`'s output instead of the raw upload
  texture.

**Done when:**
- scanlines / crt_simple / lcd_grid / gb_dmg / gba_correct all render
  on fceumm with no crash after 30 min of play.
- Multi-pass JSON manifests and single-file `.glsl` presets at
  `/foyer/content/shaders/*.json` still resolve and render.

---

### Phase 4 — pause overlay + 6 picker modals (~4 days)

Functional 1:1 port of the brls picker UX onto ImGui modals. Stock
ImGui styling (dark/light per theme). NO custom widget cloning.

**New files (all under `player/src/imgui/`):**
- `pause_modal.cpp` — Resume / Restart / Save state / Load state /
  Core options / Display / Shaders / Cheats / Quit. L3+R3 opens, B
  closes, A confirms.
- `slot_modal.cpp` — 10 slots. Save mode calls `save_state(path)`;
  Load mode calls `load_state(path)`. Quit always calls
  `Frontend::flush_sram()` before `envSetNextLoad`.
- `shaders_modal.cpp` — `ShaderPipeline::available_presets()` list
  with "None" first; "Active" marker on the current preset.
- `display_modal.cpp` — `AspectMode` radios.
- `core_options_modal.cpp` — reuses `shared/libretro/core_options.cpp`
  data layer; only the picker UI is rewritten.
- `cheats_modal.cpp` — reuses `shared/libretro/cheats.cpp` data layer.

**Edits:**
- `player/src/imgui/imgui_switch_input.cpp` — gate `poll_input` on
  `ImGui::GetIO().WantCaptureKeyboard` so the game doesn't see UI keys
  while a modal is up.
- Emulator loop keeps ticking the core under any modal (audio
  doesn't pause).

**Done when:**
- Every pause-flow action reachable; every save/load round-trips;
  shader switch is visible immediately on confirm.
- fceumm on hardware: launch + play + open pause + save + load +
  exit chain-launch back to browser, all green.

---

### Phase 5 — drop brls from player binary (~1 day)

**Edits:**
- `CMakeLists.txt` — remove `PLAYER_BRLS` option entirely.
  `PLAYER_IMGUI` becomes the only player render shell. The brls
  `FetchContent` block is guarded purely by `BUILD_BROWSER`.
- `player/CMakeLists.txt` — remove the brls branch.

**Deletions:**
- `player/src/main.cpp` (brls entry — replaced by `main_imgui.cpp`).
- `player/src/emulator_activity.{cpp,hpp}`
- `player/src/emulator_view.{cpp,hpp}`
- `player/src/pause_activity.{cpp,hpp}`
- `player/src/slot_picker_activity.{cpp,hpp}`
- `player/src/shaders_picker_activity.{cpp,hpp}`
- `player/src/display_picker_activity.{cpp,hpp}`
- `player/src/core_options_picker_activity.{cpp,hpp}`
- `player/src/cheats_picker_activity.{cpp,hpp}`

**Done when:**
- `foyer-fceumm.nro` size drops noticeably (no borealis link).
- Browser `foyer.nro` builds unchanged.
- CI matrix for fceumm flips to `PLAYER_IMGUI`; everything else
  still on brls until Phase 6.

---

### Phase 6 — roll remaining cores (~0.25 day per core)

One PR per core. Each PR: flip the core's CI matrix entry from the
brls player to the ImGui player and smoke-test on hardware.

**Rollout order (lowest risk first):**

1. **SW NES/GB/SMS/Genesis** — nestopia, mesen, snes9x, snes9x2010,
   genesis_plus_gx, genesis_plus_gx_wide, picodrive, gambatte,
   sameboy, mgba, vba_next.
2. **SW PSX/PCE/VB/WSWAN/NGP** — pcsx_rearmed, beetle_psx,
   beetle_pce, beetle_supergrafx, beetle_vb, beetle_wswan,
   mednafen_ngp.
3. **SW misc** — prosystem, stella, stella2014, handy, freeintv,
   prboom, tyrquake, frodo, caprice32, gw, race, nxengine, retro8,
   reminiscence.
4. **HW-render (verify `attach_external_context`)** —
   mednafen_psx_hw, ppsspp, flycast, mupen64plus_next, melonds,
   yabasanshiro.
5. **Currently disabled** — parallel_n64, swanstation. Re-enable
   only after their build issues are resolved.

---

## FetchContent / dependency list

| Dep | Source | Pin | Notes |
|---|---|---|---|
| Dear ImGui | `ocornut/imgui` | `v1.91.5` (docking branch) | Pull core 4 TUs + `imgui_impl_opengl3.cpp`. **Skip** `imgui_impl_glfw.cpp` / `_sdl*`. Custom libnx backend (~150 LOC) — no upstream impl. |
| GLES loader | none | — | `imgui_impl_opengl3` autodetects via `IMGUI_IMPL_OPENGL_ES3`. Switch portlibs `<GLES3/gl3.h>` is sufficient. |
| Font | already in tree — `assets/embed/JetBrainsMono.ttf` if present, else subset Roboto-Regular | — | `ImGui::AddFontFromMemoryTTF`. ~250 KB embedded, no romfs lookup. |
| stb_image | already vendored in `shared/libretro/shader.cpp` | unchanged | Reused for bezel PNG decode in `bezel_gl.cpp`. |

No upstream patches required.

---

## Risks + open questions

1. **EGL window-surface config matching.** `video_hw.cpp`'s probe
   ladder only asks for pbuffer/surfaceless. The player needs a real
   `EGL_WINDOW_BIT` config bound to libnx's `NWindow*`. Open: does
   mesa-on-Switch expose any sRGB-capable window config? Without it,
   ImGui colors will be slightly off but not blocking.
2. **Sharing the HW-render context.** Today's `HwContext` makes its
   own EGL display + context; PPSSPP relies on
   `get_current_framebuffer` returning an FBO bound on that thread.
   Phase 2 must add `attach_external_context` and short-circuit
   `ensure_context` when an external context is already current.
   Risk: libretro contract says `get_current_framebuffer` is called
   from the core's run thread; we keep `retro_run` on the main
   thread, so one context is enough.
3. **Pause modal must not freeze the core.** brls's `tick_frame`
   short-circuits when the EmulatorActivity is not on top. Under
   ImGui, the loop is unconditional — the modal is just another
   `ImGui::Begin`. This is actually easier than the brls model.
4. **Touch + joy-con mapping for ImGui.**
   `imgui_switch_input.cpp` needs `padGetButtons` →
   `ImGuiKey_Gamepad*` and `hidGetTouchScreenStates` → mouse events.
   Risk: ImGui nav vs the game eating buttons — solved by gating
   `poll_input` on `ImGui::GetIO().WantCaptureKeyboard`.
5. **Bezel PNG decode path.** Currently `nvgCreateImage` (file
   path). The GL path uses stb_image + `glTexImage2D`. The current
   `invalidate_bezel(vg)` API is nanovg-shaped; phase 2 adds
   `bezel_gl::invalidate()` with no arg and gates the existing
   function out under `PLAYER_IMGUI`.

Secondary risks:
- **HOS dock/undock resize.** Need to listen for
  `appletGetOperationMode` changes and re-`glViewport`. Two passes
  per second poll is fine.
- **Bezel + shader Y-orientation.** Current shader.cpp documents
  the inversion; keep that convention end-to-end so the bezel sits
  the right way up.
- **Audio is unaffected.** libnx audio runs on its own thread
  through `AudioSink`; the render swap doesn't touch it.

---

## What stays in brls (browser)

`foyer.nro` keeps borealis + deko3d + the brls FetchContent block
(gated on `BUILD_BROWSER`). No `browser/` source file is read or
modified by this migration.

**Cross-coupling audit:**

- `shared/library/`, `shared/scrapers/`, `shared/util/`,
  `shared/net/` — provider-neutral, used by both binaries.
- `shared/libretro/*` — player-only at runtime; browser doesn't
  link `foyer_libretro_frontend_*`. Safe to evolve.
- `shared/platform/app.cpp` — used only by the legacy `foyer_render`
  shell, kept alive only for `BUILD_BROWSER`.
- No `browser/` source includes anything under `player/`.

---

## Effort summary

| Phase | Days | Smallest commit-and-test slice |
|---|---|---|
| 1 | 2.0 | "Hello foyer" black-screen nro, `+` exits, brls path still builds. |
| 2 | 3.0 | fceumm + SMB plays through ImGui nro with bezel + audio. |
| 3 | 1.5 | `shader=scanlines` boots and runs 5 min on hardware. |
| 4 | 4.0 | L3+R3 opens pause; save/load + shader switch + display +
core options + cheats all work. |
| 5 | 1.0 | brls dependency removed from `foyer-fceumm.nro`. |
| 6 | 0.25 × ~22 cores = 5.5 | One PR per core, smoke-tested. |

**Total: ~13–14 days for fceumm parity; ~19 days fleet-wide.**

---

## Tracking

Live tasks (TaskList in this repo's session):
- #14 — Phase 1
- #15 — Phase 2 (blocked by #14)
- #16 — Phase 3 (blocked by #15)
- #17 — Phase 4 (blocked by #16)
- #18 — Phase 5 (blocked by #17)
- #19 — Phase 6 (blocked by #18)
