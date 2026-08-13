## Context

See proposal.md - Why. The constraints that shape the approach, all verified against the code and the pinned core rather than assumed:

- The OTA download is one call to `esp_https_ota()` in `_performOtaUpdate` (`mqtt.cpp:1460`), driven by `_otaTask` (`mqtt.cpp:1505`). The task self-deletes on every exit path.
- `_checkPublishMqtt` (`mqtt.cpp:2051`) is the single dispatch point for all seven publish types, so publish suppression needs one gate, not seven.
- `_publishOtaStatus` (`mqtt.cpp:2747`) builds `statusDetails` as a JSON object with one `reason` key. AWS IoT `UpdateJobExecution` types `statusDetails` as a string-to-string map, keys up to 128 chars matching `[a-zA-Z0-9:_-]+`, values up to 1024 chars with no control characters. Sibling keys are therefore native, and every value must be rendered as a string.
- `calculateExponentialBackoff` already exists at `utils.cpp:1070` with the signature this change needs, but lives in `src/`, which the `native` test environment cannot compile.
- `source/lib/` already holds 15 pure modules. `version_compare` is the closest template: a namespaced pure module with a thin forwarder left behind at `utils.cpp:2455`.
- The presigned URL is minted when the device picks up the job, not when the job is created, so the device owns the full 60 minute lifetime from its own start of work.

## Goals / Non-Goals

**Goals:**

- Give the download the quietest internal heap the device can offer without severing the MQTT session or touching library-owned tasks.
- Make a failed OTA self-describing, so the next investigation reads one job execution record instead of correlating telemetry against job timestamps.
- Keep the post-download flow (partition read, SHA256 capture, preferences write, reboot) untouched.

**Non-Goals:**

- Reducing steady-state internal-heap fragmentation. That is a separate reliability concern and is not addressed here.
- Guaranteeing a download succeeds on a device whose steady-state contiguous block sits below the mbedTLS requirement. On such a device this change converts a silent failure into a described one, nothing more.
- Treating the local web-UI OTA upload path (`customserver.cpp:1406-1548`). It runs on the `async_tcp` task, carries no TLS, and is out of scope.

## Decisions

### Suppress publishes with a single gate rather than suspending tasks

An `_otaDownloadInProgress` atomic flag guards the publish section of `_handleConnectedState`, and is cleared only when the OTA task terminates.

Chosen over suspending the InfluxDB, custom MQTT, UDP log, web server and Modbus TCP tasks, which is what the originating issue proposed. That approach does not survive contact with the code: ESPAsyncWebServer and eModbus have no app-owned task and both run on AsyncTCP's single shared `async_tcp` task, which calls `esp_task_wdt_add(NULL)` by default, so suspending it panic-reboots the device mid-download. InfluxDB and custom MQTT are disabled by default and hold their buffers in PSRAM, and the UDP log queue is PSRAM as well. `vTaskSuspend` frees nothing a task has already allocated in any case.

**The gate must sit above the whole publish section, not inside `_checkPublishMqtt`.** The first attempt put it there and it was wrong in a way worth recording. The log queue publishes one MQTT message per entry and is drained outside `_checkPublishMqtt`, so it kept publishing. Worse, the `_checkIfPublish*Needed` checks run every 100 ms tick and their "already published" markers are only advanced *inside* the publishers being held off, so each condition stayed true and re-logged on every tick. Those log lines then queued and published. Gating only the publishers therefore converted a quiet window into a publish storm on the very heap the change exists to protect, and drove `_checkIfPublishCrashNeeded` into a 10 Hz LittleFS directory walk contending with the OTA partition writes.

Outside the gate by design: `_clientMqtt.loop()`, so the session stays connected and inbound job messages keep arriving; the alarm queue, which is safety-critical; and `_drainPendingCommand()`, so an operator can still intervene during a schedule that can run 29 minutes.

The flag is written by `_otaTask` and read by the MQTT task, so it needs to be safe for concurrent access without a mutex. A plain `volatile bool` is not sufficient as a matter of principle; use an atomic. A mutex is the wrong tool for a single boolean read on the publish path.

**Suppression is only a delay for flag-driven publishes, not for queued data.** The meter and grid queues keep filling and are drop-oldest, and their capacities (roughly 34 minutes of grid points, single-digit minutes of meter points) are shorter than the worst-case retry window. A long schedule therefore loses queued points, counted in the existing dropped-point statistics. That is the accepted cost, not a claim that nothing is lost.

**Open question for the hardware pass.** The per-publish internal-RAM saving may be much smaller than the motivation implies. Publish bodies allocate from PSRAM, and the only internal-RAM allocation on the publish path is a 256-byte `BufferingPrint`. The 16-20 KB dip per TLS publish that motivated this was measured on core 3.3.11, where WiFi/LWIP buffers had moved into internal RAM; on the pinned 55.03.32 with `SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` they are back in PSRAM. Group 6 should measure `maxAlloc` during a download with and without suppression before this is claimed to be the main lever.

### Retry in `_otaTask`, not inside `_performOtaUpdate`

The loop wraps `_performOtaUpdate` so the DNS probe at `mqtt.cpp:1463-1475` does not re-run on every attempt. That probe opens a plaintext `WiFiClient` to port 443 purely to log whether the host resolves, and repeating it per attempt would add a socket allocation to the very heap the change is trying to protect. Keeping the loop in `_otaTask` also leaves `_performOtaUpdate` as a single-attempt function that returns enough for the caller to report on.

