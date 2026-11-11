## MODIFIED Requirements

### Requirement: Changing the voltage phase re-arms CT polarity detection
When the voltage phase changes, the device SHALL, once the new value is persisted and applied and under the channel lock, re-arm CT polarity detection on every active channel whose rotation changes, which is every active channel that is not split-phase 240 V, channel 0 included, except the clamped-role channels of a three-phase line that is not `inconsistent` (`phase-check`), whose orientation the phase check owns: detection armed, vote and conducting-read counters zeroed, and a priority read requested for multiplexed channels. If the channel lock cannot be taken within its timeout, the next reading SHALL perform the same re-arm. Inactive and split-phase channels SHALL NOT be armed. The re-arm SHALL NOT depend on the channel role for channels outside lines: the vote sees the rotated active power before any load or PV clamp, so a bidirectional channel that exports while the voltage phase changes can flip its `reverse` (design D4 of the archived `add-voltage-phase`). The device SHALL log the old and new value and the number of channels re-armed.

#### Scenario: Active non-split channels re-armed
- **WHEN** the voltage phase changes from 1 to 2 on a meter with channels 0 and 5 active at phase 1, channel 6 active at split-phase 240 V and channel 7 inactive
- **THEN** detection is armed on channels 0 and 5 only, and the log reports 2 channels re-armed

#### Scenario: Line channels left to the phase check
- **WHEN** the voltage phase changes on a meter with a verified three-phase load line on channels 3-5 and a single channel 6
- **THEN** only channel 6 (and channel 0 if active and outside the line) is re-armed, and the line is re-evaluated from its raw evidence

#### Scenario: Right frame confirms the CTs
- **WHEN** the new voltage phase makes every armed channel read positive active power while conducting
- **THEN** no `reverse` flag changes

#### Scenario: Wrong frame flips and recovers
- **WHEN** a wrong voltage phase makes an armed channel that conducts in the import direction read negative active power for the vote threshold, and the right value is set afterwards while it still imports
- **THEN** the channel's `reverse` flips and is persisted, and after the right value is set the channel is re-armed and flips back

### Requirement: Web UI channel page uses the voltage phase as its base
The channel configuration page SHALL read the voltage phase from the device and use it, not channel 0's phase, as the base for deciding whether the install is three-phase and for the live phase preview. The page SHALL show the voltage phase as the phase of the device's own L wire in a selector that sets it, asking for confirmation with a warning that the change re-runs the reversed-CT detection on every active channel outside three-phase lines. For a channel of a three-phase line the page SHALL show the line's phase check state and candidates instead of a preview. Every other channel, channel 0 included, SHALL get the per-phase preview, which SHALL NOT mark any row as the best. If the voltage phase cannot be read, the page SHALL show it as unavailable and hide the phase previews instead of assuming a base. Channel 0's phase selector SHALL offer phases 1 to 3 only.

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
- **WHEN** channel 0 is outside any line, at phase 1 under load on a device with voltage phase 2
- **THEN** the page shows channel 0's readings for phases 1, 2 and 3 with no row marked as best

#### Scenario: Channel of a line
- **WHEN** the page shows a channel of a `mismatch` line
- **THEN** it shows the line's proposal and no per-phase preview

#### Scenario: Voltage phase unavailable
- **WHEN** the voltage phase request fails
- **THEN** the voltage phase is shown as unavailable, no phase preview is shown and the rest of the page works
