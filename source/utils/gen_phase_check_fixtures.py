# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 Jibril Sharafi
"""Emit test/test_phase_check/fixtures.h from the reference model, so the C++ phase check
(lib/phase_check) and utils/phase_solver_ref.py cannot drift.

Run from source/:  uv run --no-project python utils/gen_phase_check_fixtures.py
All inputs are synthetic (random installs with a fixed seed), never field data.
"""

import itertools
import pathlib
import random
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import phase_solver_ref as ps  # noqa: E402

OUT = pathlib.Path(__file__).parent.parent / "test" / "test_phase_check" / "fixtures.h"
ROLE_ID = {"load": 0, "grid": 1, "pv": 2, "battery": 3, "inverter": 4, "bidirectional": 5}
STATE_ID = {"collecting": 0, "verified": 1, "fix": 2, "choose": 3}
SINGLE_ID = {"collecting": 0, "ok": 1, "suspect": 2}
CHS = (3, 4, 5)


def minutes_for(rnd, vp, role, phi, true_phase, ct_rev, n, mixed, idle=None):
    out = []
    for _ in range(n):
        raw = {}
        for i, k in enumerate(CHS):
            if idle == i:
                raw[k] = None
                continue
            p_phi = phi + (rnd.gauss(0, 15) + 12 * (i - 1) if mixed else rnd.gauss(0, 3))
            if role in ps.SIGNED_ROLES and rnd.random() < 0.3:
                p_phi += 180  # exporting minutes: axial evidence must not care
            theta = p_phi - ps.shift(vp, true_phase[i]) + 180 * ct_rev[i]
            raw[k] = rnd.uniform(300, 4000) * ps.rot(theta)
        out.append(raw)
    return out


def group_from(minutes, rating):
    g = ps.GroupEvidence(CHS)
    for k in CHS:
        g.ch[k] = ps.ChannelEvidence(rating_a=rating)
    for raw in minutes:
        g.add_minute(raw)
    return g


def c(z):
    return f"{{{z.real!r}, {z.imag!r}}}"


def hyp(h):
    return "{{" + ", ".join(str(p) for p in h.phase) + "}, {" + ", ".join("true" if r else "false" for r in h.reverse) + "}}"


def line_case(g, vp, role, cfg, r, rating):
    ch = ", ".join(f"{{{rating!r}, {c(g.ch[k].e)}, {c(g.ch[k].e_axial)}, {g.ch[k].minutes!r}}}" for k in CHS)
    pairs = ", ".join(f"{{{c(p.d)}, {p.weight!r}, {p.minutes!r}}}" for p in g.pairs.values())
    props = ", ".join(hyp(h) for h in r.proposals)
    return (f"    {{{{{{{ch}}}, {{{pairs}}}}}, {vp}, {ROLE_ID[role]}, {hyp(cfg)}, {STATE_ID[r.state]}, "
            f"{len(r.proposals)}, {{{props}}}}},")


