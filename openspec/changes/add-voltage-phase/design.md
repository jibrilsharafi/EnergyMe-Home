## Context

See proposal.md for the motivation. Constraints from today's code (paths under `source/`):

- `_readMeterValues` copies the channel's `ChannelData` under `_channelDataMutex` (`getChannelData`, `src/ade7953.cpp:3867`) and reads `basePhase = _channelData[0].phase` lock-free (`:3897`). `PhaseUtils::toChannelFrame(active, reactive, reverse, basePhase, phase)` (`lib/phase_utils/phase_utils.h`) negates both energies for `reverse` and then rotates them by `calculatePhaseShiftDeg(basePhase, phase)` when `isOffPhase(basePhase, phase)`, which is false for split-phase 240 V and for `phase == basePhase`. Every helper already takes the base as an argument, and `test/test_phase_utils` covers every base/channel pair, including a voltage-on-L3 field case.
- Angles (IEC positive sequence): `PHASE_1` 0, `PHASE_2` -120, `PHASE_3` +120 degrees; `PHASE_SPLIT_240` returns 0 and is never rotated.
- Channel 0 is read on ADE7953 channel A every window and supplies V1 (`_readVoltageRms`, times the split multiplier) and the grid frequency; every other channel reuses `_meterValues[0].voltage` (`:3929-3937`). MQTT energy and alarm payloads, Modbus registers 100/101, the `voltage_out_of_range` issue and the blackout log read `_meterValues[0].voltage` as the device voltage.
- Channel 0 cannot be split-phase 240 V: `_isValidReferencePhase` (`:3166`, called from the validators at `:3290` and `:3403`) refuses it, and `setChannelData` coerces a stored value to `PHASE_1` (`:795`).
- CT polarity detection: `setChannelData` (`:810-839`) arms it on activation, on a role change and on a phase change of an active channel; a `reverse` change in the same write disarms it (the explicit choice wins). Votes run in `_readMeterValues` under `_channelDataMutex` (`:4080-4110`) and flip and persist `reverse` after `POLARITY_DETECT_VOTE_THRESHOLD` conducting negative readings. Boot restore passes `armTransients=false` so a reboot never re-arms it (`:805-809`).
- Sample time precedent: file-static `_sampleTime`, `CONFIG_SAMPLE_TIME_KEY` in `ade7953_ns` (`include/ade7953.h:166`), `_setSampleTimeFromPreferences` (`:4435`) calling the public setter, REST `/api/v1/ade7953/sample-time` (`src/customserver.cpp:3196-3236`), meter shadow `sample_time` applied outside `METER_CAL_KEYS` (`src/shadow.cpp:852-905`).
- Boot order: `performNvsRestore()` (`src/main.cpp:146-148`) runs before `Ade7953::begin` (`src/main.cpp:208`), which loads the factory seed, calibration, sample time and channels (`:351-353`), then energy and interrupts, and starts the meter task (`:364`).
- Backup: `nvsDataToJson` (`src/utils.cpp:2195`) dumps every key of every namespace not in `EXCLUDED_NVS_NAMESPACES_LIST` (`include/constants.h:77-82`, which does not list `ade7953_ns`) with its NVS type. `restoreNvsFromJson` (`src/utils.cpp:2453`) writes only the keys present and never clears a namespace. Only the factory reset clears `ade7953_ns` (`clearAllPreferences`, `src/utils.cpp:1231`, called from `_factoryReset` at `:1147`), and the dev-only NVS endpoint (`src/customserver.cpp:2838-3051`) can delete a single key. `Ade7953::resetConfiguration` (`src/ade7953.cpp:573`) rewrites the calibration keys only and `resetChannelData` (`:862`) one channel. A factory reset reboots, and that boot runs `Ade7953::begin` (`src/main.cpp:208`) before the web server starts (`:295`), so a backup can only be uploaded and restored on a later boot.
- `_configMutex` serializes ADE7953 configuration writes (`setConfiguration`, `:557`); neither it nor `_applyConfiguration` takes `_channelDataMutex`.
- Web UI `html/channel.html` takes the base from `channelData[0].phase` (`:325`, `:356`) and shows "Voltage reference" instead of a phase preview for channel 0 (`:363-367`). The preview candidates are computed relative to the channel's configured phase (`:330-347`), so they need no base.

## Goals / Non-Goals

**Goals:**
- Configure a meter whose channel 0 CT is on any line, remotely and without opening the panel.
- No reading changes at the upgrade; the stored value is authoritative afterwards.
- Same write paths and patterns as the sample time (REST, `meter` shadow, configuration backup).

