## Context

The meter integrates every channel against one voltage input V1 on the stored voltage phase (`add-voltage-phase`, 2.8.0). For channel k the chip latches a raw energy pair (P1, Q1) per window. `PhaseUtils::toChannelFrame` negates the pair for `reverse` and rotates it by `angle(phase_k) - angle(voltagePhase)`. The load/PV clamp then zeroes negative results, energy included. The CT-reversal detector flips `reverse` after a net run of negative votes.

So a wrong phase (120 degrees) plus a detector flip (180 degrees) leaves a channel 60 degrees off, with positive power and a plausible PF. The raw pair exists only on the device: the cloud sees post-rotation, post-clamp values.

The normative model of every decision below is `source/utils/phase_solver_ref.py`, and its cases are `source/utils/test_phase_solver_ref.py`. The firmware solver must reproduce both.

## Goals / Non-Goals

**Goals:**
- Never propose a wrong configuration.
- Prefer `ambiguous`, which asks a person, over guessing.
- No load switching and no CT orientation requirement.
- Tell a configuration error from a measurement fault.
- Keep checking after setup.

**Non-Goals:**
- Absolute phase identity.
- Automatic phase changes.
- Grid-step disambiguation of `ambiguous` lines (a possible later on-device addition).
- Single channels.
- Cloud-side display (a separate `energyme-infra` change).

## Decisions

### D1. The fundamental limit, and why "highest PF" is wrong
For one line there are 48 hypotheses: 6 permutations times 8 reverse sets. They fall into 8 classes with identical pair differences. Within a class the members differ by uniform shifts of 60 degrees: a cyclic relabel is ±120, flipping all three is 180, and the two combined give ±60.

The pair test therefore fixes the class. Inside it, the configured load angle φ is known only up to 60 degrees. Two of those shifts typically fall inside any plausible load band (-50 to +75). For example, "labels right, one CT reversed" and "labels cyclically rotated, two CTs reversed" are indistinguishable from angles alone.

Ranking by PF picks the shift closest to 0. That is wrong for every inductive load above 30 degrees, and for capacitive feeders below -30.

### D2. Evidence
**Tap point.** Evidence is tapped in `_readMeterValues` right after the RMS witness check, on `rawActiveEnergy` and `rawReactiveEnergy`. That is before `toChannelFrame`, the off-phase no-load gate, the `MINIMUM_POWER_FACTOR` zeroing and the clamp: all of these depend on the configured phase.

**Gate.** Raw current ≥ 3% of the CT rating. At the conducting gate (0.03%) the angle is offset noise, and PV standby at night reads about -90 degrees.

**Weighting.** Each reading adds `(P1 + jQ1) × deltaMillis / windowMillis`, i.e. power × elapsed time. This matches the existing energy accounting, so channel 0 (read every window) and multiplexed channels accumulate alike, and a double-serviced window is not counted twice. Accumulators are `double`, for the same reason as the energy counters.

**Signed roles.** They also keep the doubled-angle sum `(P1 + jQ1)² / |S1|`.

**Pairs.** At each minute boundary, for each pair of a line in which both channels passed the gate in that minute, the minute phasors give `D_ab += min(|Sa|, |Sb|) × e^{j(θa - θb)}` (doubled angles for signed roles).

**Decay.** Half-life 3 days.

**Moved CT.** A channel whose last-hour angle differs from `arg(E)` by more than 60 degrees resets its own and its pairs' evidence.

**RAM.** Per line channel: 2 × complex double plus counters. Per line: 3 pairs. Under 1 KB for 16 channels.

### D3. Decision procedure (as in the reference model)
Constants:

| Constant | Value |
|---|---|
| `PAIR_TOL` | 40° |
| Load / grid band | -50 to +75° |
| PV band | -65 to +35° |
| PV target | -10° |
| PV margin | 15° |
| Phase-change margin | 2 |
| Minimum per channel | 50 Wh |
| Minimum per pair | 60 co-conducting minutes |

