## 1. Interrupt demux: fix unhandled-bit detection

- [x] 1.1 Change the `_handleInterrupt()` unhandled-interrupt check from "no recognized bit present" to "any bit outside the handled set present", so a bit co-occurring with ZXV is no longer silently dropped.
- [x] 1.2 Add/update a host-testable unit (in `test/test_meter_logic` or equivalent) covering: all-recognized-bits snapshot (no warning), recognized-plus-unrecognized snapshot (warning fires), all-unrecognized snapshot (warning fires, existing behavior preserved).
- [x] 1.3 Restrict the check to bits actually enabled in `IRQENA` (`statusA & DEFAULT_IRQENA_REGISTER` before comparing against the handled set) - `IRQSTATA` reports every channel/energy event continuously regardless of enable state, so an unrestricted check floods on routine current-channel/energy noise (`WSMP`, `ZXTO_IA`, etc.) that was never enabled.

## 2. SAG-based trigger (attempted, removed)

- [x] 2.1 ~~Arm SAG (`SAGCYC`/`SAGLVL`), log/count on fire, guard against runaway firing.~~ Implemented and bench-tested; produced no observable log entry (serial or UDP) on a real plug-pull/switch test. Root cause not isolated. All SAG-specific code, constants, and the `ade7953SagInterrupts` counter removed in favor of task group 3 below - see proposal.md/design.md for the pivot rationale.

## 3. ZXTO-based trigger

- [x] 3.1 Add `IRQSTATA_ZXTO_BIT` (14) to `DEFAULT_IRQENA_REGISTER` alongside ZXV/CYCEND/RESET/CRC.
- [x] 3.2 Add named constants for `ZXTOUT` sizing (~15ms target, one missed line cycle plus margin for grid-frequency tolerance and LPF1 delay) and compute the register value from the datasheet's 1/14kHz resolution.
- [x] 3.3 In `_setupInterrupts()`, write `ZXTOUT` alongside the existing `IRQENA` write. No live-derived value needed (unlike SAG's `VPEAK`-derived threshold) - `ZXTOUT` is a fixed timeout.
- [x] 3.4 Log the configured `ZXTOUT` value at setup time (`LOG_INFO`, visible on prod builds) so it's diagnosable from boot logs alone.
- [x] 3.5 Add `ade7953ZxtoInterrupts` to the statistics struct, mirroring `ade7953ZxInterrupts` / `ade7953UnhandledInterrupts`, replacing the removed SAG counter.
- [x] 3.6 Add a `_handleZxtoInterrupt()` function: increments the counter and emits `LOG_FATAL` (subject to the burst guard in task group 4) with the counter value and the current cached voltage.
- [x] 3.7 Add the ZXTO branch to `_handleInterrupt()` (checked alongside the existing ZXV/CYCEND/RESET/CRC branches), and include the ZXTO bit in the demux's handled-bit mask so it no longer counts as unhandled.
- [x] 3.8 Expose `ade7953ZxtoInterrupts` via `/system/info` (wherever the other ADE7953 interrupt counters are already surfaced).

## 4. Runaway-firing guard (count-based burst, superseded by task group 8)

- [x] 4.1 ~~Add a count-based log burst guard to `_handleZxtoInterrupt()`: first `ADE7953_ZXTO_LOG_BURST` occurrences log in full, one suppression notice, then count-only.~~ Worked, but each extra FATAL/log-forward was found (task group 6) to compete for the same thin MQTT window the alarm needs - replaced by a flat 60s suppression window, task group 8.
- [x] 4.2 ~~Rearm the burst guard from a clean `CYCEND` window.~~ Removed along with 4.1 - the flat time floor (task group 8) doesn't need a rearm signal.

## 5. Bench validation

- [x] 5.1 Build and flash via the `esp32s3-dev-v5` PlatformIO environment onto the bench device.
- [ ] 5.2 Confirm boot-time log shows the configured `ZXTOUT` value.
- [x] 5.3 Induce a grid-loss event (plug pull or switch) and confirm a `LOG_FATAL` ZXTO entry appears in the live log stream (serial/UDP listener) before/around the event. Confirmed 2026-08-09: 3 `LOG_FATAL` entries (`burst=1,2,3`) fired within ~50ms, each successfully published to MQTT (`log/fatal` topic) - and the device's next boot shows `lastResetReasonString: "Power on"` with an uptime placing the reset ~2.6s after the burst, i.e. the alert reached the cloud before the device actually lost power.
- [x] 5.4 Confirm `ade7953ZxtoInterrupts` increments as expected, and confirm the demux fix (task 1) produces no spurious new warnings under normal operation. Confirmed inline in the log itself (`count=1` -> `count=2` -> `count=3`, in lockstep with `burst`); live `/system/statistics` post-reboot shows `unhandledInterrupts: 0` under normal operation. Counters read 0 post-reboot because they are not NVS-persisted (documented non-goal, not a bug) - the reboot itself is why.
- [x] 5.5 Note observed firing behavior. ZXTO succeeded where SAG previously produced no log at all: burst fired at ~4-40ms spacing (roughly matching the ~15ms `ZXTOUT` plus jitter), burst guard correctly capped it at 3 full entries + 1 suppression notice with no flood. The device took a genuine "Power on" reset shortly after - likely reflects how long the physical plug-pull/reseat took, not the capacitor's real hold-up time (this bench board's cap differs from v6.1 - see design.md caveat).
- [x] 5.6 Repeated plug-pull test: confirm each pull produces a fresh full-detail burst (not suppressed by a previous episode) and that the suppression notice appears if a single episode exceeds `ADE7953_ZXTO_LOG_BURST` occurrences. Confirmed 2026-08-09: tested across multiple pulls, rearm behaved correctly each time.

