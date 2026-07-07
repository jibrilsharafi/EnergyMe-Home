## Why

On multi-channel devices, `active_power` intermittently drops to exactly 0 (and occasionally to a partial-low value) on channels that are physically steady (Athena prod data, device `907069886934`, 2026-07-07: PV channel reads `3920.9 -> 0.0 -> 3921.1`; ch0 glitches to 0 in the same servicing pass at 08:47:22.000). It is rare (~0.03-0.5% of reads per channel) but visible on smooth/high signals and corrupts telemetry, energy integration, and automations.

Root cause of the **zeros** is now confirmed against the datasheet. We run the ADE7953 energy registers with **read-with-reset enabled** (`LCYCMODE` bit 6 `RSTREAD` = 1; our `DEFAULT_LCYCMODE_REGISTER = 0b01111111`, whose comment wrongly says it is disabled). Per the datasheet, in line cycle accumulation mode the `AENERGY/RENERGY/APENERGY` registers latch a full window at each `CYCEND` and hold it until the next `CYCEND`; but with `RSTREAD` set, a read clears the register to 0 and it *stays 0 until the next CYCEND*. So any second read within the same window returns 0. Our servicing is non-deterministic in time, so occasionally a register is read twice within one window (e.g. a purge read plus a channel read, or two close passes) and the second read publishes a false 0. Read-with-reset is redundant here because the hardware already latches a clean full window per `CYCEND`.

The **partials** are not explained by `RSTREAD` (it can only yield full-or-zero). They are a separate question (mux settling, or real cloud edges given the PV channel is sampled only ~every 6 s) and are handled by a residual physical check plus the DEBUG-log capture.

## What Changes

- **Root fix**: disable read-with-reset for the energy registers (clear `LCYCMODE` bit 6). Reads become non-destructive: every read returns the last full latched window, a double read returns the same correct value, and Wh integration (which multiplies by actual elapsed `deltaMillis`) self-corrects so nothing is double-counted. Fix the misleading `DEFAULT_LCYCMODE_REGISTER` comment.
- **Purge rework**: the post-mux-switch purge no longer needs a reset read; the existing skip-one-window (`_hasToSkipReading`) already discards the contaminated window because the next `CYCEND` overwrites the register with a clean full window. Reduce the purge to a no-op (or remove) accordingly.
- **Guard cleanup**: the `_interruptHandledChannelA/B` double-read guard exists solely to stop the second in-window read from returning 0; with `RSTREAD` off there is nothing to guard. Remove or neutralize it once the root fix is verified.
- **Residual witness (secondary)**: add an independent `IRMS` read on the base-phase path and compare apparent power from energy (`APENERGY/_sampleTime`) against `voltage x IRMS` (apparent-vs-apparent, so power factor never false-trips). On divergence beyond a calibrated tolerance, discard the reading via the existing invalid-reading path. This catches partials/mux artifacts the root fix does not cover. `IRMS` is a witness only; base-phase current stays derived from apparent energy.

## Capabilities

### New Capabilities
- `meter-reading-integrity`: Non-destructive energy-register reads plus a physical RMS cross-check, so a published per-channel reading reflects a genuine full accumulation window and is discarded when it cannot.

### Modified Capabilities
<!-- None: no existing spec defines meter-reading behavior. -->

## Impact

- **Code**: `source/include/ade7953.h` (`DEFAULT_LCYCMODE_REGISTER` value + comment; new witness tolerance constant); `source/src/ade7953.cpp` (`_setOptimumSettings` / LCYCMODE write, `_purgeEnergyRegisters` + `_handleCycendInterrupt` skip path, `_interruptHandledChannelA/B` guard, `_readMeterValues` base-phase witness); new pure logic in `source/lib` (`MeterLogic`) with Unity tests in `source/test`.
- **Behavioral**: changes a long-standing hot-path register mode; requires an audit of every energy-register reader and on-device verification.
- **Hot path**: one extra 32-bit `IRMS` read per base-phase channel per cycle for the witness (mirrors the three-phase path).
- **Validation**: DEBUG-log capture on device `907069886934` after rollout to confirm zeros are gone and characterize any residual partials; the witness tolerance is finalized from that capture.
- **No change** to `_sampleTime`, the mux channel-selection logic, or the three-phase path.
