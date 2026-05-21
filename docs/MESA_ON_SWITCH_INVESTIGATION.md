# Mesa on Switch — investigation notes

Status: research/scoping artifact. Not a proposal yet. Captures the
state of foyer's GL stack on Switch as of 2026-05-21 and the
realistic path to getting HW-rendering libretro cores (swanstation,
flycast, ppsspp, mupen64plus, dolphin) working.

## Background

Foyer's player NRO uses libretro cores. Cores that need OpenGL
(swanstation HW, flycast, ppsspp, mupen64plus HW, dolphin) currently
don't render on real Switch hardware. Symptoms range from "no boot"
to "blank screen with audio playing" (mupen64plus).

The original framing was: "Switch needs Mesa, foyer doesn't have it,
that's the project." This investigation found that framing was wrong.

## Findings

### 1. Mesa for Switch is already in devkitPro

`switch-mesa` and `switch-libdrm_nouveau` ship as standard pacman
packages in the devkitPro toolchain. Files present at
`$DEVKITPRO/portlibs/switch/lib/`:

- `libEGL.a`
- `libGLESv2.a` (includes `nouveau_switch_winsys.c.o` baked in)
- `libGLESv1_CM.a`
- `libglapi.a`
- `libdrm_nouveau.a`

The Mesa version is 20.1.0 (released June 2020). The Switch
nouveau Gallium driver, codegen, and Switch-specific winsys are all
statically linked into `libGLESv2.a`. **There is no "fork Mesa for
Switch" problem to solve.** The libs exist.

### 2. RetroArch uses these same packages

RetroArch's Switch port (`libretro/RetroArch`) does not maintain a
private Mesa fork. They link against devkitPro's switch-mesa via
their build container and use a standard EGL + GLES2 path in
`gfx/drivers_context/switch_ctx.c` (~250 LOC). Their context init
binds the EGL surface to `nwindowGetDefault()` and creates a GLES2
context with `EGL_CONTEXT_CLIENT_VERSION = 2`. The whole Switch GL
backend is ~250 lines of generic EGL.

### 3. Tico's `/mesa-new` is a private newer Mesa build

Tico's CMakeLists references absolute paths inside `/mesa-new/`
containing `libnouveau.a`, `libnouveau_codegen.a`,
`libnouveauwinsys.a`, `libdrm_nouveau.a` etc. as **separate** static
libs. devkitPro's switch-mesa packages these into the umbrella
libGLESv2.a instead. The structural difference suggests tico built
their own newer Mesa (Mesa 24.x has shipped numerous nouveau driver
fixes since 20.1.0). The source isn't published.

### 4. `AxioDL/mesa` is a museum piece

Old (Mesa 18.3, Sept 2018 last touch), tied to a libnx API surface
that has since moved. Reusing as a starting point is ~60-70% the
effort of starting fresh, with the added burden of an obsolete Mesa
base. Skip.

### 5. Foyer's GL integration is already mostly correct

`foyer/shared/libretro/video_hw.cpp` implements the standard
libretro HW render pattern:

- EGL display via `eglGetDisplay(EGL_DEFAULT_DISPLAY)`
- 7-step probe chain to find a working EGL config (depth/stencil
  flexibility, surface-type fallback to surfaceless)
- GLES2/3 context selected from the core's stated requirement
- Surfaceless context (`eglMakeCurrent` with `EGL_NO_SURFACE`) when
  pbuffer support is unavailable
- FBO with color texture + depth/stencil renderbuffer at the size
  the core reports
- `glReadPixels` readback into a CPU buffer
- Row-flip + RGBA→BGRA swizzle
- Hand the CPU frame to the existing software video sink, which
  feeds Plutonium / SDL2

`foyer/player/CMakeLists.txt` links `EGL GLESv2 glapi drm_nouveau`
from devkitPro's portlibs. The HW render request callback
(`HwContext::request`) properly rejects desktop GL / Vulkan and
accepts GLES2/3. `frontend.cpp` correctly handles
`RETRO_HW_FRAME_BUFFER_VALID = (void*)-1` as a sentinel from the
core.