## 6. Immediate-wake mechanism (`xTaskNotifyWait`) for the issue registry and MQTT task

- [x] 6.1 Add a shared `TASK_NOTIFY_SHUTDOWN_BIT` convention (`constants.h`); migrate `stopTaskGracefully()` to `xTaskNotify(handle, TASK_NOTIFY_SHUTDOWN_BIT, eSetBits)` instead of a plain `xTaskNotifyGive()` - every existing `ulTaskNotifyTake(pdTRUE, ticks) > 0` consumer is unaffected (only checks truthiness).
- [x] 6.2 `IssueRegistry::requestImmediateEvaluation()`: registry task loop swapped from `ulTaskNotifyTake` to `xTaskNotifyWait`, own `ISSUE_REGISTRY_NOTIFY_REEVALUATE_BIT`.
- [x] 6.3 `Mqtt::requestImmediatePublish()`: MQTT task loop swapped from `ulTaskNotifyTake` to `xTaskNotifyWait`, own `MQTT_NOTIFY_WAKE_BIT`; `Shadow::requestReport()` calls it on every report request.
- [x] 6.4 Bench-confirmed the demonstrated mechanism works mechanically (wake latency eliminated at both hops) - see task group 7 for why it wasn't sufficient on its own for the grid-loss alarm.

## 7. `grid_voltage_loss` as an `IssueRegistry` code (attempted, removed)

- [x] 7.1 ~~Add `Code::GridVoltageLoss` to `IssueLogic`, evaluate in `_evaluateGlobalCodes()` (pulse on `ade7953ZxtoInterrupts` delta), raise via the normal 4-state lifecycle, push an MQTT alarm on the raise edge from inside `_updateInstance()`.~~ Implemented, bench-tested twice (2026-08-09) after task group 6's immediate-wake fix. Both times the AWS shadow never showed the issue, despite the local "Issue raised" log line firing and the wake latency being confirmed eliminated. Root cause: the registry's generic reported-state publish (`issuesToJson()` walking all 32 issue slots, full shadow doc, drift check across every other shadow) was too heavy for a deadline-critical payload - a wake-latency fix alone doesn't help if the publish itself is slow. All `grid_voltage_loss`/`GridVoltageLoss` code removed from `IssueLogic::Code`, `issueregistry.cpp`, and the unit tests - see task group 8 for the replacement.

## 8. Direct MQTT alarm (replaces task groups 4 and 7)

- [x] 8.1 New `AlarmEntry` payload type (`mqtt.h`): `eventId` (16-char hex token), `type` (e.g. `"zero_crossing_timeout"`), `unixTimeMs` - a fixed-size POD struct, independent of `IssueLogic`/`IssueRegistry`. Published JSON adds a live `voltage` + per-active-channel `channels` (`channel`/`power`/`pf`) snapshot, fetched at publish time rather than carried in the queued struct. Earlier drafts used `code`/`channel`/`severity`/`message`/ISO-string `timestamp`; simplified after review to match the `unixTime`-raw-ms convention every other structured payload uses, and to keep the wire shape extensible for future alarm `type`s instead of a one-off free-text sentence.
- [x] 8.2 New dedicated MQTT topic/AWS IoT Rule (`MQTT_TOPIC_ALARM "alarm"`, `AWS_IOT_CORE_RULE_ALARM`), following the existing one-rule-per-category convention (`meter`/`grid`/`energy`/`log`).
- [x] 8.3 `Mqtt::pushAlarm()`: FreeRTOS-queue producer (PSRAM-backed static queue, mirrors `pushLog`/`pushMeter`/`pushGrid`), safe to call from any task; wakes the MQTT task via `requestImmediatePublish()`.
- [x] 8.4 `_processAlarmQueue()` drained first in `_handleConnectedState()`, ahead of `_processLogQueue()` and everything else.
- [x] 8.5 `Ade7953::_handleZxtoInterrupt()` calls `Mqtt::pushAlarm()` directly - not routed through `IssueRegistry` (see task group 7 for why).
- [x] 8.6 Replace the count-based burst guard (task group 4) with a flat `ADE7953_ZXTO_SUPPRESS_MS` (60s) suppression window: `_zxtoLastTriggerMs` timestamp, only the first ZXTO since that window elapsed triggers the alarm/FATAL; every other occurrence is `LOG_DEBUG`-only (filtered out of the MQTT log queue by `pushLog()`'s level check, so a runaway condition costs nothing on the MQTT task beyond the one real trigger).
- [x] 8.7 `AWS_IOT_CORE_RULE_ALARM` confirmed provisioned server-side (infra repo).
- [x] 8.8 `pushAlarm()` guarded behind `globalCommunityMode` in `_handleZxtoInterrupt()`, matching every other `Mqtt::push*` call site in `ade7953.cpp` - found by adversarial code review (see design.md Decisions), fixes an unbounded alarm-queue fill + up to 100ms meter-task stall on community-mode devices.

## 9. Bench validation, round 2 (direct alarm path)

- [x] 9.1 Build and flash the direct-alarm-path firmware (task group 8) via `esp32s3-dev-v5`.
- [x] 9.2 Induce a grid-loss event; confirm exactly one `LOG_FATAL` + one alarm publish trace appears (not the old 3-burst pattern), and any further ZXTO within 60s shows only `LOG_DEBUG`.
- [x] 9.3 Confirm the alarm is received on the AWS side (not just that the device-side publish call succeeded) - the outstanding open question from design.md. Confirmed 2026-08-09.
- [x] 9.4 Repeat across multiple plug-pulls spaced more than 60s apart; confirm each produces a fresh trigger. Confirmed working end-to-end.
