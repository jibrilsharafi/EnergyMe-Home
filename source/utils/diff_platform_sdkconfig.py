#!/usr/bin/env python3
"""
Diff the baked-in sdkconfig of two pioarduino platform versions.

The core's sdkconfig is fixed when Espressif builds the libs; application
code cannot override it and `pio run` RAM totals do not reveal changes to
it. Why this matters: see the warning block above `platform =` in
source/platformio.ini (2026-08-12 OTA incident). Run this before ANY
platform bump.

Exits non-zero when CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP differs between
the two versions (the incident sentinel) unless --no-fail-on-sentinel is
given, and always on download/resolution errors.

Usage:
    uv run source/utils/diff_platform_sdkconfig.py 55.03.32 55.03.311
    uv run source/utils/diff_platform_sdkconfig.py <old-zip-url> <new-zip-url> --target esp32s3
    uv run source/utils/diff_platform_sdkconfig.py old-sdkconfig.txt new-sdkconfig.txt

Each argument may be a pioarduino release version, a platform zip URL, or a
path to an already-downloaded sdkconfig file. --dump-dir saves both raw
sdkconfigs for a side-by-side view (e.g. `code --diff old new`).

CI mode (used by .github/workflows/platform-guard.yml):
    python source/utils/diff_platform_sdkconfig.py --detect base.ini head.ini
Prints `changed=`/`old=`/`new=` GitHub-output lines comparing every
platform-defining key (platform =, platform_packages, extra_configs)
between the two platformio.ini files.
"""

import argparse
import json
import re
import shutil
import sys
import tarfile
import tempfile
import time
import urllib.request
import zipfile
from pathlib import Path

PLATFORM_ZIP_URL = (
    "https://github.com/pioarduino/platform-espressif32/releases/download/"
    "{version}/platform-espressif32.zip"
)
LIBS_PACKAGE = "framework-arduinoespressif32-libs"
SENTINEL_FLAG = "CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP"
MEMORY_FLAG_PATTERN = re.compile(r"MBEDTLS|LWIP|WIFI|SPIRAM|HEAP|CACHE|BUF|MEM")
PLATFORM_KEY_PATTERN = re.compile(r"^\s*(platform\s*=|platform_packages|extra_configs)")
URL_PIN_PATTERN = re.compile(r"^platform = (https?://\S+)", re.MULTILINE)
CONFIG_SET_PATTERN = re.compile(r"^(CONFIG_[A-Za-z0-9_]+)=(.*)$")
CONFIG_NOT_SET_PATTERN = re.compile(r"^# (CONFIG_[A-Za-z0-9_]+) is not set$")
NOT_SET = "not set"
ABSENT = "absent"


def platform_zip_url(version_or_url: str) -> str:
    if version_or_url.startswith("http"):
        return version_or_url
    return PLATFORM_ZIP_URL.format(version=version_or_url)


def open_url(url: str):
    """GitHub's edge intermittently resets urllib connections (curl is fine);
    retry with growing backoff before giving up."""
    request = urllib.request.Request(url, headers={"User-Agent": "energyme-platform-guard"})
    delays = [0, 5, 15, 45]
    for attempt, delay in enumerate(delays):
        time.sleep(delay)
        try:
            return urllib.request.urlopen(request, timeout=60)
        except OSError as error:
            if attempt == len(delays) - 1:
                raise
            print(f"retrying after {error}", file=sys.stderr)
    raise AssertionError("unreachable")


def resolve_libs_url(platform_zip: Path, label: str) -> str:
    with zipfile.ZipFile(platform_zip) as archive:
        candidates = [n for n in archive.namelist() if n.endswith("platform.json")]
        if not candidates:
            sys.exit(f"ERROR: no platform.json inside platform zip for {label}")
        # Least-nested candidate: the root manifest, not a copy inside examples/
        manifest = json.loads(archive.read(min(candidates, key=lambda n: (n.count("/"), len(n)))))
    package = manifest.get("packages", {}).get(LIBS_PACKAGE, {})
    url = package.get("version", "")
    if not url.startswith("http"):
        sys.exit(f"ERROR: {LIBS_PACKAGE} URL not found in platform.json for {label}")
    return url


def fetch_sdkconfig(label: str, target: str) -> list[str]:
    if Path(label).is_file():
        return Path(label).read_text(encoding="utf-8", errors="replace").splitlines()
    try:
        with tempfile.NamedTemporaryFile(suffix=".zip", delete=False) as tmp:
            platform_zip = Path(tmp.name)
            print(f"Downloading platform {label}", file=sys.stderr)
            with open_url(platform_zip_url(label)) as response:
                shutil.copyfileobj(response, tmp)
        libs_url = resolve_libs_url(platform_zip, label)
        platform_zip.unlink()

        # Stream the ~300 MB libs tarball and stop at the sdkconfig member:
        # only the bytes up to it are ever transferred, nothing hits disk.
        suffix = f"{target}/sdkconfig"
        print(f"Streaming core libs for {label}: {libs_url}", file=sys.stderr)
        with open_url(libs_url) as response:
            with tarfile.open(fileobj=response, mode="r|xz") as archive:
                for member in archive:
                    if member.name.endswith(suffix):
                        extracted = archive.extractfile(member)
                        if extracted is None:
                            continue  # dir/link with a matching name; keep scanning
                        return extracted.read().decode("utf-8", errors="replace").splitlines()
    except OSError as error:
        sys.exit(f"ERROR: fetching sdkconfig for {label} failed: {error}")
    sys.exit(f"ERROR: {suffix} not found in libs package for {label}")


