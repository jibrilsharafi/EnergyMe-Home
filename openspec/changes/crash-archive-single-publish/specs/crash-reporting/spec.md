## ADDED Requirements

### Requirement: Core dumps are archived to flash at detection
The system SHALL, during `CrashMonitor::begin()` and whenever a core dump image is present in the coredump partition, write the dump to LittleFS as two files under `/crashes/`: `<crashId>_<elfSha256>.json` carrying the crash metadata and `<crashId>_<elfSha256>.bin.gz` carrying the gzip-compressed raw ELF dump. `crashId` SHALL be the Unix time in milliseconds captured at archive time and SHALL be identical in the filename, the metadata and any subsequent publish of that record. The system SHALL erase the coredump partition only after both files are fully written and closed, and SHALL delete both files and leave the partition intact if either write fails.

#### Scenario: Crash produces an archived record
- **WHEN** the device boots with a core dump image present in the coredump partition
- **THEN** `/crashes/<crashId>_<elfSha256>.json` and `/crashes/<crashId>_<elfSha256>.bin.gz` exist, and the coredump partition no longer reports an image

#### Scenario: Archive write fails
- **WHEN** writing either archive file fails
- **THEN** neither file remains on disk and the coredump partition still reports an image, so the archive is retried on the next boot

#### Scenario: Dump compresses to a valid gzip stream
- **WHEN** an archived `.bin.gz` is read off the device
- **THEN** it decompresses with any standard gzip implementation to the raw ELF core dump, whose length and CRC-32 match the gzip trailer

### Requirement: Crash metadata reflects the boot that crashed
The system SHALL capture `esp_reset_reason()` into RTC-backed storage at the point in `CrashMonitor::begin()` where `isLastResetDueToCrash()` is true, and SHALL report that frozen value - not a fresh `esp_reset_reason()` call - as `resetReason` and `resetReasonCode` in the archived metadata while a core dump is pending. The frozen value SHALL be cleared when the RTC magic word is reset and once it has been durably written into an archived record.

#### Scenario: Publish deferred across a clean reboot
- **WHEN** a device panics, then restarts cleanly before the crash record is published
- **THEN** the published record's `resetReason` describes the panic, not the clean restart

#### Scenario: Reset reason frozen at detection
- **WHEN** a core dump is archived
- **THEN** the metadata's `resetReasonCode` equals the `esp_reset_reason()` value observed on the boot that followed the crash

### Requirement: Archive retention is bounded
The system SHALL retain at most 10 archived crash records totalling at most 500 kB, evicting oldest-first before writing a new record. The system SHALL refuse to archive a record that cannot fit the byte budget even with the archive empty.

#### Scenario: Record cap reached
- **WHEN** a new crash is archived while 10 records already exist
- **THEN** the oldest record's `.json` and `.bin.gz` are deleted and the new record is written

#### Scenario: Byte cap reached
- **WHEN** a new crash is archived that would push the archive past 500 kB
- **THEN** records are deleted oldest-first until it fits, and no more than that

### Requirement: One crash is published as one MQTT message
The system SHALL publish each archived crash record to the crash topic as a single message containing `crashId`, `unixTime`, `firmwareVersion`, a `crashInfo` object carrying the frozen metadata, and the base64 encoding of the gzipped dump in `coreDump` with `coreDumpEncoding` set to `"gzip+base64"`, alongside `coreDumpRawSize` and `coreDumpCompressedSize`. The system SHALL NOT emit `messageType`, `chunkIndex`, `totalChunks` or any multi-message sequence. The system SHALL delete an archived record only after its publish succeeds, and SHALL publish at most one record per publish cycle, re-arming the crash publish request while records remain.

#### Scenario: Successful publish
- **WHEN** an archived crash record is published successfully
- **THEN** exactly one message is sent on the crash topic and both files for that record are deleted

#### Scenario: Failed publish
- **WHEN** the publish fails for any reason
- **THEN** the record's files remain on disk and the publish is retried on a later cycle

#### Scenario: Several records queued
- **WHEN** more than one archived record exists at publish time
- **THEN** the oldest is published, and the crash publish request is re-armed so the remainder follow on subsequent cycles

### Requirement: Publish payload size is guarded
The system SHALL verify, before publishing, that the base64-encoded dump plus the surrounding JSON fits within `65535 - topicLength - 2` bytes, and SHALL abort the publish with an error log while retaining the record if it does not. This bound reflects PubSubClient's 16-bit MQTT remaining-length field, which is narrower than AWS IoT Core's 128 kB publish limit.

#### Scenario: Oversized payload
- **WHEN** a record's encoded payload would exceed the publishable size
- **THEN** no publish is attempted, an error is logged, and the record is retained on disk

#### Scenario: Typical compressed payload
- **WHEN** a record whose dump compresses to roughly 12 kB is published
- **THEN** the size check passes and the message is sent

### Requirement: Firmware version is explicit in the crash payload
The system SHALL include a `firmwareVersion` string field carrying `FIRMWARE_BUILD_VERSION` in every published crash message, and SHALL continue to include `appElfSha256` so the record remains matchable by hash.

#### Scenario: Published record carries both identifiers
- **WHEN** a crash record is published
- **THEN** the payload contains `firmwareVersion` (e.g. `"2.2.1"`) and `appElfSha256`

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
