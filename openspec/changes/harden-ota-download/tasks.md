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

- [x] 5.1 Extend `_publishOtaStatus` (`source/src/mqtt.cpp:2747`) to accept optional diagnostic details and emit them as sibling keys of `reason` in `statusDetails`, leaving `reason` unchanged and every existing call site working as before
- [x] 5.2 Render each value into a small `char` buffer with `snprintf`, since `statusDetails` is a string-to-string map, and keep every key within 128 chars matching `[a-zA-Z0-9:_-]+` and every value within 1024 chars and free of control characters
- [x] 5.3 Populate `espError`, `progress`, `freeHeap`, `minFreeHeap`, `maxAlloc`, `attempts`, `uptime` and `rssi` on the download-failure path only
- [x] 5.4 Sample the heap figures immediately after the failing `esp_https_ota()` returns inside the retry loop, not after the loop unwinds, so they describe the moment of failure
- [x] 5.5 Confirm the other `FAILED` reasons (`partition_error`, `sha256_read_error`, `preferences_error`, `sha256_mismatch_firmware_rollback`) report exactly as before
- [x] 5.6 Add a DEBUG log line with the same heap figures before and after each attempt, so the same data is visible over the UDP log without waiting for the job status
- [x] 5.7 Run `pio run`

## 6. Verify on hardware

- [ ] 6.1 Flash the bench device and confirm a normal OTA job still succeeds end to end with no regression to job status reporting or the reboot-and-validate flow
- [ ] 6.2 Confirm over the UDP log that telemetry, logs and shadow updates keep flowing normally for the whole retry schedule, since nothing is suppressed any more
- [ ] 6.3 Using a dev build, run an OTA job with a deliberately unreachable host to exercise the full retry schedule, then confirm the `FAILED` job execution in AWS carries all eight `statusDetails` keys (reason, espError, httpStatus, progress, heapFreeMinMax, attempts, uptime, rssi) with sensible values
- [ ] 6.4 Confirm from the same run that `httpStatus` is what separates a server refusal from a transport failure, since `espError` reports bare `ESP_FAIL` for every 4xx/5xx, and that a simulated 4xx breaks the schedule instead of running all five attempts
- [ ] 6.5 Shorten the backoff constants temporarily if the full 29 minute schedule makes 6.3 impractical, and restore them before committing
- [ ] 6.6 Confirm the AWS job's `timeoutConfig`, if set, exceeds the worst-case retry window, so an execution cannot flip to `TIMED_OUT` before the device reports
- [ ] 6.7 Consider subscribing to `jobs/+/update/rejected`: an UpdateJobExecution rejection (pair-count limit, or an execution already TIMED_OUT) is currently discarded silently, so the device would look locally successful while AWS shows nothing

## 7. Review and merge

- [ ] 7.1 Firmware version in `source/include/constants.h`: NOT part of this change. Version bumps are Jibril's call and are done manually by him, never as part of implementing a change.
- [x] 7.2 Run the code-review agents over the branch diff, with at least one adversarial brief given this touches the OTA and network path, and reproduce each finding before accepting or dismissing it
- [x] 7.3 Run the simplification agent
- [x] 7.4 Triage every finding, fix or document each with its reason, and re-run `pio run` and `pio test -e native`
- [x] 7.5 Open the PR to `development` with `Closes #191` in the body and a label
- [ ] 7.6 Retitle issue #191, whose current title names the task-suspension mechanism this change evaluated and rejected
- [ ] 7.7 Follow-up, not this PR: apply the `_finishOtaTask` single-exit pattern to `_otaValidationTask`, which still repeats `handle = nullptr; vTaskDelete(nullptr)` at six exit points
