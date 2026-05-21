#!/usr/bin/env python3
"""Build the EnergyMe Home installation manual PDFs from Markdown.

Concatenates the chapters per language in reading order and runs pandoc
with xelatex to produce one PDF per language under manual/dist/.

Requires pandoc, a TeX Live distribution providing xelatex, and librsvg
(rsvg-convert) for SVG embedding.

Usage:
  python manual/build_pdfs.py             # build all languages
  python manual/build_pdfs.py --lang en   # build a single language
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

MANUAL_DIR = Path(__file__).resolve().parent
DIST_DIR = MANUAL_DIR / "dist"

CHAPTERS = [
    "README.md",
    "01-installation.md",
    "02-setup.md",
    "03-troubleshooting.md",
    "appendices.md",
    "safety-and-compliance.md",
]

LANGUAGES = {
    "en": {
        "dir": MANUAL_DIR,
        "title": "EnergyMe Home - Installation Manual",
        "lang": "en",
    },
    "it": {
        "dir": MANUAL_DIR / "it",
        "title": "EnergyMe Home - Manuale di installazione",
        "lang": "it",
    },
    "de": {
        "dir": MANUAL_DIR / "de",
        "title": "EnergyMe Home - Installationsanleitung",
        "lang": "de",
    },
    "fr": {
        "dir": MANUAL_DIR / "fr",
        "title": "EnergyMe Home - Manuel d'installation",
        "lang": "fr",
    },
}

# In the merged PDF every chapter lives in the same document, so cross-file
# links of the form `01-installation.md#anchor` must be rewritten to plain
# `#anchor` references.
_CROSS_FILE_LINK = re.compile(r"\]\(([0-9A-Za-z_-]+)\.md(#[^)]*)?\)")


def _rewrite_links(md: str) -> str:
    def repl(m: re.Match[str]) -> str:
        anchor = m.group(2) or ""
        return f"]({anchor})" if anchor else "](#)"
    return _CROSS_FILE_LINK.sub(repl, md)


def _merge(src_dir: Path) -> str:
    parts: list[str] = []
    for ch in CHAPTERS:
        path = src_dir / ch
        if not path.is_file():
            raise SystemExit(f"missing chapter: {path}")
        parts.append(_rewrite_links(path.read_text(encoding="utf-8")))
    return "\n\n\\newpage\n\n".join(parts)


def build(lang_key: str) -> Path:
    cfg = LANGUAGES[lang_key]
    src_dir: Path = cfg["dir"]
    merged_md = _merge(src_dir)

    # Stage the merged file in the language directory so relative image paths
    # (e.g. `../assets/foo.svg` in translated files, `assets/foo.svg` in EN)
    # continue to resolve.
    with tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        suffix=".md",
        prefix=".merged-",
        dir=src_dir,
        delete=False,
    ) as fh:
        merged_path = Path(fh.name)
        fh.write(merged_md)

    out = DIST_DIR / f"EnergyMe-Home-Manual-{lang_key.upper()}.pdf"
    cmd = [
        "pandoc",
        str(merged_path),
        "-o", str(out),
        "--from=gfm+yaml_metadata_block+raw_html",
        "--pdf-engine=xelatex",
        "--toc",
        "--toc-depth=2",
        "-V", f"title={cfg['title']}",
        "-V", f"lang={cfg['lang']}",
        "-V", "geometry:margin=2cm",
        "-V", "documentclass=report",
        "-V", "linkcolor=blue",
        "-V", "urlcolor=blue",
        "-V", "toccolor=black",
    ]
    try:
        print(f"[{lang_key}] building {out.name}")
        subprocess.run(cmd, check=True)
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

    if not shutil.which("pandoc"):
        print("error: pandoc not found on PATH", file=sys.stderr)
        return 2

    DIST_DIR.mkdir(exist_ok=True)
    langs = [args.lang] if args.lang != "all" else list(LANGUAGES)
    for k in langs:
        out = build(k)
        print(f"[{k}] wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
