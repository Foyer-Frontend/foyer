#!/usr/bin/env python3
"""i18n_scan — find user-visible strings in views.cpp that aren't yet
in the i18n catalogue.

The browser UI hard-codes English strings during 0.3.x. As we migrate
to compile-time-keyed translations (StringId enum in shared/i18n/),
this script keeps tabs on what's still untranslated by:

  1. Parsing every StringId entry out of shared/i18n/i18n.hpp.
  2. Extracting the matching English text out of i18n.cpp's
     kEnglishStrings = [] {...}() initialiser.
  3. Walking browser/src/views.cpp and reporting:
       - hard-coded string literals that look like UI text and aren't
         present in the catalogue (candidates for translation)
       - StringId entries the catalogue declares but views.cpp never
         calls (dead keys)

Run from the repo root: `python3 tools/i18n_scan.py`. Exits 0 if
nothing surprising shows up (or with a summary report otherwise);
non-zero exit signals 'follow-up needed' for CI hooks later.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HPP  = ROOT / "shared" / "i18n" / "i18n.hpp"
CPP  = ROOT / "shared" / "i18n" / "i18n.cpp"
VIEWS = ROOT / "browser" / "src" / "views.cpp"

# Strings shorter than this are too noisy to flag (mostly punctuation,
# format specifiers, opcodes, single-character widget glyphs).
MIN_LITERAL_LEN = 3

# Filter out non-UI literals that match the heuristic but shouldn't
# get translated: file paths, URL fragments, format specifiers, asset
# keys, etc. Keep this list short — over-filtering hides real misses.
SKIP_PATTERNS = [
    # Filesystem-y / asset paths — never user-facing.
    re.compile(r"^/(foyer|systems|romfs|fmt|switch|atmosphere)/"),
    re.compile(r"^romfs:/"),
    re.compile(r"^\.?\./"),
    re.compile(r"\.(png|jpg|jpeg|jsonc?|nro|hpp|h|cpp|c|dksh|ico|wad|cue|chd|m3u|so|a)$"),
    re.compile(r"^https?://"),
    re.compile(r"^[a-z_]+://"),
    # Header-include lines (`#include "foo.hpp"` shows up as 'foo.hpp').
    re.compile(r"^[a-z][a-z0-9_/]*\.(?:hpp|h|cpp|c)$"),
    # printf format strings — start with %d/%s/etc. or contain \n.
    re.compile(r"%[0-9.+\-#0 ]*[lhjztL]*[diouxXeEfgGaAcspn%]"),
    re.compile(r"^\["),    # log prefixes like "[img]", "[net]"
    re.compile(r"\\n|\\t|\\r"),  # any string with explicit escapes
    # Internal sentinel keys / enum-ish markers.
    re.compile(r"^__"),
    re.compile(r"^auto-"),
    re.compile(r"^[A-Z_][A-Z0-9_]*$"),  # CONSTANT_CASE
    # Single tokens with no spaces likely aren't UI text. Tighten
    # by requiring either a space OR a punctuation hint that suggests
    # human-readable copy.
]


def looks_like_ui_text(s: str) -> bool:
    """Heuristic: only flag literals that smell like user copy."""
    # Must contain a space, OR start with a capital and contain
    # mixed-case letters (e.g. single-word labels like "Settings"
    # which would otherwise pass the simple no-space filter only
    # accidentally).
    if " " in s:
        return True
    if s and s[0].isupper() and any(c.islower() for c in s[1:]):
        # Filter known non-UI single-words (paths, protocol tokens
        # already handled by SKIP_PATTERNS above; this catches
        # things that survived).
        return True
    return False


def parse_string_ids() -> list[str]:
    """Return the StringId enum entries (excluding the trailing
    kStringIdCount sentinel) in declaration order."""
    text = HPP.read_text()
    m = re.search(
        r"enum class StringId\s*:[^{]*\{(.*?)\}\s*;",
        text, flags=re.DOTALL,
    )
    if not m:
        sys.exit("i18n_scan: couldn't locate StringId enum in i18n.hpp")
    body = m.group(1)
    ids = []
    for line in body.splitlines():
        line = line.split("//", 1)[0].strip().rstrip(",")
        if not line or line.startswith("//"):
            continue
        if line == "kStringIdCount":
            continue
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", line):
            ids.append(line)
    return ids


def parse_english_catalogue() -> dict[str, str]:
    """Pair each StringId with its kEnglishStrings text."""
    text = CPP.read_text()
    out: dict[str, str] = {}
    for m in re.finditer(
        r"a\[\(std::size_t\)StringId::([A-Za-z_][A-Za-z0-9_]*)\]"
        r"\s*=\s*((?:\"[^\"]*\"\s*)+);",
        text,
    ):
        key = m.group(1)
        # Concatenate adjacent string literals — e.g. "foo" "bar" → "foobar".
        parts = re.findall(r'"([^"]*)"', m.group(2))
        out[key] = "".join(parts)
    return out


def extract_views_literals() -> list[tuple[int, str]]:
    """All non-trivial double-quoted literals in views.cpp, with line
    numbers. Filters out preprocessor / include / format-specifier
    noise via SKIP_PATTERNS."""
    if not VIEWS.exists():
        return []
    out = []
    for lineno, line in enumerate(VIEWS.read_text().splitlines(), start=1):
        # Drop // comments; multi-line /*...*/ comments aren't worth
        # parsing properly for this script — false positives there are
        # rare enough.
        line_nocomment = line.split("//", 1)[0]
        for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', line_nocomment):
            s = m.group(1)
            if len(s) < MIN_LITERAL_LEN:
                continue
            if any(p.search(s) for p in SKIP_PATTERNS):
                continue
            if not looks_like_ui_text(s):
                continue
            out.append((lineno, s))
    return out


def main() -> int:
    ids = parse_string_ids()
    cat = parse_english_catalogue()

    declared_no_text = [k for k in ids if k not in cat]
    if declared_no_text:
        print("• StringIds declared in i18n.hpp but missing from "
              "kEnglishStrings:")
        for k in declared_no_text:
            print(f"    {k}")
        print()

    cat_text_set = set(cat.values())
    literals = extract_views_literals()

    untranslated: list[tuple[int, str]] = []
    for lineno, lit in literals:
        if lit in cat_text_set:
            continue
        untranslated.append((lineno, lit))

    if untranslated:
        print(f"• {len(untranslated)} hardcoded literal(s) in views.cpp "
              "not present in i18n catalogue:")
        for lineno, lit in untranslated[:50]:
            print(f"    views.cpp:{lineno:>5}  {lit!r}")
        if len(untranslated) > 50:
            print(f"    ... and {len(untranslated) - 50} more")
        print()

    # Catalogue keys never used.
    if VIEWS.exists():
        views_text = VIEWS.read_text()
        unused = [k for k in ids
                  if f"StringId::{k}" not in views_text]
        if unused:
            print(f"• {len(unused)} StringId(s) not referenced from "
                  "views.cpp (dead keys, or pending migration):")
            for k in unused[:30]:
                print(f"    {k}")
            if len(unused) > 30:
                print(f"    ... and {len(unused) - 30} more")
            print()

    if not (declared_no_text or untranslated):
        print("i18n: catalogue clean, views.cpp fully migrated.")
        return 0

    return 1 if declared_no_text else 0


if __name__ == "__main__":
    sys.exit(main())
