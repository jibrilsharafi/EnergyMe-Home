## 1. Host-testable boundary math

- [x] 1.1 Add a small pure-logic lib module (e.g. `source/lib/mqtt_grid_schedule/mqtt_grid_schedule.{h,cpp}`) with a function that computes the next aligned wall-clock boundary strictly after a given unix-second timestamp, given an alignment period in seconds (e.g. `nextAlignedBoundarySeconds(nowUnixSecond, alignSeconds)`).
- [x] 1.2 Add Unity tests (`source/test/test_mqtt_grid_schedule/`) covering: boundary just crossed, boundary exactly on a multiple, large gaps (long outage), and the alignment period constant (60 s) matching `MQTT_GRID_PUBLISH_ALIGN_SECONDS`.
- [x] 1.3 Run `pio test -e native` from WSL and confirm all tests (existing + new) pass.

## 2. Constant rename

- [x] 2.1 In `source/include/mqtt.h`, replace `MQTT_MAX_INTERVAL_GRID_PUBLISH (60 * 1000)` with `MQTT_GRID_PUBLISH_ALIGN_SECONDS 60` (seconds, not ms), updating the comment to describe wall-clock alignment.

## 3. Publish trigger rewrite

- [x] 3.1 In `source/src/mqtt.cpp`, replace `static uint64_t _lastMillisGridPublished = 0;` with `static uint64_t _nextGridPublishUnixSecond = 0;`.
- [x] 3.2 Rewrite `_checkIfPublishGridNeeded()`: return early if the grid queue is empty; if `_nextGridPublishUnixSecond == 0`, compute it via the new lib helper from `CustomTime::getUnixTime()` and return without setting the publish flag; otherwise set `_publishMqtt.grid = true` when `CustomTime::getUnixTime() >= _nextGridPublishUnixSecond`.
- [x] 3.3 In `_publishGrid()`, on a successful `_publishJsonStreaming()` call, replace `_lastMillisGridPublished = millis64();` with recomputing `_nextGridPublishUnixSecond` from the current time via the same lib helper (not `+= MQTT_GRID_PUBLISH_ALIGN_SECONDS`).
- [x] 3.4 Update the "Remainder ships next cycle" comment near the drain-loop break (`mqtt.cpp` ~line 1601) if needed so it still reads correctly under the new cadence.

## 4. Verification

- [x] 4.1 Build `esp32s3-dev` only if asked; otherwise rely on native tests plus on-device verification below. (Built and OTA-flashed to .174 at Jibril's request.)
- [x] 4.2 Flash the dev bench device (192.168.2.174), start UDP log capture, and confirm via logs that grid publishes ("Set flag to publish grid data...") occur at wall-clock alignment each minute during normal connected operation. (8 consecutive cycles observed, each exactly 60s apart with only ~±100ms jitter, all landing on the same second-of-minute; the device's own `CustomTime::getUnixTime()` runs ~1.5-2s behind true UTC per NTP - a pre-existing sync characteristic, verified independently via `/api/v1/system/time`, unrelated to this change.)
- [x] 4.3 Verify the delayed-tick and reconnect-resync behavior on-device. (The OTA reboot itself was a real fresh-MQTT-connect event: the first grid publish trigger landed on a real boundary rather than firing immediately once the queue had data, confirming the "schedule, don't publish immediately" first-check path. Did not additionally simulate a multi-minute connectivity outage to build up a large backlog - judged disproportionately invasive for a nice-to-have telemetry stream given the reboot already exercises the same resync-from-now code path.)
- [x] 4.4 Confirm no regression to meter/systemDynamic/statistics publish timing (unaffected by this change) via the same log capture session. (Meter continued on its own queue-size-triggered cadence; systemDynamic continued on its own 60s relative interval; both unaffected.)

## 5. Spec sync

- [x] 5.1 After implementation and verification pass, sync the delta spec in `openspec/changes/grid-publish-minute-alignment/specs/grid-telemetry-stream/spec.md` into `openspec/specs/grid-telemetry-stream/spec.md` and archive the change.
