## Context

Crash reporting today is entirely partition-backed. `CrashMonitor::begin()` calls `esp_core_dump_init()`, and if `isLastResetDueToCrash()` it bumps the counters and logs a summary via `_checkAndPrintCoreDump()`. The dump itself stays in the 64 kB `coredump` partition (`partitions_esp32s3_n16r2.csv`) until `_publishCrashJson()` ships it or a later crash overwrites it. Every reader - `getCoreDumpInfoJson()`, `getCoreDumpChunk()`, the three `/api/v1/crash/*` endpoints - reads that partition live.

That single design decision is what produces all three defects in the proposal: publish-time reads mean publish-time `esp_reset_reason()`, no persistence means an offline crash is lost, and a 4 kB chunk loop over a live partition means partial sequences with no recovery.

Two facts bound the solution:

- **The coredump partition is 64 kB**, so a raw dump can never exceed that. Production dumps run 15.6-60.7 kB (avg 48 kB).
- **PubSubClient 2.8.0 cannot publish 64 KiB or more.** `beginPublish(topic, plength, retained)` takes `plength` as `unsigned int` but passes `plength + length - MQTT_MAX_HEADER_SIZE` into `size_t buildHeader(uint8_t, uint8_t*, uint16_t length)` (`PubSubClient.cpp:535`, `PubSubClient.h:109`). The implicit narrowing truncates the MQTT remaining-length field modulo 2^16; the broker then reads a short payload and treats the rest as garbage packets, dropping the connection.

The second fact invalidates the original framing of this work, which assumed AWS IoT Core's 128 kB limit was binding and that a base64 dump topping out at ~81 kB was therefore safe. It is not: base64 of the 48 kB production average is already 65,536 bytes, over the ceiling. Roughly half of production crashes would fail to publish.

## Goals / Non-Goals

**Goals:**
- One MQTT message per crash, landing whole or not at all.
- Crash metadata that describes the boot that crashed, regardless of when it is published.
- Core dumps that survive reboots and offline periods, bounded in storage.
- A publish-size guard that fails loudly rather than corrupting the MQTT stream.
- Direct cloud-side ELF lookup by firmware version.