**Non-Goals:**
- Split-phase 240 V as the voltage phase or on channel 0. It would need raw V1 decoupled from `_meterValues[0].voltage` for MQTT, Modbus, the issue registry and InfluxDB.
- A web UI control for the voltage phase. Upgrades migrate it and new installs label lines relative to V1, so the default fits; REST and the `meter` shadow are the write paths and the channel page only reads it.
- Portal and intelligence exposure, `channel_config_history` columns, any cloud change.
- Measuring L2 and L3 voltages, or detecting the voltage phase automatically. The assumptions `|V2| = |V3| = |V1|` and exact 120 degree spacing are unchanged.

## Decisions

### D1. Every channel is rotated from the voltage phase

For channel k with phase `phase_k` in 1..3, the meter applies

    R_k = angle(phase_k) - angle(voltagePhase) + 180 * reverse_k

to the latched energy pair integrated against V1: `S_k = e^{j R_k} * (P1 + j Q1)`, with apparent energy and current unchanged. This holds for every channel, channel 0 included. A split-phase channel (`PHASE_SPLIT_240`) takes `R_k = 180 * reverse_k` and the 2x voltage multiplier, as today. The code is the existing call with a different base: `toChannelFrame(active, reactive, reverse_k, voltagePhase, phase_k)` and `isOffPhase(voltagePhase, phase_k)`. No math changes in `phase_utils`.

2.7.x applies `R_k = angle(phase_k) - angle(phase_0) + 180 * reverse_k`, which is the formula above with `voltagePhase = phase_0` (and gives `R_0 = 180 * reverse_0`). Pinning the voltage phase to channel 0's phase at the upgrade (D3) therefore reproduces every reading exactly. After that, a write to channel 0's phase changes `R_0` only, and a write to the voltage phase changes `R_k` of every non-split channel by the same amount.

An off-phase channel 0 also takes the synthetic no-load gate of off-phase channels (`:3975-3984`, IRMSA against 0.05% of its own CT rating); the polarity vote and the import/export accumulators already work on the rotated values, so nothing else in the read path is specific to channel 0. With channel 0 off-phase, channel A's reactive path feeds active power, so AVARGAIN must match AWGAIN and PHCALA must be right, as BVARGAIN and BWGAIN already must for channel B (a Q-path gain error e shows up as about 0.75 e on a resistive load). The comment block at `:3806-3842` is reworded to say V1 sits on the configured voltage phase and to extend the gain assumption to channel A; the comment at `:3906` drops "channel 0 defines basePhase".

Channel 0 stays barred from split-phase 240 V because its reading supplies V1 to every channel and integration: the 2x multiplier would double the voltage everywhere. `_isValidReferencePhase`, the coercion at `:795` and their log text are reworded to that reason; the function may be renamed (`_isValidChannel0Phase`).

### D2. Setting and storage outside the calibration struct

`static volatile Phase _voltagePhase = DEFAULT_VOLTAGE_PHASE;` in `ade7953.cpp`, `#define CONFIG_VOLTAGE_PHASE_KEY "voltage_phase"` (13 characters, within the 15-character NVS limit) and `#define DEFAULT_VOLTAGE_PHASE PHASE_1` in `ade7953.h`, `Phase getVoltagePhase()` and `bool setVoltagePhase(Phase phase)` next to the sample time accessors. Valid values are `PHASE_1` to `PHASE_3` (`PhaseUtils::isValidVoltagePhase`). The value is stored as u8 (`putUChar`) and read with `getUChar(key, 0)`, so 0 means absent; any value outside 1..3 is treated as absent (D3).

It does not go into `Ade7953Configuration`: the full-PUT validator requires every key (`:2968-2986`), so a new field breaks existing full PUTs; `resetConfiguration` would wipe the wiring with the calibration; `_applyConfiguration` and the factory seed work at register level; and the meter shadow forwards `METER_CAL_KEYS` into the calibration setter. `ade7953_ns` keeps it next to the sample time, inside the configuration backup and outside every reset except the factory reset.

### D3. One-time migration from channel 0, then the stored key wins

`_setVoltagePhaseFromPreferences()` runs in `begin()` right after the channel loop (`:351-353`) and before `_setupInterrupts()` and `_startMeterReadingTask()`:

