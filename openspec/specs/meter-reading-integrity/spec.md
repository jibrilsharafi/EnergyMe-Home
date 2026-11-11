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

### Requirement: RMS witness cross-check on every channel's reading

Every channel derives active, reactive, and apparent power from the energy registers (a channel on another line of a three-phase supply rotates its active/reactive pair into its own line; apparent energy is not rotated). On every channel's reading, the system SHALL read the independent `IRMS` register and compare apparent power derived from energy (`APENERGY / _sampleTime`) against apparent power derived from RMS (`voltage x IRMS`). The comparison SHALL be apparent-against-apparent only, never active-against-apparent, so legitimately low power-factor loads never cause a false rejection. When the two estimates diverge beyond the calibrated tolerance, the system SHALL discard the reading through the existing invalid-reading path. `IRMS` is never published; current remains derived from apparent energy (an off-phase channel also uses `IRMS` for its no-load gate).

#### Scenario: Partial-window or mux artifact while current still flows

- **WHEN** a channel's energy-derived apparent power is far below `voltage x IRMS` (divergence beyond tolerance)
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

### Requirement: Every channel is rotated from the device voltage phase into its own line
Every channel is integrated against the single voltage input V1, which sits on the device voltage phase (`voltage-phase` capability). For each channel k with a phase in 1..3, channel 0 included, the system SHALL turn the latched active/reactive energy pair into the channel's own line by `R_k = angle(phase_k) - angle(voltagePhase) + 180 * reverse_k` degrees, with `angle(PHASE_1) = 0`, `angle(PHASE_2) = -120` and `angle(PHASE_3) = +120` (IEC positive sequence), so that `S_k = e^{j R_k} * (P1 + j Q1)`. The base SHALL be the device voltage phase, never channel 0's phase. A split-phase 240 V channel SHALL take `R_k = 180 * reverse_k` and its 2x voltage multiplier only. Apparent energy and current SHALL NOT be rotated. A channel whose phase is 1..3 and differs from the voltage phase is off-phase, channel 0 included, and SHALL get the IRMS no-load gate of off-phase channels. One reading SHALL use a single voltage phase value for its whole computation.

#### Scenario: Channel 0 CT off the voltage line
- **WHEN** the voltage phase is 1, channel 0's CT sits on the line labelled 2 with reversed leads, channel 0 is configured at phase 2 with `reverse` true, and the circuit carries a resistive load
- **THEN** the raw pair, which reads about -60 degrees (|PF| about 0.5), is turned by `-120 + 180 = +60` degrees and channel 0 publishes positive active power with a power factor near 1

#### Scenario: Voltage phase change turns every non-split channel
- **WHEN** the voltage phase changes from 1 to 2 on a meter with active channels at phases 1, 2 and 3 and one split-phase 240 V channel
- **THEN** every channel with a phase in 1..3, channel 0 included, turns by +120 degrees and the split-phase channel's reading does not change

#### Scenario: Channel 0 phase write turns channel 0 only
- **WHEN** channel 0's phase changes from 1 to 2 while the voltage phase stays 1
- **THEN** channel 0 turns by -120 degrees and no other channel's reading changes

#### Scenario: Channel on the voltage phase
- **WHEN** a channel's phase equals the voltage phase
- **THEN** only `reverse` applies to it (`R_k = 180 * reverse_k`) and the off-phase no-load gate does not apply

#### Scenario: Off-phase channel 0 below the no-load current
- **WHEN** channel 0 is off-phase and its IRMS is below the off-phase no-load fraction of its CT rating
- **THEN** channel 0's powers, current and energies for that window are zero and nothing is booked in its accumulators

