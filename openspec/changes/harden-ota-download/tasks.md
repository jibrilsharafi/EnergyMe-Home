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
- [x] 4.2 Superseded by the actual shape: `_performOtaUpdate` keeps returning a bare `bool`; the `esp_err_t` and byte progress instead land in a shared `OtaAttempt` struct that the HTTP event handler also writes into, since that handler is a C callback with no context of its own and shared state is unavoidable there
- [x] 4.3 Capture bytes received and content length from the existing `_otaHttpEventHandler` counters (`source/src/mqtt.cpp:1439-1449`) into that struct so progress survives the failing call
- [x] 4.4 Wrap the `_performOtaUpdate` call in `_otaTask` (`source/src/mqtt.cpp:1508`) in a retry loop using `BackoffSchedule`, leaving the DNS probe at `mqtt.cpp:1463-1475` to run once per download rather than once per attempt
- [x] 4.5 Superseded: this described a `_otaDownloadInProgress` MQTT-publish-suppression flag. That mechanism was implemented, reviewed, and then removed in `27df03a` once the actual per-publish internal-RAM cost turned out to be ~256 bytes, not the 16-20 KB the originating issue assumed; see `design.md`'s "No publish suppression during the download" for the full reasoning
- [x] 4.6 Verify the post-download flow (partition read, SHA256, preferences, reboot) and its three early returns at `mqtt.cpp:1516`, `1526` and `1540` are unchanged and sit outside the retry loop
- [x] 4.7 Sleep between attempts in a way that still honours the module's existing shutdown notification, so a restart request during a backoff wait is not blocked for up to 15 minutes
- [x] 4.8 Run `pio run`

## 5. Report device-side diagnostics on failure

- [x] 5.1 Extend `_publishOtaStatus` (`source/src/mqtt.cpp:2747`) to accept optional diagnostic details and emit them as sibling keys of `reason` in `statusDetails`, leaving `reason` unchanged and every existing call site working as before
- [x] 5.2 Render each value into a small `char` buffer with `snprintf`, since `statusDetails` is a string-to-string map, and keep every key within 128 chars matching `[a-zA-Z0-9:_-]+` and every value within 1024 chars and free of control characters
- [x] 5.3 Populate `espError`, `httpStatus`, `progress`, `heapFreeMinMax` (free/min-free/max-alloc merged into one key, keeping the pair count well clear of the `statusDetails` limit), `attempts`, `uptime` and `rssi` on the download-failure path only
- [x] 5.4 Sample the heap figures immediately after the failing `esp_https_ota()` returns inside the retry loop, not after the loop unwinds, so they describe the moment of failure
- [x] 5.5 Confirm the other `FAILED` reasons (`partition_error`, `sha256_read_error`, `preferences_error`, `sha256_mismatch_firmware_rollback`) report exactly as before
- [x] 5.6 Add a DEBUG log line with the same heap figures before and after each attempt, so the same data is visible over the UDP log without waiting for the job status
- [x] 5.7 Run `pio run`

## 8. Make the download path testable on hardware

- [x] 8.1 Add a dev-only `POST /api/v1/ota/inject-job` that routes a synthetic job document through the same validate-and-handle path the broker RX uses, following the existing `inject-command` / `inject-delta` pattern. Without it the download failure modes could only be reached by minting real AWS jobs
- [x] 8.2 Stage the injected payload onto the MQTT task rather than handling it inline: the caller is the web server task, and `_handleSingleJobExecution` publishes `IN_PROGRESS` on the shared, unsynchronised `PubSubClient`
- [x] 8.3 Give it its own pending slot so an injected job and an injected command cannot evict each other

## 6. Verify on hardware

Bench device `588c81c479f8` at 192.168.1.82, `esp32s3-dev-v5`, dev AWS account.
Driven through the dev-only `POST /api/v1/ota/inject-job` seam added in group 8
for the synthetic cases, and through real AWS IoT jobs for the end-to-end ones.

