# iot-device-shadows Specification

## Purpose
Cloud<->device configuration sync over AWS IoT Named Device Shadows: publish-reported-first (no GET), reported-only shadows (info/issues/wifi) and writable shadows (system/meter/channels), local-edit drift-detect, optimistic concurrency, and the cloud-owns-clearing-desired contract. Secrets and high-rate telemetry stay out of shadows.
## Requirements
### Requirement: Publish-reported-first shadow sync without GET
The device SHALL synchronise configuration state with AWS IoT Named Device Shadows by publishing reported state and never issuing a `/get`. On every MQTT (re)connect the device SHALL, per shadow, subscribe to `update/delta` and `update/rejected` (NOT `update/accepted`) and publish `{state:{reported:<full current NVS config>}}` to `update`.

#### Scenario: Reconnect republishes reported state
- **WHEN** the MQTT client (re)connects to AWS IoT
- **THEN** each registered shadow publishes its full current reported state and subscribes to its `update/delta` and `update/rejected` topics, without publishing any `/get`

#### Scenario: Pending cloud intent surfaces as a delta
- **WHEN** a `desired` exists in the cloud document that differs from the just-published `reported`
- **THEN** AWS auto-pushes an `update/delta` (no GET needed) and the device applies it

### Requirement: Six named shadows with fixed read/write roles
The device SHALL expose exactly these named shadows: `info`, `issues`, `wifi` as reported-only, and `system`, `meter`, `channels` as writable. A delta delivered for a reported-only shadow SHALL be ignored.

#### Scenario: Writable shadow accepts a delta
- **WHEN** a delta arrives for `system`, `meter`, or `channels`
- **THEN** the device applies each field, persists it to NVS, and acks

#### Scenario: Reported-only shadow ignores a delta
- **WHEN** a delta arrives for `info`, `issues`, or `wifi`
- **THEN** the device logs a warning and does not apply or persist anything

### Requirement: Delta apply is atomic-acked with desired-null
On a writable-shadow delta the device SHALL apply each field, persist it, then publish a single combined `{state:{reported:{<applied>}, desired:{<field>:null}}}`. The `desired:<field>=null` removes the pending intent and that publish is the ack. Delta apply (blocking SPI/NVS work) SHALL run in the MQTT task body, never in the PubSubClient RX callback.

#### Scenario: Field applied and intent cleared in one publish
- **WHEN** a valid `system` delta sets a field
- **THEN** the device persists the value and publishes reported=<new> and desired:<field>=null in the same message

#### Scenario: Unknown desired field is nulled, not applied
- **WHEN** a delta carries a field the device does not recognise
- **THEN** the device publishes `desired:{<unknownField>:null}` and logs a WARN, applying nothing

### Requirement: Local edits drift-detect to reported-only
Local configuration changes (REST, UI, or internal) SHALL be detected source-agnostically within 3 s and republished as reported-only. A local edit SHALL NOT write `desired`, so the cloud is never overridden by silence.

#### Scenario: Local UI edit republishes reported
- **WHEN** a channel label is changed via the local web UI
- **THEN** within ~3 s the `channels` shadow republishes the new reported state with no `desired` written

### Requirement: mqtt_log_level transient auto-revert
Persistent log levels (INFO/WARNING/ERROR/FATAL) set via `system.mqtt_log_level` SHALL be persisted to NVS as the new baseline and cancel any active revert timer. Transient levels (VERBOSE/DEBUG) SHALL be applied at runtime without persisting, start a 5-minute one-shot timer, and on timer fire revert to the persisted baseline and publish the reverted reported state. A reboot while transient SHALL come back at the persisted baseline.

#### Scenario: DEBUG reverts after 5 minutes
- **WHEN** the cloud sets `mqtt_log_level` to DEBUG
- **THEN** the level applies at runtime, is not persisted, and reverts to the persisted baseline after 5 minutes with a reported republish

#### Scenario: Reboot during DEBUG restores baseline
- **WHEN** the device reboots while running a transient DEBUG level
- **THEN** it boots at the persisted baseline level

### Requirement: Optimistic concurrency via version
The device SHALL source the shadow `version` from the delta payload (not from `/update/accepted`) and use it for optimistic concurrency. On a 409 version conflict the device SHALL re-publish reported without a version and apply the fresh delta that follows, bounded and without an infinite loop.

#### Scenario: Version conflict recovers
- **WHEN** an `update` is rejected with a 409 version conflict
- **THEN** the device re-publishes reported state and applies the resulting fresh delta

### Requirement: Cloud owns clearing desired
Because the device uses asymmetric desired-null (the delta-ack nulls `desired`; local edits publish reported-only), a `desired` written equal to `reported` yields no delta and the no-GET device cannot clear it. The cloud SHALL clear `desired` reactively after convergence. Avoiding `desired == reported` writes is an optional optimisation only; no device-side change is required.

#### Scenario: No-op desired must be cloud-cleared
- **WHEN** the cloud writes a `desired` equal to the current `reported`
- **THEN** no delta is generated, the device does not clear `desired`, and the cloud must clear it after observing convergence

### Requirement: Secrets and high-rate telemetry stay out of shadows
Shadows SHALL NOT carry secrets (WiFi credentials, CustomMQTT credentials, InfluxDB token, web UI password) or high-rate telemetry (energy counters, instantaneous power). The `wifi` shadow SHALL report only non-secret network state (connected/ssid/ip/gateway/subnet/dns/mac, static_ip, fallback_to_dhcp).

#### Scenario: wifi shadow exposes no credentials
- **WHEN** the `wifi` shadow publishes reported state
- **THEN** it contains non-secret network fields only and no WiFi password

### Requirement: System shadow exposes meter publish cadence fields
The `system` shadow SHALL expose `meter_publish_threshold_bytes` and `meter_publish_max_interval_ms` as writable fields, following the same delta-apply-persist-ack pattern as `send_power_data`/`send_grid_data`: a delta sets the field, the device persists it to NVS, and the same publish carries both the new `reported` value and `desired:{<field>:null}`.

#### Scenario: Field applied and intent cleared in one publish
- **WHEN** a valid `system` delta sets `meter_publish_threshold_bytes` or `meter_publish_max_interval_ms`
- **THEN** the device persists the (possibly clamped) value and publishes `reported=<applied>` and `desired:{<field>:null}` in the same message

#### Scenario: Reconnect reports current cadence values
- **WHEN** the MQTT client (re)connects
- **THEN** the `system` shadow's full reported state includes the current `meter_publish_threshold_bytes` and `meter_publish_max_interval_ms`, alongside the existing fields


### Requirement: Info shadow reports the rollback target fingerprint
The `info` shadow SHALL report `other_partition_sha256`: the 64-hex-character application sha256 of the firmware image currently in the passive OTA partition, or `null` when the passive slot holds no readable application descriptor. This makes every device's rollback target observable fleet-wide before any incident, so a `firmware_rollback` command's `expected_sha256` can be chosen from the shadow without querying the device during an outage.

#### Scenario: Info shadow carries the passive slot fingerprint
- **WHEN** the `info` shadow is published on a device that has completed at least one OTA
- **THEN** the reported state includes `other_partition_sha256` with the passive partition's 64-hex sha256 (the previously running firmware)

#### Scenario: Unreadable passive slot reported as null
- **WHEN** the passive partition has no valid application descriptor (e.g. fresh factory device)
- **THEN** the `info` shadow reports `other_partition_sha256` as `null`
