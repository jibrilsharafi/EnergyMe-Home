# iot-device-shadows Specification (delta)

## ADDED Requirements

### Requirement: Info shadow reports the rollback target fingerprint
The `info` shadow SHALL report `other_partition_sha256`: the 64-hex-character application sha256 of the firmware image currently in the passive OTA partition, or `null` when the passive slot holds no readable application descriptor. This makes every device's rollback target observable fleet-wide before any incident, so a `firmware_rollback` command's `expected_sha256` can be chosen from the shadow without querying the device during an outage.

#### Scenario: Info shadow carries the passive slot fingerprint
- **WHEN** the `info` shadow is published on a device that has completed at least one OTA
- **THEN** the reported state includes `other_partition_sha256` with the passive partition's 64-hex sha256 (the previously running firmware)

#### Scenario: Unreadable passive slot reported as null
- **WHEN** the passive partition has no valid application descriptor (e.g. fresh factory device)
- **THEN** the `info` shadow reports `other_partition_sha256` as `null`
