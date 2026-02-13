#!/usr/bin/env python3
"""
Test script for Weighted Deficit Round-Robin (WDRR) channel scheduler.

Simulates the scheduling algorithm from ade7953.cpp to validate:
  1. No starvation: every active channel is eventually sampled
  2. Proper allocation: channels with higher power/variability get more slots
  3. Edge cases: all-zero power, single channel, sudden load changes

Usage:
    python tests/test_wdrr_scheduler.py

The constants must match those in source/include/ade7953.h.
"""

import math
import sys

# ---------- Constants (must mirror ade7953.h) ----------
CHANNEL_COUNT = 17  # 16 mux channels + 1 direct (channel 0)
WEIGHT_POWER_SHARE = 0.4
WEIGHT_VARIABILITY = 0.4
WEIGHT_MIN_BASE = 0.1
VARIABILITY_EMA_ALPHA = 0.3


# ---------- Scheduler simulation ----------

class WDRRScheduler:
    """Pure-Python replica of the WDRR scheduler in ade7953.cpp."""

    def __init__(self, active_channels: list[int]):
        """
        active_channels: list of channel indices (1..16) that are active.
        Channel 0 is never scheduled (always read separately).
        """
        self.active = set(active_channels)
        self.weight = [0.0] * CHANNEL_COUNT
        self.deficit = [0.0] * CHANNEL_COUNT
        self.prev_power = [0.0] * CHANNEL_COUNT
        self.variability = [0.0] * CHANNEL_COUNT
        self.power = [0.0] * CHANNEL_COUNT  # current simulated active power
        self._first_reading = [True] * CHANNEL_COUNT

    def set_power(self, channel: int, power: float):
        """Set the simulated active power for a channel."""
        self.power[channel] = power

    def update_variability(self, channel: int):
        """Mirrors _updateVariability() in ade7953.cpp."""
        if self._first_reading[channel]:
            self.prev_power[channel] = self.power[channel]
            self._first_reading[channel] = False
            return
        delta = abs(self.power[channel] - self.prev_power[channel])
        self.variability[channel] = (
            VARIABILITY_EMA_ALPHA * delta
            + (1.0 - VARIABILITY_EMA_ALPHA) * self.variability[channel]
        )
        self.prev_power[channel] = self.power[channel]

    def recalculate_weights(self):
        """Mirrors _recalculateWeights() in ade7953.cpp."""
        total_abs_power = sum(
            abs(self.power[i]) for i in range(1, CHANNEL_COUNT) if i in self.active
        )
        total_variability = sum(
            self.variability[i] for i in range(1, CHANNEL_COUNT) if i in self.active
        )

        for i in range(1, CHANNEL_COUNT):
            if i not in self.active:
                self.weight[i] = 0.0
                continue

            power_score = (
                abs(self.power[i]) / total_abs_power if total_abs_power > 0 else 0.0
            )
            var_score = (
                self.variability[i] / total_variability
                if total_variability > 0
                else 0.0
            )

            self.weight[i] = (
                WEIGHT_POWER_SHARE * power_score
                + WEIGHT_VARIABILITY * var_score
                + WEIGHT_MIN_BASE
            )

        self.weight[0] = 0.0

    def find_next_channel(self) -> int:
        """Mirrors _findNextActiveChannel() in ade7953.cpp."""
        # Add weight to deficit
        for i in range(1, CHANNEL_COUNT):
            if i in self.active:
                self.deficit[i] += self.weight[i]
            else:
                self.deficit[i] = 0.0

        # Pick highest deficit
        best_ch = -1
        best_def = -1e9
        for i in range(1, CHANNEL_COUNT):
            if i in self.active and self.deficit[i] > best_def:
                best_def = self.deficit[i]
                best_ch = i

        # Decrement
        if best_ch != -1:
            self.deficit[best_ch] -= 1.0

        return best_ch


# ---------- Test helpers ----------

