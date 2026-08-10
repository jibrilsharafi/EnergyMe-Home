## Context

`source/src/issueregistry.cpp::_evaluateGlobalCodes()` already runs every 5 s (`ISSUE_REGISTRY_TICK_INTERVAL`) and hosts several global-scope evaluators. Two existing shapes are relevant:

- **Streak/Evidence hysteresis** (`heap_low`, `voltage_out_of_range`, ...): a pure `IssueLogic::Evidence` predicate in `lib/issue_logic` feeds `updateStreak()`, and the issue raises once the streak reaches `ISSUE_STREAK_TO_RAISE`. One good window resets the streak to zero (symmetric raise/clear).
- **Two-threshold hysteresis** (`littlefs_near_full`): no pure predicate - a single boolean condition variable is set/cleared directly against two constants (`ISSUE_LITTLEFS_USED_FRACTION_RAISE` / `_CLEAR`), and that boolean is fed straight into `_updateInstance()`. The "sustained" duration comes from the boolean itself only flipping once the value has actually crossed the far threshold; there is no separate streak counter.

The issue asks explicitly for the second shape. See proposal.md - Why/What Changes for motivation and threshold values.

## Goals / Non-Goals

**Goals:**
- Match the `littlefs_near_full` implementation shape exactly, so the codebase has one clear pattern per hysteresis style instead of a near-duplicate third variant.
- Keep the new code entirely inside the existing registry tick - no new task, no new timer.

**Non-Goals:**
- No new pure predicate in `lib/issue_logic` (the two-constant comparison is trivial enough that `littlefs_near_full` didn't get one either; adding one here would be an inconsistent one-off).
- No throttling or corrective action (e.g. forcing lower duty cycle) on over-temperature - this change only raises the issue, matching the issue's stated scope ("nothing throttles" is listed as the current gap, not a requirement to fix it here).
- No change to `temperatureRead()` itself or to how `/system/info` reports `performance.temperatureCelsius`.

## Decisions

**Read `temperatureRead()` directly in the tick, not through the cached `SystemInfo` struct.** `SystemInfo.temperatureCelsius` is only populated on-demand when `/system/info` is built (`utils.cpp` `getSystemInfo()`), not on a periodic cadence - it would be stale or absent between REST calls. `heap_low` has the same shape problem and solves it the same way: call the cheap ESP-IDF accessor (`ESP.getFreeHeap()`) directly in the tick. `temperatureRead()` is an equally cheap direct ADC-based read, so the same idiom applies.

**Two-threshold boolean hysteresis (`littlefs_near_full` shape), not streak/Evidence.** The issue explicitly asks for this shape. It is also a better fit than streak hysteresis here: streak hysteresis resets to zero on a single good window, which suits noisy binary-ish signals (one failed request, one clean request). Temperature is a slowly-varying analog signal, so a plain "still above/below the far threshold" boolean is simpler and behaves the same way in practice (a single 5 s dip below the raise threshold but above the clear threshold should not immediately reset anything - it doesn't, since the boolean only flips at the clear threshold).

**Thresholds: raise 70 °C, clear 65 °C.** Chosen as a starting point: ESP32-S3 rated operating range tops out at 85 °C die temperature, so 70/65 °C leaves headroom before the rated limit while still catching a genuinely hot enclosure early. The issue itself flags these as TBD pending real hardware data; they are plain named constants (`ISSUE_TEMPERATURE_RAISE_CELSIUS` / `_CLEAR_CELSIUS`) in `issueregistry.h` alongside the other per-code thresholds, so retuning later is a one-line change with no logic changes.

**Severity: `Warning`.** Matches `heap_low` and `voltage_out_of_range` (degraded but functioning) rather than `Error` (data/functionality loss) - an over-temperature condition on its own has not yet caused a failure.

## Risks / Trade-offs

- [`temperatureRead()` accuracy] The ESP32-S3 internal sensor is not independently calibrated per unit and reads die temperature, not enclosure ambient - absolute values can vary between units at the same ambient. → Acceptable for a coarse "something is unusually hot" signal; not intended as a calibrated measurement. Threshold is deliberately conservative relative to the 85 °C rated max.
- [Fixed thresholds may not fit every enclosure/climate] A unit in a naturally warm location could sit near 70 °C under normal load. → Thresholds are named constants, trivially retunable once real field data exists; no design change needed.
- [Sensor readable range] The Arduino core installs the internal temperature sensor with a requested calibration range of 10-50 °C (`TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50)` in `esp32-hal-misc.c`), which raised a concern during review that reads above ~50 °C could fail (return NaN) and the 70 °C raise threshold could be unreachable. Confirmed on real hardware (a bench unit reached 70 °C with `temperatureRead()` still returning valid readings) that the underlying ESP-IDF driver selects a wider physical calibration sub-range that covers 70 °C - the (10, 50) request is only a hint for calibration accuracy, not a hard read ceiling. No design change needed.
