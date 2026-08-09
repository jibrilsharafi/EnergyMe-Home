## Context

See `proposal.md` - Why. Key facts that shape this design, gathered during exploration:

- The ISR → semaphore → line-cycle-cadence task → `_handleInterrupt()` status demux already exists and is proven in production (built for the grid-frequency EMA feature). SAG just needs to become another serviced bit in that same demux.
- `IRQSTATA_SAG_BIT` (19), `SAGCYC_8` (0x000), `SAGLVL_24`/`SAGLVL_32` (0x200/0x300) are already defined in `ade7953registers.h`; nothing new needed there.
- Datasheet mechanics (ADE7953 Rev. C, "Voltage Sag Detection", p.45): sag is disabled unless both `SAGCYC` and `SAGLVL` are nonzero. `SAGCYC` counts half line cycles the voltage must stay below `SAGLVL` before the bit sets; minimum is 1 (~10ms @ 50Hz). `SAGLVL` is in the *same raw units as `VPEAK`* - the datasheet's own calibration procedure is "read `VPEAK`, write X% of that value to `SAGLVL`," no unit conversion.
- Live register read from a deployed bench-adjacent unit confirmed the mechanics: `VRMS_32 = 5,826,688` matched the calibrated `225.2V` from `/meter-values`; `VPEAK_32 = 4,289,863`; `PERIOD_16 = 4,475` → `223750/4476 = 50.00 Hz`. 80% of that live `VPEAK` reading is `≈3,431,890` - a concrete, correctly-scaled `SAGLVL` for that specific device, computed with no calibration-flow dependency.
- PGA_V (voltage-channel gain) is set per-device during calibration, not a fixed regional constant - so a hardcoded raw `SAGLVL` would not transfer between a 120V and a 230V install even at the "same" percentage. This is why `SAGLVL` must be derived live, not hardcoded.
- Physical hold-up budget (v6.1 hardware, 3300µF external cap, ~100mA load): roughly 10-26ms depending on allowed droop before regulation/BOD margin gets uncomfortable, plus an unquantified but likely modest bonus from the PSU's own internal reservoir. This budget is *not* the concern of this change (no MQTT/publish work here) but it's why `SAGCYC=1` (the fastest available trigger) is the only sane starting point - anything slower eats directly into a budget that's already thin.
- The bench device available for testing (`esp32s3-dev-v5` build environment) is a different hardware revision (smaller cap, non-isolated voltage sensing) than the v6.1 board the hold-up budget above was computed for. It validates the ADE7953 register-level detection behavior; it does not validate v6.1 hold-up timing.
- Verified from actual code (`_handleInterrupt()`, `ade7953.cpp:1930-1936`): the existing "unhandled interrupt" check is `(statusA & handledIrqMask) == 0` - true only when *no* recognized bit is present. A SAG bit co-occurring with ZXV (which fires every line cycle) would never trip this check, so arming SAG without also fixing this check would produce zero observable signal anywhere.
- Verified `LOG_FATAL` usage elsewhere (`ade7953.cpp:4595`, `4642`, `crashmonitor.cpp`): it is a severity marker plus a `statistics.logFatal` counter only. It never triggers a reboot or crash-dump archive by itself - only the surrounding code does that explicitly, on its own separate decision. Safe to use here with no hidden side effects.

## Goals / Non-Goals

**Goals:**
- Get the ADE7953 SAG interrupt reliably firing and observable (log + counter) on real hardware, fast enough to matter given the hold-up budget.
- Make `SAGLVL` correct across devices/regions without a new calibration step or a dependency on the existing voltage-calibration flow's timing.
- Fix the interrupt demux's "unhandled bit" blind spot generally, not just work around it for SAG.

**Non-Goals:**
- MQTT/cloud publishing on SAG detection (separate follow-up change, informed by what this phase observes).
- Any hold-up-critical fast-path publish mechanism, task-priority changes, or PubSubClient concurrency work.
- SAGCYC/SAGLVL becoming user-configurable (REST/shadow-exposed). Fixed constants for this first pass.
- Persisting SAG events across reboots (NVS). A bench unit tested via plug/switch (not full destructive outage every time) can observe the log live; a device that actually browns out loses ADE7953 register state and current-boot log buffer anyway, so reboot-persisted history is a separate, later concern if it turns out to matter.
- Debounce/hysteresis logic to suppress legitimate brief sags that aren't blackouts. `SAGCYC=1` is deliberately maximally sensitive for this phase - the point is to observe real firing frequency, not pre-guess it.

## Decisions

**SAG (not ZXTO) as the trigger primitive.** `ZXTO` (zero-crossing timeout) only fires after a *complete* absence of zero crossings for the full `ZXTOUT` duration - at best matching, at worst slower than, an equivalent-cycle-count SAG trigger, and it doesn't catch a declining-but-not-yet-absent waveform the way SAG's threshold comparison does. SAG is strictly the better fit and is what issue #93 originally specified.

