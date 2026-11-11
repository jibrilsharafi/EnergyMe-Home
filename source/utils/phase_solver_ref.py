"""Reference model of the on-device phase check (openspec change add-phase-commissioning).

Pure Python, no dependencies. The firmware solver must reproduce these decisions;
the host tests use the same cases. Angles in degrees, IEC frame of phase_utils.h:
PHASE_1 = 0, PHASE_2 = -120, PHASE_3 = +120. A channel's stored angle is
psi = raw + shift(vp, phase) + 180 * reverse, raw = arg(V1 * conj(I)), so a load
lagging by phi on its own line reads psi = phi when phase and reverse are right.
"""

from __future__ import annotations

import cmath
import itertools
import math
from dataclasses import dataclass, field

PHASE_ANGLE = {1: 0.0, 2: -120.0, 3: 120.0}

# Role bands: the stored (channel-frame) angle a correct channel of that role can
# show, energy-weighted. Outside the band the reading is implausible (PF too low or
# the wrong quadrant) whatever its power sign.
ROLE_BAND = {
    "load": (-50.0, 75.0),
    "grid": (-50.0, 75.0),
    "pv": (-65.0, 35.0),
}
# Only PV has an angle tight enough to choose between 60-degree neighbours: loads
# range from capacitive feeders (-35) to motors (+60), so a load prior picks wrong.
ROLE_TARGET = {"pv": -10.0}
PRIOR_MARGIN = 15.0
PHASE_MARGIN = 2  # fewest-phase-changes wins only by at least this many
MIN_PAIRS = 2  # evaluable pairs needed: one pair cannot see the third channel
SIGNED_ROLES = {"battery", "inverter", "bidirectional"}

PAIR_TOL = 40.0  # max |mean pair difference| for a consistent hypothesis
MIN_CH_WH = 50.0  # apparent energy per channel before it is evaluable
MIN_PAIR_MIN = 60.0  # co-conducting minutes per pair before it is evaluable
GATE_FRAC = 0.03  # conduction gate for angle evidence, fraction of CT rating
HALF_LIFE_MIN = 3 * 24 * 60
NOMINAL_V = 230.0


def signed_role(role: str) -> bool:
    return role in SIGNED_ROLES


def wrap(deg: float) -> float:
    d = math.fmod(deg + 180.0, 360.0)
    if d < 0:
        d += 360.0
    return d - 180.0


def shift(vp: int, phase: int) -> float:
    return PHASE_ANGLE[phase] - PHASE_ANGLE[vp]


def rot(deg: float) -> complex:
    return cmath.exp(1j * math.radians(deg))


def ang(z: complex) -> float:
    return math.degrees(cmath.phase(z))


@dataclass
class ChannelEvidence:
    rating_a: float
    e: complex = 0j  # sum of raw phasor x hours (VA h), time-weighted
    e_axial: complex = 0j  # doubled-angle sum for signed roles
    minutes: float = 0.0


@dataclass
class PairEvidence:
    d: complex = 0j
    weight: float = 0.0
    minutes: float = 0.0


@dataclass
class GroupEvidence:
    channels: tuple[int, int, int]
    ch: dict[int, ChannelEvidence] = field(default_factory=dict)
    pairs: dict[tuple[int, int], PairEvidence] = field(default_factory=dict)

    def __post_init__(self) -> None:
        for a, b in itertools.combinations(self.channels, 2):
            self.pairs[(a, b)] = PairEvidence()

    def add_minute(self, raw: dict[int, complex | None]) -> None:
        """raw[k] = time-averaged raw phasor (VA) of channel k over the minute, or
        None when the channel was not read. Decays first, then accumulates."""
        f = 0.5 ** (1.0 / HALF_LIFE_MIN)
        for c in self.ch.values():
            c.e *= f
            c.e_axial *= f
            c.minutes *= f
        for p in self.pairs.values():
            p.d *= f
            p.weight *= f
            p.minutes *= f
        conducting: dict[int, complex] = {}
        for k, v in raw.items():
            if v is None or k not in self.ch:
                continue
            c = self.ch[k]
            if abs(v) < GATE_FRAC * c.rating_a * NOMINAL_V:
                continue
            c.e += v / 60.0
            c.e_axial += (v * v / abs(v)) / 60.0
            c.minutes += 1
            conducting[k] = v
        for (a, b), p in self.pairs.items():
            if a in conducting and b in conducting:
                va, vb = conducting[a], conducting[b]
                w = min(abs(va), abs(vb)) / 60.0
                p.d += w * (va / abs(va)) * (vb / abs(vb)).conjugate()
                p.weight += w
                p.minutes += 1


@dataclass(frozen=True)
class Hypothesis:
    phase: tuple[int, int, int]
    reverse: tuple[bool, bool, bool]


@dataclass
class Result:
    state: str  # collecting | verified | mismatch | ambiguous | inconsistent
    configured: Hypothesis
    proposals: list[Hypothesis]
    angles: list[float]  # stored-frame angle per channel under the configuration
    spread: float | None  # max |pair diff| under the configuration
    reverse_only: bool = False
    note: str = ""


