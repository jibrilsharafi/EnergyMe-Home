## MODIFIED Requirements

### Requirement: Dedicated batched grid topic
Grid points SHALL be published on a dedicated Basic Ingest MQTT topic (`grid`, via its own IoT rule), separate from the meter payload, as a batched sorted bare JSON array of positional triplets `[[t_unix_ms, frequency, voltage], ...]` (frequency serialized at 4 decimals, voltage at 1 decimal) on a wall-clock-aligned publish cadence (see the "Wall-clock-aligned publish cadence" requirement). The meter payload SHALL be unchanged by this capability.

#### Scenario: Batch publish
- **WHEN** the publish cadence fires with N queued points
- **THEN** one message with a sorted array of N points is published on the grid topic and the queue is drained

#### Scenario: Queue overflow drops oldest
- **WHEN** the grid queue is full (e.g. prolonged MQTT outage)
- **THEN** the oldest points are dropped in favor of new ones

## ADDED Requirements

### Requirement: Wall-clock-aligned publish cadence
The device SHALL trigger a grid batch publish based on an absolute wall-clock minute boundary (`:00` of every minute) rather than a relative interval measured from the last publish. The trigger comparison SHALL be "current time at or past the target boundary," not exact-equality, so a delayed check still fires instead of missing the boundary. After each publish, the next target boundary SHALL be recomputed from the current time, not by adding a fixed offset to the previous target. A single publish SHALL still never exceed the AWS IoT Core payload limit; any points that do not fit SHALL remain queued for the next publish.

#### Scenario: Aligned publish at the minute boundary
- **WHEN** the wall clock reaches a minute boundary and the grid queue is non-empty
- **THEN** a publish is triggered at or immediately after that boundary

#### Scenario: Delayed check still catches the boundary
- **WHEN** the periodic check that would normally fire exactly at the boundary is delayed (e.g. a busy loop tick)
- **THEN** the next time the check runs and finds the current time at or past the boundary, it still triggers the publish, instead of waiting for the following boundary

#### Scenario: First publish after connect is aligned
- **WHEN** the device (re)connects and starts queuing grid points with no prior publish target set
- **THEN** the first publish is scheduled for the next upcoming minute boundary, not triggered immediately regardless of the wall-clock phase

#### Scenario: Resync without a catch-up burst after a long outage
- **WHEN** the device reconnects after an outage long enough that the previous target boundary is far in the past
- **THEN** the device publishes once to drain what is due and computes its next target boundary from the current time, rather than repeatedly firing to walk a stale target forward in fixed increments

#### Scenario: Oversized backlog still respects the payload cap
- **WHEN** a triggered publish has more queued points than fit under the AWS IoT Core payload limit
- **THEN** the publish sends as many points as fit and leaves the remainder queued for the next triggered publish
