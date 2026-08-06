## 1. Archive sizing and retention policy (pure, host-tested)

- [x] 1.1 Add `source/lib/crash_archive_policy/` with `base64EncodedSize`, `maxPublishPayloadBytes`, `fitsPublishLimit`, `canStore` and `evictionCount`. The publish bound is written against PubSubClient's 16-bit remaining-length field (`65535 - topicLength - 2`), not AWS IoT Core's 128 kB limit.
- [x] 1.2 `source/test/test_crash_archive_policy/`: 31 cases covering base64 padding and saturation, the payload budget, the guard at/over the boundary, and eviction under both caps. All passing via `pio test -e native` from WSL.

## 2. Freeze the reset reason

- [ ] 2.1 Add `RTC_NOINIT_ATTR` frozen reset reason + valid flag next to the existing crash/reset counters in `crashmonitor.cpp`.
- [ ] 2.2 Set them in `begin()` where `isLastResetDueToCrash()` is true, alongside `_crashCount++`.
- [ ] 2.3 Clear them in the magic-word reset block, and once the reason has been written into an archived record.
- [ ] 2.4 `getCoreDumpInfoJson()` reports the frozen value instead of calling `esp_reset_reason()` fresh while a dump is pending.

## 3. Archive core dumps to LittleFS

- [ ] 3.1 gzip helper in `crashmonitor.cpp`: `tdefl_init`/`tdefl_compress` with the compressor state in PSRAM, raw deflate flags (no `TDEFL_WRITE_ZLIB_HEADER`), hand-written 10-byte gzip header and CRC-32/ISIZE trailer via `esp_rom_crc32_le(0, ...)`.
- [ ] 3.2 Archive write: mint `crashId`, build metadata, gzip the dump, write `.json` then `.bin.gz`, verify lengths, erase the partition. Delete both and leave the partition intact on any failure.
- [ ] 3.3 Retention enforcement before each write, using `crash_archive_policy`.
- [ ] 3.4 Archive accessors: list, oldest, metadata, dump size, dump read, remove one, clear all.
- [ ] 3.5 Call the archive step from `begin()` and re-point the publish request at the archive rather than `hasCoreDump()`.
- [ ] 3.6 Remove `getCoreDumpChunkJson()` once no caller remains.

## 4. Single-message publish

- [ ] 4.1 Rewrite `_publishCrashJson()`: oldest record, one `JsonDocument`, frozen metadata + `firmwareVersion` + `crashId` + base64 gzipped dump, one `_publishJsonStreaming()` call.
- [ ] 4.2 Apply `fitsPublishLimit()` before publishing; abort loudly and retain the record if it fails.
- [ ] 4.3 Delete the record only on success; re-arm the crash flag while records remain.
- [ ] 4.4 Remove `CORE_DUMP_CHUNK_SIZE` from `mqtt.h` and the chunk loop from `mqtt.cpp`.

## 5. HTTP API and tooling

- [ ] 5.1 `/api/v1/crash/info` lists archived records with their metadata.
- [ ] 5.2 `/api/v1/crash/dump` streams one record's `.bin.gz` as `application/gzip` in a single response; 404 for an unknown id.
- [ ] 5.3 `/api/v1/crash/clear` deletes every archived record.
- [ ] 5.4 Update `source/resources/swagger.yaml` to the new contract.
- [ ] 5.5 Update `source/utils/crash_dump_analyzer.py` to fetch once and gunzip locally instead of looping over chunks.

## 6. Verification

- [ ] 6.1 `pio test -e native` (full suite) from WSL.
- [ ] 6.2 `pio run -e esp32s3-dev` compiles clean.
- [ ] 6.3 Hardware: trigger a real crash, confirm one MQTT message arrives with the correct `resetReason` and a `coreDump` that inflates to a dump `esp-coredump` can parse.
- [ ] 6.4 Hardware: trigger a crash with WiFi disabled, reboot cleanly once, then reconnect - confirm the published `resetReason` still describes the panic (validates the deferred-publish fix).
- [ ] 6.5 Hardware: confirm retention evicts oldest-first past 10 records, and that `/api/v1/crash/dump` returns a `.gz` that gunzips.

## 7. Commit

- [x] 7.1 `crash_archive_policy` lib + tests.
- [ ] 7.2 Frozen reset reason.
- [ ] 7.3 LittleFS archive (closes #114, #115).
- [ ] 7.4 Single-message publish.
- [ ] 7.5 HTTP endpoints + tooling (closes #116).
