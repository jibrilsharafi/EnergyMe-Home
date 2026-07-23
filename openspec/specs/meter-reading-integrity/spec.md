# meter-reading-integrity Specification

## Purpose
TBD - created by archiving change harden-meter-energy-window-glitches. Update Purpose after archive.
## Requirements
### Requirement: Non-destructive energy-register reads

The system SHALL read the ADE7953 active, reactive, and apparent energy registers **without read-with-reset** (`LCYCMODE` bit 6 `RSTREAD` cleared). In line cycle accumulation mode the hardware latches a full accumulation window into each energy register at every `CYCEND` and holds it until the next `CYCEND`, so a non-destructive read always returns the last complete window regardless of when or how often the register is read.

#### Scenario: Register read twice within one line-cycle window

- **WHEN** an energy register is read a second time before the next `CYCEND`
- **THEN** it returns the same full-window value as the first read, not zero

#### Scenario: Read timing jitter does not corrupt the value

- **WHEN** a channel read happens at a variable offset after `CYCEND` (early or late within the window)
- **THEN** the value returned is the last complete latched window, unaffected by the read offset

#### Scenario: Genuine no-load still reads zero

- **WHEN** the channel is below the hardware no-load threshold
- **THEN** the energy register reads zero (the no-load feature is independent of the read-with-reset setting)

### Requirement: Contaminated post-switch window discarded by skipping, not by reset

After a multiplexer channel switch, the system SHALL discard the contaminated accumulation window by skipping one line-cycle window rather than by issuing a reset read. The next `CYCEND` overwrites the register with a clean full window on the settled channel, which is the window that is read and published.

#### Scenario: Reading after a mux switch

- **WHEN** the multiplexer switches to a new channel
- **THEN** the window spanning the switch is skipped and the first fully-settled window on the new channel is the one read and published

### Requirement: Energy integration is not double-counted on repeated reads

Because published readings can repeat within a window once reads are non-destructive, the system SHALL integrate accumulated energy (Wh) using the actual elapsed time since the last accepted read, so a repeated read within a window does not double-count energy.

#### Scenario: Two reads within one window

- **WHEN** a channel is read twice within one window (elapsed times summing to about one window)
- **THEN** the integrated energy for that window is counted once (proportional to actual elapsed time), not doubled

### Requirement: RMS witness cross-check on the base-phase reading path

On the base-phase reading path, where active, reactive, and apparent power are derived from the energy registers, the system SHALL read the independent `IRMS` register and compare apparent power derived from energy (`APENERGY / _sampleTime`) against apparent power derived from RMS (`voltage x IRMS`). The comparison SHALL be apparent-against-apparent only, never active-against-apparent, so legitimately low power-factor loads never cause a false rejection. When the two estimates diverge beyond the calibrated tolerance, the system SHALL discard the reading through the existing invalid-reading path. `IRMS` is read solely as the witness; base-phase current remains derived from apparent energy.

#### Scenario: Partial-window or mux artifact while current still flows

- **WHEN** a base-phase channel's energy-derived apparent power is far below `voltage x IRMS` (divergence beyond tolerance)
- **THEN** the reading is discarded and not published, and a failure is recorded

#### Scenario: Steady load agrees within tolerance

- **WHEN** the energy-derived and RMS-derived apparent power agree within the calibrated tolerance
- **THEN** the reading is accepted and published unchanged

#### Scenario: Low power-factor load does not false-trip

- **WHEN** a load has real active power well below its apparent power (low power factor) but the energy-derived and RMS-derived *apparent* power agree
- **THEN** the reading is accepted

#### Scenario: Legitimate close-in-time read is preserved

- **WHEN** a valid reading arrives a short wall-clock interval after the previous one but is physically consistent (energy-derived apparent power agrees with `voltage x IRMS`)
- **THEN** the reading is accepted (the check is physical, not timing-based)

### Requirement: Discard isolated glitches rather than substitute

When the witness check fails, the system SHALL discard the single affected reading and SHALL NOT substitute a computed value, hold the previous value, or zero the reading. This applies to isolated single-sample glitches only and does not reintroduce discarding of sustained readings.

#### Scenario: Single-sample glitch discarded

- **WHEN** the witness check rejects one reading for a channel
- **THEN** no value is published for that channel this cycle and the next cycle proceeds normally

### Requirement: Witness tolerance calibrated from device data

The divergence tolerance for the RMS witness SHALL be calibrated from measured device data (energy-derived apparent power, RMS-derived apparent power, and their ratio, captured during real production) so the band rejects artifacts without dropping genuine fast transients.

#### Scenario: Tolerance derived from captured data

- **WHEN** the tolerance is set
- **THEN** its value is justified by captured device data showing steady-state agreement clustered near unity and artifact divergence clearly outside the band