def run_scheduler(scheduler: WDRRScheduler, iterations: int) -> dict[int, int]:
    """Run the scheduler for N iterations and return selection counts."""
    counts: dict[int, int] = {ch: 0 for ch in scheduler.active}
    for _ in range(iterations):
        ch = scheduler.find_next_channel()
        if ch != -1:
            counts[ch] = counts.get(ch, 0) + 1
            # After reading, update variability and weights
            scheduler.update_variability(ch)
            scheduler.recalculate_weights()
    return counts


def assert_no_starvation(counts: dict[int, int], active: set[int], label: str):
    """Assert every active channel was selected at least once."""
    starved = [ch for ch in active if counts.get(ch, 0) == 0]
    if starved:
        print(f"  FAIL [{label}]: channels {starved} were STARVED (0 selections)")
        return False
    print(f"  PASS [{label}]: no starvation")
    return True


def assert_proportional(
    counts: dict[int, int],
    high_channels: list[int],
    low_channels: list[int],
    label: str,
):
    """Assert high-power channels get more selections than low-power ones."""
    if not high_channels or not low_channels:
        print(f"  SKIP [{label}]: cannot compare (empty group)")
        return True

    avg_high = sum(counts[ch] for ch in high_channels) / len(high_channels)
    avg_low = sum(counts[ch] for ch in low_channels) / len(low_channels)

    if avg_high > avg_low:
        print(
            f"  PASS [{label}]: high-power avg={avg_high:.1f} > low-power avg={avg_low:.1f}"
        )
        return True
    else:
        print(
            f"  FAIL [{label}]: high-power avg={avg_high:.1f} <= low-power avg={avg_low:.1f}"
        )
        return False


# ---------- Test scenarios ----------

def test_equal_power():
    """All active channels have equal power → roughly equal selections."""
    print("\n=== Test: Equal Power (5 channels, 100W each) ===")
    active = [1, 2, 3, 4, 5]
    s = WDRRScheduler(active)
    for ch in active:
        s.set_power(ch, 100.0)
    s.recalculate_weights()

    counts = run_scheduler(s, 1000)
    print(f"  Counts: {counts}")

    ok = assert_no_starvation(counts, set(active), "equal_power")

    # With equal weights, expect roughly equal distribution (±30%)
    avg = 1000 / len(active)
    for ch in active:
        ratio = counts[ch] / avg
        if ratio < 0.7 or ratio > 1.3:
            print(f"  FAIL [equal_power]: channel {ch} ratio={ratio:.2f} (expected ~1.0)")
            ok = False
    if ok:
        print("  PASS [equal_power]: distribution within ±30% of mean")
    return ok


def test_unequal_power():
    """One channel has much higher power → should get more selections."""
    print("\n=== Test: Unequal Power (ch1=1000W, ch2-5=10W) ===")
    active = [1, 2, 3, 4, 5]
    s = WDRRScheduler(active)
    s.set_power(1, 1000.0)
    for ch in active[1:]:
        s.set_power(ch, 10.0)
    s.recalculate_weights()

    counts = run_scheduler(s, 1000)
    print(f"  Counts: {counts}")

    ok = assert_no_starvation(counts, set(active), "unequal_power")
    ok = assert_proportional(counts, [1], [2, 3, 4, 5], "unequal_power") and ok
    return ok


def test_all_zero_power():
    """All channels at 0W → should still round-robin fairly (base weight)."""
    print("\n=== Test: All Zero Power (5 channels) ===")
    active = [1, 2, 3, 4, 5]
    s = WDRRScheduler(active)
    # Power stays at 0
    s.recalculate_weights()

    counts = run_scheduler(s, 500)
    print(f"  Counts: {counts}")

    ok = assert_no_starvation(counts, set(active), "zero_power")

    # All should be roughly equal
    avg = 500 / len(active)
    for ch in active:
        ratio = counts[ch] / avg
        if ratio < 0.7 or ratio > 1.3:
            print(f"  FAIL [zero_power]: channel {ch} ratio={ratio:.2f} (expected ~1.0)")
            ok = False
    if ok:
        print("  PASS [zero_power]: fair round-robin with base weight")
    return ok


