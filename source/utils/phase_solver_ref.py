"""Reference model of the setup phase check (openspec change add-phase-check).

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
    "pv": (-65.0, 35.0),
}
MIN_PAIRS = 2  # evaluable pairs needed: one pair cannot see the third channel
SIGNED_ROLES = {"grid", "battery", "inverter", "bidirectional"}  # a grid line exports with PV

PAIR_TOL = 40.0  # max |mean pair difference| for a consistent hypothesis
GATE_FRAC = 0.03  # conduction gate for angle evidence, fraction of CT rating
NOMINAL_V = 230.0

# The check runs only during setup and is advice: the installer applies a proposal or
# not. Fleet replay (21 verified lines, installs started in working hours, design D10):
# median 4 min to an answer, p90 19 min, no false alarm on a correct install.
MIN_CH_WH = 1.0  # apparent energy per channel before it is evaluable
MIN_PAIR_MIN = 2.0  # co-conducting minutes per pair before it is evaluable
STABLE = 2  # same answer on this many consecutive minute evaluations before it is shown
# Ranking of options: the typical angle of a correct channel. The true wiring ranks
# first in about 80% of fleet cases with phases to change (60% on synthetic loads).
LIKELY_ANGLE = {"load": 20.0, "pv": -10.0}


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
        None when the channel was not read. Plain sums: the check lasts minutes."""
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
                p.minutes += 1


@dataclass(frozen=True)
class Hypothesis:
    phase: tuple[int, int, int]
    reverse: tuple[bool, bool, bool]


@dataclass
class Result:
    state: str  # collecting | verified | fix | choose
    configured: Hypothesis
    proposals: list[Hypothesis]
    angles: list[float]  # stored-frame angle per channel under the configuration
    spread: float | None  # max |pair diff| under the configuration
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
    """One minute evaluation. `fix` is a single reverse-only proposal (the phases stay);
    any phase change is `choose`: every hypothesis that fits, most likely first, and the
    installer picks. Nothing is ever applied by the device."""
    if any(abs(g.ch[k].e) < MIN_CH_WH for k in g.channels):
        return Result("collecting", configured, [], [], None, note="channel evidence")
    ev_pairs = [k for k, p in g.pairs.items() if p.minutes >= MIN_PAIR_MIN]
    if len(ev_pairs) < MIN_PAIRS:
        return Result("collecting", configured, [], [], None, note="co-conducting pairs")
    ok, spread, psis = _eval(g, vp, role, configured, ev_pairs)
    if ok:
        return Result("verified", configured, [], psis, spread)
    signed = role in SIGNED_ROLES
    revs = [configured.reverse] if signed else list(itertools.product([False, True], repeat=3))
    consistent = []
    for perm in itertools.permutations((1, 2, 3)):
        for rv in revs:
            h = Hypothesis(perm, tuple(rv))
            if _eval(g, vp, role, h, ev_pairs)[0]:
                consistent.append(h)
    if not consistent:
        # No hypothesis fits: an unbalanced feeder or a CT on the wrong cable. Keep
        # measuring; the page shows the current per channel so the installer can see why.
        return Result("collecting", configured, [], psis, spread, note="no hypothesis fits")
    ranked = rank(g, vp, role, consistent)
    # The phases agree with each other and only the role band fails: a uniform error
    # and an unusual load (low-PF lighting, a capacitive feeder) look the same, so the
    # configuration stays the first option. Only when the configured power is positive.
    if spread <= PAIR_TOL and not signed and all(math.cos(math.radians(x)) > 0 for x in psis):
        return Result("choose", configured, [configured] + ranked, psis, spread, note="angle outside role band")
    # A reverse-only change is proposed alone only when it is the single reverse-only
    # hypothesis and also the most likely one. Labels written L1-L2-L3 but wired
    # L2-L3-L1 read like "all CTs reversed" 60 degrees away; ranking second there, the
    # flip becomes one option of a choice instead of a wrong fix.
    reverse_only = [h for h in consistent if h.phase == configured.phase]
    if len(reverse_only) == 1 and ranked[0] == reverse_only[0]:
        return Result("fix", configured, reverse_only, psis, spread)
    return Result("choose", configured, ranked, psis, spread)


def rank(g: GroupEvidence, vp: int, role: str, options: list[Hypothesis]) -> list[Hypothesis]:
    """Options ordered most likely first: mean channel angle closest to the role's typical
    angle. Ordering only; the installer still picks one."""
    if role not in LIKELY_ANGLE:
        return options
    target = LIKELY_ANGLE[role]

    def dist(h: Hypothesis) -> float:
        psis = [wrap(ang(g.ch[k].e) + shift(vp, h.phase[i]) + 180.0 * h.reverse[i]) for i, k in enumerate(g.channels)]
        return abs(wrap(ang(sum(rot(x) for x in psis)) - target))

    return sorted(options, key=dist)


def check_single(e: complex, vp: int, role: str, phase: int, reverse: bool) -> tuple[str, list[tuple[int, bool]]]:
    """Single-phase channel on a three-phase supply, advice only: 'collecting' under the line
    minimum of energy, 'ok' when the configured
    angle is inside the role band, else 'suspect' with the (phase, reverse) options that fit,
    most likely first. Fleet replay: no false alarm on correct channels, a wrong phase is
    flagged with the right option offered; a wrong phase plus a reversed CT (60 degrees)
    passes unnoticed half the time, which only a three-phase line can catch."""
    if role not in ROLE_BAND:
        return "ok", []
    if abs(e) < MIN_CH_WH:
        return "collecting", []
    lo, hi = ROLE_BAND[role]
    under = lambda p, r: wrap(ang(e) + shift(vp, p) + 180.0 * r)  # noqa: E731
    fits = [(p, r) for p in (1, 2, 3) for r in (False, True) if lo <= under(p, r) <= hi]
    if (phase, reverse) in fits:
        return "ok", []
    target = LIKELY_ANGLE.get(role, 0.0)
    return "suspect", sorted(fits, key=lambda h: abs(wrap(under(*h) - target)))