def _eval(g: GroupEvidence, vp: int, role: str, h: Hypothesis, ev_pairs) -> tuple[bool, float, list[float]]:
    signed = role in SIGNED_ROLES
    psis = []
    for i, k in enumerate(g.channels):
        c = g.ch[k]
        base = ang(c.e_axial) / 2.0 if signed else ang(c.e)
        psis.append(wrap(base + shift(vp, h.phase[i]) + (0 if signed else 180.0 * h.reverse[i])))
    spread = 0.0
    for (a, b) in ev_pairs:
        ia, ib = g.channels.index(a), g.channels.index(b)
        p = g.pairs[(a, b)]
        if signed:
            # axial pair difference: double, rotate, halve
            dd = ang(p.d * p.d / abs(p.d)) if p.d else 0.0
            diff = wrap(dd + 2 * (shift(vp, h.phase[ia]) - shift(vp, h.phase[ib]))) / 2.0
        else:
            diff = wrap(ang(p.d) + shift(vp, h.phase[ia]) - shift(vp, h.phase[ib]) + 180.0 * (h.reverse[ia] - h.reverse[ib]))
        spread = max(spread, abs(diff))
    if signed:
        ok = spread <= PAIR_TOL
    else:
        lo, hi = ROLE_BAND[role]
        ok = spread <= PAIR_TOL and all(lo <= x <= hi for x in psis)
    return ok, spread, psis


def evaluate(g: GroupEvidence, vp: int, role: str, configured: Hypothesis) -> Result:
    if any(abs(g.ch[k].e) < MIN_CH_WH for k in g.channels):
        return Result("collecting", configured, [], [], None, note="channel evidence")
    ev_pairs = [k for k, p in g.pairs.items() if p.minutes >= MIN_PAIR_MIN]
    if len(ev_pairs) < MIN_PAIRS:
        return Result("collecting", configured, [], [], None, note="co-conducting pairs")
    ok, spread, psis = _eval(g, vp, role, configured, ev_pairs)
    if ok:
        return Result("verified", configured, [], psis, spread)
    # The phases agree with each other and only the role band fails: a uniform
    # error and an unusual load (low-PF lighting, a capacitive feeder) look the
    # same, so the configuration stays one of the options and nothing is proposed.
    # Only when the configured power is positive: a negative reading is a plain error.
    pair_only = spread <= PAIR_TOL and not signed_role(role) and all(math.cos(math.radians(x)) > 0 for x in psis)
    signed = role in SIGNED_ROLES
    revs = [configured.reverse] if signed else list(itertools.product([False, True], repeat=3))
    consistent = []
    best_spread = None
    for perm in itertools.permutations((1, 2, 3)):
        for rv in revs:
            h = Hypothesis(perm, tuple(rv))
            hok, hs, hp = _eval(g, vp, role, h, ev_pairs)
            best_spread = hs if best_spread is None else min(best_spread, hs)
            if hok:
                consistent.append((hs, h, hp))
    if not consistent:
        return Result("inconsistent", configured, [], psis, spread, note=f"best spread {best_spread:.0f}")
    # Tie-break 1: trust the labelled phases over CT orientation. Among consistent
    # hypotheses keep those changing the fewest labelled phases, when they win by
    # PHASE_MARGIN. Reverse flags never count (orientation errors are common, and
    # existing flags may be detector-made). Known, accepted limit: a line whose
    # labels are cyclically rotated (written L1-L2-L3, wired L2-L3-L1) is read as
    # a reverse pattern 60 degrees away and gets a wrong reverse-only fix.
    def phase_changes(h: Hypothesis) -> int:
        return sum(a != b for a, b in zip(h.phase, configured.phase))

    consistent.sort(key=lambda t: phase_changes(t[1]))
    low = phase_changes(consistent[0][1])
    if len(consistent) > 1 and all(phase_changes(t[1]) - low >= PHASE_MARGIN for t in consistent if phase_changes(t[1]) != low):
        consistent = [t for t in consistent if phase_changes(t[1]) == low]
    # Tie-break 2: role prior, only where the role pins the angle (PV), with margin.
    if len(consistent) > 1 and role in ROLE_TARGET:
        target = ROLE_TARGET[role]

        def dist(t) -> float:
            mean = ang(sum(rot(x) for x in t[2]))
            return abs(wrap(mean - target))

        consistent.sort(key=dist)
        if dist(consistent[1]) - dist(consistent[0]) >= PRIOR_MARGIN:
            consistent = consistent[:1]
    if pair_only:
        return Result("ambiguous", configured, [configured] + [t[1] for t in consistent], psis, spread, note="angle outside role band")
    if len(consistent) == 1:
        h = consistent[0][1]
        return Result("mismatch", configured, [h], psis, spread, reverse_only=h.phase == configured.phase)
    return Result("ambiguous", configured, [t[1] for t in consistent], psis, spread)
