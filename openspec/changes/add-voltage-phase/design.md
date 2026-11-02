## Context

See proposal.md for the motivation. Constraints from today's code (paths under `source/`):

- `_readMeterValues` copies the channel's `ChannelData` under `_channelDataMutex` (`getChannelData`, `src/ade7953.cpp:3867`) and reads `basePhase = _channelData[0].phase` lock-free (`:3897`). `PhaseUtils::toChannelFrame(active, reactive, reverse, basePhase, phase)` (`lib/phase_utils/phase_utils.h`) negates both energies for `reverse` and then rotates them by `calculatePhaseShiftDeg(basePhase, phase)` when `isOffPhase(basePhase, phase)`, which is false for split-phase 240 V and for `phase == basePhase`. Every helper already takes the base as an argument, and `test/test_phase_utils` covers every base/channel pair, including a voltage-on-L3 field case.
- Angles (IEC positive sequence): `PHASE_1` 0, `PHASE_2` -120, `PHASE_3` +120 degrees; `PHASE_SPLIT_240` returns 0 and is never rotated.
- Channel 0 is read on ADE7953 channel A every window and supplies V1 (`_readVoltageRms`, times the split multiplier) and the grid frequency; every other channel reuses `_meterValues[0].voltage` (`:3929-3937`). MQTT energy and alarm payloads, Modbus registers 100/101, the `voltage_out_of_range` issue and the blackout log read `_meterValues[0].voltage` as the device voltage.
- Channel 0 cannot be split-phase 240 V: `_isValidReferencePhase` (`:3166`, called from the validators at `:3290` and `:3403`) refuses it, and `setChannelData` coerces a stored value to `PHASE_1` (`:795`).
- CT polarity detection: `setChannelData` (`:810-839`) arms it on activation, on a role change and on a phase change of an active channel; a `reverse` change in the same write disarms it (the explicit choice wins). Votes run in `_readMeterValues` under `_channelDataMutex` (`:4080-4110`) and flip and persist `reverse` after `POLARITY_DETECT_VOTE_THRESHOLD` conducting negative readings. `MeterLogic::updatePolarity` ignores the channel role and votes on the rotated active power before the load and PV clamps (`:4058-4130`). Boot restore passes `armTransients=false` so a reboot never re-arms it (`:805-809`).
- Sample time precedent: file-static `_sampleTime`, `CONFIG_SAMPLE_TIME_KEY` in `ade7953_ns` (`include/ade7953.h:166`), `_setSampleTimeFromPreferences` (`:4435`) calling the public setter, REST `/api/v1/ade7953/sample-time` (`src/customserver.cpp:3196-3236`), meter shadow `sample_time` applied outside `METER_CAL_KEYS` (`src/shadow.cpp:852-905`).
- Boot order: `performNvsRestore()` (`src/main.cpp:146-148`) runs before `Ade7953::begin` (`src/main.cpp:208`), which loads the factory seed, calibration, sample time and channels (`:351-353`), then energy and interrupts, and starts the meter task (`:364`).
- Backup: `nvsDataToJson` (`src/utils.cpp:2195`) dumps every key of every namespace not in `EXCLUDED_NVS_NAMESPACES_LIST` (`include/constants.h:77-82`, which does not list `ade7953_ns`) with its NVS type. `restoreNvsFromJson` (`src/utils.cpp:2453`) writes only the keys present, with the type the JSON names, and never clears a namespace. Only the factory reset clears `ade7953_ns` (`clearAllPreferences`, `src/utils.cpp:1231`, called from `_factoryReset` at `:1147`), and the dev-only NVS endpoint (`src/customserver.cpp:2838-3051`) can delete a single key. `Ade7953::resetConfiguration` (`src/ade7953.cpp:573`) rewrites the calibration keys only and `resetChannelData` (`:862`) one channel. A factory reset reboots, and that boot runs `Ade7953::begin` (`src/main.cpp:208`) before the web server starts (`:295`), so a backup can only be uploaded and restored on a later boot.
- `_configMutex` serializes ADE7953 configuration writes (`setConfiguration`, `:557`); neither it nor `_applyConfiguration` takes `_channelDataMutex`. `acquireMutex` waits `CONFIG_MUTEX_TIMEOUT_MS` by default (`include/utils.h:330`).
- Web UI `html/channel.html` takes the base from `channelData[0].phase` (`:325`, `:356`) and shows "Voltage reference" instead of a phase preview for channel 0 (`:363-367`). The preview candidates are computed relative to the channel's configured phase (`:330-347`), so they need no base.
- Rollback: `initArduino` marks a new image valid before `setup()`, so the only automatic rollback is the crash ladder in `CrashMonitor::_handleCounters` (`src/crashmonitor.cpp:413-450`). After `MAX_CRASH_COUNT` consecutive crashes (3 in production) or `MAX_RESET_COUNT` resets it calls `Update.rollBack()` onto the passive slot, and as the last rung a factory reset. `validatePartition` checks the passive image's product, not its version. The info shadow reports the passive slot's version as `other_image_fw_version` (`src/shadow.cpp:494-507`). An OTA job with `force` skips the version checks, `already_up_to_date` included (`src/mqtt.cpp:2566-2586`).
- AWS shadow updates merge the reported state: a key the firmware stops publishing keeps its last value, and `device_shadow_history` stores the merged `reported_json`.

