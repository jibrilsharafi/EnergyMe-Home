## MODIFIED Requirements

### Requirement: Web UI channel page uses the voltage phase as its base
The channel configuration page SHALL read the voltage phase from the device and use it, not channel 0's phase, as the base for deciding whether the install is three-phase and for the live phase preview. The page SHALL show the voltage phase as the phase of the device's own L wire in a selector that sets it, asking for confirmation with a warning that the change re-runs the reversed-CT detection on every active channel. Every channel, channel 0 included, SHALL get the per-phase preview, which SHALL NOT mark any row as the best: with `reverse` free the candidate rows are 60 degrees apart and the highest power factor is often the wrong one; the setup page's phase check is where phases are verified. If the voltage phase cannot be read, the page SHALL show it as unavailable and hide the phase previews instead of assuming a base. Channel 0's phase selector SHALL offer phases 1 to 3 only.

#### Scenario: Voltage phase shown
- **WHEN** the channel page loads on a device with voltage phase 3
- **THEN** the voltage phase selector above the channel columns shows 3

#### Scenario: Voltage phase set from the page
- **WHEN** the user picks 2 in the voltage phase selector and confirms the warning
- **THEN** the page PUTs voltage phase 2, shows the stored value, and re-reads the channel configuration

#### Scenario: Voltage phase change cancelled
- **WHEN** the user picks another value and cancels the warning
- **THEN** nothing is sent and the selector returns to the stored value

#### Scenario: Channel 0 preview on an off-line CT
- **WHEN** channel 0 is at phase 1 under load on a device with voltage phase 2
- **THEN** the page shows channel 0's readings for phases 1, 2 and 3 with no row marked as best

#### Scenario: Voltage phase unavailable
- **WHEN** the voltage phase request fails
- **THEN** the voltage phase is shown as unavailable, no phase preview is shown and the rest of the page works

### Requirement: One-time migration from channel 0
At boot, after the channel configuration is loaded and before the meter reading task starts, the device SHALL load the voltage phase, telling an absent key from a stored one by the key's presence, not by a read default. When the key is absent, the device SHALL set the voltage phase to channel 0's phase as loaded or to `PHASE_1` if that is not 1..3 (split-phase 240 V included), persist it and log the migration. When the key is present but does not hold a u8 in 1..3 (0 included, or a key of another type), the device SHALL log a warning with the stored value, derive the voltage phase the same way and replace the key with it as a u8. When a valid key is present the device SHALL use it and SHALL NOT re-derive it from channel 0. If the NVS namespace cannot be opened, the device SHALL log an error, use the derived value in RAM only and write nothing. If the migration write fails, the device SHALL log an error and use the derived value until it is stored by a later setter call or boot. Neither the load nor the migration SHALL re-arm CT polarity detection.

#### Scenario: Upgrade changes no reading
- **WHEN** a meter on 2.7.x with channel 0 at phase 2 and other channels at phases 1, 2 and 3 boots 2.8.0 for the first time
- **THEN** the voltage phase is stored as 2 before the first reading, and every channel's active power, reactive power and power factor match the 2.7.x readings

#### Scenario: Stored key wins after a downgrade and re-upgrade
- **WHEN** a meter with voltage phase 2 is downgraded to 2.7.x, channel 0's phase is changed to 3 there, and the meter is upgraded again
- **THEN** the voltage phase is still 2 and only channel 0's own rotation reflects the new phase

#### Scenario: Out-of-range stored value
- **WHEN** the stored `voltage_phase` holds 7 at boot and channel 0 is at phase 3
- **THEN** the device logs a warning, sets and persists the voltage phase 3, and uses it from the first reading

#### Scenario: Stored zero is not absent
- **WHEN** the stored `voltage_phase` holds 0 at boot and channel 0 is at phase 2
- **THEN** the device logs a warning with the value 0 (not an informational migration), sets and persists the voltage phase 2

#### Scenario: Namespace cannot be opened
- **WHEN** the NVS namespace holding a valid `voltage_phase` of 2 cannot be opened at boot and channel 0 is at phase 3
- **THEN** the device logs an error, uses 3 in RAM for that boot and leaves the stored 2 untouched

#### Scenario: Fresh device and factory reset
- **WHEN** a device boots with no `voltage_phase` key and channel 0 at its default phase
- **THEN** the voltage phase is `PHASE_1` and is persisted

#### Scenario: Reboot arms nothing
- **WHEN** a meter with a stored voltage phase of 3 reboots
- **THEN** no channel has CT polarity detection armed by the voltage phase load

## REMOVED Requirements

### Requirement: Channel 0 cannot be split-phase 240 V
**Reason**: channel 0 is a channel like the others. The rule only protected V1, which the device took from channel 0's own scaled voltage; V1 is now kept unscaled (`meter-reading-integrity`), so a split-phase 240 V circuit can sit on channel 0.
**Migration**: none; split-phase 240 V becomes selectable for channel 0 on every write path.
