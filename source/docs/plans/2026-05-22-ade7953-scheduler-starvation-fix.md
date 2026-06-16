# ADE7953 Scheduler Starvation Fix

**Date:** 2026-05-22
**Status:** Draft
**Tracking:** Issue #149
**Firmware Version:** 2.0.1 (planned)

---

## Context

The ADE7953 meter task uses Weighted Deficit Round-Robin (WDRR) to pick which multiplexed channel to sample next. Issue #149 documents that the algorithm silently starves a channel whose readings get discarded (the dominant scenario being a backwards-clamped CT producing negative active power on a LOAD-role channel). The starved channel can go 30 to 60+ seconds without a successful read while its neighbours are sampled normally; the user perceives this as "missing data" with no error log.

Three problems interact to produce the failure:

1. `_findNextActiveChannel` decrements `_channelDeficit[i]` by 1.0 the moment a channel is selected, but on a discarded read `_readMeterValues` returns `false` and no refund is performed. The deficit drifts ever more negative.
2. `_recalculateWeights` is only reached at the end of a *successful* read. A channel locked in a discard loop has its weight frozen at `WEIGHT_MIN_BASE = 0.1`, so it can never reclaim deficit ground against neighbours with weights of 0.4 to 0.6.
3. The priority-override path (`_pendingPriorityRead`) returns early without running the gain phase. The only mechanism that currently rescues a starved channel skips the very accumulation step it needs to repair the deficit hole.

The current safety nets (`ADE7953_MAX_FAILURES_BEFORE_RESTART = 100` over 1 minute) do not trip because the discard rate is ~10/min, well under the threshold.

The user observation that triggered the investigation: "channels can go minutes unnoticed". Requirement: 100% guarantee that no active channel goes more than 15 seconds without being sampled.

---

## Strategy

Five-part fix, each independently committable and testable:

1. **Refund the deficit on discarded reads.** Capture `_processChannelReading`'s return value at its single call site in `_handleCycendInterrupt` and, on `false`, restore the decrement applied during selection.
2. **Make the priority-override path run the gain phase.** Move the deficit accumulation loop above the priority claim loop in `_findNextActiveChannel`.
3. **Arm priority read when `reverse` flag flips via API.** A user-initiated reverse change is intent for "read me now".
4. **Clamp `_channelDeficit[]` magnitude to a fixed bound.** Belt-and-suspenders against any future arithmetic edge case (weight-table shock, NaN, OTA pause/resume).
5. **15-second watchdog.** First scan in `_findNextActiveChannel`: force-pick any active channel whose `_meterValues[i].lastMillis` is older than `CHANNEL_MAX_GAP_MS`. Hard upper bound regardless of WDRR state. Logs at `LOG_WARNING` so chronic starvation surfaces to the user.

Validation via local Python simulator (NOT committed) that mirrors the C++ scheduler one-for-one. The simulator reproduces issue #149's ch4 starvation against the *current* code, then verifies every documented edge case against the *fixed* code before any firmware flash.

---

## Constants

Added to `include/ade7953.h`:

```cpp
// Hard upper bound on time between successful reads for any active multiplexed channel.
// If exceeded, _findNextActiveChannel force-picks the starved channel and logs a warning.
#define CHANNEL_MAX_GAP_MS 15000ULL

// Symmetric clamp on per-channel deficit magnitude. Safety net against arithmetic edge cases.
// With the discard-refund in place, deficits naturally stay near zero; this bound just guards
// against any future shock (weight-table change, NaN, OTA pause).
#define MAX_CHANNEL_DEFICIT_BOUND 5.0f
```

No new constants in `constants.h` (the scoping is meter-specific).

---

## Component Design

### Fix 1 - Deficit refund on discarded reads

**Where:** `src/ade7953.cpp::_handleCycendInterrupt` around line 1841.

**Change:**

```cpp
if (_currentChannel != INVALID_CHANNEL) {
    bool readOk = _processChannelReading(_currentChannel, linecycUnix);
    if (!readOk && _currentChannel != 0) {
        // Refund the deficit decremented by _findNextActiveChannel on selection.
        // A discarded read is not a "service"; without this the channel drifts deeply
        // negative and starves (issue #149).
        if (acquireMutex(&_channelDataMutex)) {
            _channelDeficit[_currentChannel] += 1.0f;
            releaseMutex(&_channelDataMutex);
        }
    }
}
```

Single site, single mutex acquisition, channel 0 excluded (not part of WDRR).

### Fix 2 - Gain phase runs before priority return