`_performOtaUpdate` currently returns `bool`. It needs to hand back the `esp_err_t` and the byte progress instead, so the caller can build the status details. An out-parameter struct keeps the change local and avoids allocating.

Each `esp_https_ota()` call performs its own `esp_ota_begin`, so a retry restarts the write from scratch. With `bulk_flash_erase = false` the partition is erased as it goes, so a retry re-erases what the failed attempt wrote. That is slower but correct, and no partial-image state survives between attempts.

### Backoff of 5 attempts, 2 min initial, x2, capped at 15 min

Delays are 2, 4, 8 and 15 minutes (the fourth doubling to 16 is clamped), for 29 minutes of cumulative waiting. Adding attempt durations puts the worst case near 44 minutes, inside the 60 minute presigned-URL lifetime with roughly 15 minutes of margin.

Chosen over a tighter schedule because the failure being defended against is heap pressure, which does not clear in seconds, and over a longer one because the URL lifetime is a hard ceiling. `multiplier = 2` also takes the existing helper's bit-shift branch rather than its loop.

No elapsed-time guard, deliberately. Aborting early because the URL might expire buys nothing: an expired URL simply fails the attempt, and that failure is now reported with an error name that identifies it. A guard would add a failure mode without removing one.

Attempt 1 runs immediately, so the delay for attempt N is the wait *before* attempt N+1. This keeps the mapping onto `calculateExponentialBackoff(attempt, ...)`, which already returns 0 for attempt 0.

### Discrete `statusDetails` keys, not a stringified JSON blob

`statusDetails` is natively a string-to-string map, so nesting encoded JSON inside one value would force a parser onto the infra side and lose queryability. Keys, with values rendered via `snprintf` into small `char` buffers:

| Key | Value |
|---|---|
| `reason` | unchanged, still `download_failed` |
| `espError` | `esp_err_to_name()` of the final attempt's return |
| `progress` | `"<bytesReceived>/<contentLength>"` from the existing HTTP event handler counters |
| `freeHeap`, `minFreeHeap`, `maxAlloc` | internal-heap triple, same sources as `utils.cpp:125` |
| `attempts` | attempts made |
| `uptime` | seconds |
| `rssi` | dBm |

Eight keys. `espError` and `progress` carry the most diagnostic weight: today an allocation failure, a DNS failure, a TLS reject and a 404 all collapse into the same `download_failed` string, and there is no way to tell a download that died at 5% from one that died at 95%.

Heap figures are sampled immediately after the failing `esp_https_ota()` returns, inside the loop. Sampling after the loop unwinds would record recovered heap and describe the wrong moment.

Target version, checksum, job id and device id are excluded: AWS already holds all of them.

### Extract only the backoff schedule to `source/lib/`

A new `source/lib/backoff_schedule/` module holds the pure computation, mirroring `version_compare`: a namespaced pure function, with `calculateExponentialBackoff` in `utils.cpp` reduced to a forwarder so existing callers (`influxdbclient.cpp` and the new OTA loop) are untouched.

The remaining pure helpers in `utils.cpp` (`roundToDecimals`, `isValueInRange`, `isStringLengthValid`, `endsWith`, `startsWith`, `getContentTypeFromFilename`) stay where they are. Sweeping them out is a separate concern and would turn this into a mega-commit.

### Remove the dead pause/resume API

`Ade7953::pauseTasks()` / `resumeTasks()` (`ade7953.cpp:562-582`, `ade7953.h:619-620`) have no callers anywhere in the firmware. They were the nearest precedent for the task-suspension approach this change rejects, so leaving them in place invites a future reader to reach for a pattern that was evaluated and dropped.

## Risks / Trade-offs

- **A failed job now reports up to ~44 minutes after the download starts, instead of immediately.** → Accepted deliberately. The device publishes `IN_PROGRESS` when it accepts the job, so the execution is not silent in the meantime. If a job-level timeout is configured on the AWS side it must exceed the retry window, or the execution flips to `TIMED_OUT` before the device reports. Confirm the job's `timeoutConfig` before the first fleet job on this firmware.
- **Telemetry withheld during the window is lost on a successful download**, because the device reboots. → Bounded by the download duration and accepted: energy totals are persisted separately, so this loses live telemetry, not accounting.
- **A retry re-erases and rewrites the OTA partition each attempt.** → Flash wear across 5 attempts on a firmware image is negligible against the partition's endurance, and the alternative (resuming a partial image) needs state that the failure modes here do not justify.
- **The `_otaInProgress` flag must be cleared on every path out of the retry loop**, or the device stops publishing telemetry until it reboots. The three early-return branches after a successful download (`mqtt.cpp:1516`, `1526`, `1540`) are past the loop and so are safe, but the flag must still be cleared before the loop's own exits. → Set and clear it in the narrowest scope around the retry loop only, not across the whole of `_otaTask`.
- **Two devices could sit in a retry schedule while a fleet job rolls on.** → No mitigation needed; each execution is independent and the rollout's own failure thresholds still apply, just on a slower clock.
- **Suppressing publishes hides a device that is otherwise healthy for up to 44 minutes.** → The MQTT session stays connected throughout, so the device does not appear offline; only its telemetry pauses.
