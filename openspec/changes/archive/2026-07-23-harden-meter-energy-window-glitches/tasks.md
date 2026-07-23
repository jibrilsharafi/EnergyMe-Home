## 1. Audit (do first)

- [x] 1.1 List every reader of the energy registers (`_readActiveEnergy` / `_readReactiveEnergy` / `_readApparentEnergy` and `_purgeEnergyRegisters`) and note which relied on read-with-reset semantics
- [x] 1.2 Confirm the Wh integration path (`_readMeterValues` lines ~4099-4139) uses actual elapsed `deltaMillis`, so non-destructive repeated reads self-correct
- [x] 1.3 Confirm no calibration/setup path depends on the reset-on-read behavior of the energy registers

## 2. Root fix: disable read-with-reset

- [x] 2.1 Clear `LCYCMODE` bit 6 in `DEFAULT_LCYCMODE_REGISTER` (`0b01111111` -> `0b00111111`) in `source/include/ade7953.h`, replace the misleading comment, and document the guardrail concisely: read-with-reset is disabled because line-cycle accumulation (bits 0-5) already latches a full window per CYCEND; RSTREAD would zero a within-window second read (the false-zero bug). CRITICAL coupling: RSTREAD is safe to leave OFF only while line-cycle mode is ON - in normal/free-running mode RSTREAD is mandatory (saturation + per-interval energy), so if line-cycle bits are ever cleared, bit 6 must be set again. (Full rationale in design.md.)
- [x] 2.2 Reduce `_purgeEnergyRegisters` to a no-op (or remove it and its call site) since the contaminated window is already discarded by the `_hasToSkipReading` skip; keep a concise comment explaining skip-one-window is what flushes the mux transition
- [x] 2.3 Remove or neutralize the `_interruptHandledChannelA/B` double-read guard in `_readMeterValues` / `_handleInterrupt`, with a concise comment that it is obsolete once reads are non-destructive

## 3. Residual RMS witness (base-phase path)

- [x] 3.1 Add a `MeterLogic` function for the apparent-power divergence test (energy-derived vs RMS-derived apparent power -> discard?) with the tolerance constant in `source/include/ade7953.h`
- [x] 3.2 In `_readMeterValues` base-phase path, read `IRMS`, compute `S_rms = voltage x IRMS*aLsb` (witness only; do NOT change derived current), call the divergence test, and on failure discard via the existing invalid-reading path (`_recordFailure`, return false)
- [x] 3.3 Add a concise comment explaining the witness (independent RMS register catches partials/mux artifacts the root fix does not) and why no timing/`deltaMillis` guard is used (design.md decision 6); add a concise DEBUG log line on discard

## 4. Unit tests (Unity, native)

- [x] 4.1 Test apparent-divergence: energy-low vs RMS-high -> discard; steady agreement -> accept; low power-factor load (apparent-vs-apparent) -> accept; genuine transient (both move together) -> accept
- [x] 4.2 Test Wh integration is not double-counted when a window is read twice (elapsed times summing to one window)
- [x] 4.3 Run `pio test -e native` from WSL and confirm all pass

## 5. Calibrate and verify on device (after rollout)

**Closed out 2026-07-23 without the originally planned bench-capture workflow - tracking what was actually verified vs what was not, per Jibril's explicit call to close this rather than leave it open indefinitely.**

- [ ] 5.1 Add temporary DEBUG logging of `S_energy`, `S_rms`, ratio, and actual window per base-phase read - **not done**. No such temporary block was ever added to `ade7953.cpp` (only the concise per-discard log from 3.3 exists). Verification below used Athena production data directly instead of a bench capture.
- [ ] 5.2 Deploy to a bench/dev device; capture logs during PV production and during real fast transients - **not done as specified**. The fix shipped straight to the production fleet as part of #199 rather than a staged bench capture; no dedicated log capture during PV production/fast transients was performed.
- [x] 5.3 Confirm the exact-zero spikes are gone in Athena for the target device after the root fix - **confirmed by Jibril via direct Athena query against production data**: exact-zero spikes are gone for the target device.
- [ ] 5.4 From the capture, determine whether residual partials are artifacts (energy low while IRMS high) or real (both move together); set the final witness tolerance accordingly - **not done**. No capture was taken, so this was never empirically determined.
- [ ] 5.5 Set the final tolerance constant; remove the temporary calibration-only DEBUG logging (keep the concise per-discard log) - **not applicable / accepted as-is**. No temp logging exists to remove. `APPARENT_WITNESS_MAX_DIVERGENCE` remains at its original implementation-time value (`0.5f`, `ade7953.h:260`), never re-tuned against real capture data. Accepted as an open, untuned constant: the root cause (RSTREAD false-zeros) is what's confirmed fixed per 5.3; the RMS witness is a secondary/residual safety net whose exact threshold precision is unverified but not required for the primary fix to hold.
