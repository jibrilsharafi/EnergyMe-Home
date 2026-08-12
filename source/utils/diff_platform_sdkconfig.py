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


def sentinel_state(lines: list[str]) -> bool:
    return any(line.strip() == f"{SENTINEL_FLAG}=y" for line in lines)


def print_sides(old_label: str, old_lines: list[str], new_label: str, new_lines: list[str], keep) -> None:
    for line in old_lines:
        if keep(line):
            print(f"only in {old_label}: {line}")
    for line in new_lines:
        if keep(line):
            print(f"only in {new_label}: {line}")


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
    args = parser.parse_args()

    if args.detect:
        detect(Path(args.old), Path(args.new))
        return

    # Sequential on purpose: concurrent fetches to the GitHub CDN got reset.
    old_lines = fetch_sdkconfig(args.old, args.target)
    new_lines = fetch_sdkconfig(args.new, args.target)

    old_set, new_set = set(old_lines), set(new_lines)
    old_only = sorted(old_set - new_set)
    new_only = sorted(new_set - old_set)

    print(f"\n=== MEMORY-RELEVANT DIFF ({args.target}) ===")
    print_sides(args.old, old_only, args.new, new_only, MEMORY_FLAG_PATTERN.search)
    print(f"\n=== FULL DIFF ({len(old_only)} + {len(new_only)} lines) ===")
    print_sides(args.old, old_only, args.new, new_only, lambda _: True)

    old_sentinel = sentinel_state(old_lines)
    new_sentinel = sentinel_state(new_lines)
    if old_sentinel != new_sentinel:
        print(
            f"\nWARNING: {SENTINEL_FLAG} is "
            f"{'SET' if old_sentinel else 'NOT set'} in {args.old} but "
            f"{'SET' if new_sentinel else 'NOT set'} in {args.new}. "
            "This flag decides whether WiFi/LWIP buffers live in PSRAM or "
            "internal RAM - see the platform warning in source/platformio.ini."
        )
        if not args.no_fail_on_sentinel:
            sys.exit(2)
    else:
        print(f"\n{SENTINEL_FLAG}: consistent between versions")


if __name__ == "__main__":
    main()
