## Why

The device's configurable state (channel setup, system toggles, meter calibration) only flowed device -> cloud as telemetry, with no way for the cloud to *change* it and no cloud visibility of the on-device issue registry. This change brings cloud-side configuration parity with the local web UI / REST API, mediated by the EnergyMe Intelligence backend, using the right AWS primitive per job.

## What Changes

- Add an AWS IoT **Named Device Shadow** module (`source/src/shadow.cpp`) with 6 shadows: `info`, `issues`, `wifi` (reported-only) and `system`, `meter`, `channels` (writable).
- Add 3 **IoT Commands** for transient actions: `restart`, `factory_reset`, `energy_reset`.
- Local REST/UI edits are mirrored to shadows via a source-agnostic 3 s drift-detect (reported-only; cloud is never overridden by a local edit).
- `system.mqtt_log_level` transient levels (VERBOSE/DEBUG) auto-revert to the persisted baseline after 5 min, surviving a reboot.
- **BREAKING**: retire the legacy config topics `system/static`, `command`, `channel`. Surviving telemetry topics are unchanged, so there is zero ingest gap; `MQTT_TOPIC_VERSION` stays `v1`.
- `MQTT_BUFFER_SIZE` 5 KB -> 9 KB to fit the worst-case inbound shadow delta.

## Capabilities

### New Capabilities
- `iot-device-shadows`: cloud<->device configuration sync over AWS Named Device Shadows (publish-reported-first, no GET; writable + reported-only shadows; drift-detect; optimistic concurrency).
- `iot-commands`: transient device actions over AWS IoT Commands (restart, factory_reset, energy_reset) with anti-fat-finger and staleness guards.

### Modified Capabilities
- (none - no existing OpenSpec specs predate this change)

## Impact

- Firmware: new `shadow.cpp/.h`; `mqtt.cpp` (commands router, retired topic handlers removed); `mqtt.h` buffer bump; touches `ade7953`, `customwifi`, issue registry, system config getters/setters.
- Cloud (external, `energyme-infra`): shadow ingestion + desired-state writer + `StartCommandExecution` dispatcher. The asymmetric desired-null contract is load-bearing on the cloud writer clearing `desired` after convergence.
- No change to the local REST API; telemetry payload contents unchanged.
