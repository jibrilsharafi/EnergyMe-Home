# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jibril Sharafi

"""Every firmware build must link esp_core_dump_write() through CrashMonitor's wrapper.

Without the wrapper an Interrupt watchdog leaves no core dump (#295). Dropping the
-Wl,--wrap flag fails silently: the wrapper still compiles and is never called.
"""

from __future__ import annotations

import configparser
import re
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parents[2]
WRAP_FLAG = "-Wl,--wrap=esp_core_dump_write"


def load_platformio() -> configparser.ConfigParser:
    config = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(";",))
    config.read(FIRMWARE / "platformio.ini", encoding="utf-8")
    return config


def build_flags(config: configparser.ConfigParser, section: str) -> str:
    """The section's build_flags with ${section.option} references expanded."""
    if not config.has_option(section, "build_flags"):
        parent = config.get(section, "extends", fallback=None)
        return build_flags(config, parent.strip()) if parent else ""

    def expand(match: re.Match) -> str:
        ref_section, option = match.group(1), match.group(2)
        if option == "build_flags":
            return build_flags(config, ref_section)
        return config.get(ref_section, option)

    return re.sub(r"\$\{([^.}]+)\.([^}]+)\}", expand, config.get(section, "build_flags"))


def test_every_firmware_env_wraps_core_dump_write():
    config = load_platformio()
    envs = [s for s in config.sections() if s.startswith("env:") and s != "env:native"]
    assert envs, "no firmware environment found: the parse itself is broken"
    missing = [env for env in envs if WRAP_FLAG not in build_flags(config, env).split()]
    assert not missing, f"{WRAP_FLAG} missing from: {', '.join(missing)}"


def test_wrapper_is_defined():
    source = (FIRMWARE / "src" / "crashmonitor.cpp").read_text(encoding="utf-8")
    assert re.search(r'extern "C" void __wrap_esp_core_dump_write\(panic_info_t \*\w+\)\s*\{', source)
