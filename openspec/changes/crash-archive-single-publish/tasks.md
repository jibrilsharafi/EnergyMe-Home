## 1. Archive sizing and retention policy (pure, host-tested)

- [x] 1.1 Add `source/lib/crash_archive_policy/` with `base64EncodedSize`, `maxPublishPayloadBytes`, `fitsPublishLimit`, `canStore` and `evictionCount`. The publish bound is written against PubSubClient's 16-bit remaining-length field (`65535 - topicLength - 2`), not AWS IoT Core's 128 kB limit.
- [x] 1.2 `source/test/test_crash_archive_policy/`: 31 cases covering base64 padding and saturation, the payload budget, the guard at/over the boundary, and eviction under both caps. All passing via `pio test -e native` from WSL.

## 2. Freeze the reset reason

- [x] 2.1 Add `RTC_NOINIT_ATTR` frozen reset reason + valid flag next to the existing crash/reset counters in `crashmonitor.cpp`.
- [x] 2.2 Set them in `begin()` where `isLastResetDueToCrash()` is true, alongside `_crashCount++`.
- [x] 2.3 Clear them in the magic-word reset block, and once the reason has been written into an archived record.
- [x] 2.4 `getCoreDumpInfoJson()` reports the frozen value instead of calling `esp_reset_reason()` fresh while a dump is pending (via `_getCrashResetReason()`, also used by `_logCompleteCrashData()`).

## 3. Archive core dumps to LittleFS

- [x] 3.1 gzip helper in `crashmonitor.cpp`: `tdefl_init`/`tdefl_compress` with the compressor state in PSRAM, raw deflate flags (no `TDEFL_WRITE_ZLIB_HEADER`), hand-written 10-byte gzip header and CRC-32/ISIZE trailer via `esp_rom_crc32_le(0, ...)`.
- [x] 3.2 Archive write: mint `crashId`, build metadata, gzip the dump, write `.json` then `.bin.gz`, verify lengths, erase the partition. Delete both and leave the partition intact on any failure.
- [x] 3.3 Retention enforcement before each write, using `crash_archive_policy`.
- [x] 3.4 Archive accessors: list, oldest, find-by-id, metadata, dump size, dump read, dump path, remove one, clear all.
- [x] 3.5 Call the archive step from `begin()` (on every boot, not just the crashing one, so a dump left by a failed archive is retried) and re-point the publish request at the archive rather than `hasCoreDump()`.
- [x] 3.6 Remove `getCoreDumpChunkJson()` once no caller remains.
- [x] 3.7 `hasArchivedCrash()` implemented without a directory-entry array: it runs on the MQTT task after every crash publish and inside AsyncTCP request handlers, where ~900 bytes of stack for a yes/no answer is not affordable. `CRASH_ARCHIVE_SCAN_MAX` also reduced 32 -> 16 for the same reason.

## 4. Single-message publish

- [x] 4.1 Rewrite `_publishCrashJson()`: oldest record, one `JsonDocument`, frozen metadata + `firmwareVersion` + `crashId` + base64 gzipped dump, one `_publishJsonStreaming()` call. The base64 is attached as a linked `JsonString` so the ~20 kB buffer is not duplicated into the document.
- [x] 4.2 Apply `fitsPublishLimit()` before publishing (before spending PSRAM on encoding); abort loudly and retain the record if it fails.
- [x] 4.3 Delete the record only on success; re-arm the crash flag in `_publishCrash()` while records remain.
- [x] 4.4 Remove `CORE_DUMP_CHUNK_SIZE` from `mqtt.h` and the chunk loop from `mqtt.cpp`.
- [x] 4.5 Dropped the `#ifndef ENV_DEV` that used to keep the dump on the partition after publishing in dev. The archive supersedes it: records survive reboots, are retrievable over the local API before publication, and cloud services are off by default in dev anyway.

## 5. HTTP API and tooling

- [x] 5.1 `/api/v1/crash/info` lists archived records with their metadata.
- [x] 5.2 `/api/v1/crash/dump` streams one record's `.bin.gz` as `application/gzip` in a single response; `?id=` selects a record, default oldest; 404 for an unknown id.
- [x] 5.3 `/api/v1/crash/clear` deletes every archived record.
- [x] 5.4 Update `source/resources/swagger.yaml` to the new contract.
- [x] 5.5 Update `source/utils/crash_dump_analyzer.py`: single fetch + local gunzip, `--crash-id` selection, `--chunk-size` removed.
- [x] 5.6 `find_matching_elf_in_releases()` now tries a direct `firmwareVersion` match before the SHA256 prefix scan, which is the point of adding the field. SHA256 remains the fallback for records from older firmware, and a version match whose SHA256 disagrees falls through rather than being trusted.
- [x] 5.7 Remove `CRASH_DUMP_DEFAULT_CHUNK_SIZE` / `CRASH_DUMP_MAX_CHUNK_SIZE` from `customserver.h`.

## 6. Verification

- [x] 6.1 `pio test -e native` (full suite) from WSL: 334 cases, all passing (302 pre-existing + 32 new).
- [x] 6.2 `pio run -e esp32s3-dev` compiles clean, no warnings from any changed file. RAM 24.9%, flash 57.4%.
- [ ] 6.3 Hardware: trigger a real crash, confirm one MQTT message arrives with the correct `resetReason` and a `coreDump` that inflates to a dump `esp-coredump` can parse.
- [ ] 6.4 Hardware: trigger a crash with WiFi disabled, reboot cleanly once, then reconnect - confirm the published `resetReason` still describes the panic (validates the deferred-publish fix).
- [ ] 6.5 Hardware: confirm retention evicts oldest-first past 10 records, and that `/api/v1/crash/dump` returns a `.gz` that gunzips.
- [ ] 6.6 Hardware: confirm the archived `.bin.gz` decompresses to an ELF that `esp-coredump` parses, proving the hand-written gzip container and the `esp_rom_crc32_le` trailer are correct.

## 7. Commit

- [x] 7.1 `crash_archive_policy` lib + tests.
- [ ] 7.2 Frozen reset reason.
- [ ] 7.3 LittleFS archive (closes #114, #115).
- [ ] 7.4 Single-message publish.
- [ ] 7.5 HTTP endpoints + tooling (closes #116).
