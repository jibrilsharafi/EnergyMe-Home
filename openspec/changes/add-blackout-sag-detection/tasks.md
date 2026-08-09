## 1. Interrupt demux: fix unhandled-bit detection

- [x] 1.1 Change the `_handleInterrupt()` unhandled-interrupt check from "no recognized bit present" to "any bit outside the handled set present", so a bit co-occurring with ZXV is no longer silently dropped.
- [x] 1.2 Add/update a host-testable unit (in `test/test_meter_logic` or equivalent) covering: all-recognized-bits snapshot (no warning), recognized-plus-unrecognized snapshot (warning fires), all-unrecognized snapshot (warning fires, existing behavior preserved).
- [x] 1.3 Restrict the check to bits actually enabled in `IRQENA` (`statusA & DEFAULT_IRQENA_REGISTER` before comparing against the handled set) - `IRQSTATA` reports every channel/energy event continuously regardless of enable state, so an unrestricted check floods on routine current-channel/energy noise (`WSMP`, `ZXTO_IA`, etc.) that was never enabled.

## 2. SAG-based trigger (attempted, removed)

- [x] 2.1 ~~Arm SAG (`SAGCYC`/`SAGLVL`), log/count on fire, guard against runaway firing.~~ Implemented and bench-tested; produced no observable log entry (serial or UDP) on a real plug-pull/switch test. Root cause not isolated. All SAG-specific code, constants, and the `ade7953SagInterrupts` counter removed in favor of task group 3 below - see proposal.md/design.md for the pivot rationale.

## 3. ZXTO-based trigger

- [x] 3.1 Add `IRQSTATA_ZXTO_BIT` (14) to `DEFAULT_IRQENA_REGISTER` alongside ZXV/CYCEND/RESET/CRC.
- [x] 3.2 Add named constants for `ZXTOUT` sizing (~15ms target, one missed line cycle plus margin for grid-frequency tolerance and LPF1 delay) and compute the register value from the datasheet's 1/14kHz resolution.
- [x] 3.3 In `_setupInterrupts()`, write `ZXTOUT` alongside the existing `IRQENA` write. No live-derived value needed (unlike SAG's `VPEAK`-derived threshold) - `ZXTOUT` is a fixed timeout.
- [x] 3.4 Log the configured `ZXTOUT` value at setup time (`LOG_INFO`, visible on prod builds) so it's diagnosable from boot logs alone.
- [x] 3.5 Add `ade7953ZxtoInterrupts` to the statistics struct, mirroring `ade7953ZxInterrupts` / `ade7953UnhandledInterrupts`, replacing the removed SAG counter.
- [x] 3.6 Add a `_handleZxtoInterrupt()` function: increments the counter and emits `LOG_FATAL` (subject to the burst guard in task group 4) with the counter value and the current cached voltage.
- [x] 3.7 Add the ZXTO branch to `_handleInterrupt()` (checked alongside the existing ZXV/CYCEND/RESET/CRC branches), and include the ZXTO bit in the demux's handled-bit mask so it no longer counts as unhandled.
- [x] 3.8 Expose `ade7953ZxtoInterrupts` via `/system/info` (wherever the other ADE7953 interrupt counters are already surfaced).

## 4. Runaway-firing guard

- [x] 4.1 Add a count-based log burst guard to `_handleZxtoInterrupt()`: first `ADE7953_ZXTO_LOG_BURST` occurrences log in full, one suppression notice, then count-only (`statistics.ade7953ZxtoInterrupts` still increments every time, unthrottled).
- [x] 4.2 Rearm the burst guard from a clean `CYCEND` window (no ZXTO seen during that accumulation period) in `_handleCycendInterrupt()` - not `ZXV`, which would defeat the throttle during a still-partially-valid waveform.

## 5. Bench validation

- [ ] 5.1 Build and flash via the `esp32s3-dev-v5` PlatformIO environment onto the bench device.
- [ ] 5.2 Confirm boot-time log shows the configured `ZXTOUT` value.
- [ ] 5.3 Induce a grid-loss event (plug pull or switch) and confirm a `LOG_FATAL` ZXTO entry appears in the live log stream (serial/UDP listener) before/around the event.
- [ ] 5.4 On any run where the device survives or reboots cleanly, confirm `ade7953ZxtoInterrupts` increments as expected via `/system/info`, and confirm the demux fix (task 1) produces no spurious new warnings under normal operation (ZXV/CYCEND-only snapshots stay silent).
- [ ] 5.5 Note observed firing behavior (false positives on non-blackout dips, timing relative to actual power loss, and specifically whether ZXTO succeeds where SAG previously produced nothing) to inform the follow-up MQTT-alert change.
- [ ] 5.6 Repeated plug-pull test: confirm each pull produces a fresh full-detail burst (not suppressed by a previous episode) and that the suppression notice appears if a single episode exceeds `ADE7953_ZXTO_LOG_BURST` occurrences.
