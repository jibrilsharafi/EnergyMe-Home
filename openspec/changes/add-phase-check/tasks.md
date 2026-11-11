## 0. Prerequisites

- [x] 0.1 Archive `add-voltage-phase` (2026-11-11); this change carries MODIFIED deltas of `voltage-phase`

## 1. Reference model (done)

- [x] 1.1 `source/utils/phase_solver_ref.py`: evidence, pair accumulation, 48-hypothesis evaluation, states, tie-breaks (D3)
- [x] 1.2 `source/utils/test_phase_solver_ref.py`: synthetic cases and a randomised no-wrong-proposal test
- [x] 1.3 Fleet replay (prod, read-only, not committed) and Monte Carlo: results in design D8

## 2. Pure solver library (host-tested)

- [ ] 2.1 `source/lib/phase_utils/phase_check.h/.cpp`: line detection, hypothesis evaluation, tie-breaks, states, short-form message; constants as in the reference model
- [ ] 2.2 `source/test/test_phase_check`: port every reference test (synthetic fixtures only); `pio test -e native` from WSL
- [ ] 2.3 A fixture generator script that emits the reference model's results as C arrays, so the two cannot drift

## 3. Meter integration

- [ ] 3.1 Line detection from group label, role, active, non-split and three phases; identity keyed on label + channel set
- [ ] 3.2 Evidence tap after the RMS witness in `_readMeterValues` (raw pair x deltaMillis, double, 3% gate, signed axial), minute pairs, decay, moved-CT reset
- [ ] 3.3 Minute evaluation in the meter task; snapshot with generation under `_channelDataMutex`
- [ ] 3.4 Detector gating (D6) including `_rearmPolarityOnVoltagePhaseChangeLocked` and the WDRR boost
- [ ] 3.5 Atomic line apply with rollback (D5) and reverse-only auto-apply (D4)
- [ ] 3.6 RAM and meter-loop timing before/after (task profiler)

## 4. Issues

- [ ] 4.1 Three codes in `issue_logic` with evaluation-counted streaks; `channel_polarity_mismatch` suppression; tests

## 5. REST

- [ ] 5.1 `GET /api/v1/ade7953/phase-check`, `POST .../apply`, `POST .../reset`; error envelope; swagger, api-client, mock server

## 6. Web UI

- [ ] 6.1 `/setup` page (cloud, voltage phase, channels with group labels, live check of detected lines grid-first with per-candidate PF/power and apply), offline
- [ ] 6.2 `/` fourth twin with its ETag variant; setup-done flag in NVS, set on upgrade when the password is non-default, restored with backups
- [ ] 6.3 Channel page: line state and candidates for line channels; no best row for others

## 7. Cloud default

- [ ] 7.1 `getBool` default `!globalCommunityMode`, no write-back of an absent key, explicit setter always persists; static stays false
- [ ] 7.2 Check that the LAN gateway and tunnel defaults stay off

## 8. Review and verification

- [ ] 8.1 Code-review agents on the diff (one adversarial on the tap point, locking and apply rollback) plus a simplification agent; triage every finding
- [ ] 8.2 Bench gate (D8): known swaps, one reversed CT, all reversed, cyclic relabel, crosstalk load; states and proposals equal the reference model fed the dumped minute data
- [ ] 8.3 Cloud default on the dev meter: fresh NVS on, older-firmware NVS kept, community off
- [ ] 8.4 Manual: setup page and line detection; three-phase section rewritten
- [ ] 8.5 Release notes: cloud on for new devices, detector off for line channels, new issues

## 9. Rollout

- [ ] 9.1 After the OTA, triage every `channel_phase_suspect` and `channel_phase_ambiguous` in the fleet; phase changes only with the owner's consent
- [ ] 9.2 Open the `energyme-infra` change for portal/backoffice display and one-tap apply
