## ADDED Requirements

### Requirement: System shadow exposes meter publish cadence fields
The `system` shadow SHALL expose `meter_publish_threshold_bytes` and `meter_publish_max_interval_ms` as writable fields, following the same delta-apply-persist-ack pattern as `send_power_data`/`send_grid_data`: a delta sets the field, the device persists it to NVS, and the same publish carries both the new `reported` value and `desired:{<field>:null}`.

#### Scenario: Field applied and intent cleared in one publish
- **WHEN** a valid `system` delta sets `meter_publish_threshold_bytes` or `meter_publish_max_interval_ms`
- **THEN** the device persists the (possibly clamped) value and publishes `reported=<applied>` and `desired:{<field>:null}` in the same message

#### Scenario: Reconnect reports current cadence values
- **WHEN** the MQTT client (re)connects
- **THEN** the `system` shadow's full reported state includes the current `meter_publish_threshold_bytes` and `meter_publish_max_interval_ms`, alongside the existing fields