Comments in `video_hw.cpp` reference real iteration ("v0.2.28
hardware run", "PPSSPP null-derefs deep in its graphics init"). This
code has been hardened.

## What actually fails

Without hardware logs the candidate causes are (most likely first):

1. **`eglChooseConfig` returns 0 matches** despite the 7-step
   probe chain. devkitPro's Mesa 20.1.0 ships a sparse config
   catalogue on Switch. If all probes fail, foyer falls through
   with "core may fall back to software" — most HW cores can't,
   so we get blank screen.

2. **Surfaceless context binding** (`EGL_NO_SURFACE` in
   `eglMakeCurrent`) may have edge-case bugs in Mesa 20.1.0's
   GLES3 path. RA avoids this by binding to a real `nwindow`
   surface.

3. **FBO color readback returns blank** despite the core drawing
   into it. Possible Mesa nouveau bug in 20.1.0 specific to
   off-screen FBO color attachments. Less likely but possible.

The diagnostic logging just landed (`shared/libretro/video_hw.cpp`,
following commit) prints, on first ensure_context:

- EGL vendor / version / client APIs
- The full list of available EGL configs (first 12) with
  rgba/depth/stencil/renderable/surface bits
- Which probe slot matched
- After `eglMakeCurrent`: `GL_VENDOR / GL_RENDERER / GL_VERSION /
  GL_SHADING_LANGUAGE_VERSION`
- On first `end_frame`: whether `glReadPixels` returned non-zero
  data (frame OK) or all zeros (FBO not painted)

This is enough to localise the failure to one of:

- "no probe matched" → config selection bug
- "matched probe X but context create failed" → context attrs wrong
- "context OK but renderer = software rasterizer" → nouveau not
  selected, fallback to llvmpipe / softpipe
- "context OK, renderer = NV (Maxwell), but readback all zero" →
  Mesa FBO bug or core-side draw not happening

## Project shape

This is **not** the multi-month "fork Mesa for Switch" project.

### Phase 1 — Diagnose (cheap, days)

- Land the diagnostic logging (this commit)
- Run on hardware with mupen64plus
- Read the log; classify the failure

### Phase 2 — Fix the current Mesa 20.1.0 path (likely the answer)

Depending on what the log shows:

- **Config selection fails**: relax probe further, or rewrite to
  enumerate configs and pick the first one compatible with the
  core's stated requirements instead of attribute-matching.
- **Surfaceless context unsafe**: switch to RA's idiom — open a
  real nwindow surface, render to FBO normally. Either keep
  reading FBO back with glReadPixels (current path) or attempt
  GL→deko3d zero-copy interop (later optimisation).
- **Readback returns blank**: try `glFinish` before `glReadPixels`,
  try alternate FBO format (RGB565, RGBA without depth/stencil),
  or fall back to `glGetTexImage` against the color texture.

Estimated effort for Phase 2: **1-2 weeks** if the bug is in our
code or our usage. Could be a single PR.

### Phase 3 — Newer Mesa (only if Phase 2 hits a real Mesa 20.1.0 bug)

If diagnosis shows the failure is in devkitPro's Mesa 20.1.0 itself
(driver-side bug, not foyer's usage), the path is to rebuild Mesa
from current source against the devkitPro toolchain. This is what
tico did privately. Effort: **multi-week project**, gated on whether
Phase 2 actually requires it.

Sub-tasks:
- Cross-compile current Mesa via meson against devkitA64
- Apply Switch platform patches (look at devkitPro's
  `switch-mesa-20.1.0-5.patch` as the baseline; port to current
  Mesa's source layout)
- Update libdrm_nouveau Switch ioctls if any kernel-driver surface
  changed
- Package the rebuilt libs into a foyer-cores-side dependency

### Phase 4 — JIT cores

Separate from the Mesa work: swanstation, ppsspp, mupen64plus
dynarec, dolphin all need Switch JIT plumbing. Reference: tico's
public core forks (`ticohq/tico-*`) and the JIT plumbing patterns
the previous audit extracted. Two idioms:

1. `Jit_t` high-level (swanstation, mupen64plus) — ~20 lines per
   core.
2. Fastmem dual-mirror via `svcMapProcessCodeMemory` (ppsspp,
   dolphin) — requires foyer player NRO to have appropriate NPDM
   syscall permissions.

Independent of Mesa. Can run in parallel.

## Open questions for after Phase 1 hardware test

- Does devkitPro's Mesa 20.1.0 advertise EGL_KHR_surfaceless_context
  on Switch? The code assumes yes; the log will confirm.
- What's the GL_RENDERER string on real hardware? Nouveau Maxwell
  vs llvmpipe vs softpipe tells us if Mesa is using the GPU at all.
- Are there GL errors after FBO completion? `glGetError()` after
  `glCheckFramebufferStatus` would surface them.

## Not doing (decided 2026-05-21)

- Asking tico to open-source Mesa fork. Decided to use public
  references only.
- Starting from AxioDL/mesa. Museum piece, no time savings.
- Building a new Gallium driver against deko3d. Larger project than
  just using nouveau via existing tooling.