**`SAGCYC = 1`, fixed, not configurable.** The minimum available value, matching the original issue's framing and the thin hold-up budget. Making it configurable now would be premature - we don't yet know from real data whether 1 half-cycle produces an unmanageable rate of non-blackout false positives (motor inrush, etc.). Fixed for this pass; revisit once bench/field data exists.

**`SAGLVL` derived from a live `VPEAK` read at boot, not from the existing calibration flow.** Two options considered:
- *Tie it to the per-channel voltage-gain calibration data* (convert the calibrated nominal-voltage constant back into raw counts). Rejected: adds a dependency on calibration having already run and completed before the sag feature can arm, and requires a conversion the datasheet's own procedure doesn't need.
- *Read `VPEAK` directly and take a fixed percentage of it* (this design). `SAGLVL` and `VPEAK` are defined by the datasheet to share units exactly - no conversion, no calibration-flow dependency, and it's self-correcting per device/region since it uses whatever that specific unit is actually reading right now. Read `VPEAK` after a short post-boot settle window (matching the datasheet's "wait a few line cycles" guidance) as part of the existing one-time interrupt setup in `_setupInterrupts()`.
- Starting percentage: 80%, matching the datasheet's own worked example. Tunable later from bench data; not a structural decision.

**`LOG_FATAL` for the trigger, no new logging path.** Matches existing convention exactly (see Context) and needs no new infrastructure. It is deliberately *not* wired to trigger any reboot/recovery action - this phase is observation only.

**Fix the demux "unhandled" check generally, not with a SAG-specific carve-out.** The bug (any bit outside the handled set is silently dropped whenever a handled bit co-occurs) is generic, not SAG-specific - the SAG discovery is just what surfaced it. Fixing the check itself (flag on any bit outside the handled set, regardless of what else is set) costs about the same as a narrow workaround and restores the demux's actual intended guarantee for any future bit, not just this one.

**New statistics counter, mirroring existing pattern.** `ade7953ZxInterrupts` / `ade7953UnhandledInterrupts` already exist; add `ade7953SagInterrupts` alongside them for `/system/info` visibility, consistent with how the codebase already surfaces this class of counter.

## Risks / Trade-offs

- **[Risk]** `SAGCYC=1` will very likely fire on legitimate brief sags that never become a blackout (motor inrush elsewhere on the circuit, etc.) → **Mitigation**: explicitly the point of this phase - observe real firing frequency from logs/counter before deciding whether debounce is needed. Documented as a non-goal, not a bug.
- **[Risk]** A single `VPEAK` read at boot could be skewed if taken during a transient → **Mitigation**: read after a short settle window (a few line cycles, per datasheet guidance); log the derived `SAGLVL` value itself so a bad threshold is diagnosable after the fact.
- **[Risk]** The available bench hardware (`esp32s3-dev-v5`) has a different hold-up profile (smaller cap, non-isolated design) than the v6.1 board this was scoped for → **Mitigation**: this phase validates ADE7953-level detection behavior and firmware correctness only; timing/hold-up conclusions from this bench unit do not transfer to v6.1 survival estimates, and that distinction should be called out explicitly whenever results are discussed.
- **[Risk]** Loosening the "unhandled interrupt" check could surface previously-silent warnings for other latent conditions → **Mitigation**: `DEFAULT_IRQENA_REGISTER` only enables ZXV/CYCEND/RESET/CRC/SAG after this change, so any other bit appearing would itself be a genuine, previously-undetected condition worth seeing - this is the intended effect of the fix, not a side effect to guard against.

## Migration Plan

- Branch off `development` (per project convention), Conventional Commits, one concern per commit (e.g. demux-fix commit separate from the SAG-enable commit).
- Build/flash via the `esp32s3-dev-v5` PlatformIO environment (matches the bench device on hand).
- Bench validation: induce a sag/outage via plug pull or switch on the bench device; confirm `LOG_FATAL` entry appears (serial/UDP log listener) and, on any run where the device survives to observe it, the `ade7953SagInterrupts` counter via `/system/info`.
- No persisted state introduced (`SAGCYC`/`SAGLVL` are volatile ADE7953 registers, rewritten every boot in `_setupInterrupts()`) - rollback is a plain revert with no migration/backward-compat concern.

## Open Questions

- Exact `SAGLVL` percentage (80% starting point per datasheet example) - tune from real bench data; doesn't change the approach or task breakdown.
- Whether SAG events should eventually persist across reboots (NVS) for post-mortem inspection after a real brownout-triggered reboot - deferred; doesn't block this phase's log-based bench validation.
