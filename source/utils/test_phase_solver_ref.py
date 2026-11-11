"""Synthetic cases for the phase check reference model. Run: uv run --no-project python -m pytest source/utils/test_phase_solver_ref.py
(or plain python: every test is a function called at the bottom)."""

import itertools
import random

import phase_solver_ref as ps

VP = 1
CHS = (3, 4, 5)


def group(phi, true_phase=(1, 2, 3), ct_rev=(0, 0, 0), mixed=False, minutes=900, idle=None, seed=1):
    rnd = random.Random(seed)
    g = ps.GroupEvidence(CHS)
    for k in CHS:
        g.ch[k] = ps.ChannelEvidence(rating_a=75)
    for _ in range(minutes):
        raw = {}
        for i, k in enumerate(CHS):
            if idle == i:
                raw[k] = None
                continue
            p_phi = phi + (rnd.gauss(0, 15) + 12 * (i - 1) if mixed else rnd.gauss(0, 3))
            theta = p_phi - ps.shift(VP, true_phase[i]) + 180 * ct_rev[i]
            raw[k] = rnd.uniform(1500, 4000) * ps.rot(theta)
        g.add_minute(raw)
    return g


def H(phase, rev=(0, 0, 0)):
    return ps.Hypothesis(tuple(phase), tuple(bool(x) for x in rev))


def test_correct_inductive_load_verified_despite_higher_pf_alias():
    r = ps.evaluate(group(45), VP, "load", H((1, 2, 3)))
    assert r.state == "verified"


def test_capacitive_feeder_verified():
    r = ps.evaluate(group(-32, mixed=True), VP, "load", H((1, 2, 3)))
    assert r.state == "verified"


def test_pv_one_reversed_ct_is_a_reverse_only_mismatch():
    r = ps.evaluate(group(-10, ct_rev=(0, 1, 0)), VP, "pv", H((1, 2, 3)))
    assert r.state == "mismatch" and r.reverse_only
    assert r.proposals == [H((1, 2, 3), (0, 1, 0))]


def test_pv_swapped_l1_l3_with_detector_flips():
    # Field shape: CTs labelled L1 and L3 on each other's line, detector-made reverses.
    g = group(-15)
    r = ps.evaluate(g, VP, "pv", H((3, 2, 1), (1, 0, 0)))
    assert r.state == "mismatch"
    assert r.proposals == [H((1, 2, 3))]


def test_load_one_reversed_ct_is_fixed_trusting_labels():
    # "Labels right, one CT reversed" reads like "labels rotated, two CTs reversed":
    # the labels are trusted, so the single reversal wins.
    r = ps.evaluate(group(25, ct_rev=(0, 1, 0)), VP, "load", H((1, 2, 3)))
    assert r.state == "mismatch" and r.reverse_only
    assert r.proposals == [H((1, 2, 3), (0, 1, 0))]


def test_all_cts_reversed_is_fixed_trusting_labels():
    r = ps.evaluate(group(40, ct_rev=(1, 1, 1)), VP, "load", H((1, 2, 3)))
    assert r.state == "mismatch" and r.proposals == [H((1, 2, 3), (1, 1, 1))]


def test_rotated_labels_known_limit():
    # Labels written L1-L2-L3 but wired L2-L3-L1. At 45 degrees the configuration
    # reads -75 (positive, outside the band), so the device asks.
    r = ps.evaluate(group(45), VP, "load", H((2, 3, 1)))
    assert r.state == "ambiguous" and H((1, 2, 3)) in r.proposals
    # At 10 degrees it reads -110 (negative): the labels are trusted and the device
    # proposes flipping all three CTs, 60 degrees off. Accepted limit.
    r = ps.evaluate(group(10), VP, "load", H((2, 3, 1)))
    assert r.state == "mismatch" and r.proposals == [H((2, 3, 1), (1, 1, 1))]


def test_low_pf_load_outside_band_is_not_proposed_away():
    r = ps.evaluate(group(80), VP, "load", H((1, 2, 3)))
    assert r.state == "ambiguous" and r.proposals[0] == H((1, 2, 3))


def test_idle_phase_waits():
    r = ps.evaluate(group(25, idle=2), VP, "load", H((1, 2, 3)))
    assert r.state == "collecting"


def test_crosstalk_like_disagreement_is_inconsistent():
    rnd = random.Random(3)
    g = ps.GroupEvidence(CHS)
    for k in CHS:
        g.ch[k] = ps.ChannelEvidence(rating_a=75)
    for _ in range(900):
        g.add_minute({3: 2000 * ps.rot(25), 4: 2000 * ps.rot(25 + 120 + 70 + rnd.gauss(0, 5)), 5: 2000 * ps.rot(25 - 120)})
    r = ps.evaluate(g, VP, "load", H((1, 2, 3)))
    assert r.state == "inconsistent" and not r.proposals


def test_no_wrong_proposal_over_random_installs():
    rnd = random.Random(11)
    for i in range(150):
        role = rnd.choice(["load", "pv"])
        phi = rnd.uniform(-35, 60) if role == "load" else rnd.uniform(-30, 5)
        ct = tuple(int(rnd.random() < 0.5) for _ in range(3))
        cfg = H(rnd.choice(list(itertools.permutations((1, 2, 3)))), tuple(int(rnd.random() < 0.5) for _ in range(3)))
        r = ps.evaluate(group(phi, ct_rev=ct, mixed=rnd.random() < 0.5, minutes=400, seed=i), VP, role, cfg)
        truth = H((1, 2, 3), ct)
        if cfg.phase in ((2, 3, 1), (3, 1, 2)):
            continue  # rotated labels: accepted limit, see test_rotated_labels_known_limit
        if r.state == "mismatch":
            assert r.proposals == [truth], (i, role, phi, ct, cfg, r)
        if r.state == "ambiguous":
            assert truth in r.proposals, (i, role, phi, ct, cfg, r)


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
