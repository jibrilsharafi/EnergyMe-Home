## RENAMED Requirements

- FROM: `### Requirement: Six named shadows with fixed read/write roles`
- TO: `### Requirement: Seven named shadows with fixed read/write roles`

## MODIFIED Requirements

### Requirement: Seven named shadows with fixed read/write roles
The device SHALL expose exactly these named shadows: `info`, `issues`, `wifi` as reported-only, and `system`, `meter`, `channels`, `collector` as writable. A delta delivered for a reported-only shadow SHALL be ignored. The `collector` shadow carries the LAN collector switch, job set and job health defined in the `lan-collector` capability; it SHALL follow the same delta-apply-persist-ack contract as the other writable shadows and SHALL carry no secrets (jobs reference secrets only by name). The cloud SHALL accept the `collector` name before any device publishes it.

#### Scenario: Writable shadow accepts a delta
- **WHEN** a delta arrives for `system`, `meter`, `channels` or `collector`
- **THEN** the device applies each field, persists it, and acks

#### Scenario: Reported-only shadow ignores a delta
- **WHEN** a delta arrives for `info`, `issues`, or `wifi`
- **THEN** the device logs a warning and does not apply or persist anything

#### Scenario: Reconnect publishes all seven shadows
- **WHEN** the MQTT client (re)connects
- **THEN** the device subscribes to `update/delta` and `update/rejected` for the four writable shadows, including `collector`, and publishes reported state for all seven

#### Scenario: Collector shadow on a device without jobs
- **WHEN** a device that never received a job set publishes its `collector` shadow
- **THEN** the reported state is `enabled: false`, `rev: 0`, `sha256: null`, `apply_error: null` and an empty `jobs` array
