# iot-commands Specification (delta)

## MODIFIED Requirements

### Requirement: Five transient IoT Commands
The device SHALL support exactly five AWS IoT Commands: `restart` (no payload), `factory_reset` (`{"confirm": "<device_id>"}`), `energy_reset` (selective or full counter reset), `issue_ack` (acknowledge one or all active device issues), and `firmware_rollback` (`{"expected_sha256": "<64 hex>"}` - boot the previous firmware from the passive OTA partition without a download). Jobs remain OTA-only.

`energy_reset`'s `channels` parameter SHALL be a string in one of two forms: the literal `"all"`, or a comma-separated list of channel indices (e.g. `"5"`, `"0,2,5"`) - the only shape AWS IoT Commands can actually deliver, since command parameters are string-typed end-to-end. A JSON array of indices SHALL continue to be accepted for the on-device inject test harness.

Parsing of the comma-separated form SHALL be best-effort per channel: an out-of-range or non-numeric token SHALL log a `WARNING` and be skipped, while valid tokens are still applied - consistent with the JSON-array form's existing per-index validation. If the `channels` string yields no valid channel index at all (empty string, or every token invalid), the device SHALL reject the command with `BAD_CHANNELS` rather than report success for a no-op.

`issue_ack` SHALL accept either `{"all": true}` (acknowledge every currently-unacked issue instance) or `{"code": "<CODE>", "channel": <optional>}` (acknowledge one instance; `channel` omitted addresses the global scope). Because AWS IoT Commands parameters are string-typed end-to-end, `all` SHALL be treated as true when its value is the JSON boolean `true` or the exact string `"true"`, and `channel` SHALL be accepted as either a JSON integer or a digit string. It SHALL delegate to the same `IssueRegistry::ack`/`IssueRegistry::ackAll` functions used by the local REST issue-ack endpoint, applying no separate ack logic. On success it SHALL publish a plain `SUCCEEDED` status (no `reasonCode`/`reasonDescription`), consistent with `restart`/`factory_reset`/`energy_reset`; the acked count SHALL be logged on-device only, with the `issues` shadow's `active_count` serving as the cloud-visible outcome.

`firmware_rollback`'s precondition semantics, reason codes (`MISSING_SHA256`, `NO_ROLLBACK_TARGET`, `TARGET_MISMATCH`, `ROLLBACK_FAILED`), idempotent-redelivery behavior, and state interplay are specified in the `firmware-rollback` capability; this requirement only fixes its place in the command set and its routing through the same dispatch, staleness guard (`created_at`), and off-callback processing as the other four commands.

#### Scenario: restart command reboots the device
- **WHEN** a valid `restart` command is delivered
- **THEN** the device acks and reboots

#### Scenario: energy_reset clears the requested counters
- **WHEN** a valid `energy_reset` command is delivered
- **THEN** the device resets the requested channel energy counters and reports success

#### Scenario: energy_reset accepts a comma-separated channel list
- **WHEN** an `energy_reset` command is delivered with `channels` set to a comma-separated string such as `"0,2,5"` or a single index such as `"5"`
- **THEN** the device resets the energy counters for each listed channel and reports success

#### Scenario: energy_reset tolerates an invalid token in a channel list
- **WHEN** an `energy_reset` command is delivered with a comma-separated `channels` string containing one out-of-range or non-numeric token alongside otherwise-valid indices
- **THEN** the device logs a `WARNING` for the invalid token, still resets the valid channels, and reports success

#### Scenario: energy_reset rejects a channel list with no valid indices
- **WHEN** an `energy_reset` command is delivered with a `channels` string that is empty or contains no valid channel index (e.g. all tokens out-of-range or non-numeric)
- **THEN** the device rejects the command with `BAD_CHANNELS` and resets no channels

#### Scenario: issue_ack acknowledges a single issue instance
- **WHEN** an `issue_ack` command is delivered with `{"code": "<CODE>"}` (and optionally `channel`, as a JSON integer or a digit string) matching a currently unacked issue instance
- **THEN** the device acknowledges that instance, the `issues` shadow's next report reflects the ack, and the command reports plain `SUCCEEDED`

#### Scenario: issue_ack acknowledges every active issue
- **WHEN** an `issue_ack` command is delivered with `{"all": true}` or `{"all": "true"}`
- **THEN** the device acknowledges every currently-unacked issue instance, logs the acked count, and reports plain `SUCCEEDED`

#### Scenario: issue_ack rejects an unknown code or missing instance
- **WHEN** an `issue_ack` command is delivered with a `code` that is not a known issue code, or with no live instance for the given `code`/`channel`
- **THEN** the device rejects the command with `NO_SUCH_ISSUE` and acknowledges nothing

#### Scenario: issue_ack rejects a payload with neither code nor all
- **WHEN** an `issue_ack` command is delivered without `code` and without `all: true`
- **THEN** the device rejects the command with `MISSING_CODE` and acknowledges nothing

#### Scenario: firmware_rollback routes through the standard command dispatch
- **WHEN** a valid `firmware_rollback` command is delivered on the `.../request/json` topic
- **THEN** it is processed in the MQTT task body (not the RX callback), subject to the same `created_at` staleness guard, and its terminal status uses the `[A-Z0-9_-]+` reasonCode convention

#### Scenario: firmware_rollback with a matching precondition boots the previous firmware
- **WHEN** a `firmware_rollback` command is delivered with `expected_sha256` matching the passive OTA partition's application sha256
- **THEN** the device switches the boot partition, reports `SUCCEEDED`, and restarts into the previous firmware
