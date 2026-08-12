## Why

The device already reads its own die temperature (`temperatureRead()`, surfaced in `/system/info` under `performance.temperatureCelsius`) but nothing evaluates it against a threshold. No issue is raised, nothing alerts, and an overheating unit (bad enclosure placement, high ambient, sustained high load) gives no signal until it eventually crashes or degrades silently.

## What Changes

- Add a new `over_temperature` global issue code to the issue registry catalog (`source/lib/issue_logic/issue_logic.h` / `.cpp`), following the existing streak/hysteresis pattern.
- Evaluate it on the registry's existing 5 s tick (`_evaluateGlobalCodes` in `source/src/issueregistry.cpp`), reading `temperatureRead()` directly (same idiom as `heap_low` reading `ESP.getFreeHeap()` directly).
- Use asymmetric raise/clear thresholds (simple two-threshold hysteresis, same shape as `ISSUE_LITTLEFS_USED_FRACTION_RAISE` / `_CLEAR`) to avoid flapping around a single threshold: raise at 70 °C, clear at 65 °C. These are a reasonable starting point for ESP32-S3 die temperature in an enclosure (operating max 85 °C) - not measured against real hardware, and easy to retune later since they are named constants.
- Severity: `Warning` (degraded but functioning - matches `heap_low` / `voltage_out_of_range`, not `Error`).

## Capabilities

### New Capabilities
- `over-temperature-detection`: sustained-high-temperature evaluation on the issue registry's periodic tick, raising/clearing an `over_temperature` issue instance with asymmetric thresholds.

### Modified Capabilities
(none - this only adds a new issue code alongside existing ones; it does not change any existing capability's requirements)

## Impact

- `source/lib/issue_logic/issue_logic.h`: new `Code::OverTemperature` enum value.
- `source/lib/issue_logic/issue_logic.cpp`: new `CODE_TABLE` row (`"over_temperature"`, `Severity::Warning`).
- `source/include/issueregistry.h`: two new threshold constants (`ISSUE_TEMPERATURE_RAISE_CELSIUS`, `ISSUE_TEMPERATURE_CLEAR_CELSIUS`).
- `source/src/issueregistry.cpp`: one new evaluation block in `_evaluateGlobalCodes()` plus one new static hysteresis-condition variable, mirroring the `littlefs_near_full` block.
- No REST/API shape change (the issues list already reports `code` as a free string) and no frontend change (issue list rendering is generic, not keyed per-code).
- No new unit-testable predicate needed in `lib/issue_logic` (the raise/clear hysteresis is a two-constant comparison held as firmware-local state, same as `littlefs_near_full`, which has no dedicated `issue_logic` predicate either).
