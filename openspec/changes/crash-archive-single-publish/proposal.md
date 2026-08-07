## Why

Crash reporting has three defects that compound each other, all visible in production data (40 events across 12 devices, prod S3 bucket):

1. **Chunked publish loses data silently.** `_publishCrashJson()` splits each crash into a `crashInfo` message, N `crashChunk` messages of 4 kB, and a `crashComplete` message correlated by `crashId`. There is no resume or retry granularity, so a failure part-way leaves a permanently incomplete record with no downstream way to detect or recover it. 10% of production crash events (4/40) never produced a `crashComplete`.

2. **The reported reset reason is usually wrong.** `Mqtt::requestCrashPublish()` fires on every boot where `hasCoreDump()` is true, not only the boot that crashed. `getCoreDumpInfoJson()` then calls `esp_reset_reason()` live at publish time, which describes the *current* boot. Production shows crashes with `hasCoreDump: true` reporting `"resetReason": "Software"` - a normal restart - instead of the panic that produced the dump.

3. **Core dumps are lost on the next restart.** The dump lives only in the 64 kB coredump partition until it is published or overwritten. `_checkAndPrintCoreDump()` logs a summary and nothing else, so a device that crashes while offline loses the dump entirely (#115), and there is no way to match a dump to the firmware build that produced it offline (#114).

A single-message publish fixes (1) outright - a publish either fully lands or it does not. Persisting the dump to LittleFS at detection time fixes (2) and (3) structurally rather than by patching the symptom: metadata is frozen to disk at the one moment it is correct, so publishing may defer arbitrarily without going stale.

## What Changes

- **Archive core dumps to LittleFS at boot, then erase the partition.** On detection, capture the summary metadata, gzip the raw ELF dump, and write `/crashes/<timestamp>_<crashId>.json` + `.bin.gz`. Retention: 10 records / 500 kB, oldest evicted first. The partition is erased only after both files are durably written. The work runs on a dedicated task with a stack sized for deflate, under an RTC-backed attempt budget so a fault in this pre-network path can never hold the device in a boot loop.
- **Identify a record by its content, not by the clock.** `crashId` is the SHA-256 prefix of the dump; a separate `timestamp` field carries unix ms and orders the archive. Records are written before NTP, so a clock-derived id would collide - and overwrite - across cold-boot crashes.
- **Freeze the reset reason.** A new `RTC_NOINIT_ATTR` pair captures `esp_reset_reason()` at the point `isLastResetDueToCrash()` is true in `begin()`. It backs the archived metadata and covers the narrow window where the archive write itself fails and the dump retries a boot later.
- **Publish one crash as one MQTT message.** One `JsonDocument` carrying the frozen metadata plus the base64 of the gzipped dump, in one `_publishJsonStreaming()` call. The record is deleted only after the publish succeeds. One crash per publish cycle; the flag re-arms while more remain.
- **Add `firmwareVersion` to the payload** so cloud-side ELF lookup is a direct `fw/{firmwareVersion}/*.elf` fetch instead of brute-forcing `appElfSha256` against every mirrored build. `appElfSha256` is kept as an integrity check and as the fallback for older firmware.
- **Guard the publish size explicitly.** PubSubClient 2.8.0 builds the MQTT remaining-length field through `buildHeader(uint8_t, uint8_t*, uint16_t)` while `beginPublish()` hands it a 32-bit length, so a payload at or above 64 KiB is truncated modulo 2^16 and the broker mis-frames the packet. That ceiling - not AWS IoT Core's documented 128 kB limit - is what the guard is written against.
- **Serve archived dumps over HTTP in one request.** `/api/v1/crash/dump` streams the stored `.bin.gz` as `application/gzip` instead of wrapping 1 kB slices in JSON.
- **Remove the chunk path**: `CORE_DUMP_CHUNK_SIZE`, the chunk loop, the `crashInfo`/`crashChunk`/`crashComplete` message types, and `getCoreDumpChunkJson()`.

## Capabilities

### New Capabilities
- `crash-reporting`: On-flash crash archive lifecycle, frozen crash metadata, single-message MQTT crash publish with an explicit payload-size guard, and single-request HTTP retrieval of archived dumps.

### Modified Capabilities
(none - no existing spec covers crash reporting today)

## Impact

- `source/lib/crash_archive_policy/`: new pure library for base64 sizing, the publish-limit guard, and retention/eviction math. Host-tested under `source/test/test_crash_archive_policy/`.
- `source/include/crashmonitor.h` / `source/src/crashmonitor.cpp`: frozen-reset-reason RTC state; gzip via ROM miniz (`tdefl_init`/`tdefl_compress`, state in PSRAM) with an `esp_rom_crc32_le` trailer; archive write, listing, read and eviction; `getCoreDumpChunkJson()` removed.
- `source/src/mqtt.cpp` / `source/include/mqtt.h`: `_publishCrashJson()` rewritten to a single publish; `CORE_DUMP_CHUNK_SIZE` removed.
- `source/src/customserver.cpp` / `source/include/customserver.h`: crash endpoints read the archive; `/api/v1/crash/dump` serves the gzip file directly; `CRASH_DUMP_*_CHUNK_SIZE` removed.
- `source/resources/swagger.yaml`, `source/utils/crash_dump_analyzer.py`: updated to the single-request binary contract.
- **Breaking, cloud side**: the crash topic carries one message per crash instead of a `crashInfo`/`crashChunk`/`crashComplete` sequence, and `coreDump` is gzipped before base64 (`coreDumpEncoding: "gzip+base64"`). The ingest must branch on `coreDumpEncoding` to keep reading records from older firmware.
- No new dependency and no flash cost: miniz is in the ESP32-S3 mask ROM (`esp32s3.rom.ld`, `Group miniz`) with its header shipped at `esp_rom/include/miniz.h`, and the low-level `tdefl` API never allocates.

## Non-goals

- Replacing PubSubClient, or patching its 16-bit remaining-length field (#137). Compressing before publish makes the ceiling non-binding, so the vendored-fork maintenance cost is not taken on here.
- Uploading dumps out-of-band (presigned S3) instead of over MQTT.
- Decompressing dumps on-device for the HTTP endpoint; clients receive the `.gz` and inflate locally.