1. Any channel under 50 Wh, or no pair evaluable: `collecting`.
2. The configuration passes the pair test and the band test: `verified`.
3. The configuration passes the pair test and reads positive power on every channel, but fails the band: `ambiguous`, offering the configuration plus the consistent alternatives. A uniform error and an unusual load, such as low-PF lighting, look the same. Field case: a lighting feeder at 53-75 degrees. A configuration reading negative power is a plain error and goes on to step 4.
4. Otherwise, collect the consistent hypotheses. If there are none: `inconsistent`.
5. Tie-break 1, labels over orientation (Jibril, 2026-11-11): keep the hypotheses that change the fewest labelled phases when every other changes at least 2 more. Reverse flags never count. This resolves the common single errors (one or all CTs reversed, a transposition) on its own. Accepted limit: a line whose labels are cyclically rotated reads like a reverse pattern 60 degrees away and can get a wrong reverse-only fix. Tried and rejected: counting reverse changes too (wrong when orientation errors are frequent), and a "frame confirmed by another verified line" guard (it rules out a wrong voltage phase, not a rotation within one line).
6. PV only: keep the hypothesis whose mean is closest to -10 degrees, with a 15-degree margin. Loads get no prior: the field spans -35 (capacitive feeders) to +60 (motors), and every load prior tried proposed wrong fixes.
7. One hypothesis left: `mismatch`, with `reverse_only` when it keeps every phase. Otherwise: `ambiguous`.

Signed roles run steps 2-4 on doubled angles over the 6 permutations only, keeping the configured reverse flags.

### D4. Reverse-only auto-apply
A `mismatch` that only flips reverse flags is what the detector does today. The difference is that it is now proven by the whole line, and only as the unique consistent hypothesis, so the bands have already excluded the 60-degree alias. The device applies it through the same path as an explicit apply and raises `ct_polarity_flipped`. In the Monte Carlo every reverse-only `mismatch` was correct. An `ambiguous` line is never changed by the device.

### D5. Line detection and atomic apply
- Lines are detected from the existing configuration: group label, role, active, non-split, three different phases. No new channel setting.
- Once detected, a line is keyed on its group label and channel set, so phase writes (even one channel at a time, as the `channels` shadow applies them) never dissolve it; the "three different phases" test applies only to forming a line.
- Apply holds `_configMutex`, then `_channelDataMutex` (the `add-voltage-phase` D4 order). It checks the generation, writes the three channels' phase and reverse in memory, persists them, rolls back on any NVS failure, and re-derives once.
- The `channels` shadow path applies channel by channel and so can pass through a transient configuration. Line identity does not depend on phases, so nothing is lost; the next minute's evaluation simply sees the final state.

### D6. Detector gating
- Gated channels: load/PV channels of a line that is not `inconsistent`. Lines start `collecting`, so the detector is off from the moment a line forms, which is the window where it used to make the 60-degree errors. An `inconsistent` line (unrelated loads sharing a label) gives its channels back to the detector.
- `setChannelData` clears `_pendingPolarityCheck` and the vote/read counters for gated channels.
- `_rearmPolarityOnVoltagePhaseChangeLocked` skips them (MODIFIED `voltage-phase` requirement).
- The runtime vote ignores them. The WDRR armed boost (`_recalculateWeights`) never sees an arm on them.
- `channel_polarity_mismatch` is suppressed per the spec.

### D7. Issues
- Codes: `channel_phase_suspect`, `channel_phase_ambiguous` (Warning) and `channel_readings_inconsistent` (Info).
- Their own streaks count solver evaluations, 3 to raise and 3 to clear, not 5-second ticks.
- Messages use short form within 192 bytes, e.g. `ch3-5 L1<->L3, ch4 rev`. The full candidates come from REST.
- Evidence is RAM-only and the registry is RAM-only: after a reboot a line re-raises once its evidence is back. This is documented, not avoided.

