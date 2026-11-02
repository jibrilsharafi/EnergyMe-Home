## Why

The meter has one voltage input, V1, and every channel is integrated against it. The firmware takes the line V1 sits on from channel 0's phase (`basePhase = _channelData[0].phase`, `source/src/ade7953.cpp:3897` and the waveform JSON at `:5011`). Channel 0 is therefore assumed to be on V1's line: its own phase never rotates it (only `reverse` flips it), and changing channel 0's phase rotates every other channel instead. A meter whose channel 0 CT sits on another line cannot be configured correctly.

This happened in the field. On both a field site meters (meter A, meter B) channel 0's CT is on another line with reversed leads, so the whole meter reads 60 degrees off, and no configuration can turn channel 0 by 60 degrees. A remote fix that changed channel 0's phase on 2.7.0 moved the base together with every channel and turned both meters by 180 degrees (negative power for 24 minutes, reverted). The fix needs a voltage phase that does not depend on channel 0.

## What Changes

- New device-level setting, the **voltage phase**: the line V1 is wired to, `PHASE_1` to `PHASE_3` only. Split-phase 240 V is not a valid voltage phase. Stored as NVS key `voltage_phase` (u8) in `ade7953_ns`, default `PHASE_1`, with `Ade7953::getVoltagePhase()` / `setVoltagePhase()` modelled on the sample time. It is not part of `Ade7953Configuration`, so calibration PUTs, calibration resets and the meter shadow's calibration allow-list never touch it.
- Every channel, channel 0 included, is rotated from the voltage phase into its own line: `R_k = angle(phase_k) - angle(voltagePhase) + 180 * reverse_k` (split-phase channels take `180 * reverse_k` only, as today). Changing channel 0's phase now rotates channel 0 alone.
- **Migration**: on the first 2.8.0 boot, after the channels load and before the meter task starts, a missing key (or one outside 1..3) is written once with channel 0's (coerced) phase, so the upgrade changes no reading. From then on the stored key wins, including after a downgrade and re-upgrade and after restoring a configuration backup that lacks the key.
- Local REST `GET/PUT /api/v1/ade7953/voltage-phase` with body `{"voltagePhase": 1..3}`.
- Meter shadow key `voltage_phase` (snake_case, next to `sample_time`, outside `METER_CAL_KEYS`), accepted by `_applyMeter` and reported by `_reportMeter`.
- Configuration backup and restore carry the key (the NVS dump already covers every key in `ade7953_ns`). A backup with the key restores it; a backup without it leaves the stored value as is and never re-derives it (on a factory-reset device that value is `PHASE_1`, written by the first boot after the reset).
- Changing the voltage phase re-arms CT polarity detection on every active channel whose rotation changes (every active non-split channel), as a per-channel phase change already does.
- Waveform capture reports the voltage phase from the setting, and `phaseShiftDegrees` is 0 whenever the channel is not off-phase (today a split-phase channel shows +-120 against an L2/L3 base although the meter never rotates it).
- Web UI channel page takes its base from the voltage phase instead of channel 0, and channel 0 gets the same phase preview as every other channel.
- Channel 0 still cannot be split-phase 240 V. The reason changes: its reading supplies V1 (and the 2x multiplier would double the voltage every channel and integration reads), not that it defines the frame. Comments and log text are reworded.
- `swagger.yaml`, the manual and the dev tools follow. Firmware version 2.8.0.

## Capabilities

### New Capabilities
- `voltage-phase`: the device voltage phase setting, its range, storage and default, the one-time migration from channel 0 and the stored-key-wins rule, backup and restore, the REST interface, CT polarity re-arm on change, the waveform phase fields, the web UI base and the channel 0 split-phase rule.

### Modified Capabilities
- `meter-reading-integrity`: an ADDED requirement states the rotation of every channel, channel 0 included, from the voltage phase into its own line.
- `iot-device-shadows`: an ADDED requirement exposes `voltage_phase` as a writable field of the `meter` shadow. The shadow roles requirement is untouched (`add-lan-collector` renames it).

## Impact

- Firmware: `source/lib/phase_utils/phase_utils.h` (pure helpers), `source/include/ade7953.h` (key, default, getter/setter), `source/src/ade7953.cpp` (storage, migration in `begin()`, setter with re-arm, `_readMeterValues`, `getWaveformCaptureAsJson`, reworded channel 0 comments), `source/src/customserver.cpp` and `source/include/customserver.h` (endpoint), `source/src/shadow.cpp` (meter shadow), `source/html/channel.html`, `source/js/api-client.js`, `source/resources/swagger.yaml`, `source/test/test_phase_utils`, `source/utils/` dev tools (`mock_server.py`, `benchmark_api.py`, `shadow_cli.py`), `source/include/constants.h` (2.8.0).
- Manual: the passages that require channel 0 on the device's own line become false (`manual/01-installation.md` §3.3 note and three-phase warning and its checklist line, `manual/02-setup.md` channel 0 note, `manual/appendices.md` B.1 warning). They change to: channel 0's CT may sit on any line, phases are labelled relative to the line the device's own L wire is on, and the voltage phase is set through REST or the `meter` shadow.
- API meaning: a write to channel 0's phase (REST, web UI, `channels` shadow, portal) now rotates channel 0 only. Tools or habits that changed channel 0's phase to move the base must change the voltage phase instead.
- Downgrade: 2.7.x ignores the key and takes the base from channel 0 again. When channel 0's phase differs from the voltage phase, every non-split channel, channel 0 included, turns by `angle(voltagePhase) - angle(phase_0)` until the meter is upgraded again.
- Cloud: no change required. The shadow rule forwards the `meter` document whole, `shadow_handler` checks shadow names only and `device_shadow_history` stores `reported_json` whole, so the voltage phase has a history from the first report. Portal and intelligence exposure are out of scope. Analyses that read V1's line from channel 0's phase must switch to the `meter` shadow's `voltage_phase` from 2.8.0 on, falling back to channel 0's phase before it.
- Memory: one 4-byte static and one NVS entry; no new task, mutex or buffer.