def parse_config(lines: list[str]) -> dict[str, str]:
    """Kconfig semantics: `KEY=value` and `# KEY is not set` are both explicit
    states; anything else (comments, section banners) is presentation noise."""
    values: dict[str, str] = {}
    for line in lines:
        line = line.strip()
        if match := CONFIG_SET_PATTERN.match(line):
            values[match.group(1)] = match.group(2)
        elif match := CONFIG_NOT_SET_PATTERN.match(line):
            values[match.group(1)] = NOT_SET
    return values


def print_changes(old_label: str, old_cfg: dict[str, str], new_label: str, new_cfg: dict[str, str], keys: list[str]) -> None:
    changed = sorted(k for k in keys if k in old_cfg and k in new_cfg)
    added = sorted(k for k in keys if k not in old_cfg)
    removed = sorted(k for k in keys if k not in new_cfg)
    width = max((len(k) for k in keys), default=0)
    for key in changed:
        print(f"  {key:<{width}}  {old_cfg[key]} -> {new_cfg[key]}")
    if added:
        print(f"  --- only in {new_label} ---")
        for key in added:
            print(f"  {key:<{width}}  {new_cfg[key]}")
    if removed:
        print(f"  --- only in {old_label} ---")
        for key in removed:
            print(f"  {key:<{width}}  {old_cfg[key]}")


def detect(base_ini: Path, head_ini: Path) -> None:
    """Compare platform-defining keys of two platformio.ini files and print
    GitHub-output lines (changed/old/new). Every platform line counts, so an
    env-section override or a decoy line registers as a change too."""
    base_text = base_ini.read_text()
    head_text = head_ini.read_text()

    def pins(text: str) -> list[str]:
        return sorted(l.strip() for l in text.splitlines() if PLATFORM_KEY_PATTERN.match(l))

    changed = pins(base_text) != pins(head_text)
    if changed:
        print("platform-defining lines changed:", file=sys.stderr)
        for line in sorted(set(pins(base_text)) ^ set(pins(head_text))):
            print(f"  {line}", file=sys.stderr)

    old_match = URL_PIN_PATTERN.search(base_text)
    new_match = URL_PIN_PATTERN.search(head_text)
    print(f"changed={'true' if changed else 'false'}")
    print(f"old={old_match.group(1) if old_match else ''}")
    print(f"new={new_match.group(1) if new_match else ''}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("old", help="current platform version (e.g. 55.03.32), zip URL, or base platformio.ini with --detect")
    parser.add_argument("new", help="proposed platform version, zip URL, or head platformio.ini with --detect")
    parser.add_argument("--target", default="esp32s3", help="chip target (default: esp32s3)")
    parser.add_argument(
        "--detect",
        action="store_true",
        help="treat the two arguments as platformio.ini files and print changed/old/new outputs",
    )
    parser.add_argument(
        "--no-fail-on-sentinel",
        action="store_true",
        help=f"do not exit non-zero when {SENTINEL_FLAG} differs",
    )
    parser.add_argument(
        "--dump-dir",
        type=Path,
        help="also save both raw sdkconfigs here, for `code --diff` / `git diff --no-index`",
    )
    args = parser.parse_args()

    if args.detect:
        detect(Path(args.old), Path(args.new))
        return

    # Sequential on purpose: concurrent fetches to the GitHub CDN got reset.
    old_lines = fetch_sdkconfig(args.old, args.target)
    new_lines = fetch_sdkconfig(args.new, args.target)

    if args.dump_dir:
        args.dump_dir.mkdir(parents=True, exist_ok=True)
        paths = []
        for label, lines in ((args.old, old_lines), (args.new, new_lines)):
            safe = Path(label).stem if Path(label).is_file() else re.sub(r"[^A-Za-z0-9._-]+", "_", label)
            path = args.dump_dir / f"sdkconfig-{args.target}-{safe}.txt"
            path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            paths.append(path)
        print(f"Raw sdkconfigs saved; side-by-side view:\n  code --diff {paths[0]} {paths[1]}", file=sys.stderr)

    old_cfg = parse_config(old_lines)
    new_cfg = parse_config(new_lines)
    diff_keys = [k for k in old_cfg.keys() | new_cfg.keys() if old_cfg.get(k) != new_cfg.get(k)]
    memory_keys = [k for k in diff_keys if MEMORY_FLAG_PATTERN.search(k)]

    print(f"\n=== MEMORY-RELEVANT CHANGES ({args.target}): {len(memory_keys)} keys ===")
    print_changes(args.old, old_cfg, args.new, new_cfg, memory_keys)
    print(f"\n=== ALL CHANGES ({args.target}): {len(diff_keys)} keys ===")
    print_changes(args.old, old_cfg, args.new, new_cfg, diff_keys)

    old_sentinel = old_cfg.get(SENTINEL_FLAG, ABSENT)
    new_sentinel = new_cfg.get(SENTINEL_FLAG, ABSENT)
    if old_sentinel != new_sentinel:
        print(
            f"\nWARNING: {SENTINEL_FLAG} is '{old_sentinel}' in {args.old} but "
            f"'{new_sentinel}' in {args.new}. This flag decides whether "
            "WiFi/LWIP buffers live in PSRAM or internal RAM - see the "
            "platform warning in source/platformio.ini."
        )
        if not args.no_fail_on_sentinel:
            sys.exit(2)
    else:
        print(f"\n{SENTINEL_FLAG}: consistent between versions")


if __name__ == "__main__":
    main()
