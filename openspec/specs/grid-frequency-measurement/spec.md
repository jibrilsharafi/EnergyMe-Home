# grid-frequency-measurement Specification

## Purpose
Per-cycle, ZXV-driven measurement of grid frequency from the ADE7953 PERIOD register: an interrupt demux that services every pending status bit per wake, a Q24.8 fixed-point EMA filter with datasheet-correct conversion, and the filtered `getGridFrequency()` surface consumed by REST, Modbus, Home Assistant, the shadow, and the issue registry. Runs alongside CYCEND/energy handling without affecting the metering path.

## Requirements

### Requirement: Interrupt demux services every set status bit
The ADE7953 task SHALL read `RSTIRQSTATA` exactly once per wake and SHALL service every enabled interrupt bit set in that single snapshot (ZXV, CYCEND, RESET, CRC) with independent checks, never returning after the first match. The task SHALL NOT re-read the status register within the same wake to test further bits.

#### Scenario: ZXV and CYCEND co-pending in one wake
- **WHEN** the task wakes and the status snapshot has both ZXV and CYCEND set
- **THEN** the PERIOD read/EMA update and the full CYCEND energy handling both execute in that wake, with no energy reading lost

#### Scenario: No drain loop on continuous ZXV
- **WHEN** a new ZXV asserts after the status read-and-clear
- **THEN** it raises a new IRQ edge and is handled on the next wake, not by re-reading status in the current wake

### Requirement: ISR is pure and energy guard re-arms only on CYCEND
The ISR SHALL only give the wake semaphore and increment counters. The `_interruptHandledChannelA/B` double-read guard flags SHALL be cleared in the task, and only when the CYCEND bit is set in the status snapshot.

#### Scenario: ZXV does not re-arm the energy guard
- **WHEN** ZXV interrupts fire after a linecyc's energy registers have been read (guard flags set true)
- **THEN** the guard flags remain true until the next genuine CYCEND, and any duplicate CYCEND processing within the same linecyc window is refused

### Requirement: Per-cycle EMA of the PERIOD register
On each ZXV the device SHALL read PERIOD, validate the equivalent frequency against the 45-65 Hz range, and update a Q24.8 fixed-point EMA with alpha = 1/8 (arithmetic shift, no float/divide on the hot path). Out-of-range readings SHALL be discarded without touching the EMA. The EMA SHALL be seeded by the first in-range reading, never zero. Each accepted update SHALL increment a monotonic update counter.

#### Scenario: Out-of-range PERIOD is rejected
- **WHEN** a PERIOD read converts to a frequency outside 45-65 Hz
- **THEN** the EMA state and update counter are unchanged

#### Scenario: First valid read seeds the EMA
- **WHEN** the first in-range PERIOD arrives after boot
- **THEN** the EMA state equals that reading (no ramp from zero) and the update counter increments

### Requirement: Datasheet-correct frequency conversion everywhere
All PERIOD-to-frequency conversions SHALL use `f = 223750 / (PERIOD + 1)` (ADE7953 datasheet Eq. 36), including the raw `_readGridFrequency()` path and the EMA readout.

#### Scenario: Nominal 50 Hz register value
- **WHEN** PERIOD reads 4474
- **THEN** the converted frequency is 223750/4475 = 50.000 Hz (not 223750/4474 ≈ 50.011 Hz)

### Requirement: getGridFrequency returns the filtered value
`getGridFrequency()` SHALL return the EMA-filtered frequency for all consumers (REST, Modbus, Home Assistant, shadow, issue registry). The bootstrap 50/60 Hz detection (`_updateSampleTime`) SHALL keep using the direct raw register read so it works before the EMA seeds.

#### Scenario: Consumers get the filtered value
- **WHEN** any consumer calls `getGridFrequency()` after the EMA is seeded
- **THEN** it receives the EMA value converted at readout, not a raw single-cycle read

#### Scenario: Bootstrap before EMA seed
- **WHEN** `_updateSampleTime()` runs before any ZXV has seeded the EMA
- **THEN** it still obtains a usable frequency from the direct raw read

### Requirement: Metering path is unaffected by ZXV
Enabling ZXV (IRQENA bit 15) and positive-edge-only ZX events (CONFIG ZX_EDGE = 10b) SHALL NOT change linecyc accumulation, energy register handling, or CYCEND timing. CYCEND handling SHALL never be skipped or degraded by ZXV processing.

#### Scenario: Meter heartbeat under ZXV load
- **WHEN** ZXV runs continuously at line rate (~50 Hz)
- **THEN** the meter task heartbeat and energy readings show no measurable degradation versus pre-change behavior
