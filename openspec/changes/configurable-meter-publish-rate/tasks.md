## 1. Constants and NVS keys

- [x] 1.1 In `source/include/mqtt.h`, rename/repurpose `AWS_IOT_CORE_MQTT_PAYLOAD_MINIMUM_BILLABLE` usage and `MQTT_MAX_INTERVAL_METER_PUBLISH` as *default* values (keep the existing constants as the defaults; do not change their values).
- [x] 1.2 Add `MQTT_METER_PUBLISH_THRESHOLD_BYTES_MIN` / `_MAX` and `MQTT_METER_PUBLISH_MAX_INTERVAL_MS_MIN` / `_MAX` clamp-bound constants per design.md.
- [x] 1.3 Add NVS preference keys (e.g. `MQTT_PREFERENCES_METER_PUBLISH_THRESHOLD_KEY`, `MQTT_PREFERENCES_METER_PUBLISH_INTERVAL_KEY`), following the existing `MQTT_PREFERENCES_SEND_POWER_DATA_KEY` naming pattern.

## 2. Config getters/setters in mqtt.cpp

- [x] 2.1 Add persisted config state + getter/setter pair for `meter_publish_threshold_bytes`, mirroring `getSendPowerData`/`setSendPowerData` (load from NVS on boot, default to the existing constant if absent).
- [x] 2.2 Add persisted config state + getter/setter pair for `meter_publish_max_interval_ms`, same pattern.
- [x] 2.3 Clamp both setters to the Section 1.2 bounds using `std::clamp` (`<algorithm>`); log a WARN when the clamped value differs from the request, so callers can see it via `reported`.
- [x] 2.4 Declare the new getters/setters in `source/include/mqtt.h`'s `Mqtt` namespace, alongside `getSendPowerData`/`setSendPowerData`.

## 3. Wire into the publish trigger

- [x] 3.1 Update `_checkIfPublishMeterNeeded()` (`source/src/mqtt.cpp`) to read the threshold and max-interval from the new getters instead of the hardcoded constants. Keep the OR logic (size-trigger AND power-enabled) OR (time-trigger) exactly as-is.
- [x] 3.2 Confirm `MQTT_METER_ESTIMATED_ENERGY_VOLTAGE_OVERHEAD_BYTES` and the estimated-size math still make sense against a threshold that may now be far below 5 KB (i.e. no divide-by-zero or negative-size edge case when threshold is near the floor).

## 4. System shadow integration

- [x] 4.1 In `source/src/shadow.cpp::_reportSystem`, add `meter_publish_threshold_bytes` and `meter_publish_max_interval_ms` to the reported JSON, reading from the new getters.
- [x] 4.2 In `_applySystem`, handle deltas for both fields: validate non-negative integer, persist via the new setters (which clamp and WARN internally, per 2.3), and include the applied (possibly clamped) value read back from the getter in the combined `reported`+`desired:null` ack publish - matching the existing `send_power_data` delta-apply block.
- [x] 4.3 Reject (log WARN, apply nothing) non-integer values for either field, matching the existing "Rejected send_power_data: not a boolean" pattern.

## 5. Tests

- [x] 5.1 No new native unit tests needed: clamping uses `std::clamp` (already covered by the standard library, not worth re-testing) and the trigger logic stays inline in `_checkIfPublishMeterNeeded` rather than being extracted to `lib/` (avoids an extra module for two conditions that are trivial to read in place). Verification is hardware/e2e only (Section 6).
- [x] 5.2 Confirm `pio test -e native` still passes unchanged (no test regressions from this change, since nothing new was added to `lib/`).

## 6. Hardware verification (dev device)

- [ ] 6.1 Flash dev build to a bench device; confirm default behavior (no shadow write yet) matches today's cadence (5 KB / 60 s) via UDP log capture.
- [ ] 6.2 Push a `system` shadow `desired` lowering both values (e.g. small threshold, few-second interval); confirm via UDP logs and the reported shadow that the device applies, persists, and acks correctly, and that publish frequency visibly increases.
- [ ] 6.3 Push an out-of-range `desired` (e.g. 0, or above ceiling) and confirm the device clamps, logs a WARN, and reports the clamped value rather than the requested one.
- [ ] 6.4 Reboot the device with a non-default persisted value set; confirm it boots back into that same cadence (NVS persistence) rather than reverting to firmware defaults.
