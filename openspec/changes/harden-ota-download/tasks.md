Each numbered group is one commit. Run the applicable tests before committing each group rather than batching them.

## 1. Extract the backoff schedule into a host-testable module

- [x] 1.1 Create `source/lib/backoff_schedule/backoff_schedule.h` and `.cpp` following the `source/lib/version_compare/` pattern: SPDX header, `#pragma once`, a `BackoffSchedule` namespace, and a header comment documenting the clamping and overflow behaviour
- [x] 1.2 Move the body of `calculateExponentialBackoff` (`source/src/utils.cpp:1070`) into the module as a pure function taking attempt, initial interval, max interval and multiplier, keeping the existing attempt-0 and overflow-clamping semantics
- [x] 1.3 Reduce `calculateExponentialBackoff` in `utils.cpp` to a thin forwarder so `influxdbclient.cpp` and every other existing caller is untouched; leave the declaration in `utils.h` in place
- [x] 1.4 Add native Unity tests under `source/test/` covering the 2/4/8/15/15 minute schedule for attempts 1-5, attempt 0 returning zero, clamping at the max interval, the `multiplier == 2` bit-shift branch, and a large attempt number that would overflow
- [x] 1.5 Run `pio test -e native` from WSL and confirm the new tests pass
- [x] 1.6 Run `pio run` and confirm both firmware environments still build

## 2. Remove the dead pause/resume API

- [x] 2.1 Delete `Ade7953::pauseTasks()` and `Ade7953::resumeTasks()` from `source/src/ade7953.cpp:562-582` and their declarations at `source/include/ade7953.h:619-620`
- [x] 2.2 Confirm by grep that no callers remain anywhere under `source/`, then run `pio run`

## 3. Suppress non-essential MQTT publishes during the download window

- [x] 3.1 Add OTA-in-progress state to the MQTT module as an atomic boolean, safe for the OTA task to write and the MQTT task to read without a mutex
- [x] 3.2 Gate the body of `_checkPublishMqtt` (`source/src/mqtt.cpp:2051`) on that flag so meter, grid, energy, systemDynamic, statistics, crash and requestOta are all withheld while it is set, leaving the `_publishMqtt.*` request flags set so withheld publishes fire on the next cycle after the window
- [x] 3.3 Confirm `_publishOtaStatus` is unaffected, since it is called directly from `_otaTask` and does not route through `_checkPublishMqtt`
- [x] 3.4 Confirm the MQTT task still runs `_clientMqtt.loop()` and stays connected and subscribed while suppressed, so inbound job messages and commands keep flowing
- [x] 3.5 Run `pio run`

## 4. Retry the download on an exponential backoff

- [x] 4.1 Add constants to `source/include/mqtt.h` for max attempts (5), initial backoff (2 min), max backoff (15 min) and multiplier (2), each with a comment tying the schedule to the 60 minute presigned-URL lifetime
- [x] 4.2 Change `_performOtaUpdate` (`source/src/mqtt.cpp:1460`) to report the `esp_err_t` and the byte progress to its caller via an out-parameter struct instead of returning a bare `bool`
- [x] 4.3 Capture bytes received and content length from the existing `_otaHttpEventHandler` counters (`source/src/mqtt.cpp:1439-1449`) into that struct so progress survives the failing call
- [x] 4.4 Wrap the `_performOtaUpdate` call in `_otaTask` (`source/src/mqtt.cpp:1508`) in a retry loop using `BackoffSchedule`, leaving the DNS probe at `mqtt.cpp:1463-1475` to run once per download rather than once per attempt
- [x] 4.5 Set the OTA-in-progress flag before the first attempt and clear it on every exit from the retry loop, including the success path, so the device cannot be left permanently silent
- [x] 4.6 Verify the post-download flow (partition read, SHA256, preferences, reboot) and its three early returns at `mqtt.cpp:1516`, `1526` and `1540` are unchanged and sit outside the retry loop
- [x] 4.7 Sleep between attempts in a way that still honours the module's existing shutdown notification, so a restart request during a backoff wait is not blocked for up to 15 minutes
- [x] 4.8 Run `pio run`

## 5. Report device-side diagnostics on failure

- [ ] 5.1 Extend `_publishOtaStatus` (`source/src/mqtt.cpp:2747`) to accept optional diagnostic details and emit them as sibling keys of `reason` in `statusDetails`, leaving `reason` unchanged and every existing call site working as before
- [ ] 5.2 Render each value into a small `char` buffer with `snprintf`, since `statusDetails` is a string-to-string map, and keep every key within 128 chars matching `[a-zA-Z0-9:_-]+` and every value within 1024 chars and free of control characters
- [ ] 5.3 Populate `espError`, `progress`, `freeHeap`, `minFreeHeap`, `maxAlloc`, `attempts`, `uptime` and `rssi` on the download-failure path only
- [ ] 5.4 Sample the heap figures immediately after the failing `esp_https_ota()` returns inside the retry loop, not after the loop unwinds, so they describe the moment of failure
- [ ] 5.5 Confirm the other `FAILED` reasons (`partition_error`, `sha256_read_error`, `preferences_error`, `sha256_mismatch_firmware_rollback`) report exactly as before
- [ ] 5.6 Add a DEBUG log line with the same heap figures before and after each attempt, so the same data is visible over the UDP log without waiting for the job status
- [ ] 5.7 Run `pio run`

## 6. Verify on hardware

- [ ] 6.1 Flash the bench device and confirm a normal OTA job still succeeds end to end with no regression to job status reporting or the reboot-and-validate flow
- [ ] 6.2 Confirm over the UDP log that meter, grid, energy, statistics and crash publishes stop for the download window and resume afterwards, and that the MQTT session stays connected throughout
- [ ] 6.3 Using a dev build, run an OTA job with a deliberately unreachable host to exercise the full retry schedule, then confirm the `FAILED` job execution in AWS carries all eight `statusDetails` keys with sensible values
- [ ] 6.4 Confirm from the same run that `espError` distinguishes the unreachable-host failure from an allocation failure
- [ ] 6.5 Shorten the backoff constants temporarily if the full 29 minute schedule makes 6.3 impractical, and restore them before committing
- [ ] 6.6 Confirm the AWS job's `timeoutConfig`, if set, exceeds the worst-case retry window, so an execution cannot flip to `TIMED_OUT` before the device reports

## 7. Review and merge

- [ ] 7.1 Bump the firmware version in `source/include/constants.h`
- [ ] 7.2 Run the code-review agents over the branch diff, with at least one adversarial brief given this touches the OTA and network path, and reproduce each finding before accepting or dismissing it
- [ ] 7.3 Run the simplification agent
- [ ] 7.4 Triage every finding, fix or document each with its reason, and re-run `pio run` and `pio test -e native`
- [ ] 7.5 Open the PR to `development` with `Closes #191` in the body and a label
- [ ] 7.6 Retitle issue #191, whose current title names the task-suspension mechanism this change evaluated and rejected