### D8. Validation done before this design was fixed
**Synthetic tests.** Truth phases with random CT orientation (20/50/80% reversed), random label errors (transpositions, cycles, none), detector-made or default reverse flags, dedicated and mixed loads, and load angles from -35 to +60 (PV -30 to +5).
- Before the fixes, every earlier rule produced wrong proposals: 1-13% with label-keeping or change-count tie-breaks counted on reverse flags, and wrong proposals for capacitive and motor loads with a load prior.
- With D3: 0 wrong proposals for every error except cyclically rotated labels, in 1,200 random installs (20/50/80% of CTs reversed, 15% rotated labels, 35% transpositions). 63-71% of the erroneous installs resolve to a single `mismatch`; the rest are `ambiguous`, always containing the truth, almost always as 2 options.
- Rotated labels, the accepted limit: about a third get a wrong reverse-only fix, the rest are `ambiguous` or correct.
- "False verified" (a configuration exactly 60 degrees uniformly off) is 4-7% of installs. Most are made by the old detector, which the line marking switches off. A remainder of up to 3% comes from all three CTs reversed plus a cyclic relabel, which no angle rule can see.

**Fleet replay.** 61 three-phase groups, up to 3 days of prod data per device, restricted to the minutes after each device's last configuration write (the cloud keeps no reverse history), rebuilt to the raw frame from the stored power, PF sign and configuration.
- 23 verified, including the known-correct chillers, compressors, ovens, grids and PV, and the corrected 2.8.1 field site after its fix.
- 3 inconsistent: two genuinely unbalanced mixed feeders, and the site with known multiplexer crosstalk.
- 1 ambiguous: the low-PF lighting feeder.
- 0 mismatch proposals; 33 collecting.
- The one grid known to be misconfigured stays `collecting` on a single evaluable pair. Requiring two pairs is what stopped an earlier replay from calling it verified.
- Limit: the cloud has no samples for clamped channels, so the pre-fix field site and lines with clamped legs could not be replayed. That case needs the bench.

**Release gate.** A bench line on the dev meter with known swaps, one reversed CT, all reversed and a cyclic relabel must give exactly the states and proposals of the reference model. A debug dump of the raw accumulators (`GET .../phase-check` includes them) is compared with the model fed the same minute data. All fixtures in the repo are synthetic.

### D9. Cloud default
- The static `_cloudServicesEnabled` stays false.
- In `_loadConfigFromPreferences`, the `getBool` default becomes `!globalCommunityMode`, and the unconditional `_saveConfigToPreferences()` after load no longer writes `en_cloud` when the key is absent.
- `setCloudServicesEnabled` always persists an explicit call.
- Community mode is known before MQTT loads. Mqtt never starts in community mode anyway.

## Risks / Trade-offs

- **Most erroneous load lines land in `ambiguous`** → the setup page shows both candidates with the PF and power each would read, and a person picks; the cloud issue carries them too. Resolving them automatically needs more than angles: grid-step correlation, or an assumption that installers never rotate labels within one line (a product decision, not a math one).
- **Unbalanced mixed feeders read `inconsistent`** → Info only, no proposal.
- **The 3% gate delays evidence on lightly loaded lines** → `collecting` is harmless. The same gate keeps noise out.
- **Cloud on by default for new devices only** → matches "from now on"; the existing fleet keeps its stored value.

## Migration Plan

1. Firmware release. Lines are detected on existing devices from their group labels at the first boot. Nothing is changed automatically except unique reverse-only fixes (D4), which is what the detector does today.
2. After the OTA, every `channel_phase_suspect` and `channel_phase_ambiguous` raised in the fleet is triaged by a person before any phase is changed, with the owner's consent.

Rollback: older firmware ignores the line id and the setup flag.

## Open Questions

- None blocking. `add-voltage-phase` is archived (2026-11-11) and this change carries MODIFIED deltas for its re-arm and channel page requirements.