def test_single_channel():
    """Only 1 active channel → always selected."""
    print("\n=== Test: Single Active Channel ===")
    active = [7]
    s = WDRRScheduler(active)
    s.set_power(7, 500.0)
    s.recalculate_weights()

    counts = run_scheduler(s, 100)
    print(f"  Counts: {counts}")

    ok = counts[7] == 100
    if ok:
        print("  PASS [single_channel]: always selected")
    else:
        print(f"  FAIL [single_channel]: expected 100, got {counts[7]}")
    return ok


def test_all_16_channels():
    """All 16 multiplexed channels active with varying powers."""
    print("\n=== Test: All 16 Channels Active ===")
    active = list(range(1, 17))
    s = WDRRScheduler(active)
    # Powers: channel i gets i*100 W (ch1=100W, ch16=1600W)
    for ch in active:
        s.set_power(ch, ch * 100.0)
    s.recalculate_weights()

    counts = run_scheduler(s, 5000)
    print(f"  Counts: {dict(sorted(counts.items()))}")

    ok = assert_no_starvation(counts, set(active), "all_16")
    # Top 4 channels (13-16) should get more than bottom 4 (1-4)
    ok = assert_proportional(counts, [13, 14, 15, 16], [1, 2, 3, 4], "all_16") and ok
    return ok


def test_variability_boost():
    """A channel with high variability should get more sampling."""
    print("\n=== Test: Variability Boost ===")
    active = [1, 2, 3]
    s = WDRRScheduler(active)

    # All start with same power
    for ch in active:
        s.set_power(ch, 100.0)
    s.recalculate_weights()

    # Phase 1: Run 200 iterations to establish baseline
    # Simulate variability on channel 1 by toggling its power
    for i in range(200):
        ch = s.find_next_channel()
        if ch != -1:
            s.update_variability(ch)
            # Toggle channel 1 power to create high variability
            if ch == 1:
                s.set_power(1, 500.0 if s.power[1] < 300 else 50.0)
            s.recalculate_weights()

    # Phase 2: Run 1000 iterations and count
    counts = {ch: 0 for ch in active}
    for i in range(1000):
        ch = s.find_next_channel()
        if ch != -1:
            counts[ch] += 1
            s.update_variability(ch)
            if ch == 1:
                s.set_power(1, 500.0 if s.power[1] < 300 else 50.0)
            s.recalculate_weights()

    print(f"  Counts: {counts}")
    print(f"  Weights: ch1={s.weight[1]:.4f}, ch2={s.weight[2]:.4f}, ch3={s.weight[3]:.4f}")
    print(f"  Variability: ch1={s.variability[1]:.2f}, ch2={s.variability[2]:.2f}, ch3={s.variability[3]:.2f}")

    ok = assert_no_starvation(counts, set(active), "variability")
    ok = assert_proportional(counts, [1], [2, 3], "variability") and ok
    return ok


def test_sudden_load_change():
    """After a sudden load change, channel weights should adapt."""
    print("\n=== Test: Sudden Load Change ===")
    active = [1, 2, 3, 4]
    s = WDRRScheduler(active)

    # Phase 1: Channel 1 is dominant (1000W), others are 10W
    s.set_power(1, 1000.0)
    for ch in [2, 3, 4]:
        s.set_power(ch, 10.0)
    s.recalculate_weights()

    counts_phase1 = run_scheduler(s, 500)
    print(f"  Phase 1 (ch1=1000W): {counts_phase1}")

    ok1 = assert_proportional(counts_phase1, [1], [2, 3, 4], "sudden_load_p1")

    # Phase 2: Suddenly ch4 becomes dominant (2000W), ch1 drops
    s.set_power(1, 10.0)
    s.set_power(4, 2000.0)
    s.recalculate_weights()

    counts_phase2 = run_scheduler(s, 500)
    print(f"  Phase 2 (ch4=2000W): {counts_phase2}")

    ok2 = assert_proportional(counts_phase2, [4], [1, 2, 3], "sudden_load_p2")

    ok = assert_no_starvation(counts_phase1, set(active), "sudden_load_p1_starve")
    ok = assert_no_starvation(counts_phase2, set(active), "sudden_load_p2_starve") and ok
    return ok1 and ok2 and ok


