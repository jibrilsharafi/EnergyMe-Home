#!/usr/bin/env python3
"""
Diff the baked-in sdkconfig of two pioarduino platform versions.

The Arduino core ships as prebuilt libs whose sdkconfig is fixed at build
time; application code cannot override it, and `pio run` RAM totals do not
reveal changes to it. A dropped flag there caused the 2026-08-12 fleet OTA
incident (CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP: WiFi/LWIP buffers silently
moved from PSRAM to internal RAM). Run this before ANY platform bump.

Each platform release zip contains a platform.json that pins the
framework-arduinoespressif32-libs package by URL; this script downloads both
zips, follows those URLs, extracts <target>/sdkconfig from each libs tarball
and prints a memory-relevant filtered diff followed by the full diff.

Exits non-zero when CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP differs between the
two versions (the known fleet-incident sentinel) unless --no-fail-on-sentinel
is given, and always on download/resolution errors.

Usage:
    uv run source/utils/diff_platform_sdkconfig.py 55.03.32 55.03.311
    uv run source/utils/diff_platform_sdkconfig.py <old-zip-url> <new-zip-url> --target esp32s3
"""

import argparse
import json
import re
import sys
import tarfile
import tempfile
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
DOWNLOAD_CHUNK_BYTES = 1 << 20


def platform_zip_url(version_or_url: str) -> str:
    if version_or_url.startswith("http"):
        return version_or_url
    return PLATFORM_ZIP_URL.format(version=version_or_url)


def download(url: str, dest: Path, label: str) -> None:
    print(f"Downloading {label}: {url}")
    try:
        with urllib.request.urlopen(url, timeout=60) as response, open(dest, "wb") as out:
            while True:
                chunk = response.read(DOWNLOAD_CHUNK_BYTES)
                if not chunk:
                    break
                out.write(chunk)
    except OSError as error:
        sys.exit(f"ERROR: failed to download {label} ({url}): {error}")


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


def extract_sdkconfig(libs_tarball: Path, target: str, label: str) -> list[str]:
    suffix = f"{target}/sdkconfig"
    with tarfile.open(libs_tarball) as archive:
        for member in archive:
            if member.name.endswith(suffix):
                extracted = archive.extractfile(member)
                if extracted is None:
                    continue  # dir/link with a matching name; keep scanning
                return extracted.read().decode("utf-8", errors="replace").splitlines()
    sys.exit(f"ERROR: {suffix} not found in libs package for {label}")


def fetch_sdkconfig(version_or_url: str, target: str, workdir: Path) -> list[str]:
    label = version_or_url
    platform_zip = workdir / f"platform-{Path(label).name}.zip"
    download(platform_zip_url(version_or_url), platform_zip, f"platform {label}")
    libs_url = resolve_libs_url(platform_zip, label)
    libs_tarball = workdir / f"libs-{Path(label).name}.tar.xz"
    download(libs_url, libs_tarball, f"core libs for {label}")
    return extract_sdkconfig(libs_tarball, target, label)


def sentinel_state(lines: list[str]) -> bool:
    return any(line.strip() == f"{SENTINEL_FLAG}=y" for line in lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("old", help="current platform version (e.g. 55.03.32) or zip URL")
    parser.add_argument("new", help="proposed platform version or zip URL")
    parser.add_argument("--target", default="esp32s3", help="chip target (default: esp32s3)")
    parser.add_argument(
        "--no-fail-on-sentinel",
        action="store_true",
        help=f"do not exit non-zero when {SENTINEL_FLAG} differs",
    )
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        workdir = Path(tmp)
        old_lines = fetch_sdkconfig(args.old, args.target, workdir)
        new_lines = fetch_sdkconfig(args.new, args.target, workdir)

    old_only = sorted(set(old_lines) - set(new_lines))
    new_only = sorted(set(new_lines) - set(old_lines))

    print(f"\n=== MEMORY-RELEVANT DIFF ({args.target}) ===")
    for line in old_only:
        if MEMORY_FLAG_PATTERN.search(line):
            print(f"only in {args.old}: {line}")
    for line in new_only:
        if MEMORY_FLAG_PATTERN.search(line):
            print(f"only in {args.new}: {line}")

    print(f"\n=== FULL DIFF ({len(old_only)} + {len(new_only)} lines) ===")
    for line in old_only:
        print(f"only in {args.old}: {line}")
    for line in new_only:
        print(f"only in {args.new}: {line}")

    old_sentinel = sentinel_state(old_lines)
    new_sentinel = sentinel_state(new_lines)
    if old_sentinel != new_sentinel:
        print(
            f"\nWARNING: {SENTINEL_FLAG} is "
            f"{'SET' if old_sentinel else 'NOT set'} in {args.old} but "
            f"{'SET' if new_sentinel else 'NOT set'} in {args.new}. "
            "This flag decides whether WiFi/LWIP buffers live in PSRAM or "
            "internal RAM - see the 2026-08-12 OTA incident (issue #191)."
        )
        if not args.no_fail_on_sentinel:
            sys.exit(2)
    else:
        print(f"\n{SENTINEL_FLAG}: consistent between versions")


if __name__ == "__main__":
    main()
