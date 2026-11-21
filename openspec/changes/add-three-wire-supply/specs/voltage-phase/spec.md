## ADDED Requirements

### Requirement: Three-phase supply without neutral
The device SHALL keep a no-neutral setting, persisted as NVS key `no_neutral` (bool) in `ade7953_ns`, absent meaning off. When on, the meter's L is on the voltage phase and its N on the line that lags it (L1-L2, L2-L3, L3-L1), so V1 is that pair's line-to-line voltage. The setting SHALL be written together with the voltage phase, by the same setter and under the same rules (refused before the boot load, persisted before applied, no-op when unchanged). Turning it on SHALL be refused while any active channel is split-phase 240 V. A change SHALL re-arm CT polarity detection on every active channel with `lineCurrent` set. A factory reset SHALL remove it.

#### Scenario: Setting stored with the voltage phase
- **WHEN** the voltage phase is set to 3 with no neutral on a device at 1 without it
- **THEN** the device stores both, applies them from the next reading, and reports both after a reboot

#### Scenario: Split-phase channel blocks it
- **WHEN** no neutral is set on while channel 4 is active at split-phase 240 V
- **THEN** the device refuses it and nothing changes

### Requirement: Line current channels
Each channel SHALL carry a `lineCurrent` flag (NVS key `line_<n>`, absent meaning off), optional in the channel JSON both ways: a write without it keeps the stored value. With no neutral on, a channel with `lineCurrent` SHALL be measured against the line-to-neutral voltage of its line: its energy pair, apparent energy and voltage SHALL be scaled by 1/√3 and the pair rotated by -30 degrees right after the energy registers are read, so the phase check evidence, the rotation into its own line, the reverse flag, the no-load gate and every published value follow. The device voltage SHALL stay V1. Every other channel, and every channel while no neutral is off, SHALL read exactly as before. With no neutral on, a channel SHALL NOT be set to split-phase 240 V.

#### Scenario: Grid line on a 3x230 V office
- **WHEN** no neutral is on with voltage phase 3, grid CTs with `lineCurrent` on phases 3, 1 and 2, and the building draws 754 W at -480 var
- **THEN** the three channels sum to about 754 W and -480 var, each with its own capacitive power factor, and the device voltage stays about 236 V

#### Scenario: Load between two lines unchanged
- **WHEN** no neutral is on and a single-phase load without `lineCurrent` sits between L1 and L2 (phase 1)
- **THEN** its readings are those the same load gives with no neutral off

#### Scenario: Off reads as before
- **WHEN** no neutral is off and a channel has `lineCurrent` set
- **THEN** the channel reads exactly as a channel without it

### Requirement: Web UI for the supply without neutral
The setup page SHALL offer the setting as a single switch inside the three-phase supply choice, with no new step. While it is on, the voltage phase choice and single-phase circuits SHALL show line pairs (L1-L2, L2-L3, L3-L1) and three-phase circuits SHALL be saved with `lineCurrent` on their channels; single-phase circuits with it off. The channel page SHALL show the same switch next to the voltage phase, and while it is on each channel's phase select SHALL offer the three lines (`lineCurrent` on) and the three pairs (`lineCurrent` off) instead of phases and split-phase 240 V.

#### Scenario: Setup with no neutral
- **WHEN** the installer picks three-phase, turns on "No neutral (3×230 V)", picks the pair L3-L1 and adds a three-phase grid circuit and a single-phase load on L1-L2
- **THEN** the device stores voltage phase 3 with no neutral, the grid channels with `lineCurrent` on phases 1 to 3, and the load on phase 1 without it
