# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jibril Sharafi

"""Source scan: nothing that can block may run inside a portMUX critical section.

Interrupts are masked on the core while the spinlock is held, so a call that waits on a
FreeRTOS mutex cannot yield and spins the core into the Interrupt watchdog instead. Every
esp_wifi_* call is such a call: it takes the driver's API mutex (#295). Each call inside a
section must be on ALLOWED_CALLS; add a function only if it never blocks, logs or allocates.

A name-based regex scan: it does not see calls through function pointers, templates or
overloaded operators, and it trusts allowlisted functions without following them.
"""

from __future__ import annotations

import re
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parents[2]
SOURCES = sorted(
    path
    for folder in ("src", "include", "lib")
    for pattern in ("*.cpp", "*.h")
    for path in (FIRMWARE / folder).rglob(pattern)
)

ALLOWED_CALLS = {"memcpy", "memset", "snprintf", "CrashMonitor::canRestartNow"}
NOT_CALLS = {"if", "while", "for", "switch", "return", "sizeof"}

ENTER = re.compile(
    r"\b(?:(?:task|port)ENTER_CRITICAL(?:_ISR|_SAFE|_FROM_ISR)?|portTRY_ENTER_CRITICAL(?:_ISR|_SAFE)?"
    r"|portDISABLE_INTERRUPTS|noInterrupts)\s*\("
)
EXIT = re.compile(r"\b(?:(?:task|port)EXIT_CRITICAL(?:_ISR|_SAFE|_FROM_ISR)?|portENABLE_INTERRUPTS|interrupts)\s*\(")
LEAVE = re.compile(r"\b(?:return|goto)\b")
CALL = re.compile(r"([A-Za-z_]\w*(?:\s*(?:::|\.|->)\s*[A-Za-z_]\w*)*)\s*\(")
COMMENT_OR_LITERAL = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)


def blank_comments_and_literals(code: str) -> str:
    """Blank them out keeping newlines, so a format string or a comment is never read as code."""
    return COMMENT_OR_LITERAL.sub(lambda m: re.sub(r"[^\n]", " ", m.group()), code)


def critical_sections(code: str) -> list[tuple[int, str]]:
    """(line, body) of each section, as the code that runs with the section held.

    The section ends at the EXIT at the ENTER's own brace depth. An EXIT nested deeper is an
    early exit (`if (...) { EXIT; return; }`): the rest of its block runs outside the section
    and is left out, while the code after that block is still inside it.
    """
    code = blank_comments_and_literals(code)
    sections = []
    for enter in ENTER.finditer(code):
        line = code.count("\n", 0, enter.start()) + 1
        pos = code.index(";", enter.end()) + 1
        depth, released_at, body = 0, None, []
        while True:
            assert pos < len(code) and depth >= 0, f"line {line}: section never reaches its EXIT"
            exit_ = None if released_at is not None else EXIT.match(code, pos)
            if exit_ and depth == 0:
                break
            if exit_:
                released_at, pos = depth, exit_.end()
                continue
            char = code[pos]
            depth += {"{": 1, "}": -1}.get(char, 0)
            if released_at is not None and depth < released_at:
                released_at = None
            elif released_at is None:
                body.append(char)
            pos += 1
        sections.append((line, "".join(body)))
    return sections


def problems(body: str) -> list[str]:
    names = (re.sub(r"\s+", "", name) for name in CALL.findall(body))
    calls = [f"calls {name}()" for name in names if name not in NOT_CALLS and name not in ALLOWED_CALLS]
    return calls + [f"{word} without EXIT" for word in LEAVE.findall(body)]


def test_detects_the_driver_call_fixed_in_295():
    code = """
    taskENTER_CRITICAL(&_disconnectDiagMux);
    _readStoredSsid(_lastAttemptedSSID, sizeof(_lastAttemptedSSID));
    taskEXIT_CRITICAL(&_disconnectDiagMux);
    """
    [(_, body)] = critical_sections(code)
    assert problems(body) == ["calls _readStoredSsid()"]


def test_accepts_copies_and_ignores_text_in_literals_and_comments():
    code = """
    portENTER_CRITICAL(&_mux);
    if (_pending && CrashMonitor::canRestartNow()) {
      snprintf((char*)_reason, sizeof(_reason), "%s (WiFi.begin())", reason); // LOG_INFO(x)
      memcpy(copy, _shared, sizeof(copy));
    }
    portEXIT_CRITICAL(&_mux);
    """
    [(_, body)] = critical_sections(code)
    assert problems(body) == []


def test_follows_the_section_past_an_early_exit():
    code = """
    UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();
    if (!_ready) {
      taskEXIT_CRITICAL_FROM_ISR(saved);
      LOG_WARNING("not ready");
      return;
    }
    esp_wifi_get_config(WIFI_IF_STA, &conf);
    taskEXIT_CRITICAL_FROM_ISR(saved);
    """
    [(_, body)] = critical_sections(code)
    assert problems(body) == ["calls esp_wifi_get_config()"]


def test_flags_a_section_left_without_exit():
    code = """
    taskENTER_CRITICAL(&_mux);
    if (_busy) return;
    _count++;
    taskEXIT_CRITICAL(&_mux);
    """
    [(_, body)] = critical_sections(code)
    assert problems(body) == ["return without EXIT"]


def test_firmware_critical_sections_only_call_allowlisted_functions():
    offenders = []
    found = 0
    for path in SOURCES:
        for line, body in critical_sections(path.read_text(encoding="utf-8")):
            found += 1
            offenders += [f"{path.relative_to(FIRMWARE)}:{line}: {problem}" for problem in problems(body)]
    assert found, "no critical section found: the scan itself is broken"
    assert not offenders, "critical sections doing more than copying:\n" + "\n".join(offenders)
