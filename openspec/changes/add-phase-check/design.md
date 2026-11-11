## Context

The meter integrates every channel against one voltage input V1 on the stored voltage phase (`add-voltage-phase`, 2.8.0). For channel k the chip latches a raw energy pair (P1, Q1) per window. `PhaseUtils::toChannelFrame` negates the pair for `reverse` and rotates it by `angle(phase_k) - angle(voltagePhase)`. The load/PV clamp then zeroes negative results, current and energy included. The CT-reversal detector flips `reverse` after a net run of negative votes.

So a wrong phase (120 degrees) plus a detector flip (180 degrees) leaves a channel 60 degrees off, with positive power and a plausible PF.

The normative model of every decision below is `source/utils/phase_solver_ref.py`, and its cases are `source/utils/test_phase_solver_ref.py`. The firmware solver must reproduce both.

## Goals / Non-Goals

**Goals:**
- An answer while the installer is still on site: minutes, not hours. Fast and sometimes wrong is better than slow (Jibril, 2026-11-11): a slow check belongs in the cloud.
- Never apply anything on the device's own initiative.
- Never flag a correct install.
- Never offer a phase change as the only answer.
- No load switching and no CT orientation requirement.

**Non-Goals:**
- Checking after setup. The cloud already has the readings and can run the same math over days (`energyme-infra`, later). The installer stays at most about half an hour, and a one-hour confirmation on the device would only add auto-fixes and issues nobody watches.
- Absolute phase identity.
- Grid-step disambiguation of `choose` lines.

## Decisions

### D1. The fundamental limit, and why "highest PF" is wrong
For one line there are 48 hypotheses: 6 permutations times 8 reverse sets. They fall into 8 classes with identical pair differences. Within a class the members differ by uniform shifts of 60 degrees: a cyclic relabel is ±120, flipping all three is 180, and the two combined give ±60.

The pair test therefore fixes the class. Inside it, the configured load angle φ is known only up to 60 degrees. Two of those shifts typically fall inside any plausible load band (-50 to +75). For example, "labels right, one CT reversed" and "labels cyclically rotated, two CTs reversed" are indistinguishable from angles alone.

Ranking by PF picks the shift closest to 0. That is wrong for every inductive load above 30 degrees, and for capacitive feeders below -30. The ranking used here (D4) targets a typical angle instead, and only orders a choice.

### D2. Check window
- Open while setup was never completed, and for 2 hours after `POST /api/v1/setup/start` ("Run setup again"); closed by `setup/complete` (finish or skip), by the 2 hours, and by a reboot for a `setup/start` window. The first-setup window survives reboots because it follows the setup-done flag.
- Outside the window: no evidence, no evaluation, no CPU. The window bounds the RAM and the detector hold (D6).
- Closing drops all evidence. Nothing is applied and no issue is raised at close.

### D3. Evidence
**Tap point.** In `_readMeterValues` right after the RMS witness check, on `rawActiveEnergy` and `rawReactiveEnergy` and the RMS current. That is before `toChannelFrame`, the off-phase no-load gate, the `MINIMUM_POWER_FACTOR` zeroing and the clamp: all of these depend on the configured phase.