## Goals / Non-Goals

**Goals:**
- Configure a meter whose channel 0 CT is on any line, remotely and without opening the panel.
- No reading changes at the upgrade; the stored value is authoritative afterwards.
- Same write paths and patterns as the sample time (REST, `meter` shadow, configuration backup).

**Non-Goals:**
- Split-phase 240 V as the voltage phase or on channel 0. It would need raw V1 decoupled from `_meterValues[0].voltage` for MQTT, Modbus, the issue registry and InfluxDB.
- A web UI control for the voltage phase. Upgrades migrate it and new installs label lines relative to V1, so the default fits. REST and the `meter` shadow are the write paths; the channel page shows the value read-only (D7).
- Portal and intelligence exposure, `channel_config_history` columns, any cloud change.
- Measuring L2 and L3 voltages, or detecting the voltage phase automatically. The assumptions `|V2| = |V3| = |V1|` and exact 120 degree spacing are unchanged.

## Decisions

### D1. Every channel is rotated from the voltage phase

For channel k with phase `phase_k` in 1..3, the meter applies

    R_k = angle(phase_k) - angle(voltagePhase) + 180 * reverse_k

to the latched energy pair integrated against V1: `S_k = e^{j R_k} * (P1 + j Q1)`, with apparent energy and current unchanged. This holds for every channel, channel 0 included. A split-phase channel (`PHASE_SPLIT_240`) takes `R_k = 180 * reverse_k` and the 2x voltage multiplier, as today. The code is the existing call with a different base: `toChannelFrame(active, reactive, reverse_k, voltagePhase, phase_k)` and `isOffPhase(voltagePhase, phase_k)`. No math changes in `phase_utils`.

2.7.x applies `R_k = angle(phase_k) - angle(phase_0) + 180 * reverse_k`, which is the formula above with `voltagePhase = phase_0` (and gives `R_0 = 180 * reverse_0`). Pinning the voltage phase to channel 0's phase at the upgrade (D3) therefore reproduces every reading exactly. After that, a write to channel 0's phase changes `R_0` only, and a write to the voltage phase changes `R_k` of every non-split channel by the same amount.

An off-phase channel 0 also takes the synthetic no-load gate of off-phase channels (`:3975-3984`, IRMSA against 0.05% of its own CT rating); the polarity vote and the import/export accumulators already work on the rotated values, so nothing else in the read path is specific to channel 0. With channel 0 off-phase, channel A's reactive path feeds active power, so AVARGAIN must match AWGAIN and PHCALA must be right, as BVARGAIN and BWGAIN already must for channel B (a Q-path gain error e shows up as about 0.75 e on a resistive load; D8 step 1 sets the field criterion). The comment block at `:3806-3842` is reworded to say V1 sits on the configured voltage phase and to extend the gain assumption to channel A; the comment at `:3906` drops "channel 0 defines basePhase".

Channel 0 stays barred from split-phase 240 V because its reading supplies V1 to every channel and integration: the 2x multiplier would double the voltage everywhere. `_isValidReferencePhase`, the coercion at `:795` and their log text are reworded to that reason; the function may be renamed (`_isValidChannel0Phase`).

### D2. Setting and storage outside the calibration struct

