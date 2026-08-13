## Why

The v6.1 hardware already provisions a 3300µF bulk cap specifically "for hold-up time after grid loss, allowing the ADE7953 to detect missing voltage reference and for the firmware to dispatch a last-gasp MQTT message before power fails" (`hardware/README.md`). Issue #93 originally scoped this exact detection via the ADE7953's SAG feature (SAGCYC/SAGLVL). The interrupt/task plumbing that #157/#194 built (ISR → semaphore → line-cycle-cadence task → status demux) is proven in production.

A first pass used SAG as the trigger primitive. A real bench test (plug pull / switch) produced no observable log entry at all - neither serial nor UDP - despite the interrupt being armed and the demux fix in place. Rather than keep debugging SAG blind, this change switches the trigger primitive to `ZXTO` (zero-crossing timeout): a complete-absence-of-signal detector, distinct from SAG's below-threshold comparison, sized to fire on a single missed line cycle (see design.md for why this is not expected to be slower than SAG in practice). SAG's code, constants, and statistics counter are removed entirely rather than left disabled alongside it, so the next bench round cleanly isolates whether `ZXTO` fires where SAG did not.

Given the tight, physically-bounded hold-up budget (single-digit to low-double-digit milliseconds on the external cap alone), detection correctness was validated before any cloud-publish work was attempted. Phase 1 (get ZXTO reliably firing and visibly logged on real hardware) succeeded on bench test 2026-08-09: a burst of `LOG_FATAL` entries reached AWS via the existing log-forwarding pipeline before the device's next boot showed a genuine "Power on" reset.

Phase 2 (this update) adds the actual cloud alarm. Two rounds of bench testing after wiring a `grid_voltage_loss` issue through the existing `IssueRegistry` showed the shadow update consistently failed to reach AWS before reset, even after adding an immediate-wake mechanism (`xTaskNotifyWait`-based bit notifications, replacing the issue registry's and MQTT task's previous poll-only `ulTaskNotifyTake` waits) to eliminate poll-interval latency at every hop. The registry's generic reported-state publish path (`issuesToJson()` walking all 32 issue slots, building the full shadow doc) was too heavy for a path racing a capacitor. The fix: publish a minimal alarm payload **directly** from `Ade7953::_handleZxtoInterrupt()` to a dedicated MQTT alarm topic, bypassing the issue registry and the shadow subsystem entirely - `grid_voltage_loss` is not tracked as an issue-registry state at all.

## What Changes

- Enable the ZXTO interrupt (`IRQSTATA`/`IRQENA` bit 14) alongside the existing ZXV/CYCEND/RESET/CRC bits, and program `ZXTOUT` (Address 0x100) to ~15ms - just over one worst-case healthy half-line-cycle gap, so a single missed zero crossing is enough to trigger, matching the physically thin hold-up budget.
- Add an explicit ZXTO branch to `_handleInterrupt()` that logs via `LOG_FATAL` with enough context (a monotonic ZXTO counter and the current cached voltage) to evaluate real-world firing behavior and tune `ZXTOUT` from log data alone.
- **Fix a pre-existing observability gap in the interrupt demux**: the original "unhandled interrupt" check (`(statusA & handledIrqMask) == 0`) only flagged a status snapshot with *zero* recognized bits. Any unrecognized bit that co-occurs with a recognized one (e.g. ZXTO alongside ZXV, which happens on every cycle since ZXV fires at line rate) was silently dropped with no log line and no counter increment. Changed the check to flag any bit outside the *enabled* set, regardless of what else is set in the same snapshot - and further restricted it to only bits actually enabled in `IRQENA`, since `IRQSTATA` reports every channel/energy event continuously regardless of enable state (enable only gates the physical IRQ pin).
- Add a ZXTO interrupt counter to `statistics`, mirroring the existing `ade7953ZxInterrupts` / `ade7953UnhandledInterrupts` pattern, so firing frequency is visible via `/system/info` without grepping logs.
- Guard against runaway firing: `ZXTO` has no hardware debounce beyond its own timeout window, so a misconfigured `ZXTOUT` or a sustained no-AC condition (e.g. a bench unit staying powered from USB with AC disconnected) could otherwise re-fire indefinitely and flood the log queue - `AdvancedLogger`'s queue does synchronous flash flushes on the producer once full, which would stall the meter-reading task itself. Only the first ZXTO in a `ADE7953_ZXTO_SUPPRESS_MS` (60s) window triggers anything (FATAL log + alarm publish); every other occurrence in that window gets a `LOG_DEBUG` entry only. Replaces the phase-1 count-based burst latch (log first 3, then count-only until a clean `CYCEND` window) with a simpler flat time floor - see design.md.
- Publish a minimal alarm payload directly to a dedicated MQTT alarm topic (`Mqtt::pushAlarm()`, routed via its own AWS IoT Rule) on the same first-occurrence trigger, called directly from `Ade7953::_handleZxtoInterrupt()` - not through the issue registry.

