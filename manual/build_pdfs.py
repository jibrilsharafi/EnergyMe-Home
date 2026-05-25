#!/usr/bin/env python3
"""Build the EnergyMe Home installation manual PDFs from Markdown.

Concatenates the chapters per language in reading order and runs md-to-pdf
(headless Chromium + manual/pdf.css) to produce one PDF per language under
manual/dist/.

Requires Node.js and the md-to-pdf CLI installed globally:
  npm install -g md-to-pdf

Usage:
  python manual/build_pdfs.py             # build all languages
  python manual/build_pdfs.py --lang en   # build a single language
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

MANUAL_DIR = Path(__file__).resolve().parent
DIST_DIR = MANUAL_DIR / "dist"
STYLESHEET = MANUAL_DIR / "pdf.css"

# Single source of truth for the manual's document revision. Bump these
# whenever the manual content changes in a way users/regulators should see.
# The values are substituted into `{{MANUAL_VERSION}}` / `{{MANUAL_DATE}}`
# placeholders in every language's README.md cover and
# safety-and-compliance.md product-identification table.
MANUAL_VERSION = "1.0"
MANUAL_DATE = "2026-05-21"

CHAPTERS = [
    "README.md",
    "01-installation.md",
    "02-setup.md",
    "03-troubleshooting.md",
    "appendices.md",
    "safety-and-compliance.md",
]

LANGUAGES = {
    "en": {"dir": MANUAL_DIR, "title": "EnergyMe Home - Installation Manual"},
    "it": {"dir": MANUAL_DIR / "it", "title": "EnergyMe Home - Manuale di installazione"},
    "de": {"dir": MANUAL_DIR / "de", "title": "EnergyMe Home - Installationsanleitung"},
    "fr": {"dir": MANUAL_DIR / "fr", "title": "EnergyMe Home - Manuel d'installation"},
}

# Inter-chapter page break. md-to-pdf honours the `data-pagebreak` div.
PAGEBREAK = '\n\n<div class="page-break"></div>\n\n'

# Cross-file links of the form `[text](chapter.md#anchor)` need rewriting
# because in the merged document every chapter lives on the same page.
_CROSS_FILE_LINK = re.compile(r"\[([^\]]+)\]\(([0-9A-Za-z_-]+)\.md(#[^)]*)?\)")

PDF_OPTIONS = {
    "format": "A4",
    "printBackground": True,
    "margin": {"top": "2cm", "right": "2cm", "bottom": "2cm", "left": "2cm"},
}


def _rewrite_links(md: str) -> str:
    def repl(m: re.Match[str]) -> str:
        text, _file, anchor = m.group(1), m.group(2), m.group(3) or ""
        return f"[{text}]({anchor})" if anchor else text
    return _CROSS_FILE_LINK.sub(repl, md)


def _substitute_version(md: str) -> str:
    return md.replace("{{MANUAL_VERSION}}", MANUAL_VERSION).replace(
        "{{MANUAL_DATE}}", MANUAL_DATE
    )


def _merge(src_dir: Path) -> str:
    parts: list[str] = []
    for ch in CHAPTERS:
        path = src_dir / ch
        if not path.is_file():
            raise SystemExit(f"missing chapter: {path}")
        parts.append(_substitute_version(_rewrite_links(path.read_text(encoding="utf-8"))))
    return PAGEBREAK.join(parts)


def _md_to_pdf() -> str:
    """Locate the md-to-pdf CLI; on Windows it's a .cmd shim."""
    for name in ("md-to-pdf", "md-to-pdf.cmd"):
        found = shutil.which(name)
        if found:
            return found
    print(
        "error: md-to-pdf not found on PATH. Install it with `npm install -g md-to-pdf`",
        file=sys.stderr,
    )
    sys.exit(2)


def build(lang_key: str, md_to_pdf_bin: str) -> Path:
    cfg = LANGUAGES[lang_key]
    src_dir: Path = cfg["dir"]
    merged_md = _merge(src_dir)

    # Stage the merged file in the language directory so relative image paths
    # (e.g. `../assets/foo.svg` in translated files, `assets/foo.svg` in EN)
    # continue to resolve when Chromium loads the rendered HTML.
    merged_path = src_dir / ".merged.md"
    merged_path.write_text(merged_md, encoding="utf-8")

    out = DIST_DIR / f"EnergyMe-Home-Manual-{lang_key.upper()}.pdf"
    cmd = [
        md_to_pdf_bin,
        str(merged_path),
        "--stylesheet", str(STYLESHEET),
        "--pdf-options", json.dumps(PDF_OPTIONS),
        "--launch-options", '{"args":["--no-sandbox"]}',
    ]
    try:
        print(f"[{lang_key}] building {out.name}")
        subprocess.run(cmd, check=True)
        # md-to-pdf writes alongside the input as <name>.pdf; move it to dist/.
        produced = merged_path.with_suffix(".pdf")
        if not produced.is_file():
            raise SystemExit(f"md-to-pdf did not produce {produced}")
        shutil.move(str(produced), out)
    finally:
        merged_path.unlink(missing_ok=True)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument(
        "--lang",
        choices=list(LANGUAGES) + ["all"],
        default="all",
        help="language to build (default: all)",
    )
    args = ap.parse_args()

    md_to_pdf_bin = _md_to_pdf()
    DIST_DIR.mkdir(exist_ok=True)
    langs = [args.lang] if args.lang != "all" else list(LANGUAGES)
    for k in langs:
        out = build(k, md_to_pdf_bin)
        print(f"[{k}] wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