- [x] 6.1 Flash the bench device and confirm a normal OTA job still succeeds end to end with no regression to job status reporting or the reboot-and-validate flow — real AWS job `energyme-home-dev-ota-e2e-retry-20260813-145616` against a genuine presigned S3 URL: downloaded 2.6 MB on attempt 1 in 94 s, rebooted, booted the delivered image, and reported `SUCCEEDED` after the 300 s validation window. Status walked `downloading` → `rebooting` → `SUCCEEDED`
- [x] 6.2 Confirm telemetry, logs and shadow updates keep flowing normally for the whole retry schedule, since nothing is suppressed any more — the `meter` shadow was 98 s old when checked mid-schedule, during the attempt-4 backoff
- [x] 6.3 Using a dev build, run an OTA job with a deliberately unreachable host to exercise the full retry schedule, then confirm the `FAILED` job execution in AWS carries all eight `statusDetails` keys with sensible values — real AWS job `energyme-home-dev-ota-fail-diag-20260813-150045` reported `FAILED` with all eight accepted, none rejected or truncated: `attempts 5`, `espError ESP_ERR_HTTP_CONNECT`, `heapFreeMinMax 52644/39028/31732`, `httpStatus 0`, `progress n/a`, `reason download_failed`, `rssi -86`, `uptime 73`. The unreachable-host schedule was also run once at the real constants, taking exactly 29 min across 5 attempts
- [x] 6.4 Confirm `httpStatus` is what separates a server refusal from a transport failure, and that a 4xx breaks the schedule instead of running all five attempts — an S3 404 reports `ESP_FAIL` + `httpStatus 404` and fails after 1 attempt in 0.65 s; an unresolvable host reports `ESP_ERR_HTTP_CONNECT` + `httpStatus 0` and runs the full schedule. This found a real defect: the status was read in `HTTP_EVENT_ON_HEADER`, which runs before the client assigns it, so every refusal reported `-1` and the break never fired
- [x] 6.5 Backoff timing verified at the real constants rather than shortened: 120 000, 240 000, 480 000 and 900 000 ms between attempts, the last being the 15 minute clamp. Shortened constants are used only for the throwaway recovery build, which is never committed
- [x] 6.6 Confirm the AWS job's `timeoutConfig` exceeds the worst-case retry window — the dev jobs set `inProgressTimeoutInMinutes: 60` against a worst case of ~44 min of download plus the ~5 min validation window. It fits, but the margin is ~10 min: raising `OTA_DOWNLOAD_MAX_ATTEMPTS` or the cap without also raising the job timeout would make executions flip to `TIMED_OUT` before the device reports
- [x] 6.8 Confirm the concurrent-job guard still holds over the widened window — this change stretches the OTA task's life from one download to ~44 min, during which MQTT reconnects re-deliver the still-`IN_PROGRESS` job. A second job injected mid-schedule was dropped before the `Received OTA Job` log line
- [x] 6.9 Confirm a failed attempt followed by a successful one on the same URL completes the download and proceeds to the post-download flow — real AWS job `energyme-home-dev-ota-recovery-20260813-150046`, with a throwaway build forcing the first two attempts to fail. Attempts 1 and 2 failed at the 5 s and 10 s shortened delays, attempt 3 downloaded the image from the same presigned URL, and the job reported `SUCCEEDED`. This is the one behaviour the change exists for, and it cannot be staged without either forcing the failures or controlling the device's uplink
- [ ] 6.7 Follow-up, not this PR: subscribe to `jobs/+/update/rejected`. An UpdateJobExecution rejection is still discarded silently, so the device would look locally successful while AWS shows nothing. The specific worry that motivated it is settled — AWS accepted all eight pairs in 6.3 — but the blind spot itself remains
- [ ] 6.10 Follow-up, not this PR: `minFreeHeap` read 8128 bytes on the fifth attempt of the 29 min run, having been 32048 at the fourth. It is a since-boot watermark over a 31 min window rather than something the diff introduces, and `maxAlloc` recovered to 31732 between attempts throughout, so nothing here ratchets. Worth a look on its own

## 7. Review and merge

- [ ] 7.1 Firmware version in `source/include/constants.h`: NOT part of this change. Version bumps are Jibril's call and are done manually by him, never as part of implementing a change.
- [x] 7.2 Run the code-review agents over the branch diff, with at least one adversarial brief given this touches the OTA and network path, and reproduce each finding before accepting or dismissing it. Re-run on the diff since the first pass once the hardware-testing round added the `httpStatus` capture fix and the dev-only inject-job seam
- [x] 7.3 Run the simplification agent (also re-run on the same later diff)
- [x] 7.4 Triage every finding, fix or document each with its reason, and re-run `pio run` and `pio test -e native`
- [x] 7.5 Open the PR to `development` with `Closes #191` in the body and a label
- [ ] 7.6 Retitle issue #191, whose current title names the task-suspension mechanism this change evaluated and rejected
- [ ] 7.7 Follow-up, not this PR: apply the `_finishOtaTask` single-exit pattern to `_otaValidationTask`, which still repeats `handle = nullptr; vTaskDelete(nullptr)` at six exit points
- [ ] 7.8 Follow-up, not this PR: `_captureOtaHttpStatus` works around the event-based status API; `esp_https_ota_get_status_code()` would let it collapse to one read, but only against the handle from the granular `esp_https_ota_begin`/`_perform`/`_finish` sequence, which this PR's retry loop was not built or hardware-tested against. Consider the switch once the current retry/backoff behaviour has run in the fleet