def test_mixed_active_inactive():
    """Some channels active, some not → inactive never scheduled."""
    print("\n=== Test: Mixed Active/Inactive ===")
    active = [1, 5, 10]
    s = WDRRScheduler(active)
    for ch in active:
        s.set_power(ch, 200.0)
    s.recalculate_weights()

    counts = run_scheduler(s, 300)
    print(f"  Counts: {counts}")

    # Inactive channels should have 0 count
    ok = True
    for ch in range(1, 17):
        if ch not in active and counts.get(ch, 0) > 0:
            print(f"  FAIL [mixed]: inactive channel {ch} was scheduled!")
            ok = False

    ok = assert_no_starvation(counts, set(active), "mixed") and ok
    if ok:
        print("  PASS [mixed]: inactive channels never scheduled")
    return ok


def test_weight_distribution():
    """Verify weight values match expected formula."""
    print("\n=== Test: Weight Calculation Verification ===")
    active = [1, 2, 3]
    s = WDRRScheduler(active)
    s.set_power(1, 300.0)
    s.set_power(2, 100.0)
    s.set_power(3, 100.0)
    s.recalculate_weights()

    total_power = 300.0 + 100.0 + 100.0
    expected_w1 = WEIGHT_POWER_SHARE * (300.0 / total_power) + WEIGHT_VARIABILITY * 0.0 + WEIGHT_MIN_BASE
    expected_w2 = WEIGHT_POWER_SHARE * (100.0 / total_power) + WEIGHT_VARIABILITY * 0.0 + WEIGHT_MIN_BASE
    expected_w3 = WEIGHT_POWER_SHARE * (100.0 / total_power) + WEIGHT_VARIABILITY * 0.0 + WEIGHT_MIN_BASE

    ok = True
    for ch, expected in [(1, expected_w1), (2, expected_w2), (3, expected_w3)]:
        actual = s.weight[ch]
        if abs(actual - expected) > 1e-6:
            print(f"  FAIL [weight_calc]: ch{ch} weight={actual:.6f}, expected={expected:.6f}")
            ok = False
        else:
            print(f"  PASS [weight_calc]: ch{ch} weight={actual:.6f} == {expected:.6f}")

    # Verify ch1 weight is higher than ch2/ch3
    if s.weight[1] <= s.weight[2]:
        print(f"  FAIL [weight_calc]: ch1 ({s.weight[1]:.4f}) should be > ch2 ({s.weight[2]:.4f})")
        ok = False
    return ok


# ---------- Main ----------

def main():
    print("=" * 60)
    print("WDRR Channel Scheduler Test Suite")
    print("=" * 60)
    print(f"Constants: WEIGHT_POWER_SHARE={WEIGHT_POWER_SHARE}, "
          f"WEIGHT_VARIABILITY={WEIGHT_VARIABILITY}, "
          f"WEIGHT_MIN_BASE={WEIGHT_MIN_BASE}, "
          f"VARIABILITY_EMA_ALPHA={VARIABILITY_EMA_ALPHA}")

    tests = [
        test_equal_power,
        test_unequal_power,
        test_all_zero_power,
        test_single_channel,
        test_all_16_channels,
        test_variability_boost,
        test_sudden_load_change,
        test_mixed_active_inactive,
        test_weight_distribution,
    ]

    results = []
    for test in tests:
        try:
            results.append(test())
        except Exception as e:
            print(f"  ERROR: {e}")
            results.append(False)

    print("\n" + "=" * 60)
    passed = sum(results)
    total = len(results)
    print(f"Results: {passed}/{total} tests passed")

    if all(results):
        print("ALL TESTS PASSED ✓")
        return 0
    else:
        failed = [tests[i].__name__ for i, r in enumerate(results) if not r]
        print(f"FAILED TESTS: {failed}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
