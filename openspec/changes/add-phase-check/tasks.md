## 0. Prerequisites

- [x] 0.1 Archive `add-voltage-phase` (2026-11-11); this change carries MODIFIED deltas of `voltage-phase`

## 1. Reference model (done)

- [x] 1.1 `source/utils/phase_solver_ref.py`: evidence, pair accumulation, 48-hypothesis evaluation, states `collecting`/`verified`/`fix`/`choose`, ranking (D4)
- [x] 1.2 `source/utils/test_phase_solver_ref.py`: synthetic cases, randomised no-false-alarm and truth-offered tests, three-minute noisy test
- [x] 1.3 Fleet replay (prod, read-only, not committed): results in design D10
- [x] 1.4 Single-phase channel check in the reference model

## 2. Pure solver library (host-tested)

- [x] 2.1 `source/lib/phase_check/phase_check.h/.cpp`: line detection, hypothesis evaluation, ranking, states, two-agreeing-evaluations filter, single-phase check; constants as in the reference model
- [x] 2.2 `source/test/test_phase_check`: port every reference test (synthetic fixtures only); `pio test -e native` from WSL
- [x] 2.3 A fixture generator script that emits the reference model's results as C arrays, so the two cannot drift

## 3. Meter integration

- [x] 3.1 Check window (D2): open while setup is not done or for 2 hours after `setup/start`; closed by `setup/complete`, timeout, reboot of a `setup/start` window; evidence dropped at close
- [x] 3.2 Line detection from group label, role, active, non-split and three phases; identity keyed on label + channel set
- [x] 3.3 Evidence tap after the RMS witness in `_readMeterValues` (raw pair x deltaMillis, double, 3% gate, signed axial, pre-clamp RMS current), minute pairs; only inside the window
- [x] 3.4 Evaluation at each closed minute and after any configuration write to a line, in the meter task; snapshot with generation under `_channelDataMutex`
- [x] 3.5 Detector hold (D6): no flip of load/PV channels on three-phase devices while the window is open; arms and counters kept
- [x] 3.6 Atomic line apply with rollback (D5); generated labels follow the phase
- [x] 3.7 Energy reset of every channel on the first `setup/complete`, before cloud services start
- [ ] 3.8 RAM and meter-loop timing before/after (task profiler)
- [x] 3.9 Keep V1 unscaled and apply the split-phase 2x to a channel's own readings only; drop the channel 0 split-phase coercion, validators and channel page filter

## 4. REST

- [x] 4.1 `GET /api/v1/ade7953/phase-check` (window flag, per-line generation, currents, candidates with readings, single-phase checks), `POST .../apply` (channel-keyed, serialized, idempotent); `GET /api/v1/setup/status`, `POST /api/v1/setup/start`, `POST /api/v1/setup/complete`; error envelope; swagger, api-client, mock server

## 5. Web UI

- [x] 5.1 `/setup` page from the prototype in `source/html/setup.html` (cloud, supply, circuits, phase check), reusing shared styles, toasts and tooltips; skip link; resume on the phase check
- [x] 5.2 `/` fourth twin with its ETag variant; setup-done flag in NVS, set on upgrade when the first-boot flag or stored energy shows a device in service, restored with backups
- [x] 5.3 Configuration page: "Run setup again" calls `setup/start` and opens `/setup`
- [x] 5.4 Channel page: per-phase preview with no best row
- [x] 5.5 Info icon more visible on every page (`css/tooltip.css`)

## 6. Cloud default

- [x] 6.1 `getBool` default `!globalCommunityMode`, no write-back of an absent key, explicit setter always persists; static stays false
- [x] 6.2 Check that the LAN gateway and tunnel defaults stay off
- [x] 6.3 Hold cloud services until setup completes or is skipped; `setup/complete` stores the choice and starts them

## 7. Review and verification

- [ ] 7.1 Code-review agents on the diff (one adversarial on the tap point, locking and apply rollback) plus a simplification agent; triage every finding
- [ ] 7.2 Bench gate: known swaps, one reversed CT, all reversed, cyclic relabel, one CT unclamped; states and proposals equal the reference model fed the dumped minute data
- [ ] 7.3 Cloud default on the dev meter: fresh NVS on, older-firmware NVS kept, community off
- [ ] 7.4 Manual: setup page and line detection; three-phase section rewritten
- [ ] 7.5 Release notes: setup page, cloud on for new devices, energy reset at first setup, detector hold during setup

## 8. Rollout

- [ ] 8.1 Open the `energyme-infra` change for the cloud-side check over days of readings
