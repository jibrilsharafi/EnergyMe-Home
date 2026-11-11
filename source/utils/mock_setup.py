# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 Jibril Sharafi
"""Stateful mock of the first-setup and phase-check API, for mock_server.py.

Prototype support for the openspec change add-phase-check: the page under test is
html/setup.html. Everything is in memory and restarts with the server. Line outcomes
are scripted by detection order so every state can be seen on the page:
1st line verified, 2nd choose (L1/L3 swapped, truth first), 3rd fix (one CT reversed,
its current still shown), 4th choose with the configuration first (low PF), 5th never
leaves collecting (third CT reads no current), the rest verified. The first
single-phase load/grid/PV channel on a three-phase device is suspect (wired on L2).
"""

import time

CHANNELS = 16
ROLE_READING = {
    "grid": (2400, 0.91), "pv": (690, 0.97), "load": (1400, 0.90),
    "battery": (900, 0.98), "inverter": (1500, 0.98), "bidirectional": (800, 0.95),
}


def _factory_channels():
    chans = []
    for i in range(CHANNELS):
        chans.append({
            "index": i, "active": i == 0, "reverse": False, "label": f"Channel {i}",
            "phase": 1, "groupLabel": f"Group {i}", "role": "grid" if i == 0 else "load",
            "ctSpecification": {"currentRating": 50.0 if i == 0 else 30.0, "voltageOutput": 0.333, "scalingFraction": 0.0},
        })
    return chans


STATE = {
    "channels": _factory_channels(),
    "cloud": True,
    "voltagePhase": 1,
    "setupDone": False,
    "startedAt": None,  # setup/start ("Run setup again") opens a 2-hour check window
    "savedAt": None,
    "generation": 1,
    "lineGen": {},
    "resolved": {},  # lowestChannel -> apply time; ("single", index) -> True once applied
    "energyResets": 0,
}
WINDOW_S = 2 * 3600


def _window_open():
    return not STATE["setupDone"] or (STATE["startedAt"] is not None and time.time() - STATE["startedAt"] < WINDOW_S)


def _gen(low):
    """Per-line generation: any channel save, or an apply to this line, moves it."""
    return STATE["generation"] * 1000 + STATE["lineGen"].get(low, 0)


def _lines():
    by_group = {}
    for c in STATE["channels"]:
        if c["active"] and c["phase"] in (1, 2, 3):
            by_group.setdefault(c["groupLabel"], []).append(c)
    lines = []
    for label, chans in by_group.items():
        if len(chans) == 3 and len({c["phase"] for c in chans}) == 3 and len({c["role"] for c in chans}) == 1:
            lines.append(sorted(chans, key=lambda c: c["phase"]))
    return sorted(lines, key=lambda l: min(c["index"] for c in l))


def _reading(role, k=1.0, pf=None):
    p, f = ROLE_READING.get(role, (1000, 0.9))
    return {"power": round(p * k), "pf": round(pf if pf is not None else f, 2)}


def _amps(r):
    return round(r["power"] / 230 / max(r["pf"], 0.2), 1) if r["power"] else 0.0


def _line_view(order, chans):
    role = chans[0]["role"]
    low = min(c["index"] for c in chans)
    age = (time.time() - STATE["savedAt"]) if STATE["savedAt"] else 0
    need = 6 + 4 * order  # seconds here, minutes on the device
    good = [_reading(role, k) for k in (1.0, 0.98, 1.02)]
    base = {
        "lowestChannel": low, "groupLabel": chans[0]["groupLabel"], "role": role,
        "generation": _gen(low), "candidates": [],
        "channels": [{"index": c["index"], "label": c["label"], "phase": c["phase"], "reverse": c["reverse"],
                      "current": _amps(good[i])} for i, c in enumerate(chans)],
    }

    def view(state, readings, candidates=()):
        return {**base, "state": state, "readings": readings, "candidates": list(candidates)}

    if order == 4:  # third CT not clamped: two channels carry current, the third none, forever
        if age >= need * 0.4:
            base["channels"][2]["current"] = 0.0
        else:
            for c in base["channels"]:
                c["current"] = 0.0
        return view("collecting", None)
    if age < need:  # the load starts part way through the wait
        if age < need * 0.4:
            for c in base["channels"]:
                c["current"] = 0.0
        return view("collecting", None)
    if STATE["resolved"].get(low) is not None or order == 0 or order >= 5:
        return view("verified", good)
    if order == 1:  # L1 and L3 swapped: the truth first, its 60-degree alias second
        return view("choose", [_reading(role, 0.95, 0.72), _reading(role, 1.26, 0.94), {"power": 0, "pf": 0}], [
            {"phases": [3, 2, 1], "reverse": [False, False, False], "readings": good},
            {"phases": [2, 1, 3], "reverse": [True, True, True], "readings": [_reading(role, 0.9, 0.55)] * 3}])
    if order == 2:  # one CT reversed: its power is clamped to 0 W, its current is not
        rev = [c["reverse"] for c in chans]
        rev[1] = not rev[1]
        return view("fix", [good[0], {"power": 0, "pf": 0}, good[2]],
                    [{"phases": [c["phase"] for c in chans], "reverse": rev, "readings": good}])
    # order 3, low PF: the configuration stays first
    return view("choose", [_reading(role, 0.36, 0.26)] * 3, [
        {"phases": [c["phase"] for c in chans], "reverse": [c["reverse"] for c in chans], "readings": [_reading(role, 0.36, 0.26)] * 3},
        {"phases": [2, 3, 1], "reverse": [False, False, False], "readings": [_reading(role, 1.0, 0.72)] * 3}])


