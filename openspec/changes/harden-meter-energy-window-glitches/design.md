## Context

`_readMeterValues` (`source/src/ade7953.cpp`) base-phase path derives active/reactive/apparent power from the ADE7953 energy registers: `power = energy / _sampleTime` (lines ~3790-3822); current is derived as `apparentPower / voltage`; `IRMS` is not read here. The non-base-phase (three-phase) path already reads `IRMS` and computes `voltage x IRMS` (lines ~3833, 3871).

Athena prod analysis (device `907069886934`, 2026-07-07) showed intermittent zeros on physically-steady channels, global to a servicing pass (ch0 and the active mux channel glitch together, e.g. both 0.0 at 08:47:22.000), plus rarer partial-low readings.

Datasheet (Active Energy Line Cycle Accumulation): "AENERGYA/AENERGYB ... hold their current values until the end of the next line cycle period, when the contents are replaced with the new reading. If the read-with-reset bit (RSTREAD) ... is set, the contents ... are cleared after a read and remain at 0 until the end of the next line cycle period." We currently run with `RSTREAD` = 1 (`DEFAULT_LCYCMODE_REGISTER = 0b01111111`, bit 6 set - its "disable read with reset" comment is wrong). So a second read of a register within one window returns 0 until the next `CYCEND`. Non-deterministic servicing occasionally reads a register twice within one window (purge read + channel read, or two close passes), publishing a false 0. This is the confirmed cause of the zeros. Partials cannot come from `RSTREAD` (it yields only full-or-zero); they are a separate matter (mux settling, or real cloud edges given the PV channel is sampled ~every 6 s).

Constraints: no `String` (use `char[]` + `snprintf`); no try/catch; host-testable pure logic in `source/lib` with Unity tests in `source/test` (`pio test -e native` from WSL); do not change `_sampleTime`, the mux channel-selection logic, or the three-phase path.

## Goals / Non-Goals

**Goals:**
- Eliminate the false-zero spikes at the source by making energy-register reads non-destructive.
- Keep the resulting purge / guard / energy-integration logic correct and simpler.
- Add a physical RMS witness on the base-phase path to catch residual artifacts (partials, mux) the root fix does not cover.
- Validate on-device with a DEBUG-log capture after rollout.

**Non-Goals:**
- Any timing / `deltaMillis`-based rejection - explicitly rejected below.
- Changing base-phase current to be sourced from `IRMS` (kept derived from apparent energy).
- Touching `_sampleTime`, the mux channel-selection logic, or the three-phase path.

## Decisions

**1. Root fix: disable read-with-reset (`LCYCMODE` bit 6).**
In line cycle accumulation mode the hardware already latches a full window per `CYCEND` and holds it until the next, so `RSTREAD` is redundant and is the mechanism that creates the zero: a within-window second read returns 0. With `RSTREAD` cleared, every read returns the last full latched window; a double read returns the same correct value. Alternative - keep `RSTREAD` and only filter downstream (witness/guard) - rejected as treating the symptom when the cause is a one-bit config error.

**2. Purge becomes skip-only.**
The post-switch contaminated window is already skipped via `_hasToSkipReading`; the next `CYCEND` overwrites the register with a clean full window. With non-destructive reads, the reset read in `_purgeEnergyRegisters` serves no purpose - reduce it to a no-op read or remove it. The correctness comes from skipping one window, not from resetting.

**3. Remove/neutralize the `_interruptHandledChannelA/B` guard.**
Its sole purpose is to stop a within-window second read from returning 0. With `RSTREAD` off there is nothing to guard. Remove it once the root fix is verified, to avoid dead complexity in the hot path.

**4. Energy integration already self-corrects.**
Wh counters integrate `power x deltaMillis` using actual elapsed time (line ~4099). A double read adds `P x ~200ms` then `P x ~5ms` ~= one true window, so non-destructive repeated reads do not double-count. No change needed beyond confirming this with a test.

