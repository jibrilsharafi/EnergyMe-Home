# energy-telemetry-stream Specification

## Purpose
The device-side mechanism that snapshots per-channel energy counters and voltage on a wall-clock-minute-aligned cadence and publishes them on a dedicated `energy` MQTT topic, separate from the `meter` (power) payload. Unconditional (not gated by `send_power_data` or `send_grid_data`), serving as the voltage-visibility floor.

## Requirements
### Requirement: Wall-clock-aligned energy snapshot cadence
The device SHALL publish an energy snapshot based on an absolute wall-clock minute boundary (`:00` of every minute), computed via the same alignment primitive used by the grid telemetry stream (`nextAlignedBoundarySeconds`). The trigger comparison SHALL be "current time at or past the target boundary," not exact-equality, so a delayed check still fires instead of missing the boundary. After each publish, the next target boundary SHALL be recomputed from the current time, not by adding a fixed offset to the previous target.

#### Scenario: Aligned publish at the minute boundary
- **WHEN** the wall clock reaches a minute boundary
- **THEN** an energy snapshot publish is triggered at or immediately after that boundary

#### Scenario: Delayed check still catches the boundary
- **WHEN** the periodic check that would normally fire exactly at the boundary is delayed
- **THEN** the next time the check runs and finds the current time at or past the boundary, it still triggers the publish, instead of waiting for the following boundary

#### Scenario: First publish after connect is aligned
- **WHEN** the device (re)connects with no prior publish target set
- **THEN** the first energy publish is scheduled for the next upcoming minute boundary, not triggered immediately regardless of the wall-clock phase

### Requirement: Energy snapshot is read directly, not queued
At each publish boundary, the device SHALL read current per-channel energy and voltage state directly rather than buffering intermediate values in a queue between boundaries. No new PSRAM-backed queue SHALL be introduced for energy data.

#### Scenario: Snapshot reflects state at boundary time
- **WHEN** the `:00` boundary triggers a publish
- **THEN** the device reads each active channel's current energy counters and the cached channel-0 voltage at that moment, with no prior buffering of intermediate readings

### Requirement: Energy snapshot content and shape
Each energy publish SHALL be a bare JSON array of self-contained objects: exactly one voltage object (`{unixTime, voltage}`), followed by one object per active channel with valid measurements (`{unixTime, channel, activeEnergyImported, activeEnergyExported, reactiveEnergyImported, reactiveEnergyExported, apparentEnergy}`). Per-channel `unixTime` SHALL be the real last-read time for that channel, not the nominal boundary time. Fields SHALL use real key names (not positional/compacted arrays).

#### Scenario: Only active channels included
- **WHEN** a channel is inactive or has no valid measurements
- **THEN** it SHALL NOT appear in the energy snapshot array

#### Scenario: Per-channel timestamp reflects actual read time
- **WHEN** two channels are read at slightly different times during the same snapshot pass (round-robin reading)
- **THEN** each channel's object carries its own actual `unixTime`, not a shared nominal boundary timestamp

### Requirement: Unconditional emission
Energy snapshots SHALL be published regardless of the `send_power_data` or `send_grid_data` shadow settings. There SHALL be no separate boolean flag gating energy emission.

#### Scenario: Energy publishes with power data disabled
- **WHEN** `send_power_data` is false
- **THEN** energy snapshots are still published on the aligned `:00` cadence

#### Scenario: Energy publishes with grid data disabled
- **WHEN** `send_grid_data` is false
- **THEN** energy snapshots (including voltage) are still published on the aligned `:00` cadence, serving as the voltage-visibility floor

### Requirement: Dedicated energy topic
Energy snapshots SHALL be published on a dedicated MQTT topic (`energy`, via its own AWS IoT rule), separate from both the `meter` (power) topic and the `grid` topic.

#### Scenario: Energy and meter topics are independent
- **WHEN** the device publishes an energy snapshot
- **THEN** it is sent as a separate MQTT message on the `energy` topic, not merged into any `meter` topic message
