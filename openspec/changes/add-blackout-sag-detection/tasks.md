## 1. Interrupt demux: fix unhandled-bit detection

- [x] 1.1 Change the `_handleInterrupt()` unhandled-interrupt check from "no recognized bit present" to "any bit outside the handled set present", so a bit like SAG co-occurring with ZXV is no longer silently dropped.
- [x] 1.2 Add/update a host-testable unit (in `test/test_meter_logic` or equivalent) covering: all-recognized-bits snapshot (no warning), recognized-plus-unrecognized snapshot (warning fires), all-unrecognized snapshot (warning fires, existing behavior preserved).

## 2. SAG configuration

- [x] 2.1 Add `IRQSTATA_SAG_BIT` to `DEFAULT_IRQENA_REGISTER` alongside ZXV/CYCEND/RESET/CRC.
- [x] 2.2 Add named constants for `SAGCYC` value (1) and the `SAGLVL` threshold percentage (80%, tunable constant, not yet exposed via API/shadow).
- [x] 2.3 In `_setupInterrupts()`, read `VPEAK` (after resetting it and a short settle delay) and write `SAGCYC`/`SAGLVL` (derived from that reading) alongside the existing `IRQENA` write.
- [x] 2.4 Log the derived `SAGLVL` value (and the `VPEAK` reading it came from) at setup time so a bad threshold is diagnosable from boot logs alone.

## 3. SAG interrupt handling and observability

- [x] 3.1 Add `ade7953SagInterrupts` to the statistics struct, mirroring `ade7953ZxInterrupts` / `ade7953UnhandledInterrupts`.
- [x] 3.2 Add a `_handleSagInterrupt()` function: increments the new counter and emits `LOG_FATAL` with the counter value and the current VRMS/VPEAK snapshot.
- [x] 3.3 Add the SAG branch to `_handleInterrupt()` (checked alongside the existing ZXV/CYCEND/RESET/CRC branches), and include the SAG bit in the demux's handled-bit mask (task 1.1) so it no longer counts as unhandled.
- [x] 3.4 Expose `ade7953SagInterrupts` via `/system/info` (wherever the other ADE7953 interrupt counters are already surfaced).

## 4. Bench validation

- [ ] 4.1 Build and flash via the `esp32s3-dev-v5` PlatformIO environment onto the bench device.
- [ ] 4.2 Confirm boot-time log shows the derived `SAGLVL`/`VPEAK` values and they look sane (roughly 80% of a plausible peak reading for the device's mains).
- [ ] 4.3 Induce a sag/outage (plug pull or switch) and confirm a `LOG_FATAL` SAG entry appears in the live log stream (serial/UDP listener) before/around the event.
- [ ] 4.4 On any run where the device survives or reboots cleanly, confirm `ade7953SagInterrupts` increments as expected via `/system/info`, and confirm the demux fix (task 1) produces no spurious new warnings under normal operation (ZXV/CYCEND-only snapshots stay silent).
- [ ] 4.5 Note observed firing behavior (false positives on non-blackout dips, timing relative to actual power loss) to inform the follow-up MQTT-alert change.
