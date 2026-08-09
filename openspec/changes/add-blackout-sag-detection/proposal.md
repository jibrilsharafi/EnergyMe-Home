## Why

The v6.1 hardware already provisions a 3300µF bulk cap specifically "for hold-up time after grid loss, allowing the ADE7953 to detect missing voltage reference and for the firmware to dispatch a last-gasp MQTT message before power fails" (`hardware/README.md`). Issue #93 originally scoped this exact detection via the ADE7953's SAG feature (SAGCYC/SAGLVL, `RSTIRQSTATA` bit 19), but was deferred in favor of #157's grid-frequency EMA work, which explicitly listed "Sag/swell detection via SAGCYC/SAGLVL" as out of scope. The interrupt/task plumbing that #157/#194 built (ISR → semaphore → line-cycle-cadence task → status demux) is proven in production, but the SAG bit itself is still never enabled or serviced.

Given the tight, physically-bounded hold-up budget (single-digit to low-double-digit milliseconds on the external cap alone), detection correctness has to be validated before any cloud-publish work is attempted. This change is phase 1 only: get the SAG interrupt reliably firing and visibly logged on real hardware. No MQTT/cloud alerting yet - that depends on results from this phase and is deferred to a follow-up change.

## What Changes

- Enable the SAG interrupt (bit 19) in `DEFAULT_IRQENA_REGISTER`, alongside the existing ZXV/CYCEND/RESET/CRC bits.
- Program `SAGCYC = 1` (the minimum - one half line cycle, ~10ms @ 50Hz - matching the original issue's own framing and the physically thin hold-up budget) and `SAGLVL` derived at boot from a live `VPEAK` register read (80% threshold, per the ADE7953 datasheet's own calibration procedure). This is self-scaling per device/region since it never depends on a hardcoded raw threshold or the separate per-channel voltage-gain calibration flow.
- Add an explicit SAG branch to `_handleInterrupt()` that logs via `LOG_FATAL` with enough context (timestamp, a monotonic SAG counter, and the current VRMS/VPEAK snapshot) to evaluate real-world firing behavior and tune SAGCYC/SAGLVL from log data alone.
- **Fix a pre-existing observability gap in the interrupt demux**: the current "unhandled interrupt" check (`(statusA & handledIrqMask) == 0`) only flags a status snapshot with *zero* recognized bits. Any unrecognized bit that co-occurs with a recognized one (e.g. SAG alongside ZXV, which happens on every cycle since ZXV fires at line rate) is silently dropped today with no log line and no counter increment. Change the check to flag any bit outside the handled set, regardless of what else is set in the same snapshot.
- Add a SAG interrupt counter to `statistics`, mirroring the existing `ade7953ZxInterrupts` / `ade7953UnhandledInterrupts` pattern, so firing frequency is visible via `/system/info` without grepping logs.

**Explicitly out of scope for this change** (deferred to a follow-up once phase 1 is validated on hardware):
- MQTT/cloud alert publishing on SAG detection.
- Any hold-up-time-critical fast-path publish mechanism (bypassing the normal MQTT task queue).
- Cloud-side correlation of "device offline" vs. "confirmed blackout" (e.g. a >30s offline heuristic) - a cloud/backend concern, not firmware.

## Capabilities

### New Capabilities
- `grid-loss-detection`: SAG-based voltage-loss precursor detection - ADE7953 SAG interrupt configuration, live-VPEAK-derived threshold, and `LOG_FATAL` observability. No cloud/MQTT surface in this phase.

### Modified Capabilities
- `grid-frequency-measurement`: the interrupt demux ("Interrupt demux services every set status bit") is extended to also service the SAG bit, and the "unhandled interrupt" detection requirement is corrected to catch bits outside the handled set even when a recognized bit co-occurs in the same snapshot.

## Impact

- `source/src/ade7953.cpp`: `_setupInterrupts()` (IRQENA/SAGCYC/SAGLVL writes), `_handleInterrupt()` (new SAG branch, corrected unhandled check), new `_handleSagInterrupt()`.
- `source/include/ade7953.h`: new constants (SAGCYC value, SAGLVL threshold percentage), statistics field.
- `source/include/ade7953registers.h`: no changes needed - `IRQSTATA_SAG_BIT`, `SAGCYC_8`, `SAGLVL_24/32` already defined.
- `source/include/structs.h`: new SAG counter in the statistics struct.
- `source/test/test_meter_logic`: coverage for the corrected unhandled-interrupt check and any host-testable SAG threshold logic.
- No MQTT, cloud, or REST API surface changes in this phase.