**Gate.** Per minute: average raw apparent power ≥ 3% of the CT rating × 230 V (the reference model's gate). At the conducting gate (0.03%) the angle is offset noise, and PV standby at night reads about -90 degrees.

**Weighting.** Each reading adds `(P1 + jQ1) × deltaMillis / windowMillis`, i.e. power × elapsed time, so channel 0 (read every window) and multiplexed channels accumulate alike. Accumulators are `double`.

**Signed roles.** They also keep the doubled-angle sum `(P1 + jQ1)² / |S1|`.

**Pairs.** At each minute boundary, for each pair of a line in which both channels passed the gate in that minute, the minute phasors give `D_ab += min(|Sa|, |Sb|) × e^{j(θa - θb)}` (doubled angles for signed roles).

**No decay.** The check lasts minutes; plain sums.

**Current.** The latest pre-clamp RMS current per checked channel, for the page's waiting view. The stored reading of a reversed load CT is 0 A (the clamp zeroes current too, `ade7953.cpp` around line 4261), so the page would otherwise read a reversed CT as an unclamped one.

**RAM.** Per checked channel: 2 × complex double, a current, counters. Per line: 3 pairs. Under 1 KB for 16 channels, only inside the window.

### D4. Decision procedure (as in the reference model)

| Constant | Value |
|---|---|
| `PAIR_TOL` | 40° |
| Load / grid band | -50 to +75° |
| PV band | -65 to +35° |
| Typical angle (ranking) | load, grid 20°; PV -10° |
| Minimum per channel | 1 Wh |
| Minimum per pair | 2 co-conducting minutes, on 2 pairs |
| Agreeing evaluations | 2 |

1. Any channel under 1 Wh, or fewer than two evaluable pairs: `collecting`.
2. The configuration passes the pair test and the band test: `verified`.
3. Collect the fitting hypotheses. None: `collecting` (an unbalanced feeder, crosstalk, or a CT on the wrong cable; the page's current view shows which). Rank them by distance of the mean angle to the typical angle.
4. The configuration's pairs agree, every channel reads positive power, only the band fails: `choose` with the configuration first, then the ranked hypotheses. A uniform error and an unusual load (the field's low-PF lighting feeder at 53-75 degrees) look the same, so the configuration keeps the default position.
5. Exactly one fitting hypothesis keeps every configured phase and it ranks first: `fix`, alone.
6. Otherwise: `choose`, ranked.

Evaluated at each closed minute and at once after a configuration write. A state is shown once two consecutive minute evaluations agree.

Signed roles run steps 1-3 and 6 on doubled angles over the 6 permutations only, keeping the configured reverse flags.

Earlier rules, tried and replaced:
- "Fewest phase changes wins, by a margin of 2" (labels over orientation) gave single proposals for swaps but a wrong reverse-only fix for 37% of rotated labels. Rule 5 keeps the reverse-only case single only when the ranking agrees.
- A single proposal only when it is the only fitting hypothesis: almost never, because the 60-degree alias of a reverse-only fix usually fits too.
- Putting the configuration first in step 4 only when it lags: 3 points better on rotated labels, worse on correct installs. Correct installs win.

### D5. Line detection and atomic apply
- Lines are detected from the existing configuration: group label, role, active, non-split, three different phases. No new channel setting.
- Once detected, a line is keyed on its group label and channel set, so phase writes (even one channel at a time) never dissolve it; the "three different phases" test applies only to forming a line.
- Apply holds `_configMutex`, then `_channelDataMutex` (the `add-voltage-phase` D4 order). It checks the generation, writes the three channels' phase and reverse in memory, persists them, rolls back on any NVS failure, and re-derives once.
- Generated labels (`<group> L<n>`) follow the new phase; any other label is kept.

### D6. Detector hold
- On a device with three-phase channels, no sign-vote flip of a load/PV channel while the window is open: a wrong phase reads negative, and a flip makes it a 60-degree error the check can no longer see (the single-phase replay showed half the wrong phases hidden this way, the Roncade sequence). The candidates are on the page instead.
- Arms and counters are kept, so the detector resumes at close; a still-wrong channel is then handled as today.
- While held, an armed channel gets no WDRR boost (it cannot vote) and its clamped readings are not counted as `channel_polarity_mismatch` evidence.
- Single-phase devices keep the detector unchanged.

### D7. Energy reset at first setup
Completing or skipping the first setup resets the energy counters of every channel, before cloud services start (`cloud-services-default`), so the cloud never sees a drop. Bench tests and a wrong configuration during the visit leave nothing behind. "Run setup again" never resets energy.

### D8. Validation of the angle math
**Synthetic tests.** Truth phases with random CT orientation, random label errors (transpositions, cycles, none), detector-made or default reverse flags, dedicated and mixed loads, load angles from -35 to +60 (PV -30 to +5). With the current rules: no correct install flagged, the truth always offered, a single fix wrong only for rotated labels.

**Fleet replay.** 61 three-phase groups, up to 3 days of prod data per device, rebuilt to the raw frame from the stored power, PF sign and configuration. 23 lines verify on days of data with the old long thresholds (50 Wh, 60 co-conducting minutes); their wiring is taken as known. Limit: the cloud has no samples for clamped channels, so lines with clamped legs could not be replayed. That case needs the bench.

### D9. Cloud default
- The static `_cloudServicesEnabled` stays false.
- In `_loadConfigFromPreferences`, the `getBool` default becomes `!globalCommunityMode`, and the unconditional `_saveConfigToPreferences()` after load no longer writes `en_cloud` when the key is absent.
- `setCloudServicesEnabled` always persists an explicit call.
- Held until setup completes or is skipped, so no update restarts the meter mid-setup.

### D10. Speed and accuracy on the fleet
The 22 of those lines with enough working-hour data. Each was configured in every way it could be got wrong, and the installation started at 10 random working-hour minutes; the check ran for up to 60 minutes with the D4 constants. Script `quick3.py`, read-only, not committed.

| Configured | Right answer alone | Choose, truth first | Choose, truth lower | Truth missing | Wrong | Still measuring at 60 min |
|---|---|---|---|---|---|---|
| As wired | 92.7% verified | 1.8% (configuration) | 0% | 0% | 0% | 5.5% |
| CTs reversed (1, 2 or 3) | 80.0% fix | 0% | 11.9% | 1.9% | 0% | 6.2% |
| Two labels swapped | 0% | 79.5% | 12.4% | 1.9% | 0% | 6.2% |
| Labels rotated | 0% | 70.8% | 10.2% | 0.9% | 6.0% fix, 5.9% verified | 6.1% |

- Time to an answer: median 3 minutes, p90 25 minutes. The wait is for the load to run.
- Choices have 2 options (median), 3 at most.
- "Verified" on rotated labels is rotated labels plus every CT reversed: a uniform 60-degree shift no angle rule can see.
- The ranking is in-sample (the typical angle was chosen on these lines). On synthetic loads spread evenly from -35 to 60 degrees, the truth comes first about 60% of the time. Real loads cluster, so the order helps, but the installer chooses.

The single-phase check, on the same channels taken one at a time: no false alarm on correct channels; a wrong phase flagged 96% of the time, with the truth first 82% of the time; a wrong phase plus a reversed CT passes unnoticed half the time (the 60-degree case).

**Data limits:** the cloud data keeps only positive power samples (the load clamp). The device sees the raw frame, so the numbers should be no worse there.

## Risks / Trade-offs

- **A wrong choice made on site** → the configuration is still the installer's; the cloud check can catch it later from the readings.
- **Rotated labels plus every CT reversed read as verified** → no angle rule can see it; the cloud check over days with grid steps can.
- **No load during the visit** → nothing is checked on the device; the cloud check covers it.
- **Cloud on by default for new devices only** → the existing fleet keeps its stored value.

## Migration Plan

1. Firmware release. Upgraded devices (the first-boot flag set by earlier firmware, or energy already stored) count as set up: no window, no check, no energy reset, detector unchanged.
2. Cloud-side checking is a separate `energyme-infra` change.

Rollback: older firmware ignores the setup flag.

## Open Questions

- None blocking.
