## MODIFIED Requirements

### Requirement: Four transient IoT Commands
The device SHALL support exactly four AWS IoT Commands: `restart` (no payload), `factory_reset` (`{"confirm": "<device_id>"}`), `energy_reset` (selective or full counter reset), and `issue_ack` (acknowledge one or all active device issues). Jobs remain OTA-only.

`energy_reset`'s `channels` parameter SHALL be a string in one of two forms: the literal `"all"`, or a comma-separated list of channel indices (e.g. `"5"`, `"0,2,5"`) - the only shape AWS IoT Commands can actually deliver, since command parameters are string-typed end-to-end. A JSON array of indices SHALL continue to be accepted for the on-device inject test harness.

Parsing of the comma-separated form SHALL be best-effort per channel: an out-of-range or non-numeric token SHALL log a `WARNING` and be skipped, while valid tokens are still applied - consistent with the JSON-array form's existing per-index validation. If the `channels` string yields no valid channel index at all (empty string, or every token invalid), the device SHALL reject the command with `BAD_CHANNELS` rather than report success for a no-op.

`issue_ack` SHALL accept either `{"all": true}` (acknowledge every currently-unacked issue instance) or `{"code": "<CODE>", "channel": <optional>}` (acknowledge one instance; `channel` omitted addresses the global scope). Because AWS IoT Commands parameters are string-typed end-to-end, `all` SHALL be treated as true when its value is the JSON boolean `true` or the exact string `"true"`, and `channel` SHALL be accepted as either a JSON integer or a digit string. It SHALL delegate to the same `IssueRegistry::ack`/`IssueRegistry::ackAll` functions used by the local REST issue-ack endpoint, applying no separate ack logic. On success it SHALL publish a plain `SUCCEEDED` status (no `reasonCode`/`reasonDescription`), consistent with `restart`/`factory_reset`/`energy_reset`; the acked count SHALL be logged on-device only, with the `issues` shadow's `active_count` serving as the cloud-visible outcome.

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

### Requirement: factory_reset requires device-id confirmation
The device SHALL reject a `factory_reset` whose `confirm` field does not equal the device id. Only a matching `confirm` SHALL trigger the wipe of user NVS (factory NVS preserved).

#### Scenario: Mismatched confirm is rejected
- **WHEN** a `factory_reset` arrives with `confirm` not equal to the device id
- **THEN** the device rejects it and performs no wipe

### Requirement: Only the json request topic routes to the handler
The device SHALL route only `.../request/json` to the command handler. AWS rejection echoes and any other payload-format segment SHALL be ignored, preventing an infinite status-publish loop.

#### Scenario: Rejection echo is ignored
- **WHEN** an AWS rejection echo is received on the command topic
- **THEN** the device does not route it to the handler and does not publish a status in response

### Requirement: Commands processed off the RX callback
Command handling (per-channel NVS work and status publishes) SHALL run in the MQTT task body, not in the PubSubClient callback, to avoid corrupting the QoS1 PUBACK. Emitted `reasonCode`s SHALL be uppercased to the AWS `[A-Z0-9_-]+` pattern.

#### Scenario: Status published from the task body
- **WHEN** a command requires NVS work and a status publish
- **THEN** that work runs in the MQTT task body and the reasonCode matches `[A-Z0-9_-]+`