def main():
    rnd = random.Random(20261111)
    lines = []
    # Named shapes first, then random installs over every voltage phase and role.
    named = [
        ("load", 1, 45, (0, 0, 0), (1, 2, 3), (0, 0, 0), 30, False, None),
        ("load", 1, 25, (0, 1, 0), (1, 2, 3), (0, 0, 0), 30, False, None),
        ("load", 1, 22, (0, 0, 0), (3, 2, 1), (0, 0, 0), 30, False, None),
        ("load", 1, 10, (0, 0, 0), (2, 3, 1), (0, 0, 0), 30, False, None),
        ("load", 1, 80, (0, 0, 0), (1, 2, 3), (0, 0, 0), 30, False, None),
        ("load", 1, 25, (0, 0, 0), (1, 2, 3), (0, 0, 0), 30, False, 1),
        ("load", 1, 25, (0, 0, 0), (1, 2, 3), (0, 0, 0), 1, False, None),
        ("pv", 2, -10, (0, 1, 0), (1, 2, 3), (0, 0, 0), 30, False, None),
        ("inverter", 3, 5, (0, 0, 0), (2, 1, 3), (0, 0, 0), 30, False, None),
    ]
    cases = []
    for role, vp, phi, ct, cfg_phase, cfg_rev, n, mixed, idle in named:
        cases.append((role, vp, phi, ct, ps.Hypothesis(cfg_phase, tuple(bool(x) for x in cfg_rev)), n, mixed, idle))
    roles = ["load", "grid", "pv", "battery", "inverter", "bidirectional"]
    for _ in range(120):
        role = rnd.choice(roles)
        phi = rnd.uniform(-30, 5) if role == "pv" else rnd.uniform(-35, 60)
        ct = tuple(int(rnd.random() < 0.4) for _ in range(3))
        cfg = ps.Hypothesis(rnd.choice(list(itertools.permutations((1, 2, 3)))), tuple(rnd.random() < 0.4 for _ in range(3)))
        if rnd.random() < 0.25:
            cfg = ps.Hypothesis((1, 2, 3), tuple(bool(x) for x in ct))
        cases.append((role, rnd.choice((1, 2, 3)), phi, ct, cfg, rnd.choice((2, 3, 5, 30)), rnd.random() < 0.5, None))
    for role, vp, phi, ct, cfg, n, mixed, idle in cases:
        rating = rnd.choice((30.0, 75.0))
        g = group_from(minutes_for(rnd, vp, role, phi, (1, 2, 3), ct, n, mixed, idle), rating)
        lines.append(line_case(g, vp, role, cfg, ps.evaluate(g, vp, role, cfg), rating))

    singles = []
    for _ in range(60):
        role = rnd.choice(["load", "grid", "pv", "battery"])
        vp = rnd.choice((1, 2, 3))
        truth = rnd.choice((1, 2, 3))
        phi = rnd.uniform(-30, 5) if role == "pv" else rnd.uniform(-35, 60)
        rev = rnd.random() < 0.3
        e = rnd.choice((0.5, 300.0, 2000.0)) * ps.rot(phi - ps.shift(vp, truth) + 180 * rev)
        cph, crev = rnd.choice((1, 2, 3)), rnd.random() < 0.3
        st, opts = ps.check_single(e, vp, role, cph, crev)
        o = ", ".join(f"{{{p}, {'true' if r else 'false'}}}" for p, r in opts)
        singles.append(f"    {{{c(e)}, {vp}, {ROLE_ID[role]}, {cph}, {'true' if crev else 'false'}, {SINGLE_ID[st]}, {len(opts)}, {{{o}}}}},")

    # Accumulation: a few raw minutes and the evidence they must sum to.
    acc_min = minutes_for(rnd, 2, "load", 30, (1, 2, 3), (0, 1, 0), 4, True, None)
    acc_min[1][4] = None  # a channel not read in one minute
    acc_min[2][5] = 50 * ps.rot(10)  # under the gate for 30 A
    g = group_from(acc_min, 30.0)
    acc_rows = ", ".join("{" + ", ".join(c(m[k]) if m[k] is not None else "{0.0, 0.0}" for k in CHS) + "}" for m in acc_min)
    acc_read = ", ".join("{" + ", ".join("false" if m[k] is None else "true" for k in CHS) + "}" for m in acc_min)
    acc_ch = ", ".join(f"{{30.0, {c(g.ch[k].e)}, {c(g.ch[k].e_axial)}, {g.ch[k].minutes!r}}}" for k in CHS)
    acc_pairs = ", ".join(f"{{{c(p.d)}, {p.weight!r}, {p.minutes!r}}}" for p in g.pairs.values())

    OUT.write_text(f"""// Generated by utils/gen_phase_check_fixtures.py from utils/phase_solver_ref.py. Do not edit.
#pragma once
#include "phase_check.h"

struct LineFixture {{
    PhaseCheck::LineEvidence evidence;
    uint8_t voltagePhase;
    uint8_t role;
    PhaseCheck::Hypothesis configured;
    uint8_t state;
    uint8_t proposalCount;
    PhaseCheck::Hypothesis proposals[8];
}};

struct SingleFixture {{
    PhaseCheck::Complex e;
    uint8_t voltagePhase;
    uint8_t role;
    uint8_t phase;
    bool reverse;
    uint8_t state;
    uint8_t optionCount;
    PhaseCheck::SingleOption options[6];
}};

static const LineFixture LINE_FIXTURES[] = {{
{chr(10).join(lines)}
}};

static const SingleFixture SINGLE_FIXTURES[] = {{
{chr(10).join(singles)}
}};

static const PhaseCheck::Complex ACC_MINUTES[][3] = {{{acc_rows}}};
static const bool ACC_READ[][3] = {{{acc_read}}};
static const PhaseCheck::LineEvidence ACC_EXPECTED = {{{{{acc_ch}}}, {{{acc_pairs}}}}};
""", encoding="utf-8", newline="\n")
    print(f"{OUT}: {len(lines)} line cases, {len(singles)} single cases")


if __name__ == "__main__":
    main()
