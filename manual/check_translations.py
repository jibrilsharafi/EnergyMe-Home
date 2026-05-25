#!/usr/bin/env python3
"""Check whether translated manual files are still in sync with their English sources.

Translations live under ``manual/<lang>/<filename>.md`` and are paired with
the English source at ``manual/<filename>.md`` (same filename). The sync
check compares the **last commit that touched each file**:

* if the translation's last-touching commit is at least as recent as the
  source's, the translation is considered in sync (it was updated together
  with the source, or after it);
* if the source has been touched more recently than the translation, the
  translation is flagged as drifted.

A single commit that edits both the source and the translation is treated
as synced automatically.

Exit code 0 if everything is in sync, 1 if any translation is missing its
English source or has drifted. Stdlib only; works the same on Windows,
Linux, and macOS. Suitable for CI and as a pre-commit hook.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

LANG_DIR_RE = re.compile(r"^[a-z]{2}$")


def repo_root() -> Path:
    """Return the repo root, anchored on the script's location."""
    return Path(__file__).resolve().parent.parent


def git_last_commit_info(path: Path) -> tuple[str, int] | None:
    """Return ``(sha, committer_timestamp)`` of the most recent commit that touched ``path``.

    Returns ``None`` if git has no record of the file (untracked, or git not
    available).
    """
    try:
        result = subprocess.run(
            ["git", "log", "-1", "--format=%H %ct", "--", str(path)],
            capture_output=True,
            text=True,
            check=True,
            cwd=repo_root(),
        )
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    line = result.stdout.strip()
    if not line:
        return None
    parts = line.split()
    if len(parts) < 2:
        return None
    try:
        return parts[0], int(parts[1])
    except ValueError:
        return None


def check_file(translated: Path, manual_dir: Path) -> tuple[str | None, str | None]:
    """Check one translated file. Returns ``(drift, missing)`` messages or ``None``."""
    source_path = manual_dir / translated.name
    if not source_path.exists():
        return None, f"{translated.relative_to(manual_dir.parent)}: no English source named `{translated.name}` in manual/"

    source_info = git_last_commit_info(source_path)
    if source_info is None:
        return None, f"{translated.relative_to(manual_dir.parent)}: cannot resolve git history for `{translated.name}`"
    source_sha, source_ts = source_info

    translation_info = git_last_commit_info(translated)
    if translation_info is None:
        return None, f"{translated.relative_to(manual_dir.parent)}: cannot resolve git history for this file"
    translation_sha, translation_ts = translation_info

    if translation_ts >= source_ts:
        return None, None

    msg = (
        f"{translated.relative_to(manual_dir.parent)}: last touched at {translation_sha[:7]}, "
        f"but manual/{translated.name} has been touched more recently at {source_sha[:7]}"
    )
    return msg, None


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
        print("Translations with no matching English source:")
        for line in missing:
            print(f"  - {line}")
        print()

    if not drift and not missing:
        print("All translations are in sync.")
        return 0

    return 1


if __name__ == "__main__":
    sys.exit(main())
