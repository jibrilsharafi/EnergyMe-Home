## ADDED Requirements

### Requirement: No sign-vote flips during the phase check
On a device with three-phase channels, the CT-reversal sign-vote detector SHALL NOT flip any load or PV channel while the phase check window is open (`phase-check`): a channel on the wrong phase reads negative, and a flip turns it into a 60-degree error that looks plausible. Pending arms and vote counters SHALL be kept, so the detector resumes when the window closes. A device with no three-phase channels SHALL keep the detector unchanged.

#### Scenario: Wrong phase does not trigger a flip during setup
- **WHEN** a load channel of a line is configured on the wrong phase and reads steadily negative while the installer is on the setup page
- **THEN** its reverse flag is not changed, its readings are clamped as before, and the phase check offers the candidates

#### Scenario: Single-phase install unchanged
- **WHEN** a single-phase device's load channel is activated with a reversed CT during setup
- **THEN** the detector flips its reverse flag as before

#### Scenario: Detector after setup
- **WHEN** setup completes on a three-phase device with an armed channel
- **THEN** the detector works on that channel as before

### Requirement: V1 is never scaled by a channel's split-phase multiplier
The device SHALL keep the voltage it reads from V1 unscaled: it is the voltage every channel is integrated against and the device voltage that MQTT, Modbus and the issue registry report. The 2x multiplier of a split-phase 240 V channel SHALL apply only to that channel's own voltage, powers and energies, on channel 0 as on any other channel.

#### Scenario: Split-phase circuit on channel 0
- **WHEN** channel 0 is set to split-phase 240 V on a 120 V leg, next to 120 V channels
- **THEN** channel 0 reports about 240 V and twice the leg power, while the device voltage and every other channel's readings stay as with channel 0 at phase 1
