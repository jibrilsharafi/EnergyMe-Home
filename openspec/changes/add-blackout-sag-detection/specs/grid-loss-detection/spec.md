## Purpose

Detect an in-progress grid voltage loss (blackout precursor) via the ADE7953's built-in sag feature, using the capacitor-backed hold-up window while the device is still powered, and surface it observably in the device's own logs. This phase has no cloud/MQTT surface - it exists to validate detection correctness and timing on real hardware first.

## ADDED Requirements

### Requirement: SAG interrupt is armed for fastest available trigger
The device SHALL enable the ADE7953 SAG interrupt (`IRQENA` bit 19) and SHALL program `SAGCYC` to 1 (the minimum, one half line cycle) so that a sag condition is detected as fast as the hardware allows, matching the physically thin capacitor hold-up budget.

#### Scenario: Voltage stays below threshold for one half-cycle
- **WHEN** the voltage channel remains below `SAGLVL` for one half line cycle
- **THEN** the ADE7953 sets the SAG bit in `IRQSTATA`/`RSTIRQSTATA` and asserts the IRQ pin

### Requirement: SAGLVL is derived from a live measurement, not a hardcoded threshold
`SAGLVL` SHALL be computed from a live `VPEAK` register reading at startup (a fixed percentage of that reading), not from a hardcoded raw register value. This SHALL hold regardless of the device's mains region (e.g. 120V vs 230V) or per-device voltage-channel gain calibration.

#### Scenario: Different devices, different regions
- **WHEN** two devices are calibrated with different voltage-channel gains (e.g. one on a 120V system, one on a 230V system)
- **THEN** each device's `SAGLVL` is derived from its own live `VPEAK` reading, so the sag threshold is correct relative to that device's actual measured peak rather than a value copied from another device or hardcoded for one region

### Requirement: SAG detection is logged with diagnostic context
On a serviced SAG interrupt, the device SHALL emit a `LOG_FATAL` entry and SHALL increment a monotonic SAG counter exposed via the existing statistics surface. The log entry SHALL include enough context (at minimum: the counter value and the most recent VRMS or VPEAK reading) to evaluate real-world firing behavior and tune `SAGCYC`/`SAGLVL` from log data alone, without requiring a scope or external instrumentation.

#### Scenario: SAG interrupt observed
- **WHEN** the ADE7953 task services a status snapshot with the SAG bit set
- **THEN** a `LOG_FATAL` entry is emitted and the SAG counter increments by exactly one

### Requirement: No cloud or network side effects in this phase
Servicing the SAG interrupt SHALL NOT trigger any MQTT publish, HTTP request, or other network I/O beyond what already happens independently of SAG (e.g. the existing log-forwarding pipeline, if enabled, may carry the FATAL entry - but the SAG handler itself SHALL NOT open a connection, queue an MQTT payload, or otherwise initiate new outbound traffic).

#### Scenario: SAG detected while MQTT is connected
- **WHEN** a SAG interrupt is serviced and the device has an active MQTT connection
- **THEN** the SAG handler itself performs no MQTT publish or other new network I/O
