"""Synthetic cases for the setup phase check reference model. Run: uv run --no-project python test_phase_solver_ref.py
(or pytest: every test is a plain function)."""

import itertools
import random

import phase_solver_ref as ps

VP = 1
CHS = (3, 4, 5)
ROTATED = ((2, 3, 1), (3, 1, 2))


def group(phi, true_phase=(1, 2, 3), ct_rev=(0, 0, 0), mixed=False, minutes=30, idle=None, seed=1):
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


def random_install(rnd, i, minutes):
    role = rnd.choice(["load", "pv"])
    phi = rnd.uniform(-35, 60) if role == "load" else rnd.uniform(-30, 5)
    ct = tuple(int(rnd.random() < 0.5) for _ in range(3))
    cfg = H(rnd.choice(list(itertools.permutations((1, 2, 3)))), tuple(int(rnd.random() < 0.5) for _ in range(3)))
    truth = H((1, 2, 3), ct)
    if rnd.random() < 0.25:
        cfg = truth
    r = ps.evaluate(group(phi, ct_rev=ct, mixed=rnd.random() < 0.5, minutes=minutes, seed=i), VP, role, cfg)
    return r, cfg, truth


def test_answer_after_two_minutes_of_load():
    assert ps.evaluate(group(25, minutes=1), VP, "load", H((1, 2, 3))).state == "collecting"
    assert ps.evaluate(group(25, minutes=2), VP, "load", H((1, 2, 3))).state == "verified"


def test_waits_while_one_phase_is_idle():
    assert ps.evaluate(group(25, idle=1), VP, "load", H((1, 2, 3))).state == "collecting"


def test_correct_inductive_load_verified():
    assert ps.evaluate(group(45), VP, "load", H((1, 2, 3))).state == "verified"


def test_capacitive_feeder_verified():
    assert ps.evaluate(group(-32, mixed=True), VP, "load", H((1, 2, 3))).state == "verified"


def test_one_reversed_ct_is_a_single_fix():
    r = ps.evaluate(group(25, ct_rev=(0, 1, 0)), VP, "load", H((1, 2, 3)))
    assert r.state == "fix" and r.proposals == [H((1, 2, 3), (0, 1, 0))]
    r = ps.evaluate(group(-10, ct_rev=(0, 1, 0)), VP, "pv", H((1, 2, 3)))
    assert r.state == "fix" and r.proposals == [H((1, 2, 3), (0, 1, 0))]


def test_all_cts_reversed_is_a_single_fix():
    r = ps.evaluate(group(40, ct_rev=(1, 1, 1)), VP, "load", H((1, 2, 3)))
    assert r.state == "fix" and r.proposals == [H((1, 2, 3), (1, 1, 1))]


def test_phase_change_is_always_a_choice_with_the_truth_first():
    # Labels L1 and L3 swapped on a typical load: the truth and its 60-degree alias both
    # fit; never applied alone, the one nearer a typical power factor comes first.
    r = ps.evaluate(group(22), VP, "load", H((3, 2, 1)))
    assert r.state == "choose" and r.proposals[0] == H((1, 2, 3))
    # Field shape: PV, L1 and L3 swapped, detector-made reverse on one.
    r = ps.evaluate(group(-15), VP, "pv", H((3, 2, 1), (1, 0, 0)))
    assert r.state == "choose" and r.proposals[0] == H((1, 2, 3))


def test_rotated_labels_offer_the_truth_instead_of_a_wrong_fix():
    # Written L1-L2-L3, wired L2-L3-L1. At 10 degrees the configuration reads negative
    # and "all CTs reversed" fits too, 60 degrees away; it ranks second, so it is only
    # an option, never a fix.
    r = ps.evaluate(group(10), VP, "load", H((2, 3, 1)))
    assert r.state == "choose" and r.proposals[0] == H((1, 2, 3)), r
    # At 45 degrees the configuration reads -75: its phases agree, only the band fails,
    # so it stays first (as for a low-PF load) and the truth is the next option.
    r = ps.evaluate(group(45), VP, "load", H((2, 3, 1)))
    assert r.state == "choose" and r.proposals[:2] == [H((2, 3, 1)), H((1, 2, 3))], r


def test_low_pf_load_keeps_the_configuration_first():
    r = ps.evaluate(group(80), VP, "load", H((1, 2, 3)))
    assert r.state == "choose" and r.proposals[0] == H((1, 2, 3))


def test_no_fit_keeps_measuring():
    rnd = random.Random(3)
    g = ps.GroupEvidence(CHS)
    for k in CHS:
        g.ch[k] = ps.ChannelEvidence(rating_a=75)
    for _ in range(30):
        g.add_minute({3: 2000 * ps.rot(25), 4: 2000 * ps.rot(25 + 120 + 70 + rnd.gauss(0, 5)), 5: 2000 * ps.rot(25 - 120)})
    r = ps.evaluate(g, VP, "load", H((1, 2, 3)))
    assert r.state == "collecting" and not r.proposals and r.note == "no hypothesis fits"


def test_no_false_alarm_and_truth_always_offered():
    # A correct install is never flagged, the truth is always among the proposals, and
    # a single fix is wrong only for rotated labels.
    rnd = random.Random(11)
    for i in range(300):
        r, cfg, truth = random_install(rnd, i, minutes=30)
        if cfg == truth:
            assert r.state == "verified" or (r.state == "choose" and r.proposals[0] == truth), (i, r)
        if r.state == "fix" and cfg.phase not in ROTATED:
            # Rotated labels can still read as a reverse-only change (fleet: 6% of them).
            assert r.proposals == [truth], (i, cfg, r)
        if r.state == "choose":
            assert truth in r.proposals, (i, cfg, r)
        if r.state == "verified" and cfg != truth:
            # Only possible for rotated labels with every CT flipped: a uniform 60-degree
            # shift that a correct load could also show. Accepted limit.
            assert cfg.phase in ROTATED and all(a != b for a, b in zip(cfg.reverse, truth.reverse)), (i, cfg, r)


def test_three_noisy_minutes_rarely_wrong():
    # Advice after three minutes of a noisy mixed load: wrong rarely, never on a correct install.
    rnd = random.Random(12)
    wrong = total = 0
    for i in range(400):
        r, cfg, truth = random_install(rnd, i, minutes=3)
        if cfg == truth:
            assert r.state in ("verified", "collecting") or (r.state == "choose" and truth in r.proposals), (i, r)
        total += 1
        wrong += (r.state == "verified" and cfg != truth) or (r.state == "fix" and r.proposals != [truth]) \
            or (r.state == "choose" and truth not in r.proposals)
    assert wrong / total < 0.05, (wrong, total)


def test_single_channel_wrong_phase_suspect_with_right_option_first():
    e = 2000 * ps.rot(20 - ps.shift(VP, 2))  # load at PF 0.94 wired on L2
    assert ps.check_single(e, VP, "load", 2, False) == ("ok", [])
    state, options = ps.check_single(e, VP, "load", 1, False)
    assert state == "suspect" and options[0] == (2, False)


def test_single_channel_wrong_phase_and_reversed_can_pass():
    # Known limit: 120 + 180 degrees is a 60-degree error, which a typical load absorbs.
    e = 2000 * ps.rot(10 - ps.shift(VP, 2))
    assert ps.check_single(e, VP, "load", 3, True)[0] == "ok"


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
