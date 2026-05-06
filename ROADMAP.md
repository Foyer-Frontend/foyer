# foyer roadmap

Three minor releases planned after 0.2.x stabilises. Each is feature-focused;
0.2.x stays in maintenance mode until everything below the line is green on
hardware.

## 0.2.x — stabilise + ship a final patch

Hardware testing on Switch is the gate. Outstanding before the final 0.2.x tag:

- **Cores known broken**
  - `gpsp` — interpreter shipped in v0.2.29 (dynarec disabled). Proper fix:
    patch `gpsp_jit_switch.c` to translate emit-time write addresses to
    rx-alias addresses before branches, so the dynarec can run on Switch
    without dropping into the C interpreter. Restore `HAVE_DYNAREC=1`
    once that lands.
  - `mesen` — `mesen_hdpacks` defaulted to disabled in v0.2.29 to dodge
    the `HdPackLoader::ProcessPatchTag` null-deref. Optional follow-up:
    upstream fix or a recipe-level guard so users who flip the option
    on don't crash when the rom has no matching pack.
  - `ppsspp` — GLES2 backend shipped in foyer-cores v0.2.28 + the
    `RETRO_HW_FRAME_BUFFER_VALID` sentinel fix in foyer v0.2.55. Needs
    hardware verification on multiple games to confirm the GL path
    is stable end-to-end.
- **Cores CI rebuilds everything on every tag.** Per-core version is
  derived from the nro's sha256, and rebuilds aren't bit-identical, so
  a recipe change to one core triggers "update available" for all 28.
  Fix: matrix should diff `recipes/<core>.cmake` against the previous
  release tag, only rebuild changed cores, and pull unchanged nros from
  the prior release before publishing the merged manifest.
- **Switch JitMemory NACP capability.** Several cores want JIT and
  Switch homebrew gates `svcMapJitMemory` behind an NACP capability we
  don't currently declare. Adding it unblocks the gpsp-dynarec follow-
  up above and any future core that needs runtime code generation.

Final 0.2.x tag is whichever release has every box above checked.

## 0.3.0 — UI translations

Full i18n for the browser UI. Every user-visible string in `views.cpp`,
the boot splash, banners, popup labels, and the option-picker action
verbs becomes a translation key. Locale comes from the Switch system
language; foyer falls back to English when a string is missing.

Scope:
- `shared/i18n.{hpp,cpp}` — small static catalogue keyed by enum/id, no
  runtime parsing. One C++ struct per language; foyer ships English +
  whichever languages get community translations before tag.
- Tooling: a script that scans `views.cpp` for localisable strings and
  reports new untranslated keys against the catalogues so additions
  don't silently regress non-English builds.
- Player nros stay English-only — the libretro overlay is mostly core
  text + button hints; not worth the size cost in 28 player binaries.

## 0.4.0 — Dolphin (GameCube + Wii) at 100%

Get GameCube + Wii running through foyer's libretro frontend. The
existing recipe set (the standalone-launcher path that chains into
`/switch/dolphin/dolphin.nro`) stays as a fallback for users who
already have it working, but the goal is libretro-native so foyer
manages saves / cheats / overlay / chain-back uniformly.

Scope:
- libretro `dolphin` recipe in foyer-cores: build against tico-dolphin
  or upstream libretro-dolphin (whichever is more current on libnx),
  bundle whatever asset directories the libretro init needs (sysconf,
  fonts), patch the build for Switch's GLES3-only HwContext.
- Per-rom save plumbing through retro_get_memory + savestate.
- Verified working list: 5+ commercial titles + at least one Wii title
  hitting 30 fps on docked Switch.

Depends on: 0.3.0 i18n, plus a working JIT path on Switch (Dolphin's
PowerPC interpreter is too slow for real games — see 0.2.x JitMemory
NACP item).

## 0.5.0 — Network play where possible

Wire up libretro's netplay protocol for the cores that support it
(snes9x, fceumm, gambatte, mgba, mednafen_psx_hw, ...). Switch's
network stack via libnx is reachable via libcurl (already used for
manifests + downloads); netplay needs a UDP socket and a few hundred
KB/s of state-sync traffic, both within Switch's network budget.

Scope:
- libretro `RETRO_ENVIRONMENT_NETPACKET_*` callbacks wired in foyer's
  shared frontend.
- Browser UI: a "Host" / "Join code" pair on the GameDetail view.
- Tested with at least 2-player NES + SNES on a LAN.

Depends on: 0.3.0 i18n (so the netplay UI is translatable from day
one), 0.4.0 Dolphin (because Dolphin netplay is its own protocol that
we'll want consistent shape with).

---

Drafts; cadence depends on hardware test cycles. Issues, PRs, or
"please add X to 0.3" requests welcome at
https://github.com/Foyer-Frontend/foyer/issues.
