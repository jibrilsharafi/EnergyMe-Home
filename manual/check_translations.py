#!/usr/bin/env python3
"""Check whether translated manual files are still in sync with their English sources.

Each translated file under ``manual/<lang>/*.md`` must begin with a comment of the form::

    <!-- translation
    source: <english-filename>
    source-commit: <commit-sha>
    translated: YYYY-MM-DD
    -->

This script walks every two-letter language subdirectory under ``manual/``,
reads that header, asks ``git log`` for the latest commit that touched the
English source file, and reports drift when the recorded SHA does not prefix
the current SHA.

Exit code 0 if everything is in sync, 1 if any translation is stale or
missing provenance metadata. Stdlib only; works the same on Windows, Linux,
and macOS. Suitable for CI and as a pre-commit hook.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

LANG_DIR_RE = re.compile(r"^[a-z]{2}$")

HEADER_RE = re.compile(
    r"<!--\s*translation\s*"
    r"\r?\n\s*source:\s*(?P<source>\S+)\s*"
    r"\r?\n\s*source-commit:\s*(?P<sha>[0-9a-f]{7,40})\s*"
    r"\r?\n",
    re.IGNORECASE,
)


def repo_root() -> Path:
    """Return the repo root, anchored on the script's location."""
    return Path(__file__).resolve().parent.parent


def git_last_commit_for(path: Path) -> str | None:
    """Return the SHA of the most recent commit that touched ``path``, or None."""
    try:
        result = subprocess.run(
            ["git", "log", "-1", "--format=%H", "--", str(path)],
            capture_output=True,
            text=True,
            check=True,
            cwd=repo_root(),
        )
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    sha = result.stdout.strip()
    return sha or None


def check_file(translated: Path, manual_dir: Path) -> tuple[str | None, str | None]:
    """Check one translated file.

    Returns a tuple ``(drift, missing)`` where each is either a human-readable
    message or ``None``.
    """
    try:
        content = translated.read_text(encoding="utf-8")
    except OSError as exc:
        return None, f"{translated.relative_to(manual_dir.parent)}: cannot read ({exc})"

    match = HEADER_RE.search(content)
    if not match:
        return None, f"{translated.relative_to(manual_dir.parent)}: no provenance header found"

    source_name = match.group("source")
    recorded = match.group("sha")
    source_path = manual_dir / source_name
    if not source_path.exists():
        return None, f"{translated.relative_to(manual_dir.parent)}: source `{source_name}` not found"

    current = git_last_commit_for(source_path)
    if not current:
        return None, f"{translated.relative_to(manual_dir.parent)}: cannot resolve current commit for `{source_name}`"

    if not current.startswith(recorded):
        msg = (
            f"{translated.relative_to(manual_dir.parent)}: "
            f"recorded {recorded} but {source_name} is now at {current[:7]}"
        )
        return msg, None

    return None, None


def main() -> int:
    manual_dir = repo_root() / "manual"
    if not manual_dir.is_dir():
        print(f"manual/ not found at {manual_dir}", file=sys.stderr)
        return 2

    language_dirs = sorted(
        p for p in manual_dir.iterdir() if p.is_dir() and LANG_DIR_RE.match(p.name)
    )
    if not language_dirs:
        print("No language subdirectories found under manual/.")
        return 0

    drift: list[str] = []
    missing: list[str] = []

    for lang_dir in language_dirs:
        print(f"Checking {lang_dir.name}...")
        for translated in sorted(lang_dir.glob("*.md")):
            d, m = check_file(translated, manual_dir)
            if d:
                drift.append(d)
            if m:
                missing.append(m)

    print()
    if drift:
        print("Translations out of sync with English source:")
        for line in drift:
            print(f"  - {line}")
        print()

    if missing:
        print("Translations with missing or unreadable provenance:")
        for line in missing:
            print(f"  - {line}")
        print()

    if not drift and not missing:
        print("All translations are in sync.")
        return 0

    return 1


if __name__ == "__main__":
    sys.exit(main())
