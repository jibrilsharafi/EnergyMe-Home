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

## 6. Content-derived record identity

Raised during hardware verification: `crashId` was unix ms minted in `begin()`, which runs before NTP. On a cold boot that is epoch plus a second or two, so two such records collide and the second overwrites the first - the archive losing exactly what it exists to keep.

- [x] 6.1 `crashId` is now the first 16 hex characters of the SHA-256 of the raw dump (`mbedtls_sha256`), hashed before compression so it describes the crash and not the compressor.
- [x] 6.2 New explicit `timestamp` field (unix ms at archive time). Record name is `<timestamp>_<crashId>`: the prefix orders, the id identifies.
- [x] 6.3 `_scanArchive()` sorts on the parsed timestamp; `findArchivedCrashById()` takes a `const char*` and matches the segment after the separator.
- [x] 6.4 `listArchivedCrashes()` derives both fields from the name on the unreadable-metadata path.
- [x] 6.5 `/api/v1/crash/dump?id=` no longer parses an integer; swagger and `crash_dump_analyzer.py` updated for the string id and the new field.

## 7. Boot-path safety

Raised by a real incident: `_archiveCoreDump()` ran inline on the loop task, overflowed its stack inside ROM miniz, and the resulting panic wrote a fresh dump that reproduced the fault on the next boot - an unrecoverable pre-WiFi loop on the bench device (2026-08-06).

- [x] 7.1 Archive moved to a dedicated task, `CRASH_ARCHIVE_TASK_STACK_SIZE` 16 kB, caller waits on a semaphore with a bounded timeout.
- [x] 7.2 `_archiveAttemptCount` in RTC memory, spent before the attempt so a panic mid-archive still costs one; dump discarded after `MAX_CRASH_ARCHIVE_ATTEMPTS`, count reset on success.
- [x] 7.3 `_handleCounters()` moved ahead of the archive so rollback/factory-reset recovery cannot be taken down by a fault in the archive path.
- [x] 7.4 `debugCommand` removed from the payload and the boot log: it named an ELF by build path, meaningless off the build machine. Frees ~2.1 kB of stack across `_logCompleteCrashData` (1600 -> 592 B) and `getCoreDumpInfoJson` (1280 -> 144 B), and cuts ~27% from each crash payload. `crash_dump_analyzer.py` composes the command locally from `addresses` + the ELF it resolves.
- [x] 7.5 `--elf` flag added to `crash_dump_analyzer.py`; its fallback previously hardcoded `.pio/build/esp32s3-dev/`, unusable for any other environment.

## 8. Verification

- [x] 8.1 `pio test -e native` (full suite) from WSL: 334 cases, all passing (302 pre-existing + 32 new).
- [x] 8.2 `pio run -e esp32s3-dev` compiles clean, no warnings from any changed file. RAM 24.9%, flash 57.4%.
- [x] 8.3 Hardware (v5 bench, `esp32s3-dev-v5-bench`): crash -> archive -> gzip -> LittleFS -> partition cleared, over serial from t=0. Confirms ROM miniz works on real silicon - the one piece never previously executed on hardware.
- [x] 8.4 Hardware: `/api/v1/crash/dump` returns a `.gz` that passes `gzip -t` (CRC and length) and inflates to a valid ELF whose size matches `coreDumpRawSize`, proving the hand-written gzip container and the `esp_rom_crc32_le` trailer.
- [x] 8.5 Hardware: `sha256sum` of the inflated dump matches the advertised `crashId`, independently confirming the id is content-derived and that the device digest agrees with a standard implementation.
- [x] 8.6 Hardware: `crash_dump_analyzer.py --elf ...` decodes the backtrace to the exact faulting line (`customserver.cpp:2044`) and verifies the ELF SHA against the record.
- [x] 8.7 Hardware: by-id lookup returns the requested record rather than the oldest, with a matching `Content-Disposition`.
- [x] 8.8 Hardware: crashes archived while cloud was disabled published intact once it was enabled, with `resetReason` "Exception/panic" - the deferred-publish path, exercised incidentally rather than by design.
- [x] 8.9 Retention eviction: deliberately not exercised on hardware. The arithmetic has 32 host unit tests in `crash_archive_policy`, and both halves of the integration - `_scanArchive()` and `removeArchivedCrash()` - ran repeatedly tonight, since every successful publish pops its record that way. Reaching the 10-record cap would also mean disabling cloud first, because publishing drains the archive as it fills.
- [x] 8.10 Cloud (AWS dev, Thing `588c81c479f8`): both archived crashes published as one message each on `energyme/home/v1/+/crash`, landed in `s3://energymesrl-energyme-home-dev-raw/crash/device=588c81c479f8/`. Payload carries `crashId`, `timestamp`, `unixTime`, `firmwareVersion`, `coreDumpEncoding: gzip+base64`, `crashInfo`; no `messageType`/`chunkIndex`/`totalChunks`. From the S3 object alone: base64 -> 8918 gzip bytes (matches `coreDumpCompressedSize`) -> 52428 raw bytes (matches `coreDumpRawSize`) -> valid ELF, and `sha256(dump)[:16]` equals the advertised `crashId`. `esp-coredump` accepts the ELF; its full thread dump needs GDB, absent locally.
- [x] 8.11 Publish cost measured: 11831 B in 153 ms and 12657 B in 159 ms (~77 KB/s), byte counts exact, no retries, ~19% of the 16-bit size guard. Records published one per cycle with telemetry interleaved.

### Validated incidentally

One published record was `1873_6cc030b7f78013b5` - a timestamp of 1873 ms, i.e. archived on a cold boot ~1.8 s after epoch, before NTP. Under the previous clock-derived id this is exactly the record that would have collided with any other cold-boot crash and been overwritten. It published with a unique identity, which is the case section 6 exists to prevent.

### Findings

- A stack-overflow crash can produce no archivable dump: the saved stack exceeds the 64 kB `coredump` partition and `esp_core_dump_image_check()` reports `ESP_ERR_INVALID_SIZE`. Abort and null-deref dumps land at 52-55 kB, already near that ceiling. Worth revisiting the partition size separately.

## 9. Commit

- [x] 9.1 `crash_archive_policy` lib + tests.
- [x] 9.2 Firmware: `crashmonitor`, `mqtt` and `customserver` in one commit. Not split further by concern (frozen reset reason / archive / boot-path safety / content-derived id / single-message publish): removing the chunk API and removing its only callers has to be atomic, and the crash module changes are one arc through the same functions rather than separable edits. Any finer split would produce commits that do not build.
- [x] 9.3 `swagger.yaml`.
- [x] 9.4 `crash_dump_analyzer.py`.
- [x] 9.5 This openspec change.
- [x] 9.6 Dev-only crash trigger endpoints kept: `#ifdef ENV_DEV`, alongside the existing dev-only shadow/command-injection endpoints, and the only way to re-exercise this path on hardware.
- [x] 9.7 v5 bench scaffolding dropped: the `esp32s3-dev-v5-bench` environment, the v5 `HardwareProfile` entry and the `parsePcbRevision()` leniency for a bare `vMAJOR` were all reverted. They existed to test on an old board away from the office; the leniency in particular was never meant to become the contract. The bench board keeps `pcb_revision = "v5"` in NVS and will boot into community mode until its NVS is rewritten as `"v5.0"`.
