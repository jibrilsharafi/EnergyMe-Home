## Why

The meter payload mixes two data shapes with very different volatility: compact power points (`[unixTimeMs, channel, activePower, powerFactor]`) and near-static per-channel energy counters + voltage. At 16 active channels the energy/voltage block is ~2.9-3 KB of every ~4.86 KB message (~60%), and it is re-sent on every meter publish even though energy counters barely change between publishes. This directly undercuts the cadence knob just added in `configurable-meter-publish-rate`: lowering `meter_publish_threshold_bytes` to get faster power updates means paying to re-ship the full energy block every time too. There is also a live estimation bug in the current trigger - `MQTT_METER_ESTIMATED_ENERGY_VOLTAGE_OVERHEAD_BYTES` is hardcoded to 500 bytes but real overhead at 16 channels is ~2.9-3 KB, so `_checkIfPublishMeterNeeded()` under-estimates payload size. The grid topic already proves the fix: its own queue, its own topic, its own AWS IoT rule, wall-clock-minute-aligned publish. Energy gets the same treatment.

## What Changes

- **BREAKING** (cross-repo contract): remove voltage and per-channel energy data from the `meter` MQTT topic. `meter` becomes power-only: bare array of `[unixTimeMs, channel, activePower, powerFactor]`, gated on `send_power_data` as today, no other change to its threshold/interval trigger mechanics.
- Add a new `energy` MQTT topic (own AWS IoT rule, mirroring `meter`/`grid`/`log`), publishing a bare JSON array of self-contained objects - one voltage object plus one object per active channel - unchanged in shape from what `meter` emits today, just relocated:
  ```
  [
    {"unixTime": <ms>, "voltage": <float>},
    {"unixTime": <ms>, "channel": <n>, "activeEnergyImported": <f>, "activeEnergyExported": <f>, "reactiveEnergyImported": <f>, "reactiveEnergyExported": <f>, "apparentEnergy": <f>},
    ...
  ]
  ```
- Energy publishes once per minute, aligned to the wall-clock `:00` boundary, reusing the existing `MqttGridSchedule::nextAlignedBoundarySeconds` primitive (same mechanism the grid topic already uses).
- Energy is a snapshot, not a queued stream: at each boundary, read current per-channel values directly (same accessor path used today) - no new PSRAM queue.
- Energy publishing is unconditional (no boolean gate), matching today's existing behavior where these fields are already independent of `send_power_data`.
- Remove the now-irrelevant `MQTT_METER_ESTIMATED_ENERGY_VOLTAGE_OVERHEAD_BYTES` term from `_checkIfPublishMeterNeeded()`'s size estimate, since `meter` no longer carries energy/voltage weight.
- No change to `meter_publish_threshold_bytes` / `meter_publish_max_interval_ms` shadow key names - their semantics now describe power-only cadence, documented via updated comments, not a shadow schema change.
- Out of scope: AWS IoT rule, Lambda routing, and ingestion/storage for the new `energy` topic live in `energyme-infra` and are tracked separately; this change only defines and documents the new topic/contract so that work can proceed in parallel.

## Capabilities

### New Capabilities
- `energy-telemetry-stream`: the device-side mechanism that snapshots per-channel energy counters and voltage on a wall-clock-minute-aligned cadence and publishes them on a dedicated MQTT topic, separate from the meter (power) payload.

### Modified Capabilities
- `meter-publish-cadence`: the `meter` topic payload and size-estimate calculation change to power-only (energy/voltage removed); the threshold/interval trigger logic itself is unchanged.

## Impact

- `source/src/mqtt.cpp`: `_publishMeterStreaming()` loses the voltage-object and channel-energy-object blocks; new `_publishEnergy()` mirrors `_publishGrid()`'s structure; new `_checkIfPublishEnergyNeeded()` (or equivalent scheduling state, e.g. `_nextEnergyPublishUnixSecond`) mirrors `_checkIfPublishGridNeeded()`; `_checkPublishMqtt()` gains an energy branch; `_checkIfPublishMeterNeeded()` drops the energy/voltage overhead term; new `_setTopicEnergy()` mirrors `_setTopicGrid()`.
- `source/include/mqtt.h`: new `MQTT_TOPIC_ENERGY`, `MQTT_ENERGY_PUBLISH_ALIGN_SECONDS`; remove `MQTT_METER_ESTIMATED_ENERGY_VOLTAGE_OVERHEAD_BYTES`.
- `source/include/awsconfig.h`: new `AWS_IOT_CORE_RULE_ENERGY` (dev/prod), mirroring the existing per-topic rule constants.
- No new struct needed in `source/include/structs.h` - energy publish reads current channel state directly rather than draining a queue.
- No firmware change to `lib/mqtt_grid_schedule` - its alignment primitive is reused as-is.
- Cross-repo: `energyme-infra` needs a new AWS IoT rule + Lambda routing + ingestion/storage decision for the `energy` topic before this change ships to fleet (tracked separately, out of scope here).
- Related: builds on the just-completed `configurable-meter-publish-rate` change; touches code near the in-progress `harden-meter-energy-window-glitches` change (different concern - RSTREAD double-read correctness, not cadence/topic split), worth a cross-reference in tasks to avoid merge friction.
