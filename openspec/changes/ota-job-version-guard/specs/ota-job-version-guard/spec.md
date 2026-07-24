## ADDED Requirements

### Requirement: Shared version comparison utility
The system SHALL provide a single shared function, `compareVersions(current, available)`, in `utils.cpp`/`utils.h` that parses two `"vX.Y.Z"`/`"X.Y.Z"`-style version strings and returns a positive value if `current` is newer, zero if equal, and a negative value if `current` is older. All firmware call sites that compare `FIRMWARE_BUILD_VERSION` against another version string (the firmware update-check endpoint and the OTA job version guard) SHALL use this shared function rather than a private/duplicated implementation.

#### Scenario: Current newer than available
- **WHEN** `compareVersions("1.5.0", "1.4.9")` is called
- **THEN** it returns a positive value

#### Scenario: Versions equal
- **WHEN** `compareVersions("1.5.0", "1.5.0")` is called
- **THEN** it returns zero

#### Scenario: Current older than available
- **WHEN** `compareVersions("1.4.9", "1.5.0")` is called
- **THEN** it returns a negative value

### Requirement: OTA job execution rejects non-upgrade targets by default
The system SHALL, in `_handleSingleJobExecution`, compare the job document's `firmware.version` against `FIRMWARE_BUILD_VERSION` using `compareVersions` before starting an OTA download, unless the job document's `force` field is `true`. When the target version is not strictly newer than the running firmware, the system SHALL publish a `REJECTED` job status with a reason indicating why, and SHALL NOT create the OTA download task.

#### Scenario: Job targets an older version than currently running
- **WHEN** an `ota_update` job document's `firmware.version` compares older than `FIRMWARE_BUILD_VERSION` and `force` is absent or `false`
- **THEN** the system publishes job status `REJECTED` with reason `downgrade_not_allowed` and does not start the OTA task

#### Scenario: Job targets the currently running version
- **WHEN** an `ota_update` job document's `firmware.version` equals `FIRMWARE_BUILD_VERSION` and `force` is absent or `false`
- **THEN** the system publishes job status `REJECTED` with reason `already_up_to_date` and does not start the OTA task

#### Scenario: Job targets a newer version
- **WHEN** an `ota_update` job document's `firmware.version` compares newer than `FIRMWARE_BUILD_VERSION`
- **THEN** the system proceeds with the existing OTA flow (acknowledges `IN_PROGRESS` and starts the download task) exactly as before this change

### Requirement: Force override bypasses the version guard
The system SHALL read an optional `force` boolean field from the job document (sibling of `operation` and `firmware`), defaulting to `false` when absent or not a boolean. When `force` is `true`, the system SHALL skip the version comparison entirely and proceed with the OTA flow regardless of the target version relative to the running firmware.

#### Scenario: Forced downgrade
- **WHEN** an `ota_update` job document has `firmware.version` older than `FIRMWARE_BUILD_VERSION` and `force` is `true`
- **THEN** the system proceeds with the OTA flow without evaluating the version comparison

#### Scenario: Forced same-version re-flash
- **WHEN** an `ota_update` job document has `firmware.version` equal to `FIRMWARE_BUILD_VERSION` and `force` is `true`
- **THEN** the system proceeds with the OTA flow without evaluating the version comparison

#### Scenario: Force field absent
- **WHEN** an `ota_update` job document does not include a `force` field
- **THEN** the system treats `force` as `false` and applies the version guard