1. Read the u8 with sentinel 0.
2. `PhaseUtils::resolveVoltagePhase(stored, _channelData[0].phase)` returns the value and whether it must be persisted: a stored 1..3 is used as is; otherwise channel 0's phase (already coerced off split-phase by `:795`), or `PHASE_1` if that is not 1..3, with persist set.
3. Assign `_voltagePhase` directly. The boot path never calls the public setter, so it never re-arms polarity detection and never looks like a change (the sample time precedent calls its setter; copying that here would re-arm every channel at every boot with a stored value other than `PHASE_1`).
4. When persisting, write the key and log INFO "voltage phase migrated from channel 0: N" (WARNING with the bad value when a stored value was out of range). On a write failure log ERROR and keep the value in RAM; the next boot repeats the migration.

Invariants:
- I1. Zero change at upgrade: on the first 2.8.0 boot every channel has the `R_k` it had on 2.7.x (D1).
- I2. Written once: the boot path writes the key only while it is absent or out of range. A valid stored key is never re-derived, whatever channel 0's phase is, after a downgrade and re-upgrade, or after restoring a backup without the key.
- I3. Ready before the first reading: the load completes before the meter task starts, so no window uses the default by mistake.
- I4. Boot arms nothing: neither the load nor the migration touches polarity detection, for the reason `armTransients=false` exists.
- I5. RAM equals NVS after every successful setter call; a failed persist leaves both unchanged (D4).
- I6. Only the factory reset removes the key (plus the dev-only NVS endpoint on dev builds). The boot right after the reset migrates from channel 0's default phase (`PHASE_1`), which is the fresh-device state, and writes the key before any backup can be restored. `resetConfiguration` and `resetChannelData` leave it alone.

