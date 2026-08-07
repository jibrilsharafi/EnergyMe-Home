## ADDED Requirements

### Requirement: Core dumps are archived to flash at detection
The system SHALL, during `CrashMonitor::begin()` and whenever a core dump image is present in the coredump partition, write the dump to LittleFS as two files under `/crashes/`: `<timestamp>_<crashId>.json` carrying the crash metadata and `<timestamp>_<crashId>.bin.gz` carrying the gzip-compressed raw ELF dump. The system SHALL erase the coredump partition only after both files are fully written and closed, and SHALL delete both files and leave the partition intact if either write fails.

#### Scenario: Crash produces an archived record
- **WHEN** the device boots with a core dump image present in the coredump partition
- **THEN** `/crashes/<timestamp>_<crashId>.json` and `/crashes/<timestamp>_<crashId>.bin.gz` exist, and the coredump partition no longer reports an image

#### Scenario: Archive write fails
- **WHEN** writing either archive file fails
- **THEN** neither file remains on disk and the coredump partition still reports an image, so the archive is retried on the next boot

#### Scenario: Dump compresses to a valid gzip stream
- **WHEN** an archived `.bin.gz` is read off the device
- **THEN** it decompresses with any standard gzip implementation to the raw ELF core dump, whose length and CRC-32 match the gzip trailer

### Requirement: Record identity is derived from the dump, not the clock
The system SHALL identify each archived record by a `crashId` that is the first 16 hexadecimal characters of the SHA-256 of the raw ELF core dump, and SHALL carry the Unix time in milliseconds at archive time as a separate `timestamp` field. The `crashId` SHALL be identical in the filename, the metadata and any publish of that record. The `timestamp` SHALL order the archive and SHALL NOT be used as an identity: records are archived during `CrashMonitor::begin()`, before the network is up and therefore before NTP, so after a cold boot it is close to the Unix epoch and not unique.

#### Scenario: Identity survives an unset clock
- **WHEN** two different crashes are archived on cold boots before the clock is set, and so share a near-identical timestamp
- **THEN** they receive different `crashId` values and both records exist, neither having overwritten the other

#### Scenario: Identity is reproducible from the dump
- **WHEN** an archived dump is decompressed and hashed with any standard SHA-256 implementation
- **THEN** the first 16 hexadecimal characters of the digest equal the record's `crashId`

#### Scenario: Retried archive does not duplicate
- **WHEN** archiving the same pending dump is attempted again after a failed attempt
- **THEN** the record is written under the same name rather than added a second time beside itself

### Requirement: Archiving cannot render the device unbootable
The system SHALL perform the archive on a dedicated FreeRTOS task with a stack sized for the deflate implementation, and SHALL NOT run it on the Arduino loop task. The system SHALL count each archive attempt in RTC-backed storage before the attempt begins, SHALL discard the pending dump and clear the coredump partition once `MAX_CRASH_ARCHIVE_ATTEMPTS` attempts have been spent, and SHALL reset that count on a successful archive. The system SHALL also reset it on any boot that finds no dump pending, so that attempts spent on a dump that is no longer present cannot be charged against the next one - the wait for the archive task is bounded, and a boot that hits that bound returns with an attempt already spent and no way to observe the task's later success. The system SHALL bound its wait for the archive task and continue booting if that bound is exceeded. `_handleCounters()`, which performs the rollback and factory-reset recovery, SHALL run before the archive so a fault in the archive path cannot prevent recovery.

#### Scenario: Archive faults on every boot
- **WHEN** the archive path crashes or hangs on each of `MAX_CRASH_ARCHIVE_ATTEMPTS` consecutive boots
- **THEN** the pending dump is discarded, the coredump partition is cleared, and the device completes boot rather than looping

#### Scenario: Compression does not exhaust the boot stack
- **WHEN** a core dump is compressed during boot
- **THEN** the compression runs on the dedicated archive task and the loop task's stack canary is not tripped

