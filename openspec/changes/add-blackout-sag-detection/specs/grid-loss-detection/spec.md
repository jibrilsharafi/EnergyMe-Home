## Purpose

Detect an in-progress grid voltage loss (blackout precursor) via the ADE7953's built-in zero-crossing-timeout feature, using the capacitor-backed hold-up window while the device is still powered, and publish a last-gasp MQTT alarm before power is actually lost.

## ADDED Requirements

### Requirement: ZXTO interrupt is armed for a single-missed-cycle trigger
The device SHALL enable the ADE7953 voltage-channel zero-crossing-timeout interrupt (`IRQENA` bit 14) and SHALL program `ZXTOUT` to a value that fires after a single missed zero crossing (accounting for normal grid frequency tolerance and filter delay), so that a grid-loss condition is detected as fast as the hardware allows, matching the physically thin capacitor hold-up budget.

#### Scenario: No zero crossing obtained within the timeout window
- **WHEN** the voltage channel produces no zero crossing for the configured `ZXTOUT` duration
- **THEN** the ADE7953 sets the ZXTO bit in `IRQSTATA`/`RSTIRQSTATA` and asserts the IRQ pin

### Requirement: The first ZXTO in a suppression window triggers a direct MQTT alarm
On the first serviced ZXTO interrupt since the last trigger (or after `ADE7953_ZXTO_SUPPRESS_MS` has elapsed since the last trigger), the device SHALL push a minimal alarm payload directly to the MQTT alarm topic and SHALL emit a `LOG_FATAL` entry, both with enough context (at minimum: the ZXTO counter value and the most recent cached voltage reading) to evaluate real-world firing behavior without requiring a scope or external instrumentation. The alarm publish SHALL NOT be routed through the issue registry: it is a direct, fire-and-forget MQTT message queued for the MQTT task from the ADE7953 task, so it is not delayed by the registry's task hop or its evaluation of unrelated global codes. The device SHALL increment a monotonic ZXTO counter, exposed via the existing statistics surface, on every occurrence regardless of suppression.

#### Scenario: ZXTO interrupt observed after the suppression window
- **WHEN** the ADE7953 task services a status snapshot with the ZXTO bit set, and at least `ADE7953_ZXTO_SUPPRESS_MS` has elapsed since the last trigger (or none has occurred yet)
- **THEN** a `LOG_FATAL` entry is emitted, an alarm is queued directly for the MQTT task, and the ZXTO counter increments by exactly one

### Requirement: Runaway ZXTO firing is suppressed for a fixed window without losing observability
Because `ZXTO` has no hardware debounce beyond its own timeout window, the device SHALL suppress every FATAL log and alarm publish for `ADE7953_ZXTO_SUPPRESS_MS` after a trigger, without suppressing the statistics counter and without delaying or suppressing the first occurrence of a real event. Suppressed occurrences SHALL still be observable via a `LOG_DEBUG` entry, so the suppression window itself never hides evidence needed to tune it from log data.

#### Scenario: Sustained ZXTO firing within the suppression window
- **WHEN** ZXTO fires again within `ADE7953_ZXTO_SUPPRESS_MS` of the last trigger
- **THEN** the device emits only a `LOG_DEBUG` entry (no FATAL, no alarm publish, no MQTT log forwarding) and still increments the counter

#### Scenario: A later, separate event after the suppression window elapses
- **WHEN** a ZXTO occurs at least `ADE7953_ZXTO_SUPPRESS_MS` after the last trigger
- **THEN** the device treats it as a fresh trigger: FATAL log, alarm publish, and counter increment