`static volatile Phase _voltagePhase = DEFAULT_VOLTAGE_PHASE;` in `ade7953.cpp`, `#define CONFIG_VOLTAGE_PHASE_KEY "voltage_phase"` (13 characters, within the 15-character NVS limit) and `#define DEFAULT_VOLTAGE_PHASE PHASE_1` in `ade7953.h`, `Phase getVoltagePhase()` and `bool setVoltagePhase(Phase phase)` next to the sample time accessors. Valid values are `PHASE_1` to `PHASE_3` (`PhaseUtils::isValidVoltagePhase`). The value is stored as u8 (`putUChar`). The boot load tells an absent key from a stored one with `isKey()` (D3), never from a read default. A file-static `bool _voltagePhasePersisted` records whether NVS holds the value in RAM: it is false only after a boot that could not write the value it derived, and true after any successful write and whenever a valid stored key was loaded.

It does not go into `Ade7953Configuration`: the full-PUT validator requires every key (`:2968-2986`), so a new field breaks existing full PUTs; `resetConfiguration` would wipe the wiring with the calibration; `_applyConfiguration` and the factory seed work at register level; and the meter shadow forwards `METER_CAL_KEYS` into the calibration setter. `ade7953_ns` keeps it next to the sample time, inside the configuration backup and outside every reset except the factory reset.

### D3. One-time migration from channel 0, then the stored key wins

`_setVoltagePhaseFromPreferences()` runs in `begin()` right after the channel loop (`:351-353`) and before `_setupInterrupts()` and `_startMeterReadingTask()`:

1. Open `ade7953_ns` read-write. If the open fails, derive the value as for an absent key, assign it, leave `_voltagePhasePersisted` false, log ERROR and write nothing, so a valid key that could not be read is never overwritten.
2. Classify the key. `isKey(CONFIG_VOLTAGE_PHASE_KEY)` false means absent. Otherwise read `getUChar(key, 0)`: 1..3 is valid, anything else is invalid (0, above 3, or a key of another type, for which the read returns its default 0).
3. `PhaseUtils::resolveVoltagePhase(state, stored, _channelData[0].phase)` returns the value and whether to persist. A valid key is used as is. An absent or invalid key gives channel 0's phase (already coerced off split-phase by `:795`), or `PHASE_1` if that is not 1..3, with persist set.
4. Assign `_voltagePhase` directly. The boot path never calls the public setter, so it never re-arms polarity detection and never looks like a change (the sample time precedent calls its setter; copying that here would re-arm every channel at every boot with a stored value other than `PHASE_1`).
5. When persisting, log INFO "voltage phase migrated from channel 0: N" for an absent key, or WARNING with the raw value for an invalid one, whose key is removed before the `putUChar` so a key of another type cannot survive. A successful write sets `_voltagePhasePersisted`. A failed write logs ERROR, keeps the derived value in RAM and leaves the flag false, so the next setter call persists it, the current value included (D4 step 3). D8 step 5 makes that call before any channel 0 edit.

Invariants:
- I1. Zero change at upgrade: on the first 2.8.0 boot every channel has the `R_k` it had on 2.7.x (D1).
- I2. Written once: the boot path writes the key only when the namespace opened and the key is absent or invalid. A valid stored key is never re-derived, whatever channel 0's phase is, after a downgrade and re-upgrade, or after restoring a backup without the key.
- I3. Ready before the first reading: the load completes before the meter task starts, so no window uses the default by mistake.
- I4. Boot arms nothing: neither the load nor the migration touches polarity detection, for the reason `armTransients=false` exists.
- I5. After every setter call NVS and RAM hold the same value: a call that returns false changed neither (its persist failed), a call that returns true wrote NVS and then RAM (D4). They can differ only after a boot whose migration write or namespace open failed, and the next successful setter call closes that gap.
- I6. Only the factory reset removes the key (plus the dev-only NVS endpoint on dev builds). The boot right after the reset migrates from channel 0's default phase (`PHASE_1`), which is the fresh-device state, and writes the key before any backup can be restored. `resetConfiguration` and `resetChannelData` leave it alone.

