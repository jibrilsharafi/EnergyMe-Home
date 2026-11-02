## Purpose

A device-level voltage phase: the line the meter's single voltage input V1 is wired to. Every channel, channel 0 included, is rotated from it into its own line (`meter-reading-integrity`), so channel 0's CT can sit on any line. This capability covers the setting, its storage, the one-time migration from channel 0, backup and restore, the local REST interface, CT polarity re-arm on change, the waveform phase fields, the web UI base and the channel 0 split-phase rule.

## ADDED Requirements

### Requirement: Device voltage phase setting
The device SHALL keep a voltage phase with the values `PHASE_1`, `PHASE_2` and `PHASE_3` only, persisted as NVS key `voltage_phase` (u8) in `ade7953_ns` with default `PHASE_1`, separate from the ADE7953 calibration configuration. Any other value (0, `PHASE_SPLIT_240`, anything above 3, a non-integer) SHALL be refused and leave the stored and applied value unchanged. Setting the current value SHALL be a no-op with no NVS write and no CT polarity re-arm. A new value SHALL be persisted before it is applied; if persisting fails, the stored and applied value SHALL stay unchanged and the setter SHALL report failure. Concurrent writers SHALL be serialized so the stored and applied value always end equal. Resetting the ADE7953 calibration or a channel SHALL NOT change the voltage phase; a factory reset SHALL remove it.

#### Scenario: Valid value persisted and applied
- **WHEN** the voltage phase is set to 2 on a device at 1
- **THEN** the device stores 2, applies it from the next reading on, and still reports 2 after a reboot

#### Scenario: Split-phase refused
- **WHEN** the voltage phase is set to 4 (`PHASE_SPLIT_240`)
- **THEN** the device refuses it with a warning and the stored and applied value stay unchanged

#### Scenario: Same value is a no-op
- **WHEN** the voltage phase is set to the value it already has
- **THEN** the device writes nothing to NVS and re-arms no channel

#### Scenario: Calibration and channel resets keep it
- **WHEN** the ADE7953 calibration is reset to defaults, or channel 0 is reset to its default configuration
- **THEN** the voltage phase keeps its value

### Requirement: One-time migration from channel 0
At boot, after the channel configuration is loaded and before the meter reading task starts, the device SHALL load the voltage phase. When the key is absent or holds a value outside 1..3, the device SHALL set the voltage phase to channel 0's phase as loaded (after the split-phase coercion of channel 0), or to `PHASE_1` if that is not 1..3, and persist it, logging the migration (at WARNING when an out-of-range value was replaced). When a valid key is present the device SHALL use it and SHALL NOT re-derive it from channel 0. If the migration write fails, the device SHALL log an error and use the derived value until the next boot. Neither the load nor the migration SHALL re-arm CT polarity detection.

#### Scenario: Upgrade changes no reading
- **WHEN** a meter on 2.7.x with channel 0 at phase 2 and other channels at phases 1, 2 and 3 boots 2.8.0 for the first time
- **THEN** the voltage phase is stored as 2 before the first reading, and every channel's active power, reactive power and power factor match the 2.7.x readings

#### Scenario: Stored key wins after a downgrade and re-upgrade
- **WHEN** a meter with voltage phase 2 is downgraded to 2.7.x, channel 0's phase is changed to 3 there, and the meter is upgraded again
- **THEN** the voltage phase is still 2 and only channel 0's own rotation reflects the new phase

#### Scenario: Out-of-range stored value
- **WHEN** the stored `voltage_phase` holds 7 at boot and channel 0 is at phase 3
- **THEN** the device logs a warning, sets and persists the voltage phase 3, and uses it from the first reading

#### Scenario: Fresh device and factory reset
- **WHEN** a device boots with no `voltage_phase` key and channel 0 at its default phase
- **THEN** the voltage phase is `PHASE_1` and is persisted

#### Scenario: Reboot arms nothing
- **WHEN** a meter with a stored voltage phase of 3 reboots
- **THEN** no channel has CT polarity detection armed by the voltage phase load

### Requirement: Configuration backup and restore carry the voltage phase
The configuration backup SHALL include `ade7953_ns.voltage_phase` with its type and value. Restoring a backup that contains the key SHALL restore that value; a restored value outside 1..3 SHALL be handled at the next boot like any out-of-range stored value. Restoring a backup that does not contain the key SHALL leave the stored voltage phase unchanged and SHALL NOT re-derive it from the restored channel 0 phase, also on a factory-reset device, whose first boot after the reset has already stored the migrated value.

#### Scenario: Backup contains the key
- **WHEN** a configuration backup is downloaded from a 2.8.0 device with voltage phase 2
- **THEN** its `nvs.ade7953_ns.voltage_phase` entry is `{"type": "u8", "value": 2}`

#### Scenario: Restore with the key
- **WHEN** that backup is restored on the same device after the voltage phase was changed to 1
- **THEN** after the restore reboot the voltage phase is 2

#### Scenario: Restore without the key keeps the stored value
- **WHEN** a 2.7.x backup with channel 0 at phase 1 is restored on a 2.8.0 device whose voltage phase is 2
- **THEN** after the restore reboot the voltage phase is still 2 and channel 0 is at phase 1

#### Scenario: Old backup on a factory-reset device
- **WHEN** a 2.8.0 device is factory reset, boots, and then a 2.7.x backup with channel 0 at phase 3 is restored
- **THEN** after the restore reboot channel 0 is at phase 3 and the voltage phase is still 1, the value the first boot after the reset stored