**Explicitly out of scope for this change** (deferred to a later phase):
- Cloud-side correlation of "device offline" vs. "confirmed blackout" (e.g. a >30s offline heuristic) - a cloud/backend concern, not firmware.
- Re-adding SAG as a secondary/confirmation signal alongside ZXTO - a candidate for a later phase once ZXTO's real-world false-positive rate is known, not this one.
- Tracking grid-loss as an `IssueRegistry` state (`/issues` list, device shadow) - tried, found too slow for this specific alarm's deadline, removed; the alarm topic is the sole cloud surface for this signal for now.

## Capabilities

### New Capabilities
- `grid-loss-detection`: ZXTO-based voltage-loss precursor detection - ADE7953 zero-crossing-timeout configuration, `LOG_FATAL` observability, and a direct MQTT alarm publish bypassing the issue registry and shadow subsystem.

### Modified Capabilities
- `grid-frequency-measurement`: the interrupt demux ("Interrupt demux services every set status bit") is extended to also service the ZXTO bit, and the "unhandled interrupt" detection requirement is corrected to catch bits outside the enabled set even when a recognized bit co-occurs in the same snapshot.

## Impact

- `source/src/ade7953.cpp`: `_setupInterrupts()` (IRQENA/ZXTOUT write), `_handleInterrupt()` (ZXTO branch, corrected unhandled check), `_handleZxtoInterrupt()` (suppression window + direct `Mqtt::pushAlarm()` + `LOG_FATAL`).
- `source/include/ade7953.h`: new constants (`ZXTOUT` target/value, `ADE7953_ZXTO_SUPPRESS_MS`), statistics field.
- `source/include/ade7953registers.h`: no changes needed - `IRQSTATA_ZXTO_BIT`, `ZXTOUT_16` already defined.
- `source/include/structs.h`: new ZXTO counter in the statistics struct (`ade7953ZxtoInterrupts`), replacing the removed SAG counter.
- `source/include/mqtt.h`, `source/src/mqtt.cpp`: new `AlarmEntry` payload type, `Mqtt::pushAlarm()`, dedicated alarm queue/topic (`MQTT_TOPIC_ALARM`, `AWS_IOT_CORE_RULE_ALARM`), drained ahead of the log queue and everything else in `_handleConnectedState()`; `requestImmediatePublish()` (`xTaskNotifyWait`-based wake, replacing the MQTT task's poll-only `ulTaskNotifyTake`).
- `source/include/issueregistry.h`, `source/src/issueregistry.cpp`: `requestImmediateEvaluation()` (`xTaskNotifyWait`-based wake for the registry task) - added, then the `grid_voltage_loss` code itself was tried and removed once the alarm was moved to the direct path.
- `source/include/constants.h`: shared `TASK_NOTIFY_SHUTDOWN_BIT` convention, `stopTaskGracefully()` migrated to it.
- Matching `AWS_IOT_CORE_RULE_ALARM` AWS IoT Rule (infra repo, server-side) confirmed provisioned.