| Situation | Voltage phase after boot |
| --- | --- |
| Fresh device | `PHASE_1` (migrated from channel 0's default) |
| Upgrade from 2.7.x with channel 0 at p | p, persisted |
| Downgrade, channel 0 edited to q on 2.7.x, re-upgrade | stored value, not q |
| Restore of a 2.8.0 backup with the key | the backup's value |
| Restore of a backup without the key | unchanged |
| Factory reset, then restore of a 2.7.x backup with channel 0 at p | `PHASE_1`, written by the first boot after the reset and not re-derived; channel 0 takes p, so the frame matches the backup's 2.7.x frame only when p is 1 (see Risks) |
| Stored value 0, above 3 or of another type (a hand-edited backup) | channel 0's phase, re-persisted as u8, WARNING |
| `ade7953_ns` cannot be opened at boot | channel 0's phase in RAM only, ERROR, nothing written |

### D4. Setter, re-arm and concurrency

`setVoltagePhase(phase)`:
1. Refuse anything outside 1..3 with a WARNING and return false.
2. Take `_configMutex` (`CONFIG_MUTEX_TIMEOUT_MS`). It serializes the REST handler (async TCP task) and the shadow apply (MQTT task), so two writes can never persist in one order and apply in the other.
3. Equal to the current value: when `_voltagePhasePersisted` is set, release and return true with no NVS write and no re-arm (a re-sent shadow value or a repeated PUT is a no-op). Otherwise write the u8, set the flag on success, release and return the write's result; nothing is re-armed, since the rotation does not change.
4. Persist the u8. On failure log ERROR, release and return false; RAM is unchanged.
5. Assign `_voltagePhase` and set `_voltagePhasePersisted`. Readers are lock-free, so the assignment needs no lock, and from here NVS and RAM agree whatever happens next.
6. Take `_channelDataMutex` and, for every channel below `globalHwProfile->totalChannelCount` for which `PhaseUtils::rearmOnVoltagePhaseChange(active, old, new, phase_k)` holds (active and `rotationChanges(old, new, phase_k)`, which is true for every non-split channel when the value changes), set `_pendingPolarityCheck = true`, zero `_polarityVoteCount` and `_polarityConductingReads`, and set `_pendingPriorityRead` for index > 0, as `:835-838` does for a per-channel phase change. Clear `_voltagePhaseRearmPending` and release. If the lock cannot be taken within its timeout, set the volatile `_voltagePhaseRearmPending` and log ERROR "voltage phase N applied, CT polarity re-arm deferred". The next reading that holds `_channelDataMutex` in `_readMeterValues` re-arms every active non-split channel the same way and clears the flag. The setter still returns true, since the value is stored and applied.
7. Release `_configMutex` and log INFO with the old and new value and the number of channels re-armed (or "deferred").

Writing the old value back to NVS when the channel lock times out was rejected: it is a second flash write that can fail like the first and leave the same split between NVS and RAM.

Lock order is `_configMutex` then `_channelDataMutex`; nothing that holds `_channelDataMutex` takes `_configMutex`, so there is no cycle. The NVS write happens outside `_channelDataMutex`, so the meter task never waits on flash. The loops are bounded by the channel count.

Readers never lock. `getVoltagePhase()` loads an aligned 32-bit enum. `_readMeterValues` loads it once per reading into a local right after its channel copy and uses only that local, so one reading never mixes two values. A set that lands during a reading books at most that window in the old frame, and that window can cast at most one polarity vote after the re-arm, below the flip threshold. A deferred re-arm follows within one reading. A per-channel phase change has the same exposure today.

Re-arming follows issue #174: a frame change invalidates the detector's premise. The detector votes on the sign of the rotated active power and ignores the channel role, so it only repairs a channel that conducts in the import direction. Two costs follow.
- A wrong voltage phase on a loaded meter can flip and persist many `reverse` flags. Setting the right value re-arms them, and the channels that import then flip back.
- A bidirectional channel (grid, battery, hybrid) that exports at the moment of any voltage phase change, the right value included, reads legitimately negative, flips and persists `reverse`, and from then on books export as import. Procedure, in the release notes and the manual: change the voltage phase only while every bidirectional channel imports (no PV surplus, no battery discharging to the grid), and read back every `reverse` flag after the change.

Each flip is a "CT reversal detected" INFO line and a `channels` shadow drift publish. A D8 fix is not exposed: it never changes the voltage phase (its step 5 writes the current value), and the `reverse` change in each channel's delta disarms that channel.

### D5. Interfaces

- REST `GET /api/v1/ade7953/voltage-phase` returns `{"voltagePhase": n}`. `PUT` takes `{"voltagePhase": n}`: anything but an integer 1..3 gets 400 and changes nothing, a setter failure gets 500 (only a failed persist, which changed nothing), success gets the usual success response. Same `_validateRequest` and authentication as the sample time, `HTTP_MAX_CONTENT_LENGTH_ADE7953_VOLTAGE_PHASE 64`. camelCase matches the waveform JSON `voltagePhase`.
- `meter` shadow key `voltage_phase`, snake_case like `sample_time`. `_reportMeter` adds it. `_applyMeter` handles it like `sample_time`: an integer 1..3 accepted by the setter is echoed as `reported.voltage_phase`; anything else, or a value the setter could not persist, is a WARNING with reported unchanged, and `_applyDelta` nulls the received desired key as for any field. It stays out of `METER_CAL_KEYS`. REST edits reach `reported` through the 3 s drift check, and the migrated value goes out with the first full report after (re)connect. The cloud forwards the document untouched (no infra change).
- Configuration backup and restore need no code: `nvsDataToJson` already emits `ade7953_ns.voltage_phase` as `{"type": "u8", "value": n}`, and the restore writes it at early boot, before `begin()` loads it. A backup without the key leaves the stored value in place. A hand-edited backup with a value outside 1..3, 0 included, or another type is replaced by the boot load with a WARNING (D3). A 2.8.0 backup cannot be restored on 2.7.x (`isBackupVersionCompatible` refuses a higher minor).
- `swagger.yaml` (CRLF kept) gains the path and a `VoltagePhase` schema (enum 1, 2, 3); the waveform `voltagePhase` description becomes "device voltage phase setting"; the channel `phase` description says it is relative to the voltage phase.

### D6. Waveform phase fields

`getWaveformCaptureAsJson` reports `voltagePhase = getVoltagePhase()`, `currentPhase` = the captured channel's phase, and `phaseShiftDegrees = isOffPhase(voltagePhase, currentPhase) ? calculatePhaseShiftDeg(voltagePhase, currentPhase) : 0`. A split-phase channel or a channel on the voltage phase therefore shows 0, matching what the meter applies, and channel 0 shows a shift when it is off-phase. `reverse` stays out of the shift, as today. `html/waveform.html` needs no change.

### D7. Web UI channel page

`js/api-client.js` gains `getAde7953VoltagePhase()`. `channel.html` fetches it at init and with the periodic config refresh (the channel ETag at `ade7953.cpp:921` covers `ChannelData` only, so it is a separate GET) and uses it as the base in `isThreePhaseInstall` (an active channel, channel 0 included, with a phase in 1..3 different from the voltage phase) and `updatePhasePreview`. The channel 0 branch goes, so channel 0 shows its own candidates. If the fetch fails the previews stay hidden instead of guessing a base.

Above the channel columns the page shows the value read-only, as "Voltage input (device L wire): phase N", or "unavailable" when the fetch fails. It is not a control. It tells an installer which label the device's own line carries, which matters because an upgraded device keeps channel 0's former phase: meter A, whose channel 0 is labelled L3, migrates to 3. The tooltip at `:225` says phases are labelled relative to the voltage phase, the line of the device's own L wire (phase 1 on a new device, channel 0's former phase on an upgraded one, shown on this page), and that channel 0 is set like any other channel. Channel 0's phase select offers 1-3 only (the API already refuses 4); the message at `:476` says channel 0 stays active because it supplies the voltage and frequency reading.

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

