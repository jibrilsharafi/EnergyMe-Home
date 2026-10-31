## ADDED Requirements

### Requirement: System shadow exposes the gateway write grants
The `system` shadow SHALL expose `gateway_write_grants`, a writable array of at most 8 entries (write grants and read declarations with `ip`, `port`, `module`, `kind`, optional `mac` and `allow`) as defined by the `gateway-modules` capability, with the same delta-apply-persist-ack pattern as `gateway_enabled`: a delta replaces the whole list, the device validates and persists it, and the same publish carries the new reported list and `desired:{gateway_write_grants:null}`. An invalid list (any invalid entry, more than 8 entries, or more than 3072 bytes as compact JSON) or one that could not be persisted SHALL be ignored with a warning and leave the stored and reported list unchanged. A local change through the REST API SHALL appear in the reported state through the existing drift detection. Grants hold no secrets.

#### Scenario: Grants applied from the cloud
- **WHEN** a `system` delta sets `gateway_write_grants` to one valid Modbus grant
- **THEN** the device persists it and publishes the reported list and `desired:{gateway_write_grants:null}` in the same message

#### Scenario: One bad entry rejects the list
- **WHEN** a `system` delta sets two grants and the second targets `8.8.8.8`
- **THEN** the device logs a warning and the stored and reported list stay unchanged

#### Scenario: Reconnect reports the grants
- **WHEN** the MQTT client (re)connects
- **THEN** the `system` shadow's full reported state includes `gateway_write_grants`, an empty array when none are set