#### Scenario: Recovery precedes the risky work
- **WHEN** the consecutive crash or reset counters have reached their limits on a boot that also has a dump pending
- **THEN** the rollback or factory-reset path runs before the archive is attempted

#### Scenario: Attempts do not carry over to an unrelated dump
- **WHEN** a boot spends archive attempts on a dump, and a later boot finds no dump pending
- **THEN** the attempt count is back to zero, so the next crash gets the full budget

### Requirement: Crash metadata reflects the boot that crashed
The system SHALL capture `esp_reset_reason()` into RTC-backed storage at the point in `CrashMonitor::begin()` where `isLastResetDueToCrash()` is true, and SHALL report that frozen value - not a fresh `esp_reset_reason()` call - as `resetReason` and `resetReasonCode` in the archived metadata while a core dump is pending. The frozen value SHALL be cleared when the RTC magic word is reset and once it has been durably written into an archived record.

#### Scenario: Publish deferred across a clean reboot
- **WHEN** a device panics, then restarts cleanly before the crash record is published
- **THEN** the published record's `resetReason` describes the panic, not the clean restart

#### Scenario: Reset reason frozen at detection
- **WHEN** a core dump is archived
- **THEN** the metadata's `resetReasonCode` equals the `esp_reset_reason()` value observed on the boot that followed the crash

### Requirement: Archive retention is bounded
The system SHALL retain at most 10 archived crash records totalling at most 500 kB, evicting oldest-first before writing a new record. The byte total SHALL count both files of each record, since the cap is a ceiling on the `/crashes/` directory. The system SHALL refuse to archive a record that cannot fit the byte budget even with the archive empty.

A directory scan SHALL be able to observe every retained record. Because a record is two files, any bound on the number of directory entries walked SHALL be at least twice the record cap; a bound expressed in records rather than files would hide every record past the halfway point and silently disable both caps.

#### Scenario: Record cap reached
- **WHEN** a new crash is archived while 10 records already exist
- **THEN** the oldest record's `.json` and `.bin.gz` are deleted and the new record is written

#### Scenario: Byte cap reached
- **WHEN** a new crash is archived that would push the archive past 500 kB
- **THEN** records are deleted oldest-first until it fits, and no more than that

#### Scenario: Full archive is fully visible
- **WHEN** the archive holds the maximum number of records, so `/crashes/` holds twice that many files
- **THEN** a scan reports every record, and both retention caps are evaluated against the complete set

### Requirement: A partially written record is never accepted
The system SHALL compare the number of bytes actually written for each archive file against the length that file was expected to have, and SHALL treat any mismatch as a failed archive. Testing the metadata write for a non-zero length alone is insufficient: a nearly full filesystem accepts part of the document and reports the bytes it took, which would leave truncated JSON behind after the coredump partition has already been erased.

#### Scenario: Filesystem accepts only part of the metadata
- **WHEN** the metadata write returns a byte count lower than the serialized document's length
- **THEN** the record is deleted, the coredump partition is left intact, and the archive is retried on the next boot

### Requirement: One crash is published as one MQTT message
The system SHALL publish each archived crash record to the crash topic as a single message containing `crashId`, `timestamp`, `unixTime`, `firmwareVersion`, a `crashInfo` object carrying the frozen metadata, and the base64 encoding of the gzipped dump in `coreDump` with `coreDumpEncoding` set to `"gzip+base64"`, alongside `coreDumpRawSize` and `coreDumpCompressedSize`. The published fields SHALL be the stored metadata document verbatim plus the dump, so a record's representation over MQTT and over the local HTTP API cannot diverge. `timestamp` is when the record was archived; `unixTime` is when it was published. The system SHALL NOT emit `messageType`, `chunkIndex`, `totalChunks` or any multi-message sequence. The system SHALL delete an archived record only after its publish succeeds, and SHALL publish at most one record per publish cycle.