Procedure on a meter (each prod step needs Jibril's explicit consent):
1. Before the OTA, read-only. Save the `channels` reported state as the revert snapshot and record channel 0's phase p0 and the live angles. Check channel A's gains in the `meter` shadow: `aVarGain` within 0.5% of `aWGain`, and `phCalA` equal to its first reported value in `device_shadow_history` (the factory seed; `factory_ns` is not readable remotely). Once off-phase, channel 0 turns a Q-path gain error e into about 0.75 e on its active power, which the angle checks of step 7 cannot see. A failed check does not block the fix: channel 0's accuracy is flagged in the curation, or the gains are equalised through the `meter` shadow with Jibril's consent.
2. OTA 2.8.0. The migration pins the voltage phase to p0 and no reading changes (I1).
3. Gate, all of: the info shadow's `firmware_version` is 2.8.0 and the OTA job execution is SUCCEEDED (after the 5-minute validation window); the `meter` shadow's `voltage_phase` equals p0 and its metadata timestamp is at or after that of the info shadow's `firmware_version` (both are refreshed by the full report at every connect, and info registers before meter at `src/shadow.cpp:131-135`, so it publishes first and a key left over from an earlier run fails the check); the live angles match the pre-OTA ones. The presence of `voltage_phase` proves nothing on its own: AWS merges reported state and 2.7.x never removes a key it does not know, so a meter that ran 2.8.0 and then went back to 2.7.x (for example by the crash ladder during the validation window) keeps reporting `voltage_phase = p0`, and a `channels` write to it is the 22:42 incident again. Stop on any failure.
4. Rollback target. Run a forced same-version OTA of 2.8.0 so the passive slot holds 2.8.0, check that the info shadow's `other_image_fw_version` is 2.8.0, and repeat the gate. Without it the crash ladder (Risks) can activate 2.7.x with nobody in the loop. The cost is that a 2.8.0 crash loop then rolls back onto 2.8.0 and ends at the factory-reset rung instead of a running 2.7.x; skipping this step needs Jibril's explicit acceptance of the crash-ladder risk. In both cases a read-only watch on the info shadow's `firmware_version` of both meters alerts on any version below 2.8.0, and stays on while a fixed meter's channel 0 phase differs from its voltage phase.
5. Pin the key. Write `meter` desired `voltage_phase = p0` and confirm within seconds that desired is nulled and `reported.voltage_phase` is re-echoed (its metadata timestamp moves to the write). This persists the key if the boot migration write failed (D4 step 3) and re-arms nothing, since the value does not change.
6. Re-check that `firmware_version` is 2.8.0 and that the meter is online (a reported update in the last minute), then write one `channels` desired update per meter with `{phase, reverse}` for every channel to correct, channel 0 included. Confirm within seconds that desired is nulled; otherwise set every written desired key back to null and stop, so a queued write cannot reach a meter that rolls back before it reconnects. Both keys of a channel arrive in one delta entry, `_applyChannels` merges them into one `setChannelData`, and the `reverse` change disarms polarity detection for that channel, so the detector never votes in a half-applied frame. Never split a channel's phase and reverse into separate writes: the phase-only state is +-120 off, arms detection, can read negative and flips `reverse` on its own.
7. Verify resistive steps (defrost heaters) near 0 degrees and positive grid legs during consumption, with no "CT reversal detected" line. Revert by writing the snapshot back.

field site: the per-channel corrections come from the analysis curation (the site analysis). It overrides older wording elsewhere, including older site notes's 2026-11-01 fix path ("channel 0 untouched", written for 2.7.x). As of 2026-11-01 they are +60, i.e. (k -> k+1, flip), for meter A ch0-8 and ch10-15 and for meter B ch0-13; -60, i.e. (k -> k-1, flip), for meter B ch14; none for meter A ch9 and meter B ch15. Channel 0 is included on both meters: meter A's channel 0 (Rete, labelled L3, a 150 A grid leg) migrates to voltage phase 3 and takes (3 -> 1, flip), and meter B's channel 0 (labelled L1) migrates to 1 and takes (1 -> 2, flip). meter B ch4 is inactive (its jack was not seated and the curation excludes it); writing its pair is harmless, since nothing arms on an inactive channel, and keeps its frame right if it is re-activated. Re-read the curation before writing. Once the fix is live, the curation's field site rotation windows end at the fix time, before any post-fix data is analysed.

After the fix channel 0's phase differs from the voltage phase, so any return to 2.7.x, deliberate or by the crash ladder, turns the whole meter by `angle(voltagePhase) - angle(phase_0)` (+120 for a channel 0 moved k -> k+1). Write the snapshot back before a deliberate rollback; step 4 and the watch cover the automatic one.

## Risks / Trade-offs

- [A return to 2.7.x while channel 0's phase differs from the voltage phase turns every non-split channel by `angle(voltagePhase) - angle(phase_0)`; after a D8 fix that is +120, so resistive loads read P = -0.5 S, grid legs go negative and load channels are clamped to 0. Besides a deliberate downgrade or `firmware_rollback`, the crash ladder does it unattended: after `MAX_CRASH_COUNT` consecutive crashes it calls `Update.rollBack()`, and `validatePartition` checks the product, not the version, so after an OTA from 2.7.x the target is 2.7.x] → release notes; before a deliberate rollback make channel 0's phase equal to the voltage phase (or revert a D8 fix); against the crash ladder, D8 step 4 (forced same-version OTA, `other_image_fw_version` 2.8.0) and the `firmware_version` watch. The key survives the downgrade and wins on re-upgrade (I2).
- [Writes to channel 0's phase change meaning: installers, `shadow_cli` habits and portal editors used them to move the base] → release notes, swagger, web UI text and the manual say channel 0 is set like any other channel and the voltage phase moves the base.
- [Restoring a pre-2.8.0 backup on a device whose voltage phase differs from the backup's channel 0 phase gives a frame other than the one the backup had on 2.7.x. This includes a factory-reset device, which holds `PHASE_1` by the time a backup can be restored] → decided: a restore never re-derives (I2). After restoring an old backup, compare the voltage phase (channel page or `GET /api/v1/ade7953/voltage-phase`) with the backup's channel 0 phase and, to reproduce the 2.7.x frame, set the voltage phase to it. The restore boot arms no polarity detection, so a wrong frame reads wrong until then without flipping any `reverse`.
- [A wrong voltage phase on a loaded meter re-arms detection everywhere and can flip many `reverse` flags] → intended (issue #174); every flip is logged and published; setting the right value re-arms them and the channels that import flip back.
- [A bidirectional channel (grid, battery, hybrid) that exports during any voltage phase change, the right value included, flips and persists `reverse` and then books export as import, because the vote ignores the role and runs before the clamps] → release notes and manual: change the voltage phase only while every bidirectional channel imports, and read back every `reverse` flag after the change (D4). D8 never changes the voltage phase.
- [Off-phase channel 0 accuracy depends on AVARGAIN matching AWGAIN and on PHCALA; factory seeds write `avar_gain` and `aw_gain` separately] → D8 step 1 criterion (`aVarGain` within 0.5% of `aWGain`, `phCalA` unchanged since the factory seed); a failure is flagged in the curation, or the gains are equalised with consent.
- [Migration write failure or `ade7953_ns` open failure: the key stays absent while RAM and the `meter` shadow hold the derived value, so a later edit of channel 0's phase would be migrated at the next boot and turn every non-split channel unarmed] → ERROR log; `_voltagePhasePersisted` makes the next setter call persist even the current value, and D8 step 5 makes that call before channel 0 is touched. An NVS that refuses writes breaks every other setting too.
- [The setter cannot take the channel lock after persisting] → the value is stored and applied anyway and the re-arm is deferred to the next reading (D4 step 6), with an ERROR log.
- [Analyses that read V1's line from channel 0's phase] → choose per interval by firmware version, never by the presence of `voltage_phase`: use `voltage_phase` only for intervals whose firmware (info shadow, `firmware_segments()` in the site analysis) is 2.8.0 or later, and channel 0's phase otherwise. AWS merges reported state and 2.7.x never nulls the key, so `device_shadow_history` rows from a downgraded period still carry a stale `voltage_phase`.
- [The bench supply is single-phase] → the e2e run proves the transform, the config paths, the re-arm and persistence only; physical off-phase correctness comes from the D8 fix at field site.

## Migration Plan

1. Firmware 2.8.0 on the dev bench (.174 Home, .80 Home Pro) with the e2e run of tasks section 5, which seeds channel 0's phase on 2.7.0 first so the migration is observable.
2. Fleet OTA. Zero change by construction (I1); each device reports `voltage_phase` in its `meter` shadow after the first reconnect.
3. field site fix (D8), each prod step with Jibril's explicit consent.

Rollback: there is no switch. A meter whose channel 0 phase equals its voltage phase rolls back to 2.7.x with identical readings; any other meter needs its channel 0 phase made equal to the voltage phase (or its D8 fix reverted) first, and is exposed to the crash ladder until its passive slot holds 2.8.0 (D8 step 4).

## Open Questions

- D8 step 4: the forced same-version OTA trades the crash-ladder rollback onto 2.7.x for the factory-reset rung on a 2.8.0 crash loop. Jibril decides before the field site write whether to run it or to accept the crash-ladder risk with the watch alone.
- Where the `firmware_version` watch runs (a scheduled read-only check of the info shadow or `device_shadow_history`) is decided with the D8 work; it is not part of this firmware change.

## Rejected review points

- Write the old value back to NVS when the setter cannot take the channel lock: a second flash write can fail like the first; the value is applied and the re-arm deferred instead (D4).
- "A crash during the validation window is enough" for a rollback: the ladder needs `MAX_CRASH_COUNT` consecutive crashes (3 in production); the gate in D8 step 3 is kept as proposed.
- Manual and tooltip wording "1 unless changed": an upgraded device keeps channel 0's former phase, so the text says "phase 1 on a new device, channel 0's former phase on an upgraded one" (D7).