#### Scenario: Hand-edited backup with an invalid value
- **WHEN** a backup whose `nvs.ade7953_ns.voltage_phase` is `{"type": "u8", "value": 7}` and whose channel 0 is at phase 2 is restored
- **THEN** the restore boot logs a warning and the voltage phase is 2, derived from the restored channel 0 and persisted

### Requirement: Local REST interface for the voltage phase
The authenticated local REST API SHALL expose `GET /api/v1/ade7953/voltage-phase`, returning `{"voltagePhase": n}`, and `PUT /api/v1/ade7953/voltage-phase` with body `{"voltagePhase": n}`. A PUT whose `voltagePhase` is not an integer 1..3, or is missing, SHALL get HTTP 400 and change nothing. A PUT the setter cannot persist SHALL get HTTP 500 and change nothing. A local change SHALL reach the `meter` shadow's reported state through the existing drift detection.

#### Scenario: Read the voltage phase
- **WHEN** an authenticated client sends `GET /api/v1/ade7953/voltage-phase` on a device at voltage phase 3
- **THEN** the response is HTTP 200 with `{"voltagePhase": 3}`

#### Scenario: Set the voltage phase
- **WHEN** an authenticated client sends `PUT` with `{"voltagePhase": 2}`
- **THEN** the response is HTTP 200, the voltage phase is 2, and within about 3 s the `meter` shadow reports `voltage_phase: 2`

#### Scenario: Invalid body refused
- **WHEN** a `PUT` carries `{"voltagePhase": 4}`, `{"voltagePhase": 0}`, `{"voltagePhase": "2"}` or no `voltagePhase`
- **THEN** the response is HTTP 400 and the voltage phase and every reading stay unchanged

### Requirement: Changing the voltage phase re-arms CT polarity detection
When the voltage phase changes, the device SHALL, in the same critical section that applies the new value, re-arm CT polarity detection on every active channel whose rotation changes, which is every active channel that is not split-phase 240 V, channel 0 included: detection armed, vote and conducting-read counters zeroed, and a priority read requested for multiplexed channels. Inactive and split-phase channels SHALL NOT be armed. The device SHALL log the old and new value and the number of channels re-armed.

#### Scenario: Active non-split channels re-armed
- **WHEN** the voltage phase changes from 1 to 2 on a meter with channels 0 and 5 active at phase 1, channel 6 active at split-phase 240 V and channel 7 inactive
- **THEN** detection is armed on channels 0 and 5 only, and the log reports 2 channels re-armed

#### Scenario: Right frame confirms the CTs
- **WHEN** the new voltage phase makes every armed channel read positive active power while conducting
- **THEN** no `reverse` flag changes

#### Scenario: Wrong frame flips and recovers
- **WHEN** a wrong voltage phase makes an armed load channel read negative active power for the vote threshold, and the right value is set afterwards
- **THEN** the channel's `reverse` flips and is persisted, and after the right value is set the channel is re-armed and flips back

### Requirement: Channel 0 cannot be split-phase 240 V
Channel 0's reading supplies V1, the voltage every channel is integrated against, so the device SHALL refuse `PHASE_SPLIT_240` for channel 0 on every write path (REST, web UI, `channels` shadow) and SHALL coerce a stored channel 0 value of `PHASE_SPLIT_240` to `PHASE_1` at load with a warning. Channels other than 0 SHALL keep accepting `PHASE_SPLIT_240`.

#### Scenario: Write refused
- **WHEN** a `channels` delta or a REST channel update sets channel 0's phase to 4
- **THEN** the write is refused with a warning and channel 0 keeps its phase

#### Scenario: Stored value coerced
- **WHEN** channel 0's stored phase is 4 at boot
- **THEN** channel 0 loads as phase 1 with a warning, and a missing voltage phase migrates from that coerced value

### Requirement: Waveform capture reports the voltage phase
The waveform capture JSON SHALL report `voltagePhase` from the device voltage phase setting, `currentPhase` from the captured channel's phase, and `phaseShiftDegrees` as `angle(currentPhase) - angle(voltagePhase)` when the channel is off-phase (its phase is 1..3 and differs from the voltage phase) and 0 otherwise. `reverse` SHALL NOT enter the shift.

#### Scenario: Channel 0 off the voltage line
- **WHEN** channel 0 at phase 1 is captured with voltage phase 2
- **THEN** the JSON reports `voltagePhase: 2`, `currentPhase: 1` and `phaseShiftDegrees: 120`

#### Scenario: Split-phase channel never shifted
- **WHEN** a split-phase 240 V channel is captured with voltage phase 2
- **THEN** `phaseShiftDegrees` is 0

#### Scenario: Channel on the voltage phase
- **WHEN** a channel at phase 3 is captured with voltage phase 3
- **THEN** `phaseShiftDegrees` is 0

### Requirement: Web UI channel page uses the voltage phase as its base
The channel configuration page SHALL read the voltage phase from the device and use it, not channel 0's phase, as the base for deciding whether the install is three-phase and for the live phase preview. Channel 0 SHALL get the same phase preview as every other channel. If the voltage phase cannot be read, the page SHALL hide the phase previews instead of assuming a base. Channel 0's phase selector SHALL offer phases 1 to 3 only.

#### Scenario: Channel 0 preview on an off-line CT
- **WHEN** channel 0 is at phase 1 under load on a device with voltage phase 2
- **THEN** the page shows channel 0's candidates for phases 1, 2 and 3 instead of a "voltage reference" note

#### Scenario: Voltage phase unavailable
- **WHEN** the voltage phase request fails
- **THEN** no phase preview is shown and the rest of the page works
