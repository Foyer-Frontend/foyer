# System artwork — license + attribution

The per-system logo (`<system>.png`, ~755 × 240) and splash
(`<system>-splash.png`, 454 × 1080) PNGs in this directory are
redistributed from the **Art Book Next ES-DE** theme by Anthony Caccese.

- Source repository: https://github.com/anthonycaccese/art-book-next-es-de
- Author: Anthony Caccese
- License: **Creative Commons Attribution-NonCommercial-ShareAlike 2.0**
  (CC-BY-NC-SA 2.0) — https://creativecommons.org/licenses/by-nc-sa/2.0/

Per the license you may share and adapt these images **as long as**:

1. **Attribution** — you credit Anthony Caccese (and the upstream credits
   listed in the Art Book Next repo).
2. **NonCommercial** — you do **not** use the material for commercial
   purposes.
3. **ShareAlike** — any redistribution or adaptation is published under
   the same CC-BY-NC-SA terms.

These three conditions therefore apply to any further redistribution of
**this directory's contents**. Foyer's source code itself is licensed
separately (see the project root LICENSE) — the NC clause only attaches
to the bundled artwork.

Logos were rasterised from the upstream `_inc/systems/logos/<name>.svg`
sources to PNG at 755 × 240 to match Foyer's UI layout. Splash images
are byte-identical copies of `_inc/systems/artwork/<name>.png`.

If you contribute new system art back upstream, please also send a PR
to art-book-next-es-de — this helps both projects.

Run `tools/sync-system-art.sh` to refresh this directory from a local
clone of art-book-next-es-de.
