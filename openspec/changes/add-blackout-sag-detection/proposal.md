## Why

The v6.1 hardware already provisions a 3300µF bulk cap specifically "for hold-up time after grid loss, allowing the ADE7953 to detect missing voltage reference and for the firmware to dispatch a last-gasp MQTT message before power fails" (`hardware/README.md`). Issue #93 originally scoped this exact detection via the ADE7953's SAG feature (SAGCYC/SAGLVL). The interrupt/task plumbing that #157/#194 built (ISR → semaphore → line-cycle-cadence task → status demux) is proven in production.

A first pass used SAG as the trigger primitive. A real bench test (plug pull / switch) produced no observable log entry at all - neither serial nor UDP - despite the interrupt being armed and the demux fix in place. Rather than keep debugging SAG blind, this change switches the trigger primitive to `ZXTO` (zero-crossing timeout): a complete-absence-of-signal detector, distinct from SAG's below-threshold comparison, sized to fire on a single missed line cycle (see design.md for why this is not expected to be slower than SAG in practice). SAG's code, constants, and statistics counter are removed entirely rather than left disabled alongside it, so the next bench round cleanly isolates whether `ZXTO` fires where SAG did not.

Given the tight, physically-bounded hold-up budget (single-digit to low-double-digit milliseconds on the external cap alone), detection correctness has to be validated before any cloud-publish work is attempted. This change is phase 1 only: get a grid-loss interrupt reliably firing and visibly logged on real hardware. No MQTT/cloud alerting yet - that depends on results from this phase and is deferred to a follow-up change.

## What Changes

- Enable the ZXTO interrupt (`IRQSTATA`/`IRQENA` bit 14) alongside the existing ZXV/CYCEND/RESET/CRC bits, and program `ZXTOUT` (Address 0x100) to ~15ms - just over one worst-case healthy half-line-cycle gap, so a single missed zero crossing is enough to trigger, matching the physically thin hold-up budget.
- Add an explicit ZXTO branch to `_handleInterrupt()` that logs via `LOG_FATAL` with enough context (a monotonic ZXTO counter and the current cached voltage) to evaluate real-world firing behavior and tune `ZXTOUT` from log data alone.
- **Fix a pre-existing observability gap in the interrupt demux**: the original "unhandled interrupt" check (`(statusA & handledIrqMask) == 0`) only flagged a status snapshot with *zero* recognized bits. Any unrecognized bit that co-occurs with a recognized one (e.g. ZXTO alongside ZXV, which happens on every cycle since ZXV fires at line rate) was silently dropped with no log line and no counter increment. Changed the check to flag any bit outside the *enabled* set, regardless of what else is set in the same snapshot - and further restricted it to only bits actually enabled in `IRQENA`, since `IRQSTATA` reports every channel/energy event continuously regardless of enable state (enable only gates the physical IRQ pin).
- Add a ZXTO interrupt counter to `statistics`, mirroring the existing `ade7953ZxInterrupts` / `ade7953UnhandledInterrupts` pattern, so firing frequency is visible via `/system/info` without grepping logs.
- Guard against runaway firing: `ZXTO` has no hardware debounce beyond its own timeout window, so a misconfigured `ZXTOUT` or a sustained no-AC condition (e.g. a bench unit staying powered from USB with AC disconnected) could otherwise re-fire indefinitely and flood the log queue - `AdvancedLogger`'s queue does synchronous flash flushes on the producer once full, which would stall the meter-reading task itself. A count-based burst latch logs the first few occurrences in full, then suppresses until a clean `CYCEND` window proves the line healthy again.

**Explicitly out of scope for this change** (deferred to a follow-up once phase 1 is validated on hardware):
- MQTT/cloud alert publishing on grid-loss detection.
- Any hold-up-time-critical fast-path publish mechanism (bypassing the normal MQTT task queue).
- Cloud-side correlation of "device offline" vs. "confirmed blackout" (e.g. a >30s offline heuristic) - a cloud/backend concern, not firmware.
- Re-adding SAG as a secondary/confirmation signal alongside ZXTO - a candidate for a later phase once ZXTO's real-world false-positive rate is known, not this one.

## Capabilities

### New Capabilities
- `grid-loss-detection`: ZXTO-based voltage-loss precursor detection - ADE7953 zero-crossing-timeout configuration and `LOG_FATAL` observability. No cloud/MQTT surface in this phase.

### Modified Capabilities
- `grid-frequency-measurement`: the interrupt demux ("Interrupt demux services every set status bit") is extended to also service the ZXTO bit, and the "unhandled interrupt" detection requirement is corrected to catch bits outside the enabled set even when a recognized bit co-occurs in the same snapshot.

## Impact

- `source/src/ade7953.cpp`: `_setupInterrupts()` (IRQENA/ZXTOUT write), `_handleInterrupt()` (ZXTO branch, corrected unhandled check), new `_handleZxtoInterrupt()`, `_handleCycendInterrupt()` (burst-guard rearm).
- `source/include/ade7953.h`: new constants (`ZXTOUT` target/value, log burst limit), statistics field.
- `source/include/ade7953registers.h`: no changes needed - `IRQSTATA_ZXTO_BIT`, `ZXTOUT_16` already defined.
- `source/include/structs.h`: new ZXTO counter in the statistics struct (`ade7953ZxtoInterrupts`), replacing the removed SAG counter.
- No MQTT, cloud, or REST API surface changes in this phase.
