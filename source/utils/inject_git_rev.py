# SPDX-License-Identifier: GPL-3.0-or-later
# PlatformIO pre-script: generates <build dir>/generated/git_rev.h with the short
# commit hash for the firmware image descriptor and the HEAD commit's committer
# unix time for the NTP time floor. Falls back to "unknown" / 0 (no floor) outside
# a git checkout (e.g. a source tarball build).
#
# A header rather than a -DGIT_REV flag: a global define changes every object's
# command line, so each commit forced a full rebuild. The header is only rewritten
# when HEAD changes and only the few files that need it include it.
import os
import subprocess

Import("env")  # noqa: F821  (PlatformIO SCons context)


def _git(*args):
    return subprocess.check_output(
        ["git", *args],
        cwd=env["PROJECT_DIR"],
        stderr=subprocess.DEVNULL,
    ).decode().strip()


rev = "unknown"
try:
    rev = _git("rev-parse", "--short=8", "HEAD") or "unknown"
except Exception as e:
    print(f"inject_git_rev: git revision unavailable ({e}), image descriptor will say 'unknown'")

# The firmware cannot be running before its source commit existed, so this is a
# safe lower bound for the clock on a device that has never synced.
commit_unix_time = 0
try:
    commit_unix_time = int(_git("log", "-1", "--format=%ct", "HEAD"))
except Exception as e:
    print(f"inject_git_rev: commit time unavailable ({e}), no build-time floor for NTP")

gen_dir = os.path.join(env.subst("$BUILD_DIR"), "generated")
header = os.path.join(gen_dir, "git_rev.h")
content = (
    "#pragma once\n"
    f'#define GIT_REV "{rev}"\n'
    f"#define GIT_COMMIT_UNIX_TIME {commit_unix_time}ULL\n"
)
os.makedirs(gen_dir, exist_ok=True)
try:
    with open(header) as f:
        current = f.read()
except OSError:
    current = None
if current != content:
    with open(header, "w") as f:
        f.write(content)

env.Append(CPPPATH=[gen_dir])
