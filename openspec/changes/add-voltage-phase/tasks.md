## 1. Pure logic (`source/lib/phase_utils`)

- [ ] 1.1 `PhaseUtils::isValidVoltagePhase` (1..3 only), `resolveVoltagePhase` (a stored 1..3 is kept; absent or out of range gives channel 0's phase when it is 1..3, else `PHASE_1`, with a persist flag) and `rotationChanges(oldVoltagePhase, newVoltagePhase, channelPhase)` (false for split-phase 240 V and for an unchanged value)
- [ ] 1.2 Unity tests in `source/test/test_phase_utils`: 0, 4 and 5 refused; migration cases (absent, absent with channel 0 at 4, stored 7, a stored valid value against every channel 0 phase); `rotationChanges` for every pair; `toChannelFrame(..., voltagePhase = phase_0, phase_k)` equals the 2.7.x output for every channel and base (zero change at upgrade); channel 0 field regression (V1 on L1, CT on L2 reversed, resistive load: raw -60 degrees, voltage phase 1, phase 2, `reverse` true gives PF near 1 and P > 0); the design D8 table (k -> k+1 with a flip adds +60, k -> k-1 with a flip adds -60, k -> k-1 adds +120, k -> k+1 adds -120, a flip adds 180). Green via `pio test -e native -f test_phase_utils` from WSL

## 2. Firmware (`source/`, one commit per task)

- [ ] 2.1 `include/ade7953.h`: `CONFIG_VOLTAGE_PHASE_KEY "voltage_phase"`, `DEFAULT_VOLTAGE_PHASE PHASE_1`, `getVoltagePhase()` and `setVoltagePhase()` next to the sample time accessors; `static volatile Phase _voltagePhase` in `ade7953.cpp`
- [ ] 2.2 `_setVoltagePhaseFromPreferences()` in `begin()` after the channel loop and before `_setupInterrupts()` (design D3): u8 read with sentinel 0, direct assignment (never the public setter), persist only when absent or out of range, INFO on migration, WARNING with the bad value, ERROR on a failed write, no polarity arming
- [ ] 2.3 `setVoltagePhase()` (design D4): 1..3 only, `_configMutex`, no-op on an equal value, persist, then under `_channelDataMutex` the new value and the re-arm of every active channel whose rotation changes (same fields as `setChannelData` at `ade7953.cpp:835-838`), INFO with old value, new value and re-armed count
- [ ] 2.4 `_readMeterValues`: one voltage phase load per reading into a local after the channel copy, passed to `isOffPhase` and `toChannelFrame`; reword the comment block at `:3806-3842` (V1 on the configured voltage phase, AVARGAIN == AWGAIN and PHCALA for an off-phase channel 0) and the comment at `:3906`
- [ ] 2.5 `getWaveformCaptureAsJson`: `voltagePhase` from `getVoltagePhase()`, `phaseShiftDegrees` 0 when the channel is not off-phase
- [ ] 2.6 Channel 0 split-phase rule: keep `_isValidReferencePhase` (rename optional) and the coercion at `:795`, reword their comments and log text to "channel 0 supplies V1 to every channel"
- [ ] 2.7 `GET/PUT /api/v1/ade7953/voltage-phase` in `customserver.cpp`, `HTTP_MAX_CONTENT_LENGTH_ADE7953_VOLTAGE_PHASE 64` in `customserver.h`: 400 for a missing value or anything but an integer 1..3, 500 when the setter fails
- [ ] 2.8 `shadow.cpp`: `_reportMeter` adds `voltage_phase`, `_applyMeter` applies it like `sample_time` outside `METER_CAL_KEYS`, header comment updated
- [ ] 2.9 Confirm `nvsDataToJson` emits `ade7953_ns.voltage_phase` as `u8` and `restoreNvsFromJson` writes it back (no code change expected)

## 3. Web UI, API docs, tools and manual

- [ ] 3.1 `js/api-client.js` `getAde7953VoltagePhase()`; `html/channel.html` takes the base from the voltage phase in `isThreePhaseInstall` and `updatePhasePreview` (fetched at init and on the config refresh), drops the channel 0 branch, hides the previews when the fetch fails, rewords the tooltip at `:225` and the message at `:476`, and offers phases 1-3 only on channel 0
- [ ] 3.2 `resources/swagger.yaml` (keep CRLF): path `/api/v1/ade7953/voltage-phase`, `VoltagePhase` schema (enum 1, 2, 3), waveform `voltagePhase` described as the device voltage phase setting, channel `phase` relative to the voltage phase
- [ ] 3.3 Dev tools: `utils/mock_server.py` and `utils/benchmark_api.py` list the endpoint, `utils/shadow_cli.py` prints `voltage_phase`
- [ ] 3.4 Manual: `01-installation.md` (§3.3 channel 0 note at `:106`, three-phase note and warning at `:117-122`, checklist line at `:183`), `02-setup.md` (channel 0 note at `:113`, phase row at `:91`), `appendices.md` (B.1 warning at `:38`): channel 0's CT may sit on any line, phases are labelled relative to the line the device's own L wire is on, and the voltage phase is set through REST or the `meter` shadow

## 4. Build, review and version

- [ ] 4.1 `pio run -e esp32s3-dev` and `pio run -e esp32s3-dev-pro` clean, `pio check -e esp32s3-dev` with no new high-severity finding, `pio test -e native` (WSL) and `node --test source/test/js/` green
- [ ] 4.2 Code-review agents on the branch diff, one with an adversarial brief on the migration, the setter's locking and the shadow path, plus a simplification agent (repo CLAUDE.md); triage every finding, then re-run 4.1
- [ ] 4.3 `include/constants.h` version 2.8.0 in its own commit

## 5. Bench e2e (dev devices .174 Home and .80 Home Pro)

Every step keeps active power positive on the bench loads (capacitive standby, about -75 to -81 degrees): only voltage phases 1 and 2 and channel phases 1 and 2 are used, never 3 and never a resistive load, because a re-armed channel that reads negative flips and persists `reverse`. After every step read `reverse` on every active channel and compare medians of 10 `GET /api/v1/ade7953/meter-values` samples (3 degrees, 1 VA). Logs through `utils/udp_log_listener.py`, `save` level unchanged. AWS checks are read-only `get-thing-shadow` in dev.

- [ ] 5.1 Baseline P0 on 2.7.0: P, Q, S, PF, angle and `reverse` of .174 ch0, ch5, ch6 and .80 ch0
- [ ] 5.2 Seed P1 on 2.7.0: channel 0 phase 2 by `PATCH /api/v1/ade7953/channel` on both (on .174 ch5 and ch6 turn by +120, the field bug); record readings and `reverse`; download backup B0 (`GET /api/v1/backup/configuration`, no key)
- [ ] 5.3 Step A, OTA 2.8.0 (`utils/ota_updater.py`): `GET /api/v1/ade7953/voltage-phase` returns 2 on both, one INFO migration line, every reading equals P1, no "CT reversal detected", the `meter` shadow reports `voltage_phase: 2`; a second reboot logs no migration and arms nothing
- [ ] 5.4 Step B, channel 0 phase 1 by REST with the voltage phase at 2: channel 0 turns by +120 (.174 about +45 degrees, .80 about +39), .174 ch5 unchanged and ch6 still 0, `reverse` unchanged
- [ ] 5.5 Web UI in state B on .174: `channel.html` shows channel 0's own phase candidates; with the voltage-phase request blocked in the browser the previews are hidden and the page still works
- [ ] 5.6 Step C, `PUT` voltage phase 1: every reading back to P0, the INFO line reports 3 channels re-armed on .174 and 1 on .80, no `reverse` change, the `meter` shadow reports 1 within about 3 s without a `desired` write
- [ ] 5.7 Step D, voltage phase 2 through `utils/shadow_cli.py set meter voltage_phase=2`: every phase-1 channel turns by +120 together; REST reads back 2 (the inject returns 200 even for a dropped key); `reported.voltage_phase` is 2 and `desired.voltage_phase` null
- [ ] 5.8 Negative cases in state D: `PUT` with 0, 4, 5, `"2"` and no `voltagePhase` gets 400; `shadow_cli` with 4 and 0 logs a warning, leaves `reported` unchanged and nulls `desired`; a `PUT` of the current value logs no change and re-arms nothing; readings and `reverse` unchanged
- [ ] 5.9 Waveform (`POST /api/v1/ade7953/waveform/arm`, `GET /api/v1/ade7953/waveform/data`) on channel 0, and ch5 on .174, in P1, A, B, C and D: `voltagePhase` and `currentPhase` match the configuration, `phaseShiftDegrees` on channel 0 is 0 in P1, A and C and +120 in B and D, on ch5 +120 in P1, A, B and D and 0 in C; a spare .174 channel activated at split-phase 240 V with no CT shows 0 in state D, then goes back to inactive at phase 1
- [ ] 5.10 Persistence in state D: after a reboot the voltage phase is 2 and readings equal D; backup B1 carries `ade7953_ns.voltage_phase` as `{"type": "u8", "value": 2}`
- [ ] 5.11 Restore with the key: voltage phase 1 by `PUT`, restore B1, after the reboot the voltage phase is 2 (state D)
- [ ] 5.12 Restore without the key: voltage phase 1 by `PUT`, restore B0, after the reboot the voltage phase is still 1 and channel 0 is at phase 2. Channel 0 now reads negative unarmed (a restore boot arms nothing); change nothing until 5.13
- [ ] 5.13 Migration again: delete `ade7953_ns/voltage_phase` through the dev NVS endpoint (never a factory reset, which wipes the bench) and reboot: one INFO migration line, the voltage phase is 2 from channel 0, channel 0 positive again, nothing armed
- [ ] 5.14 Out-of-range value: restore B1 edited to `"value": 7`: one WARNING at boot, the voltage phase is 1 from B1's channel 0 and persisted, every reading equals P0
- [ ] 5.15 Downgrade and re-upgrade on .174 (stored key wins): roll back to 2.7.0 with channel 0 at the voltage phase and check readings equal P0; on 2.7.0 set channel 0 phase 2; OTA 2.8.0: no migration line, the voltage phase is still 1, ch5 and ch6 equal P0, channel 0 reads at -120 unarmed; set channel 0 phase 1 by REST and check P0
- [ ] 5.16 End state on both: 2.8.0, voltage phase 1, every channel at phase 1, `reverse` as in P0 (true on .174, false on .80), readings equal P0, and no `reverse` changed during the run

## 6. Field fix at a field site (prod, each step needs Jibril's explicit consent)

- [ ] 6.1 Read-only before the OTA on meter A and meter B: `aVarGain`, `aWGain` and `phCalA` from the `meter` shadow (open question in design.md), and the `channels` reported state saved as the revert snapshot
- [ ] 6.2 OTA 2.8.0 on both; gate: the `meter` shadow's `voltage_phase` equals channel 0's pre-OTA phase and the live angles match the pre-OTA ones, stop otherwise
- [ ] 6.3 Re-read the site analysis and build one `channels` desired update per meter with `{phase, reverse}` for every corrected channel, channel 0 included (design D8); never split a channel's phase and reverse into separate writes
- [ ] 6.4 Write the update and verify: defrost heater steps near 0 degrees, positive grid legs during consumption, no "CT reversal detected"; on any failure write the snapshot back
- [ ] 6.5 the site analysis: end the field site rotation windows at the fix time before any post-fix data is analysed
