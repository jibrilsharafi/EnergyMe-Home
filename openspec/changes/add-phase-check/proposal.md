## Why

Three-phase installs keep going live with the wrong phase or reverse on some channels, and nothing on the device notices. The meter has one voltage input, so a channel's phase is pure math on the angle between V1 and its current, and every wrong choice is plausible on its own: a CT on the wrong line reads 120 degrees off, the CT-reversal detector then flips `reverse` to make the power positive, and the channel lands 60 degrees off with a believable but wrong power factor. Where the reading still goes negative, the load/PV clamp writes 0 W. Field case on 2.8.1: a three-phase PV inverter whose third phase read 0 W at noon and a plant room whose second phase read 0 W for a day, from CTs labelled L1 and L3 that sit on each other's line, with `reverse` set by the detector on four of six channels.

The channel page's phase preview does not prevent it: it ranks candidate phases by |PF|, and with `reverse` free the candidates are 60 degrees apart, so an inductive load above about 30 degrees (chillers and compressors read 35-50 degrees in the field) has a wrong row with a higher |PF| than the right one. Installers will keep getting CT orientation wrong and cannot be asked to switch loads on, so the check has to run on the device, during setup and afterwards, without blocking anyone.

## What Changes

- **Three-phase lines detected automatically**: three active channels with the same group label and role on three different phases. No new setting for the installer.
- **Phase check on the device**: per line channel, raw-frame evidence (the latched V1-to-current pair before reverse, rotation and clamp, time-weighted, decaying) and per pair of channels the co-conducting angle difference. From it each line gets a state:
  - `collecting`;
  - `verified`: the configuration is consistent; it wins whenever it is, even against a higher-PF alternative;
  - `mismatch`: a single fix;
  - `ambiguous`: two or three candidates the angles cannot separate, offered to a person;
  - `inconsistent`: no configuration makes the phases agree, as with an unbalanced feeder or a measurement fault.
- **Validated before writing firmware**: the math is pinned by a Python reference model (`source/utils/phase_solver_ref.py`) and synthetic tests. On randomised installs it never produced a wrong proposal (design D8).
- **Labels are tested, not overwritten.** Phase changes need an explicit apply. The device applies alone only a reverse-only fix that is the unique consistent hypothesis, which is what the CT-reversal detector does today, now proven by the whole line.
- **The CT-reversal sign vote no longer runs on load/PV channels of a line** (it caused the 60-degree errors), except while the line is `inconsistent`; single channels keep it.
- **Labels over orientation**: when the angles admit both, the device prefers the fix that keeps the labelled phases (one CT reversed beats rotated labels plus two reversed CTs). Accepted limit: cyclically rotated labels can get a wrong reverse-only fix.
- **New issues**: `channel_phase_suspect`, `channel_phase_ambiguous` (Warning), `channel_readings_inconsistent` (Info). They reach the cloud through the existing `issues` shadow; a fix can be applied remotely through the `channels` shadow.
- **REST** `GET /api/v1/ade7953/phase-check`, `POST .../phase-check/apply` (atomic, generation-checked, idempotent), `POST .../phase-check/reset`.
- **Setup page `/setup`** after the existing Wi-Fi and password steps: cloud (on, local-only option), voltage phase, channels with line marking, live line check with one-tap apply. A future app is another client of the same REST. The channel page preview stops ranking rows.
- **BREAKING (new devices only): cloud services on by default** outside community mode when no value is stored. Devices that already booted older firmware keep their stored value. The LAN gateway and tunnel stay opt-in.

## Capabilities

### New Capabilities
- `phase-check`: line detection, raw-frame evidence, line states, hypotheses and tie-breaks, the label rule and the reverse-only auto-apply, the issues, the REST interface, the setup page.
- `cloud-services-default`: cloud on by default for factory-fresh non-community devices, stored value wins, explicit choices always stored, remote access stays opt-in.

### Modified Capabilities
- `meter-reading-integrity`: an ADDED requirement keeps the sign-vote detector off line load/PV channels unless the line is `inconsistent`.
- `voltage-phase`: MODIFIED "Changing the voltage phase re-arms CT polarity detection" (line channels left to the phase check) and "Web UI channel page uses the voltage phase as its base" (line state instead of preview; preview rows no longer ranked).

## Impact

- **Firmware**:
  - `source/lib/phase_utils/` (new pure solver, host-tested);
  - `source/include/ade7953.h` and `source/src/ade7953.cpp` (line detection, evidence tap after the RMS witness, minute evaluation, detector gating including the deferred voltage phase re-arm, atomic group apply);
  - `source/lib/issue_logic/` and `source/src/issueregistry.cpp`;
  - `source/src/customserver.cpp` (endpoints, the `/` setup twin);
  - `source/src/mqtt.cpp` (default without write-back, explicit writes always stored);
  - `source/html/` (new setup page; channel page);
  - `source/js/api-client.js`, `source/resources/swagger.yaml`, `source/test/`.
- **Builds on `voltage-phase`** (2.8.0, archived 2026-11-11).
- **Memory**: per line channel two double-complex accumulators plus counters, per line three pair accumulators; well under 1 KB static, no new task.
- **Cloud**: nothing required to receive the issues or apply a fix. Portal and backoffice display and one-tap apply are a separate `energyme-infra` change.
- **Non-goals**:
  - absolute phase identity (needs more voltage inputs);
  - automatic resolution of `ambiguous` lines from grid steps (a possible future cloud feature);
  - single channels (keep the detector and `channel_polarity_mismatch`);
  - automatic phase changes.
