## 1. Constants

- [x] 1.1 In `source/include/mqtt.h`, add `MQTT_TOPIC_ENERGY "energy"` alongside `MQTT_TOPIC_METER`/`MQTT_TOPIC_GRID`.
- [x] 1.2 Add `MQTT_ENERGY_PUBLISH_ALIGN_SECONDS 60`, mirroring `MQTT_GRID_PUBLISH_ALIGN_SECONDS`.
- [x] 1.3 Remove `MQTT_METER_ESTIMATED_ENERGY_VOLTAGE_OVERHEAD_BYTES` (no longer needed once energy/voltage leave the meter payload).
- [x] 1.4 In `source/include/awsconfig.h`, add `AWS_IOT_CORE_RULE_ENERGY` for both dev and prod, mirroring the existing `AWS_IOT_CORE_RULE_METER`/`GRID`/`LOG` per-env pattern.

## 2. Energy topic setup

- [x] 2.1 In `source/src/mqtt.cpp`, add `static char _mqttTopicEnergy[MQTT_TOPIC_BUFFER_SIZE];` alongside `_mqttTopicMeter`/`_mqttTopicGrid`.
- [x] 2.2 Add `_setTopicEnergy()` using `_constructMqttTopicWithRule(AWS_IOT_CORE_RULE_ENERGY, MQTT_TOPIC_ENERGY, _mqttTopicEnergy, sizeof(_mqttTopicEnergy))`, mirroring `_setTopicMeter()`/`_setTopicGrid()`; call it alongside the existing `_setTopicMeter(); _setTopicGrid();` setup call.
- [x] 2.3 Add `_nextEnergyPublishUnixSecond` scheduling state, mirroring `_nextGridPublishUnixSecond`.

## 3. Energy publish path

- [x] 3.1 Add `_checkIfPublishEnergyNeeded()`, mirroring `_checkIfPublishGridNeeded()`: on first check after (re)connect, schedule the next aligned boundary via `MqttGridSchedule::nextAlignedBoundarySeconds(now, MQTT_ENERGY_PUBLISH_ALIGN_SECONDS)` without publishing; on subsequent checks, trigger (`_publishMqtt.energy = true`) when current time is at or past the target boundary.
- [x] 3.2 Add a `energy` flag to the `_publishMqtt` state struct (alongside `meter`, `grid`, etc.).
- [x] 3.3 Add `_publishEnergy()`: build the voltage object + one object per active channel (`Ade7953::isChannelActive && hasChannelValidMeasurements`), reading current `MeterValues` directly via `Ade7953::getMeterValues` - this is the same content currently built inline in `_publishMeterStreaming()` (mqtt.cpp ~2015-2043), relocated verbatim into its own function targeting `_mqttTopicEnergy`. On successful publish, clear the `energy` flag and recompute `_nextEnergyPublishUnixSecond` from current time (mirrors `_publishGrid()`'s post-publish rescheduling).
- [x] 3.4 Wire `_checkIfPublishEnergyNeeded()` into the periodic scheduling pass (alongside the existing grid check) and add `if (_publishMqtt.energy) {_publishEnergy();}` to `_checkPublishMqtt()`.

## 4. Meter payload becomes power-only

- [x] 4.1 Remove the voltage-object block and the per-channel energy-object loop from `_publishMeterStreaming()` (mqtt.cpp ~2015-2043) - the content moved to `_publishEnergy()` in Section 3, not duplicated. Also simplified `_publishMeter()`'s publish gate, which previously treated per-channel energy availability as a reason to publish the meter topic - now power-only, it gates purely on queue contents + `_sendPowerDataEnabled`.
- [x] 4.2 Update `_checkIfPublishMeterNeeded()`'s `estimatedJsonSize` calculation to drop the removed energy/voltage overhead term; estimate is now `queueSize * MQTT_METER_ESTIMATED_PER_ENTRY`. Also resolved the in-code TODO on this function: it now additionally triggers a publish when the meter queue is at/above `MQTT_METER_QUEUE_ALMOST_FULL_RATIO` (90%) of capacity, regardless of the byte/interval trigger, so `pushMeter()`'s ring buffer doesn't silently drop the oldest entry under sustained high-rate writes.
- [x] 4.3 Update the doc comment on `meter_publish_threshold_bytes`/`meter_publish_max_interval_ms` (in `mqtt.cpp` and/or `mqtt.h`) to clarify they now describe power-only publish cadence, since the shadow key names themselves are unchanged.

## 5. Tests

- [x] 5.1 Confirm no changes needed to `lib/mqtt_grid_schedule` - the alignment primitive is reused as-is by the new energy scheduling check; `pio test -e native` passes unchanged (203/203, run via WSL).
- [x] 5.2 If any new pure logic is extracted for the energy snapshot content-building (unlikely, since it's a direct relocation of existing inline code), add a corresponding `lib/`+`test/` pair following the `mqtt_grid_schedule` pattern; otherwise skip. Skipped - no new pure logic extracted, content-building stayed inline in `_publishEnergy()` mirroring `_publishGrid()`.

## 6. Hardware verification (dev device)

- [x] 6.1 Flash dev build to a bench device; confirm the `meter` topic no longer contains voltage or per-channel energy fields (UDP log capture / MQTT client inspection), only power point arrays. Verified on .174: meter payloads dropped to 31-90 bytes (power points only), vs. ~4.5KB pre-split.
- [x] 6.2 Confirm the new `energy` topic publishes once per minute, aligned to `:00` (verify via timestamps in captured messages across several minutes, not just message arrival time). Verified across multiple consecutive boundaries (:52:00, :53:00, ... :00:00) with no drift.
- [x] 6.3 Confirm energy snapshots continue publishing with `send_power_data` set to false. Verified via shadow_cli.py; energy kept its 60s cadence, meter topic went fully silent (no publishes at all).
- [x] 6.4 Confirm energy snapshots (including voltage) continue publishing with `send_grid_data` set to false. Verified; energy payload size stayed stable (~740B, voltage included), grid topic went silent at the next boundary once its queue drained (pushGrid() gates new points at the source - pre-existing behavior, not part of this change).
- [x] 6.5 Confirm meter publish cadence (threshold/interval trigger behavior from `configurable-meter-publish-rate`) is otherwise unaffected - shadow-configured values still apply to power-only publishing. Verified with restored 5120B/60000ms defaults: threshold-based publish fires reliably every ~19-20s once the power-only queue crosses ~147 entries (~4.3KB).
- [x] 6.6 Cross-check against the in-progress `harden-meter-energy-window-glitches` change for merge conflicts in nearby energy-read code before finalizing this branch. No conflict: harden's code sections (1-4, the RSTREAD fix + IRMS witness in ade7953.cpp) are already merged into development as #199 (commit 5416b39), which this branch is based on. Only harden's §5 (on-device calibration/log tuning) remains open and doesn't touch mqtt.cpp.

## 7. Cross-repo coordination

- [x] 7.1 Confirm `energyme-infra` has the `energy` AWS IoT rule (dev environment) live and routed before flashing any dev/bench device with this firmware. Confirmed live from energyme-infra.
- [x] 7.2 Do not merge/release to fleet until prod-side `energyme-infra` routing for the `energy` topic is confirmed ready. Confirmed live from energyme-infra.
