## Purpose

Detect an in-progress grid voltage loss (blackout precursor) via the ADE7953's built-in zero-crossing-timeout feature, using the capacitor-backed hold-up window while the device is still powered, and surface it observably in the device's own logs. This phase has no cloud/MQTT surface - it exists to validate detection correctness and timing on real hardware first.

## ADDED Requirements

### Requirement: ZXTO interrupt is armed for a single-missed-cycle trigger
The device SHALL enable the ADE7953 voltage-channel zero-crossing-timeout interrupt (`IRQENA` bit 14) and SHALL program `ZXTOUT` to a value that fires after a single missed zero crossing (accounting for normal grid frequency tolerance and filter delay), so that a grid-loss condition is detected as fast as the hardware allows, matching the physically thin capacitor hold-up budget.

#### Scenario: No zero crossing obtained within the timeout window
- **WHEN** the voltage channel produces no zero crossing for the configured `ZXTOUT` duration
- **THEN** the ADE7953 sets the ZXTO bit in `IRQSTATA`/`RSTIRQSTATA` and asserts the IRQ pin

### Requirement: ZXTO detection is logged with diagnostic context
On a serviced ZXTO interrupt, the device SHALL emit a `LOG_FATAL` entry and SHALL increment a monotonic ZXTO counter exposed via the existing statistics surface. The log entry SHALL include enough context (at minimum: the counter value and the most recent cached voltage reading) to evaluate real-world firing behavior and tune `ZXTOUT` from log data alone, without requiring a scope or external instrumentation.

#### Scenario: ZXTO interrupt observed
- **WHEN** the ADE7953 task services a status snapshot with the ZXTO bit set
- **THEN** a `LOG_FATAL` entry is emitted (subject to the runaway-firing guard below) and the ZXTO counter increments by exactly one

### Requirement: Runaway ZXTO firing is rate-limited without losing observability
Because `ZXTO` has no hardware debounce beyond its own timeout window, the device SHALL bound the rate of `LOG_FATAL` emissions during sustained or misconfigured firing, without suppressing the statistics counter and without delaying or suppressing the first occurrence of a real event.

#### Scenario: Sustained ZXTO firing
- **WHEN** ZXTO fires more times in a row than the configured burst limit, with no intervening clean line-cycle window
- **THEN** the device logs the first burst-limit occurrences in full, logs one suppression notice, and then only increments the counter for further occurrences until a clean line-cycle window is observed

#### Scenario: A later, separate event after suppression
- **WHEN** a clean line-cycle accumulation window (no ZXTO during that window) is observed after a suppressed run
- **THEN** the burst guard resets, and the next ZXTO occurrence logs in full again

### Requirement: No cloud or network side effects in this phase
Servicing the ZXTO interrupt SHALL NOT trigger any MQTT publish, HTTP request, or other network I/O beyond what already happens independently of ZXTO (e.g. the existing log-forwarding pipeline, if enabled, may carry the FATAL entry - but the ZXTO handler itself SHALL NOT open a connection, queue an MQTT payload, or otherwise initiate new outbound traffic).

#### Scenario: ZXTO detected while MQTT is connected
- **WHEN** a ZXTO interrupt is serviced and the device has an active MQTT connection
- **THEN** the ZXTO handler itself performs no MQTT publish or other new network I/O