**Where:** `src/ade7953.cpp::_findNextActiveChannel` (currently line 4457).

**Current order:**
1. Priority-claim loop (returns early if any channel has `_pendingPriorityRead`).
2. Gain loop (adds weight to deficit for every active channel).
3. Argmax selection.
4. Decrement winner.

**New order:**
1. Watchdog scan (Fix 5, see below).
2. Gain loop.
3. Deficit clamp (Fix 4, see below).
4. Priority-claim loop (returns the claimed channel; does NOT decrement its deficit - the priority slot is a freebie).
5. Argmax selection.
6. Decrement winner.

### Fix 3 - Reverse-flag PUT arms priority

**Where:** `src/ade7953.cpp::setChannelData` around line 763.

**Change:** snapshot `bool oldReverse = _channelData[channelIndex].reverse;` alongside the existing snapshots, then inside the `armTransients` block:

```cpp
if (oldReverse != _channelData[channelIndex].reverse && channelIndex > 0) {
    _channelData[channelIndex]._pendingPriorityRead = true;
}
```

### Fix 4 - Deficit clamp

**Where:** new step in `_findNextActiveChannel` right after the gain loop.

```cpp
for (uint8_t i = 1; i < globalHwProfile->totalChannelCount; i++) {
    // NaN guard - a NaN deficit fails every comparison and breaks argmax selection
    if (isnanf(_channelDeficit[i])) _channelDeficit[i] = 0.0f;
    if (_channelDeficit[i] > MAX_CHANNEL_DEFICIT_BOUND) _channelDeficit[i] = MAX_CHANNEL_DEFICIT_BOUND;
    if (_channelDeficit[i] < -MAX_CHANNEL_DEFICIT_BOUND) _channelDeficit[i] = -MAX_CHANNEL_DEFICIT_BOUND;
}
```

Also add a NaN guard in `_recalculateWeights` before accumulating into `totalAbsPower` and `totalVariability`.

### Fix 5 - Starvation watchdog

**Where:** first step in `_findNextActiveChannel`.

```cpp
uint64_t now = millis64();
for (uint8_t i = 1; i < globalHwProfile->totalChannelCount; i++) {
    if (!_channelData[i].active) continue;
    if (_meterValues[i].lastMillis == 0) continue; // Never read - _pendingPriorityRead handles it
    uint64_t gap = now - _meterValues[i].lastMillis;
    if (gap > CHANNEL_MAX_GAP_MS) {
        LOG_WARNING("Channel %u starved (%llu ms since last read), forcing pick", i, gap);
        // Reset deficit to 0 to avoid immediate re-trigger
        if (acquireMutex(&_channelDataMutex)) {
            _channelDeficit[i] = 0.0f;
            releaseMutex(&_channelDataMutex);
        }
        return i;
    }
}
```

Watchdog runs *before* the gain phase, so a starved channel is returned immediately without further bookkeeping. The deficit reset prevents the warning from firing again on the very next call.

---

## Data Flow

**Normal read:** CYCEND → `_processChannelReading` succeeds → `lastMillis` updated → `_findNextActiveChannel` (watchdog clean, no priority, gain + clamp + argmax + decrement) → mux set to next channel.

**Discarded read:** CYCEND → `_processChannelReading` returns false → deficit refunded → `lastMillis` unchanged → next `_findNextActiveChannel` call: discarded channel has its full deficit, competes fairly next round.

**Discard storm (CT permanently misoriented):** Channel keeps being picked and discarded. Net deficit change per round: 0 (selection -1.0 + refund +1.0). At the 15 s mark, watchdog force-picks the channel, logs the warning, the read still fails, deficit reset, cycle continues with one `LOG_WARNING` every 15 s. The user has a clear signal that something is physically wrong.

**User PUT reverse: false:** `setChannelData` sees `reverse` flipped → arms `_pendingPriorityRead` → next CYCEND, the priority-claim loop returns immediately (after gain + clamp), reading lands with corrected sign.

**Boot sequence:** All `lastMillis == 0`, watchdog skips them. `_pendingPriorityRead` is armed on inactive→active for each channel (existing path in `_setChannelDataFromPreferences`), so first read is forced via priority. After the first read, the watchdog takes over.

---

## Edge Cases

