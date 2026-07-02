## ADDED Requirements

### Requirement: 500 ms sampler aligned to absolute wall-clock boundaries
A dedicated sampler task SHALL emit at most one grid point per absolute half-second boundary (.000/.500), deriving each sleep from the current wall clock (`delay = 500 - (now % 500)`, with a minimum-sleep guard that skips to the next boundary). Each emitted point SHALL be timestamped with the true `getUnixTimeMilliseconds()` read at emission time, never the nominal boundary.

#### Scenario: Alignment after NTP step
- **WHEN** NTP steps the wall clock between ticks
- **THEN** the next iteration computes its delay from the corrected clock and lands on the new absolute boundary without any re-phasing logic

#### Scenario: Honest timestamps
- **WHEN** a tick fires at 12:00:00.503
- **THEN** the point carries 12:00:00.503, not 12:00:00.500

### Requirement: Emission gates with honest gaps
Each tick SHALL pass all gates before emitting, in order: (1) time is NTP-synced, (2) the grid report flag is enabled, (3) the EMA update counter has advanced since the last emission. A failed gate SHALL skip the tick entirely, producing a timestamp gap - the device SHALL never emit stale, repeated, or fabricated points.

#### Scenario: Dead line stops emission
- **WHEN** the voltage line is dead and no ZXV updates the EMA
- **THEN** the update counter does not advance and no points are emitted until real zero crossings resume

#### Scenario: Unsynced clock stops emission
- **WHEN** the device has not (yet) synchronized time via NTP
- **THEN** no grid points are emitted

### Requirement: Grid point content
Each point SHALL contain `{timestamp_ms (uint64), frequency (float), voltage (float)}` where frequency is the EMA readout and voltage is the cached channel-0 RMS value (refreshed each CYCEND). The sampler SHALL perform RAM-only work: no SPI, no flash access, no JSON serialization.

#### Scenario: Sampler stays off the SPI bus
- **WHEN** the sampler builds a point
- **THEN** it reads only in-RAM state (EMA, cached voltage, wall clock) and enqueues the struct

### Requirement: Dedicated batched grid topic
Grid points SHALL be published on a dedicated Basic Ingest MQTT topic (`grid`, via its own IoT rule), separate from the meter payload, as a batched sorted bare JSON array of positional triplets `[[t_unix_ms, frequency, voltage], ...]` (frequency serialized at 4 decimals, voltage at 1 decimal) on the existing publish cadence. The meter payload SHALL be unchanged by this capability.

#### Scenario: Batch publish
- **WHEN** the publish cadence fires with N queued points
- **THEN** one message with a sorted array of N points is published on the grid topic and the queue is drained

#### Scenario: Queue overflow drops oldest
- **WHEN** the grid queue is full (e.g. prolonged MQTT outage)
- **THEN** the oldest points are dropped in favor of new ones

### Requirement: Cloud-controlled report flag
A persisted boolean `send_grid_data` (default off) SHALL gate grid point emission. It SHALL be settable from the cloud via the existing writable `system` shadow mechanism and SHALL survive reboot. The flag SHALL NOT stop the EMA computation itself - local frequency consumers work regardless.

#### Scenario: Cloud enables reporting
- **WHEN** the cloud sets `send_grid_data: true` via the system shadow delta
- **THEN** the device persists it, reports it in the shadow, and starts emitting grid points at the next boundary that passes all gates

#### Scenario: Reporting off keeps local frequency alive
- **WHEN** `send_grid_data` is false
- **THEN** no grid points are emitted, while `getGridFrequency()` keeps returning the live filtered value
