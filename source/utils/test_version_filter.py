# source/utils/test_version_filter.py
"""
Validates the major-version-constrained release filtering logic
before it is implemented in C++.

Simulates the GitHub /releases API (a list of release objects) and
finds the latest release whose major version matches the device's
current major version.
"""

import re
import sys


def parse_major(tag: str) -> int | None:
    """Return the major version integer from a tag like 'v1.2.3' or '2.0.0'.
    Returns None if the tag cannot be parsed."""
    m = re.match(r"^v?(\d+)\.\d+\.\d+$", tag.strip())
    return int(m.group(1)) if m else None


def compare_versions(current: str, available: str) -> int:
    """
    Returns:
      positive  -> current > available  (current is newer)
      0         -> equal
      negative  -> current < available  (update available)
    Mirrors the C++ _compareVersions logic.
    """
    def parts(tag):
        t = tag.lstrip("v")
        segs = t.split(".")
        return tuple(int(x) for x in segs[:3])

    c = parts(current)
    a = parts(available)
    for ci, ai in zip(c, a):
        if ci != ai:
            return ci - ai
    return 0


def find_latest_release_for_major(releases: list[dict], current_major: int) -> dict | None:
    """
    Iterate releases (assumed newest-first, as returned by GitHub) and
    return the first one whose major version matches current_major.
    Returns None if no matching release is found.
    """
    for release in releases:
        tag = release.get("tag_name", "")
        major = parse_major(tag)
        if major == current_major:
            return release
    return None


# ---------------------------------------------------------------------------
# Test cases
# ---------------------------------------------------------------------------

def run_tests():
    errors = []

    def check(description, actual, expected):
        if actual != expected:
            errors.append(f"FAIL: {description}\n  expected={expected!r}\n  got={actual!r}")
        else:
            print(f"PASS: {description}")

    # Simulated GitHub releases list (newest first, as the API returns them)
    releases = [
        {"tag_name": "v2.1.0", "assets": [], "published_at": "2026-01-01", "html_url": "http://x"},
        {"tag_name": "v2.0.0", "assets": [], "published_at": "2025-12-01", "html_url": "http://x"},
        {"tag_name": "v1.2.0", "assets": [], "published_at": "2025-11-01", "html_url": "http://x"},
        {"tag_name": "v1.1.1", "assets": [], "published_at": "2025-10-01", "html_url": "http://x"},
        {"tag_name": "v1.0.0", "assets": [], "published_at": "2025-09-01", "html_url": "http://x"},
    ]

    # --- parse_major ---
    check("parse v1.1.1", parse_major("v1.1.1"), 1)
    check("parse v2.0.0", parse_major("v2.0.0"), 2)
    check("parse without v prefix", parse_major("1.2.3"), 1)
    check("parse invalid tag returns None", parse_major("latest"), None)
    check("parse empty string returns None", parse_major(""), None)

    # --- compare_versions ---
    check("same version = 0",          compare_versions("v1.1.1", "v1.1.1"), 0)
    check("current newer patch > 0",  compare_versions("v1.1.2", "v1.1.1") > 0, True)
    check("current older patch < 0",  compare_versions("v1.1.0", "v1.1.1") < 0, True)
    check("current newer minor > 0",  compare_versions("v1.2.0", "v1.1.9") > 0, True)
    check("current older major < 0",  compare_versions("v1.0.0", "v2.0.0") < 0, True)

    # --- find_latest_release_for_major ---
    # Device is on major 1 → should find v1.2.0 (first v1.x in the list)
    result = find_latest_release_for_major(releases, current_major=1)
    check("major=1 finds v1.2.0", result["tag_name"] if result else None, "v1.2.0")

    # Device on major 2 → should find v2.1.0
    result = find_latest_release_for_major(releases, current_major=2)
    check("major=2 finds v2.1.0", result["tag_name"] if result else None, "v2.1.0")

    # No matching major → returns None
    result = find_latest_release_for_major(releases, current_major=3)
    check("major=3 finds nothing", result, None)

    # Single-entry list, exact match
    result = find_latest_release_for_major([{"tag_name": "v1.0.0", "assets": []}], 1)
    check("single entry match", result["tag_name"] if result else None, "v1.0.0")

    # Single-entry list, no match
    result = find_latest_release_for_major([{"tag_name": "v2.0.0", "assets": []}], 1)
    check("single entry no match", result, None)

    # Empty list
    result = find_latest_release_for_major([], current_major=1)
    check("empty list", result, None)

    # Non-semver tags must be ignored (e.g. "beta", "v1-rc1")
    mixed = [
        {"tag_name": "beta",   "assets": []},
        {"tag_name": "v1-rc1", "assets": []},
        {"tag_name": "v1.2.0", "assets": []},
    ]
    result = find_latest_release_for_major(mixed, current_major=1)
    check("non-semver tags skipped, finds v1.2.0", result["tag_name"] if result else None, "v1.2.0")

    # --- isLatest logic (current == latest 1.x → isLatest=True) ---
    current_version = "v1.1.1"
    latest_1x = find_latest_release_for_major(releases, current_major=1)
    is_latest = compare_versions(current_version, latest_1x["tag_name"]) >= 0
    check("v1.1.1 is NOT latest when v1.2.0 exists", is_latest, False)

    current_version = "v1.2.0"
    is_latest = compare_versions(current_version, latest_1x["tag_name"]) >= 0
    check("v1.2.0 IS latest 1.x", is_latest, True)

    # --- Summary ---
    print()
    if errors:
        for e in errors:
            print(e)
        print(f"\n{len(errors)} test(s) FAILED.")
        sys.exit(1)
    else:
        print("All tests passed.")


if __name__ == "__main__":
    run_tests()