| # | Scenario | Expected behaviour |
|---|----------|--------------------|
| 1 | Flipped CT for 60 s | LOAD-discards happen, deficit stays near 0, watchdog fires every 15 s, no silent gap > 15 s. |
| 2 | Single huge transient -100 kW | Auto-polarity unchanged (already gated by `AUTO_POLARITY_MIN_POWER_W = 5W`), validation rejects per `VALIDATE_POWER_MIN/MAX`, single discard, no permanent flip. |
| 3 | Random 30% discard rate across all channels | Every channel read at its weight share +/- 20% over a 1 h window. |
| 4 | All channels discarding simultaneously | No deadlock. Watchdog rotates through all of them, one force-pick per 15 s per channel. |
| 5 | Channel deactivated mid-stream | Its deficit reset to 0 by gain loop's `else` branch, others redistribute traffic. |
| 6 | Channel toggles active/inactive every 100 ms | Deficit oscillates but never escapes `[-5, +5]` thanks to clamp. |
| 7 | Variability spikes 1000x on one channel | Weight redistribution happens, but min-base still ensures other channels are serviced. |
| 8 | NaN active power injected | `isnanf` guards in `_recalculateWeights` and the clamp prevent NaN propagation into deficits. |
| 9 | 1 hour of skewed traffic | Every active channel read at least once per 15 s, verified by simulator. |
| 10 | Channel 0 in any scenario | Always read on every CYCEND, never in the rotation, never in deficit arrays. |
| 11 | Concurrent setChannelData + scheduler | `_channelDataMutex` serializes; priority flag survives concurrent arm + claim. |
| 12 | OTA pause + resume | Watchdog tolerates a 15 s+ pause: on resume, all stale channels get force-picked one by one. |

---

## Testing

**Local Python simulator** (`_scheduler_sim/`, gitignored, removed before final commit):

- `scheduler_buggy.py` - faithful port of the *current* C++ algorithm, used to first reproduce issue #149's ch4 starvation.
- `scheduler_fixed.py` - port of the *fixed* algorithm including all five fixes.
- `scenarios.py` - the 12 edge cases above as deterministic generators.
- `test_scheduler.py` - pytest suite asserting:
  - Buggy version: ch4 read count drops to <= 5 per 100 s during a 60 s discard storm (reproduces #149).
  - Fixed version: every active channel read at least once per `CHANNEL_MAX_GAP_MS` in every scenario.
  - Fixed version: read distribution within 20% of weight share over 1 h simulated time.
  - Fixed version: `|deficit[i]|` stays under `MAX_CHANNEL_DEFICIT_BOUND` in every scenario.
  - Fixed version: after a 60 s discard storm clears, all channels return to their weight share within one round.
  - NaN injection: simulator continues running, no channel index becomes invalid.

**On-device validation** (Jibril, hardware):

1. Flash fixed firmware on the dev board.
2. Clamp ch4 backwards, wait 30 s. Expect: ch4 sampled at watchdog cadence (~15 s) with a `LOG_WARNING` line per pick, no silent gaps.
3. Correct the orientation, PUT `reverse: false`. Expect: ch4's very next CYCEND lands a successful reading, channel returns to its normal cadence within the next round, no further warnings.
4. Toggle ch4 active/inactive several times. Expect: scheduler stabilises immediately.
5. Run for 1 hour, capture UDP log. Expect: zero `Channel %u starved` warnings under normal operation.

---

## Commit Plan

Each step independently testable against the simulator before moving to the next:

1. `fix(ade7953): refund channel deficit on discarded reads`
2. `fix(ade7953): run gain phase before priority-override return`
3. `fix(ade7953): arm priority read on reverse flag change`
4. `fix(ade7953): clamp deficit magnitude and guard against nan`
5. `feat(ade7953): add 15s starvation watchdog with warning log`

Commits land on `fix/ade7953-scheduler-starvation`, off `development`. PR closes #149.

---

## Risks and Rollback

- **Risk:** the watchdog could mask a deeper hardware problem (mux not switching, SPI flake) by repeatedly force-picking a channel that never reads successfully. **Mitigation:** every force-pick logs `LOG_WARNING` with the gap; existing `ADE7953_MAX_FAILURES_BEFORE_RESTART = 100/min` still trips on the underlying SPI failures.
- **Risk:** the deficit clamp could mask a genuine weight-table issue. **Mitigation:** the clamp is symmetric and large (5x the typical operating range), so it only triggers in pathological cases. Add a `LOG_DEBUG` when the clamp engages so issues are still visible at higher log levels.
- **Rollback:** each commit is independent. The watchdog (Fix 5) is the most behaviour-changing; the first four are pure correctness fixes that match what WDRR is supposed to do per the literature.
