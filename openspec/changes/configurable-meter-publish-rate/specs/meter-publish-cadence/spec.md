## ADDED Requirements

### Requirement: Configurable meter publish trigger
The device SHALL trigger an AWS IoT Core meter publish when EITHER the estimated queued meter payload size reaches a configurable byte threshold (`meter_publish_threshold_bytes`) AND power-data sending is enabled, OR the time since the last meter publish exceeds a configurable max interval (`meter_publish_max_interval_ms`). Both values SHALL default to the device's built-in constants (5120 bytes / 60000 ms) until the cloud sets a different `desired` value via the `system` shadow. Neither value SHALL introduce any on-device notion of a publish "mode" - the trigger logic SHALL remain the same OR of size-or-time regardless of what the two values are set to.

#### Scenario: Default cadence unchanged from today
- **WHEN** a device has never received a `desired` for `meter_publish_threshold_bytes` or `meter_publish_max_interval_ms`
- **THEN** it publishes meter data exactly as it does today (5 KB queue threshold OR 60 s ceiling)

#### Scenario: Cloud dials cadence down for near-real-time publishing
- **WHEN** the cloud sets `meter_publish_threshold_bytes` to a small value and `meter_publish_max_interval_ms` to a few seconds
- **THEN** the device publishes far more frequently, using the same OR trigger, with no separate "real-time mode" code path

#### Scenario: Cloud dials cadence up for batching
- **WHEN** the cloud sets both values higher than today's defaults
- **THEN** the device accumulates a larger queue and/or waits longer before publishing, using the same OR trigger

### Requirement: Meter publish cadence values are clamped, not rejected
An out-of-range `meter_publish_threshold_bytes` or `meter_publish_max_interval_ms` delta value SHALL be clamped to the nearest valid bound rather than ignored. The clamped value SHALL be the one persisted to NVS, applied at runtime, reported back to the shadow, and logged at WARN.

#### Scenario: Threshold below floor is clamped
- **WHEN** a delta sets `meter_publish_threshold_bytes` below the minimum floor
- **THEN** the device persists and reports the floor value instead, and logs a WARN

#### Scenario: Interval above ceiling is clamped
- **WHEN** a delta sets `meter_publish_max_interval_ms` above the maximum ceiling
- **THEN** the device persists and reports the ceiling value instead, and logs a WARN