def _singles():
    """Single-phase checks: load, grid and PV channels outside lines, on a three-phase device only."""
    in_lines = {c["index"] for l in _lines() for c in l}
    act = [c for c in STATE["channels"] if c["active"]]
    if STATE["voltagePhase"] == 1 and not any(c["phase"] in (2, 3) for c in act):
        return []
    age = (time.time() - STATE["savedAt"]) if STATE["savedAt"] else 0
    out = []
    singles = [c for c in act if c["index"] not in in_lines and c["role"] in ("load", "grid", "pv")]
    for n, c in enumerate(singles):
        r = _reading(c["role"])
        view = {"index": c["index"], "label": c["label"], "groupLabel": c["groupLabel"], "role": c["role"],
                "phase": c["phase"], "reverse": c["reverse"], "current": _amps(r) if age >= 4 else 0.0, "options": []}
        if age < 8:
            out.append({**view, "state": "collecting", "reading": None})
        elif n == 0 and not STATE["resolved"].get(("single", c["index"])):
            # Wired on L2 (L3 when configured on L2): anything else reads off-band.
            truth = 2 if c["phase"] != 2 else 3
            other = next(p for p in (1, 2, 3) if p not in (truth, c["phase"]))
            out.append({**view, "state": "suspect", "reading": _reading(c["role"], 0.5, 0.31), "options": [
                {"phase": truth, "reverse": False, **r},
                {"phase": other, "reverse": True, **_reading(c["role"], 0.8, 0.6)}]})
        else:
            out.append({**view, "state": "ok", "reading": r})
    return out


def handle(method, path, body):
    """Return (payload, status) for a handled route, or None."""
    if path == "/api/v1/ade7953/channel" and method == "GET":
        return STATE["channels"], 200
    if path == "/api/v1/ade7953/channels" and method == "PUT":
        body = body or []
        for c in body:
            STATE["channels"][int(c["index"])] = {**STATE["channels"][int(c["index"])], **c}
        STATE["channels"][0]["active"] = True
        if len(body) == 1:  # a single-phase option applied: only that channel's check settles
            STATE["resolved"][("single", int(body[0]["index"]))] = True
        else:  # the circuits step: every check starts over
            STATE["savedAt"] = time.time()
            STATE["resolved"] = {}
        STATE["generation"] += 1
        return {"success": True}, 200
    if path == "/api/v1/mqtt/cloud-services":
        if method == "PUT":
            STATE["cloud"] = bool((body or {}).get("enabled"))
        return {"enabled": STATE["cloud"]}, 200
    if path == "/api/v1/ade7953/voltage-phase":
        if method == "PUT":
            vp = (body or {}).get("voltagePhase")
            if vp not in (1, 2, 3):
                return {"error": "invalid_value", "message": "voltagePhase must be 1, 2 or 3"}, 400
            STATE["voltagePhase"] = vp
        return {"voltagePhase": STATE["voltagePhase"]}, 200
    if path == "/api/v1/ade7953/phase-check" and method == "GET":
        if not _window_open():
            return {"window": False, "lines": [], "singles": []}, 200
        return {"window": True, "lines": [_line_view(i, l) for i, l in enumerate(_lines())], "singles": _singles()}, 200
    if path == "/api/v1/ade7953/phase-check/apply" and method == "POST":
        body = body or {}
        low = body.get("lowestChannel")
        for l in _lines():
            if min(c["index"] for c in l) == low:
                # Phases and flags are keyed by channel index: the line's order follows the phases, so it moves on apply.
                want = dict(zip(body.get("channels", []), zip(body.get("phases", []), body.get("reverse", []))))
                if sorted(want) != sorted(c["index"] for c in l):
                    return {"error": "invalid_value", "message": "channels must be the line's channels"}, 400
                if all((c["phase"], c["reverse"]) == want[c["index"]] for c in l):
                    return {"success": True}, 200  # already applied: a repeat changes nothing
                if body.get("generation") != _gen(low):
                    return {"error": "stale", "message": "The proposal changed, reload it"}, 409
                for c in l:
                    ph, rv = want[c["index"]]
                    ch = STATE["channels"][c["index"]]
                    if ch["label"] == f"{ch['groupLabel']} L{ch['phase']}":  # generated label follows the phase
                        ch["label"] = f"{ch['groupLabel']} L{ph}"
                    ch["phase"] = ph
                    STATE["channels"][c["index"]]["reverse"] = rv
                STATE["resolved"][low] = time.time()
                STATE["lineGen"][low] = STATE["lineGen"].get(low, 0) + 1
                return {"success": True}, 200
        return {"error": "not_found", "message": "No such line"}, 404
    if path == "/api/v1/setup/status" and method == "GET":
        return {"setupDone": STATE["setupDone"], "checkWindow": _window_open(), "energyResets": STATE["energyResets"]}, 200
    if path == "/api/v1/setup/start" and method == "POST":
        STATE["startedAt"] = time.time()
        return {"success": True}, 200
    if path == "/api/v1/setup/complete" and method == "POST":
        if not STATE["setupDone"]:  # first setup only: the install starts from zero
            STATE["energyResets"] += 1
        STATE["startedAt"] = None
        STATE["setupDone"] = True
        if "cloudServices" in (body or {}):  # cloud starts only now, so no update restarts the meter mid-setup
            STATE["cloud"] = bool(body["cloudServices"])
        return {"success": True}, 200
    if path == "/api/v1/mock/setup-reset" and method == "POST":
        STATE.update(channels=_factory_channels(), cloud=True, voltagePhase=1, setupDone=False, startedAt=None, savedAt=None,
                     resolved={}, lineGen={}, energyResets=0)
        return {"success": True}, 200
    return None
