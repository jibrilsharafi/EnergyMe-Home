## Why

Three-phase installs keep going live with the wrong phase or reverse on some channels, and nothing on the device notices. The meter has one voltage input, so a channel's phase is pure math on the angle between V1 and its current, and every wrong choice is plausible on its own: a CT on the wrong line reads 120 degrees off, the CT-reversal detector then flips `reverse` to make the power positive, and the channel lands 60 degrees off with a believable but wrong power factor. Where the reading still goes negative, the load/PV clamp writes 0 W. Field case on 2.8.1: a three-phase PV inverter whose third phase read 0 W at noon and a plant room whose second phase read 0 W for a day, from CTs labelled L1 and L3 that sit on each other's line, with `reverse` set by the detector on four of six channels.

The channel page's phase preview does not prevent it: it ranks candidate phases by |PF|, and with `reverse` free the candidates are 60 degrees apart, so an inductive load above about 30 degrees has a wrong row with a higher |PF| than the right one. Installers will keep getting CT orientation wrong and cannot be asked to switch loads on. The installer is on site for about half an hour, so the check has to answer in minutes, while they are still there.

## What Changes

- **Setup page `/setup`** after the existing Wi-Fi and password steps: cloud (on, local-only option), supply (single, three or split-phase), circuits (name, role, CT size, channels), and for three-phase installs a phase check of every circuit. Skip link; "Run setup again" from the configuration page; the page resumes on the phase check when reopened.
- **Three-phase lines detected automatically**: three active channels with the same group label and role on three different phases. No new setting.
- **Phase check during setup only**: raw-frame evidence (the latched V1-to-current pair before reverse, rotation and clamp) per channel and per pair of channels, from the moment the circuits are saved until setup finishes. Answers in minutes (median 3 on the fleet):
  - `collecting`, with each channel's current shown (taken before the clamp, so a reversed CT still shows it, and an unclamped CT stands out);
  - `verified`;
  - `fix`: only reverse flags change, proposed alone;
  - `choose`: any phase change, two or three candidates, most likely first, the installer picks.
- **Nothing is applied by the device**, and no issue is raised. The installer applies a proposal or not. Checking installs over days is left to the cloud, which already has the readings.
- **Single-phase channels on three-phase installs** get the same advisory check: a wrong phase is flagged with the options that fit.
- **The CT-reversal detector does not flip channels during the check** on three-phase devices (its flips made the 60-degree errors); it resumes after setup.
- **Energy counters reset at the end of the first setup**, so the install starts from zero.
- **REST** `GET /api/v1/ade7953/phase-check`, `POST .../phase-check/apply` (atomic, generation-checked, idempotent), `GET /api/v1/setup/status`, `POST /api/v1/setup/start`, `POST /api/v1/setup/complete`.
- **Split-phase 240 V allowed on channel 0**: V1 stays unscaled.
- **BREAKING (new devices only): cloud services on by default** outside community mode when no value is stored, started only once setup completes. Devices that already booted older firmware keep their stored value. The LAN gateway and tunnel stay opt-in.

## Capabilities

### New Capabilities
- `phase-check`: check window, line detection, raw-frame evidence, line states, hypotheses and proposals, single-phase check, REST, setup page.
- `cloud-services-default`: cloud on by default for factory-fresh non-community devices, held until setup completes, stored value wins, remote access stays opt-in.

### Modified Capabilities
- `meter-reading-integrity`: no sign-vote flips during the phase check on three-phase devices; V1 never scaled by a channel's split-phase multiplier.
- `voltage-phase`: channel page preview no longer ranks rows; migration keeps channel 0's split-phase value out of the voltage phase without coercing the channel; channel 0 may be split-phase 240 V.

## Impact

- **Firmware**:
  - `source/lib/phase_utils/` (new pure solver, host-tested);
  - `source/include/ade7953.h` and `source/src/ade7953.cpp` (check window, line detection, evidence tap after the RMS witness, minute evaluation, detector hold, atomic line apply, V1 unscaled, energy reset);
  - `source/src/customserver.cpp` (endpoints, the `/` setup twin, setup flag);
  - `source/src/mqtt.cpp` (default without write-back, held until setup completes);
  - `source/html/` (setup page; channel page; configuration page link), `source/css/tooltip.css`;
  - `source/js/api-client.js`, `source/resources/swagger.yaml`, `source/test/`.
- **Builds on `voltage-phase`** (2.8.0, archived 2026-11-11).
- **Memory**: under 1 KB, only while the check window is open; no new task.
- **Cloud**: nothing required. A later `energyme-infra` change can run the same math over days of readings.
- **Non-goals**: checking after setup on the device; absolute phase identity (needs more voltage inputs); grid-step disambiguation.
