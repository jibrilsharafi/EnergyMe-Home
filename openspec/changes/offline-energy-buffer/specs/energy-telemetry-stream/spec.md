## MODIFIED Requirements

### Requirement: Energy snapshot is read directly, not queued
At each publish boundary, the device SHALL read current per-channel energy and voltage state directly rather than buffering intermediate values in a queue between boundaries. No new PSRAM-backed queue SHALL be introduced for energy data. The only exception is a minute boundary whose live snapshot could not be published: its active energy counters are kept in the flash-backed offline energy buffer and replayed later (see the offline-energy-buffer capability). Live snapshots SHALL never be delayed to wait for that replay.

#### Scenario: Snapshot reflects state at boundary time
- **WHEN** the `:00` boundary triggers a publish
- **THEN** the device reads each active channel's current energy counters and the cached channel-0 voltage at that moment, with no prior buffering of intermediate readings

#### Scenario: Unpublished minute goes to the offline buffer
- **WHEN** the live snapshot for a boundary cannot be published because the cloud is unreachable
- **THEN** that boundary's active energy counters are stored in the offline energy buffer instead of being lost

### Requirement: Energy snapshot content and shape
Each energy publish SHALL be a bare JSON array of self-contained objects. A live snapshot SHALL contain exactly one voltage object (`{unixTime, voltage}`), followed by one object per active channel with valid measurements (`{unixTime, channel, activeEnergyImported, activeEnergyExported, reactiveEnergyImported, reactiveEnergyExported, apparentEnergy}`). A replay message from the offline energy buffer SHALL contain only channel objects with `{unixTime, channel, activeEnergyImported, activeEnergyExported}` and no voltage object. Consumers SHALL treat `reactiveEnergyImported`, `reactiveEnergyExported` and `apparentEnergy` as optional. Per-channel `unixTime` SHALL be the real last-read time for that channel, not the nominal boundary time. Fields SHALL use real key names (not positional/compacted arrays).

#### Scenario: Only active channels included
- **WHEN** a channel is inactive or has no valid measurements
- **THEN** it SHALL NOT appear in the energy snapshot array

#### Scenario: Per-channel timestamp reflects actual read time
- **WHEN** two channels are read at slightly different times during the same snapshot pass (round-robin reading)
- **THEN** each channel's object carries its own actual `unixTime`, not a shared nominal boundary timestamp

#### Scenario: Replay message carries only active counters
- **WHEN** buffered offline records are replayed
- **THEN** each object carries `unixTime`, `channel`, `activeEnergyImported` and `activeEnergyExported` only, and the message has no voltage object