| Situation | Voltage phase after boot |
| --- | --- |
| Fresh device | `PHASE_1` (migrated from channel 0's default) |
| Upgrade from 2.7.x with channel 0 at p | p, persisted |
| Downgrade, channel 0 edited to q on 2.7.x, re-upgrade | stored value, not q |
| Restore of a 2.8.0 backup with the key | the backup's value |
| Restore of a backup without the key | unchanged |
| Factory reset, then restore of a 2.7.x backup with channel 0 at p | `PHASE_1`, written by the first boot after the reset and not re-derived; channel 0 takes p, so the frame matches the backup's 2.7.x frame only when p is 1 (see Risks) |
| Stored value out of range (a hand-edited backup) | channel 0's phase, re-persisted, WARNING |

### D4. Setter, re-arm and concurrency

`setVoltagePhase(phase)`:
1. Refuse anything outside 1..3 with a WARNING and return false.
2. Take `_configMutex` (`CONFIG_MUTEX_TIMEOUT_MS`). It serializes the REST handler (async TCP task) and the shadow apply (MQTT task), so two writes can never persist in one order and apply in the other.
3. Equal to the current value: release and return true, with no NVS write and no re-arm (a re-sent shadow value or a repeated PUT is a no-op).
4. Persist the u8. On failure log ERROR, release and return false; RAM is unchanged.
5. Take `_channelDataMutex`, write `_voltagePhase`, and for every channel below `globalHwProfile->totalChannelCount` that is active and whose rotation changes (`PhaseUtils::rotationChanges(old, new, phase_k)`, true for every non-split channel when the value changes) set `_pendingPolarityCheck = true`, zero `_polarityVoteCount` and `_polarityConductingReads`, and set `_pendingPriorityRead` for index > 0, as `:835-838` does for a per-channel phase change. Release.
6. Release `_configMutex` and log INFO with the old and new value and the number of channels re-armed.

Lock order is `_configMutex` then `_channelDataMutex`; nothing that holds `_channelDataMutex` takes `_configMutex`, so there is no cycle. The NVS write happens outside `_channelDataMutex`, so the meter task never waits on flash. The loop is bounded by the channel count.

Readers never lock. `getVoltagePhase()` loads an aligned 32-bit enum. `_readMeterValues` loads it once per reading into a local right after its channel copy and uses only that local, so one reading never mixes two values. A set that lands during a reading books at most that window in the old frame, and that window can cast at most one polarity vote after the re-arm, below the flip threshold. A per-channel phase change has the same exposure today.

Re-arming follows issue #174: a frame change invalidates the detector's premise, and with the right frame a wrongly flipped `reverse` flips back within seconds. The cost is that a wrong voltage phase on a loaded meter can flip and persist many `reverse` flags; setting it back re-arms and flips them again. Each flip is a "CT reversal detected" INFO line and a `channels` shadow drift publish.

### D5. Interfaces

- REST `GET /api/v1/ade7953/voltage-phase` returns `{"voltagePhase": n}`. `PUT` takes `{"voltagePhase": n}`: anything but an integer 1..3 gets 400 and changes nothing, a setter failure gets 500, success gets the usual success response. Same `_validateRequest` and authentication as the sample time, `HTTP_MAX_CONTENT_LENGTH_ADE7953_VOLTAGE_PHASE 64`. camelCase matches the waveform JSON `voltagePhase`.
- `meter` shadow key `voltage_phase`, snake_case like `sample_time`. `_reportMeter` adds it. `_applyMeter` handles it like `sample_time`: an integer 1..3 accepted by the setter is echoed as `reported.voltage_phase`; anything else is a WARNING with reported unchanged, and `_applyDelta` nulls the received desired key as for any field. It stays out of `METER_CAL_KEYS`. REST edits reach `reported` through the 3 s drift check, and the migrated value goes out with the first full report after (re)connect. The cloud forwards the document untouched (no infra change).
- Configuration backup and restore need no code: `nvsDataToJson` already emits `ade7953_ns.voltage_phase` as `{"type": "u8", "value": n}`, and the restore writes it at early boot, before `begin()` loads it. A backup without the key leaves the stored value in place. A hand-edited backup with a value outside 1..3 is replaced by the boot load (D3). A 2.8.0 backup cannot be restored on 2.7.x (`isBackupVersionCompatible` refuses a higher minor).
- `swagger.yaml` (CRLF kept) gains the path and a `VoltagePhase` schema (enum 1, 2, 3); the waveform `voltagePhase` description becomes "device voltage phase setting"; the channel `phase` description says it is relative to the voltage phase.

### D6. Waveform phase fields

`getWaveformCaptureAsJson` reports `voltagePhase = getVoltagePhase()`, `currentPhase` = the captured channel's phase, and `phaseShiftDegrees = isOffPhase(voltagePhase, currentPhase) ? calculatePhaseShiftDeg(voltagePhase, currentPhase) : 0`. A split-phase channel or a channel on the voltage phase therefore shows 0, matching what the meter applies, and channel 0 shows a shift when it is off-phase. `reverse` stays out of the shift, as today. `html/waveform.html` needs no change.

### D7. Web UI channel page

`js/api-client.js` gains `getAde7953VoltagePhase()`. `channel.html` fetches it at init and with the periodic config refresh (the channel ETag at `ade7953.cpp:921` covers `ChannelData` only, so it is a separate GET) and uses it as the base in `isThreePhaseInstall` (an active channel, channel 0 included, with a phase in 1..3 different from the voltage phase) and `updatePhasePreview`. The channel 0 branch goes, so channel 0 shows its own candidates. If the fetch fails the previews stay hidden instead of guessing a base. The tooltip at `:225` says phases are relative to the line the device's own L wire is on and that channel 0 is set like any other channel; channel 0's phase select offers 1-3 only (the API already refuses 4); the message at `:476` says channel 0 stays active because it supplies the voltage and frequency reading.

### D8. Field fix for a meter whose channel 0 is off the voltage line

With the voltage phase fixed, a channel's rotation changes only through its own phase and reverse. Moving the phase one step k -> k+1 (cyclic 1 -> 2 -> 3 -> 1) adds -120 degrees, k -> k-1 adds +120, and flipping `reverse` adds 180. The correction a channel needs is minus the offset it reads at (resistive loads read the offset directly, since their true angle is 0):

| Correction | Phase | Reverse |
| --- | --- | --- |
| +60 | k -> k+1 | flip |
| -60 | k -> k-1 | flip |
| +120 | k -> k-1 | keep |
| -120 | k -> k+1 | keep |
| 180 | keep | flip |

Changing the voltage phase only adds +-120 to every non-split channel at once, so it can never fix a 60 degree error; the per-channel pair does. On 2.7.x the same pair failed on channel 0 because its phase moved the base too (proposal.md).

Procedure on a meter:
1. OTA 2.8.0. The migration pins the voltage phase to channel 0's current phase and no reading changes (I1).
2. Gate: `GET /api/v1/ade7953/voltage-phase` or the `meter` shadow's `voltage_phase` equals channel 0's phase from before the OTA, and live angles match the pre-OTA ones. Stop otherwise.
3. Snapshot the `channels` shadow reported state as the revert source.
4. Write one `channels` desired update per meter with `{phase, reverse}` for every channel to correct, channel 0 included. Both keys of a channel arrive in one delta entry, `_applyChannels` merges them into one `setChannelData`, and the `reverse` change disarms polarity detection for that channel, so the detector never votes in a half-applied frame. Never split a channel's phase and reverse into separate writes: the phase-only state is +-120 off, arms detection, can read negative and flips `reverse` on its own.
5. Verify resistive steps (defrost heaters) near 0 degrees and positive grid legs during consumption, with no "CT reversal detected" line. Revert by writing the snapshot back.

field site: the per-channel corrections come from the analysis curation (the site analysis). As of 2026-11-01 they are +60, i.e. (k -> k+1, flip), for meter A ch0-8 and ch10-15 and for meter B ch0-13; -60, i.e. (k -> k-1, flip), for meter B ch14; none for meter A ch9 and meter B ch15. meter B ch4 is inactive (its jack was not seated and the curation excludes it); writing its pair is harmless, since nothing arms on an inactive channel, and keeps its frame right if it is re-activated. Re-read the curation before writing. These are prod meters, so the OTA, the gate and the write each need Jibril's explicit consent. Once the fix is live, the curation's field site rotation windows end at the fix time, before any post-fix data is analysed.

After the fix channel 0's phase differs from the voltage phase, so a rollback to 2.7.x would turn the whole meter by `angle(voltagePhase) - angle(phase_0)` (+120 for a channel 0 moved k -> k+1). Write the snapshot back before any rollback.

## Risks / Trade-offs

- [Downgrade or `firmware_rollback` to 2.7.x while channel 0's phase differs from the voltage phase turns every non-split channel by `angle(voltagePhase) - angle(phase_0)`] → release notes; make channel 0's phase equal to the voltage phase (or revert a D8 fix) before rolling back. The key survives the downgrade and wins on re-upgrade (I2).
- [Writes to channel 0's phase change meaning: installers, `shadow_cli` habits and portal editors used them to move the base] → release notes, swagger, web UI text and the manual say channel 0 is set like any other channel and the voltage phase moves the base.
- [Restoring a pre-2.8.0 backup on a device whose voltage phase differs from the backup's channel 0 phase gives a frame other than the one the backup had on 2.7.x. This includes a factory-reset device, which holds `PHASE_1` by the time a backup can be restored] → decided: a restore never re-derives (I2). After restoring an old backup, compare `GET /api/v1/ade7953/voltage-phase` with the backup's channel 0 phase and, to reproduce the 2.7.x frame, set the voltage phase to it. The restore boot arms no polarity detection, so a wrong frame reads wrong until then without flipping any `reverse`.
- [A wrong voltage phase on a loaded meter re-arms detection everywhere and can flip many `reverse` flags] → intended (issue #174); every flip is logged and published; setting the right value flips them back.
- [Off-phase channel 0 accuracy depends on AVARGAIN matching AWGAIN and on PHCALA; factory seeds write `avar_gain` and `aw_gain` separately] → open question below; the reported calibration in the `meter` shadow shows both gains per device.
- [Migration write failure: if channel 0's phase is edited before the next boot, that boot migrates the edited value] → ERROR log; an NVS that refuses writes breaks every other setting too.
- [Analyses that read V1's line from channel 0's phase] → from 2.8.0 use the `meter` shadow `voltage_phase` in `device_shadow_history`, falling back to channel 0's phase for earlier rows.
- [The bench supply is single-phase] → the e2e run proves the transform, the config paths and persistence only; physical off-phase correctness comes from the D8 fix at field site.

## Migration Plan

1. Firmware 2.8.0 on the dev bench (.174 Home, .80 Home Pro) with the e2e run of tasks section 5, which seeds channel 0's phase on 2.7.0 first so the migration is observable.
2. Fleet OTA. Zero change by construction (I1); each device reports `voltage_phase` in its `meter` shadow after the first reconnect.
3. field site fix (D8), each prod step with Jibril's explicit consent.

Rollback: there is no switch. A meter whose channel 0 phase equals its voltage phase rolls back to 2.7.x with identical readings; any other meter needs its channel 0 phase made equal to the voltage phase (or its D8 fix reverted) first.

## Open Questions

- Do the factory calibration records keep AVARGAIN consistent with AWGAIN (and PHCALA right) on channel A? That decides whether an off-phase channel 0 keeps channel 0's usual accuracy. Read-only check on the field site meters' `meter` shadow before the fix.
