## ADDED Requirements

### Requirement: Meter shadow exposes the voltage phase
The `meter` shadow SHALL expose `voltage_phase`, the device voltage phase of the `voltage-phase` capability (an integer 1 to 3), as a writable field next to `sample_time` and outside the calibration keys, following the same delta-apply-persist-ack pattern as the other writable fields. A valid delta value SHALL be applied through the same setter as the local REST API, and the same publish SHALL carry both `reported.voltage_phase` and `desired:{voltage_phase:null}`. A delta carrying only `voltage_phase` SHALL NOT rewrite the ADE7953 calibration. A value that is not an integer 1 to 3, or that could not be persisted, SHALL be ignored with a warning, SHALL leave the stored and reported value unchanged, and its desired key SHALL still be nulled. The full reported state SHALL include `voltage_phase`. A local change through the REST API SHALL appear in the reported state through the existing drift detection.

#### Scenario: Voltage phase applied from the cloud
- **WHEN** a `meter` delta sets `voltage_phase: 2` on a device at 1
- **THEN** the device persists and applies 2 and publishes `reported.voltage_phase=2` and `desired:{voltage_phase:null}` in the same message

#### Scenario: Calibration untouched
- **WHEN** a `meter` delta carries `voltage_phase` and no calibration key
- **THEN** no calibration register or calibration NVS key is written

#### Scenario: Invalid value ignored
- **WHEN** a `meter` delta sets `voltage_phase` to 4, 0 or `"2"`
- **THEN** the device logs a warning, nulls `desired.voltage_phase`, and the stored and reported value stay unchanged

#### Scenario: Local REST change reaches the shadow
- **WHEN** `PUT /api/v1/ade7953/voltage-phase` sets 3
- **THEN** within about 3 s the `meter` shadow republishes `reported.voltage_phase=3` with no `desired` written

#### Scenario: Reconnect reports the voltage phase
- **WHEN** the MQTT client (re)connects
- **THEN** the `meter` shadow's full reported state includes the current `voltage_phase`, alongside the calibration keys and `sample_time`