The crash publish request SHALL be re-armed on an interval while records remain, in the same way as the other periodic publishers, rather than only from the success path of a previous publish. A transient failure SHALL therefore cost one interval; it SHALL NOT stop crash publishing until the device reboots.

#### Scenario: Successful publish
- **WHEN** an archived crash record is published successfully
- **THEN** exactly one message is sent on the crash topic and both files for that record are deleted

#### Scenario: Failed publish
- **WHEN** the publish fails for any reason
- **THEN** the record's files remain on disk and the publish is retried on a later cycle

#### Scenario: Several records queued
- **WHEN** more than one archived record exists at publish time
- **THEN** the oldest is published, and the remainder follow on subsequent cycles

#### Scenario: Transient failure does not stall the queue
- **WHEN** a publish fails and the device stays up without rebooting
- **THEN** the crash publish is re-armed once the interval elapses and the same record is retried

### Requirement: Publish payload size is guarded
The system SHALL verify, before publishing, that the base64-encoded dump plus the surrounding JSON fits within `65535 - topicLength - 2` bytes, and SHALL abort the publish with an error log while retaining the record if it does not. This bound reflects PubSubClient's 16-bit MQTT remaining-length field, which is narrower than AWS IoT Core's 128 kB publish limit.

A record rejected by this guard can never become publishable, so the system SHALL step over it and attempt the next-oldest record rather than retrying it. Retrying it would leave it permanently at the head of the queue, and no later crash would ever reach the cloud. The number of records stepped over in one cycle SHALL be bounded.

#### Scenario: Oversized payload
- **WHEN** a record's encoded payload would exceed the publishable size
- **THEN** no publish is attempted for it, an error is logged, and the record is retained on disk

#### Scenario: Oversized record does not block the queue
- **WHEN** the oldest archived record cannot fit one publish and a newer record can
- **THEN** the newer record is published, and the oversized one stays on disk for retrieval over the local HTTP API

#### Scenario: Typical compressed payload
- **WHEN** a record whose dump compresses to roughly 12 kB is published
- **THEN** the size check passes and the message is sent

### Requirement: Firmware version is explicit in the crash payload
The system SHALL include a `firmwareVersion` string field carrying `FIRMWARE_BUILD_VERSION` in every published crash message, and SHALL continue to include `appElfSha256` so the record remains matchable by hash.

#### Scenario: Published record carries both identifiers
- **WHEN** a crash record is published
- **THEN** the payload contains `firmwareVersion` (e.g. `"2.2.1"`) and `appElfSha256`

### Requirement: Crash records carry backtrace addresses, not a decode command
The system SHALL report the backtrace as its `depth`, `corrupted` flag and `addresses` array, and SHALL NOT include a prebuilt `addr2line` invocation in the record or in the boot log. A decode command names an ELF by its build path, which identifies nothing on any machine other than the one that produced the firmware, while the record exists to be read elsewhere.

#### Scenario: Record is consumed off-device
- **WHEN** an archived or published crash record is read
- **THEN** it contains `addresses` and `appElfSha256`, and contains no `debugCommand` field

### Requirement: Archived dumps are retrievable over HTTP in one request
The system SHALL expose the archived records over the local HTTP API: a listing endpoint returning each record's metadata, a retrieval endpoint returning a single record's stored gzip bytes in one response as `application/gzip`, and a clear endpoint deleting all archived records. The retrieval endpoint SHALL NOT wrap the dump in JSON or require the client to reassemble chunks.

#### Scenario: Retrieve an archived dump
- **WHEN** a client requests an archived record's dump by `crashId`
- **THEN** the response body is the stored `.bin.gz` bytes, complete, in a single response

#### Scenario: Retrieve an unknown record
- **WHEN** a client requests a `crashId` that is not archived
- **THEN** the endpoint responds 404 and no body is streamed

#### Scenario: Clear the archive
- **WHEN** a client calls the clear endpoint
- **THEN** every `.json` and `.bin.gz` under `/crashes/` is deleted