**Non-Goals:**
- Replacing or patching PubSubClient (#137).
- Out-of-band dump upload (presigned S3).
- On-device decompression for the HTTP endpoint.
- Changing the crash/reset counter, safe-mode, or rollback logic - untouched.

## Decisions

**1. Archive to LittleFS at detection, then erase the partition. The archive becomes the source of truth.**
The partition is a fixed-size, single-slot, live-read buffer; a filesystem is not. Moving the dump off it at the one moment its metadata is correct is what makes every other property fall out for free: the reset reason is frozen because it is written to a file, publishing may defer for days because nothing it reads can change, offline crashes survive because they are on disk, and retries are cheap because the record is still there. Alternative considered: keep the partition authoritative and freeze only the reset reason into RTC (the literal ask). Rejected - it fixes the symptom in the proposal's defect (2) but leaves (3) entirely, and leaves publish reading a mutable source.

**2. Two files per record: `<crashId>_<elfSha256>.json` and `<crashId>_<elfSha256>.bin.gz`.**
Metadata stays outside the gzip so `/api/v1/crash/info` and the publish path can read a reset reason without inflating anything, and so the record is legible straight off the filesystem. `crashId` is unix milliseconds minted at archive time, and is the same value in the filename, the metadata, the MQTT payload and the resulting S3 key - one identifier end to end. Note this differs from today, where `crashId` is minted per publish attempt (`mqtt.cpp:2232`) and so changes on every retry. Name length is 30 characters (13-digit id + `_` + 9-char SHA per `CONFIG_APP_RETRIEVE_LEN_ELF_SHA=9` + suffix) against `CONFIG_LITTLEFS_OBJ_NAME_LEN=64`; the buffer is sized from `APP_ELF_SHA256_SZ` rather than hardcoded so a future sdkconfig change cannot silently overflow it.

**3. gzip via ROM miniz, compressor state in PSRAM.**
`esp32s3.rom.ld` exports the miniz `tdefl_*` symbols and the framework ships `esp_rom/include/miniz.h`, so deflate costs zero flash and adds no dependency. The header unconditionally defines `MINIZ_NO_ZLIB_APIS` and `MINIZ_NO_ZLIB_COMPATIBLE_NAMES` (lines 30, 33), so including it leaks no `compress`/`crc32` macros. The low-level `tdefl_init`/`tdefl_compress` pair never allocates - the ~164 kB `tdefl_compressor` is caller-owned - so it goes in PSRAM via `ps_malloc` and is freed immediately; the ~95 kB internal-heap allocation `tdefl_compress_mem_to_mem()` would have made is avoided. `tdefl_create_comp_flags_from_zip_params` is *not* in ROM, so flags are built directly: `TDEFL_DEFAULT_MAX_PROBES` with no `TDEFL_WRITE_ZLIB_HEADER` yields a raw deflate stream, which is wrapped in a gzip container by hand (10-byte header, CRC-32 and ISIZE trailer). The CRC comes from `esp_rom_crc32_le(0, buf, len)`: working through the `~`-convention documented in `esp_rom_crc.h`, gzip's CRC-32 (init `0xFFFFFFFF`, xorout `0xFFFFFFFF`, reflected) reduces exactly to a plain call with init 0.
Compressing at *archive* time rather than publish time is deliberate: the compressor then runs once, during `setup()`, with PSRAM uncontended and before any service task exists; storage shrinks 3-5x so the record cap binds before the byte cap; and the publish path only has to base64 an already-small file, making retries cheap.

**4. Retention: 10 records / 500 kB, oldest evicted first, enforced before each write.**
Gzipped dumps average ~12 kB, so the record cap binds first in practice (~120 kB) and the byte cap is the backstop for a pathological dump that compresses badly. The arithmetic lives in `lib/crash_archive_policy` rather than inline so it is host-testable; `canStore()` is separate from `evictionCount()` because the latter cannot distinguish "evict everything, then store" from "this can never fit".

**5. Publish guard written against 64 KiB, not 128 kB.**
`fitsPublishLimit(compressedSize, metadataBytes, topicLength)` checks the base64 expansion plus JSON overhead against `65535 - topicLength - 2`. With compression the guard has roughly a 3x margin and should never fire; it exists so that if some future dump compresses badly the publish fails loudly with the record retained, rather than silently corrupting the MQTT stream. `base64EncodedSize()` saturates instead of wrapping on absurd inputs, because a wrapped result would be small enough to read as "fits" - the one failure mode a guard must not have.

**6. One crash per publish cycle, flag re-armed while records remain.**
A backlog of 10 records is ~200 kB of back-to-back publishes; draining it in one task slice would delay meter and log publishing. Publishing the oldest and re-arming keeps each slice bounded and lets normal telemetry interleave.

**7. The frozen reset reason still goes in RTC, narrowly.**
Archiving covers the normal path. The RTC pair covers the case where the LittleFS write fails (full, corrupt) and the dump stays on the partition to be retried a boot later, when `esp_reset_reason()` already describes the wrong boot. Cleared with the other RTC state on magic-word reset, and once the reason has been durably written into an archived record.

**8. `/api/v1/crash/dump` serves the stored `.gz` unmodified.**
`application/gzip` with `Content-Encoding: gzip`, so `requests` and browsers inflate transparently and the saved artifact is a normal `.gz`. Inflating on-device would cost a `tinfl` pass and a 64 kB PSRAM buffer per request to reproduce bytes the client can trivially produce itself.

## Risks / Trade-offs

- **Breaking change for the cloud ingest.** One message replaces the `crashInfo`/`crashChunk`/`crashComplete` sequence, and `coreDump` is gzipped before base64. The ingest must branch on `coreDumpEncoding` (`"gzip+base64"` vs absent/`"base64"`) to keep reading records from firmware still in the field. Mitigated by `firmwareVersion` now being explicit in the payload.
- **A crash during the archive write leaves a partial record.** Mitigated by writing metadata first, verifying the dump write length, deleting both files on any failure, and erasing the partition only after both are closed - so a failed archive retries next boot from an intact partition rather than losing the dump.
- **Boot-time cost.** Compressing ~48 kB adds a few hundred milliseconds to `setup()`, on the crash path only. Acceptable against losing the dump entirely.
- **`crashId` semantics change.** It is now stable per crash rather than per publish attempt. Downstream grouping keyed on `crashId` gets strictly better (retries no longer create duplicate ids), but anything that assumed uniqueness *per message* must be checked.
- **RTC time may be invalid** if the very first boot crashes before any NTP sync, yielding a near-zero `crashId`. The dump is still archived and published; only the id is degraded.
