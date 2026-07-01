## MODIFIED Requirements

### Requirement: Three transient IoT Commands
The device SHALL support exactly three AWS IoT Commands: `restart` (no payload), `factory_reset` (`{"confirm": "<device_id>"}`), and `energy_reset` (selective or full counter reset). Jobs remain OTA-only.

`energy_reset`'s `channels` parameter SHALL be a string in one of two forms: the literal `"all"`, or a comma-separated list of channel indices (e.g. `"5"`, `"0,2,5"`) - the only shape AWS IoT Commands can actually deliver, since command parameters are string-typed end-to-end. A JSON array of indices SHALL continue to be accepted for the on-device inject test harness.

Parsing of the comma-separated form SHALL be best-effort per channel: an out-of-range or non-numeric token SHALL log a `WARNING` and be skipped, while valid tokens are still applied - consistent with the JSON-array form's existing per-index validation. If the `channels` string yields no valid channel index at all (empty string, or every token invalid), the device SHALL reject the command with `BAD_CHANNELS` rather than report success for a no-op.

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
