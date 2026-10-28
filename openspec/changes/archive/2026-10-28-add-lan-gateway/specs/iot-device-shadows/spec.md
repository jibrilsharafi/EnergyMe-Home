## ADDED Requirements

### Requirement: System shadow exposes the gateway switch
The `system` shadow SHALL expose `gateway_enabled` as a writable boolean, following the same delta-apply-persist-ack pattern as the other `system` fields. A delta sets the field, the device persists it to NVS, and the same publish carries both the new `reported` value and `desired:{gateway_enabled:null}`. A non-boolean value, or a value that could not be persisted, SHALL be ignored with a warning and leave the stored and reported state unchanged. A local change through the REST API SHALL appear in the reported state through the existing drift detection.

#### Scenario: Gateway switch applied from the cloud
- **WHEN** a `system` delta sets `gateway_enabled: true`
- **THEN** the device persists it and publishes `reported.gateway_enabled=true` and `desired:{gateway_enabled:null}` in the same message

#### Scenario: Invalid value ignored
- **WHEN** a `system` delta sets `gateway_enabled: "yes"`
- **THEN** the device logs a warning and the stored and reported value stay unchanged

#### Scenario: Reconnect reports the gateway switch
- **WHEN** the MQTT client (re)connects
- **THEN** the `system` shadow's full reported state includes the current `gateway_enabled`, alongside the existing fields
