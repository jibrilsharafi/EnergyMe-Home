# SPDX-License-Identifier: GPL-3.0-or-later
# PlatformIO pre-script: generates <build dir>/generated/git_rev.h with the short
# commit hash for the firmware image descriptor. Falls back to "unknown" outside a
# git checkout (e.g. a source tarball build).
#
# A header rather than a -DGIT_REV flag: a global define changes every object's
# command line, so each commit forced a full rebuild. The header is only rewritten
# when the hash changes and only app_image_descriptor.cpp includes it.
import os
import subprocess

Import("env")  # noqa: F821  (PlatformIO SCons context)

rev = "unknown"
try:
    out = subprocess.check_output(
        ["git", "rev-parse", "--short=8", "HEAD"],
        cwd=env["PROJECT_DIR"],
        stderr=subprocess.DEVNULL,
    )
    rev = out.decode().strip() or "unknown"
except Exception:
    pass

gen_dir = os.path.join(env.subst("$BUILD_DIR"), "generated")
header = os.path.join(gen_dir, "git_rev.h")
content = f'#pragma once\n#define GIT_REV "{rev}"\n'
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
