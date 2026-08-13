## Why

The AWS IoT Jobs OTA download is a single unretried `esp_https_ota()` call that reports every failure as the bare string `download_failed`. When it fails there is no way to tell an internal-heap exhaustion (`esp-aes: Failed to allocate memory` / `MBEDTLS_ERR_SSL_ALLOC_FAILED`) from a DNS failure, a TLS reject, or a stalled transfer, and no second attempt is made even when the presigned URL stays valid for another hour. Diagnosing the 2026-06-22 and 2026-08-12 failures both required manually correlating separate device telemetry against job timestamps after the fact.

The failures that motivated this were of two different kinds, and only one of them is still live. The 2026-08-12 fleet event (3 of 6 beta devices) was the 55.03.311 platform regression, fixed by reverting the pin. What remains, on the good core, is the 2026-06-22 case: a single prod device whose internal heap had collapsed to a 15.8 KB largest contiguous block while eight fleet siblings held a steady 31.7 KB. That is device-specific fragmentation, it is rare, and the firmware's response to it should be to retry and to say precisely what happened, not to be silently marked `download_failed`.

## What Changes

- Retry `esp_https_ota()` up to 5 attempts with exponential backoff (2 min initial, x2, capped at 15 min) before reporting `FAILED`. Worst case is roughly 44 min, inside the 60 min presigned-URL lifetime, which starts when the device picks up the job.
- Abandon the schedule early on a 4xx, which cannot succeed on a later attempt with the same URL, and on module shutdown.
- Report diagnostics on failure as discrete `statusDetails` name-value pairs rather than a single opaque reason: the failing `esp_err_t` name, the HTTP status, download progress, the internal-heap figures, attempt count, uptime and RSSI. The existing `reason` key keeps its current value so nothing downstream breaks.
- Extract the exponential-backoff schedule into `source/lib/backoff_schedule/` as a pure, host-testable module, following the `version_compare` pattern, with `utils.cpp` keeping a thin forwarder. Other pure helpers in `utils.cpp` are deliberately left where they are.
- Remove `Ade7953::pauseTasks()` / `Ade7953::resumeTasks()`, which have no callers anywhere in the firmware.

Explicitly **not** in this change, after evaluating each against the code:

- Suspending non-essential tasks. ESPAsyncWebServer and eModbus share AsyncTCP's single `async_tcp` task, which registers with the task watchdog by default, so suspending it panic-reboots the device mid-download. InfluxDB and custom MQTT are disabled by default and hold their buffers in PSRAM; the UDP log queue is PSRAM too. The measured win does not justify the risk.
- Suppressing MQTT publishes during the download, which is the other half of what the issue proposes. Every `JsonDocument` on the publish path is built with `SpiRamAllocator`, so a publish costs 256 bytes of internal RAM (one `BufferingPrint`), not the 16-20 KB the issue implies. That figure was measured on core 3.3.11, where `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` had been dropped and WiFi/LWIP buffers sat in internal RAM; on the reverted pin they are back in PSRAM. Suppression would buy a few hundred bytes and cost real telemetry, since the meter and grid queues are drop-oldest and hold less than the worst-case retry window. The fleet had healthy OTAs on this platform pin before the bump, which is the strongest evidence that publish contention is not the problem to solve.
- `partial_http_download`. Verified against the ESP-IDF v5.5 source: it only adds `Range:` headers and does not change any buffer allocation. The documented memory saving requires lowering `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` in sdkconfig, which is baked into the prebuilt Arduino core libs and unreachable from this project.
- Disconnecting the AWS IoT TLS session for the download window, and shrinking mbedTLS buffers via `custom_sdkconfig`. Both would free far more internal RAM than anything here, and both are deferred as too invasive for now.
- A pre-flight heap gate. Failing fast buys nothing over letting the download fail and reporting why.

## Capabilities

### New Capabilities
- `ota-download-resilience`: how the firmware OTA download behaves under contention and failure - publish quiescing during the download window, retry scheduling, and the diagnostic detail reported with a failed job execution.

### Modified Capabilities

(none - `ota-job-version-guard` governs whether a job is accepted, which is unchanged; this change governs what happens after the download starts)

## Impact

- `source/src/mqtt.cpp`: `_otaTask` retry loop, `_performOtaUpdate` return signature, `_publishOtaStatus` status details, `_checkPublishMqtt` gate.
- `source/include/mqtt.h`: retry and backoff constants, OTA-in-progress state.
- `source/lib/backoff_schedule/`: new pure module.
- `source/src/utils.cpp`, `source/include/utils.h`: `calculateExponentialBackoff` becomes a forwarder.
- `source/src/ade7953.cpp`, `source/include/ade7953.h`: dead pause/resume API removed.
- `source/test/`: native Unity tests for the backoff schedule.
- Fleet-facing: `statusDetails` on a `FAILED` OTA job execution gains new keys. Additive only, `reason` is unchanged. A `FAILED` report can now arrive up to ~44 min after the download starts instead of immediately.
- No effect on devices already stranded on an older firmware; this hardens future downloads only.