**5. Residual RMS witness (secondary), apparent-vs-apparent.**
`IRMS` is a continuous RMS register, independent of the energy-register path, so it stays truthful when the energy path is wrong (partials, mux contamination). Compare apparent-vs-apparent so power factor is irrelevant and low-PF loads never false-trip. On divergence beyond a calibrated tolerance, discard via the existing invalid-reading path. Alternative - active-vs-apparent - rejected (false-trips low-PF loads). Alternative - instantaneous power register - rejected (shares the energy path's timing sensitivity).

**6. Rejected alternative: timing / `deltaMillis` guard.**
An earlier design rejected within-window double reads by a wall-clock threshold. Dropped as risky and, after the root fix, pointless. `deltaMillis` is the gap between two firmware reads, not the hardware window (fixed ~200 ms by `LINECYC`); they decouple under jitter. Counterexample: window N read late (~350 ms after N-1's read), then window N+1 read on time - the two firmware reads are ~50 ms apart yet both legitimate; a threshold would false-reject the second. With `RSTREAD` off the second read is simply the correct value anyway.

**7. Discard on witness failure; no substitute/hold/zero.**
Isolated single-sample artifacts; discarding one and waiting one cadence beats publishing a false value. Distinct from issue #149 (which discarded *sustained* readings and created silent gaps).

**8. Build now, validate with logs after.**
Implement the root fix + witness now. Capture DEBUG logs on device `907069886934` after rollout to confirm zeros are gone, characterize residual partials, and finalize the witness tolerance.

## Why RSTREAD was enabled, and what disabling it costs

This section is deliberately preserved so the RSTREAD/line-cycle coupling is not re-misunderstood in future.

**Why read-with-reset was on:**
- It is the chip default. `LCYCMODE` default is `0x40`: bit 6 `RSTREAD` set, line-cycle bits 0-5 clear. Our config enabled bits 0-5 (line-cycle accumulation) and left bit 6 at its default. RSTREAD-on was inherited, not deliberately chosen (and the constant's comment wrongly claimed it was disabled).
- It is the canonical energy-IC idiom. In *normal* (free-running) accumulation mode, read-with-reset is mandatory: the register accumulates continuously, so you read-and-clear to obtain energy-per-interval and to avoid saturation. It is the reflexive way to read an ADE.
- It provides a register-level "freshness" signal: a nonzero read means new data, a repeated read returns 0.

**What RSTREAD costs in line-cycle mode (the bug):**
- Redundant: the hardware already latches a full window into the energy register at each `CYCEND` and holds it until the next.
- Creates the false-zero: any second read within one window returns 0 until the next `CYCEND`.
- Forced extra machinery: the `_interruptHandledChannelA/B` guard and the purge-reset exist only to fight this self-inflicted zero.

**What we lose by disabling it (enumerated so nothing is missed):**
- Freshness signal: moved, not lost - the `CYCEND` interrupt already marks each new window (a better layer than inferring freshness from register zeroing).
- No-load detection: unaffected - it is a power-threshold feature independent of RSTREAD; idle channels still read 0.
- Overflow/saturation: unaffected - in line-cycle mode the register is replaced each window, never free-runs.
- Mux settling: unaffected - settling is handled by skip-one-window.
- Energy Wh accuracy: unaffected - integration uses actual elapsed `deltaMillis`, so a repeated read adds `P x ~5ms` on top of `P x ~200ms` ~= one true window (self-correcting).
- Duplicate data points: minor cosmetic - a double-service now yields two near-identical samples instead of one-good-one-false-zero; benign and strictly better than a false 0.

**The guardrail (must stay in the LCYCMODE comment):** disabling RSTREAD is safe ONLY because line-cycle accumulation (LCYCMODE bits 0-5) is enabled. Line-cycle mode ON -> register auto-latches per window -> RSTREAD redundant. Normal mode -> register free-runs -> RSTREAD mandatory (saturation + per-interval energy). Never disable RSTREAD without line-cycle mode on; if line-cycle mode is ever disabled, RSTREAD must be re-enabled.

## Risks / Trade-offs

- **Behavioral change to a long-standing hot-path register mode** -> Mitigation: audit every energy-register reader (list in tasks), unit-test the pure logic, verify on a bench device before wide rollout.
- **Residual partials may be real (cloud edges at ~6 s PV cadence), not artifacts** -> Mitigation: the DEBUG-log capture distinguishes artifact (energy low while `IRMS` high) from real (both move together); the witness only rejects the former.
- **Too-tight witness tolerance drops real fast transients** -> Mitigation: calibrate from captured data; start loose (artifacts diverge far from unity).
- **Extra `IRMS` SPI read per base-phase channel** -> Mitigation: one read alongside existing 3 energy + 1 voltage reads; the three-phase path already does it.
- **Removing the guard could regress if some untested path still depends on reset semantics** -> Mitigation: the reader audit; remove guard only after the root fix is verified on-device.

## Migration Plan

1. Land the LCYCMODE change (+ comment fix), purge/guard rework, witness pure logic, and unit tests.
2. Deploy to a bench/dev device; capture DEBUG logs; confirm zeros gone and characterize partials.
3. Finalize the witness tolerance from the capture; roll out.
4. Rollback: restore `RSTREAD` = 1 in `DEFAULT_LCYCMODE_REGISTER` and re-enable the guard; raise the witness tolerance to a no-op.

## Validation results (prod device `907069886934`, 2026-07-08 -> 07-14)

Firmware manually rolled out mid-day 2026-07-08. Verified read-only via Athena (`energyme_home_prod_meter`), the S3 `statistics/` payloads, and CloudWatch DEBUG logs. One continuous boot across the window (all statistics counters monotonically increasing, no reset).

**Root fix - false-zeros eliminated.** ch0 grid false-zeros (`active_power = 0` flanked by `|>500 W|` both sides), per day:
- Old fw ramping: 07-05 **90** -> 07-06 **131** -> 07-07 **164**.
- New fw: 07-09 **0**, 07-10 2, 07-11 0, 07-12 3, 07-13 4, 07-14 1 (07-08 = 52, changeover day). Clamped to the noise floor.

**PV artifacts gone.** ch15 isolated up-spikes (`> both neighbors + 1000 W`): old fw 0-3/day -> new fw **0/day** every day, despite PV cadence doubling (16.5k -> 34k samples/day after the RSTREAD fix).

**Servicing keeps up.** `unhandledInterrupts = 0` and `meterPointsDropped = gridPointsDropped = 0` across ~29.4 M interrupts / 4.08 M reads. No crashes since 07-04 (pre-fix); 3 warnings / 2 errors total over ~6 days.

**Witness behaving as designed.** `readingCountFailure` steady at ~1.2% (39,059 discards / 3,284,242 reads over the 07-09..07-14 interval, not growing) - the apparent-vs-apparent witness quietly dropping bad reads without gaps in published data. Grid-conservation cross-check (Grid + PV - Sum loads) stays within <1% of throughput and tightens to std ~42 W with zero >500 W deviations at 15 s; real transients (shutter motors corroborated on the independent grid clamp) are preserved, only artifacts rejected.

## Open Questions

- ~~Final numeric tolerance for the apparent-power divergence band~~ - resolved: `APPARENT_WITNESS_MAX_DIVERGENCE = 0.5` validated in prod (steady ~1.2% discard rate, no real-transient loss over a week).
- ~~Whether the residual partials are artifacts or real~~ - resolved: the witness rejects only artifacts (energy low while `IRMS` high); genuine transients move both together and pass. Confirmed against grid-corroborated motor events.
