## ADDED Requirements

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
