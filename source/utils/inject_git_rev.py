# SPDX-License-Identifier: GPL-3.0-or-later
# PlatformIO pre-script: injects -DGIT_REV="<short hash>" for the firmware
# image descriptor. Falls back to "unknown" outside a git checkout (e.g. a
# source tarball build).
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

env.Append(CPPDEFINES=[("GIT_REV", env.StringifyMacro(rev))])
