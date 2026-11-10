## MODIFIED Requirements

### Requirement: Archiving cannot render the device unbootable
The system SHALL perform the archive on a dedicated FreeRTOS task with a stack sized for the deflate implementation, and SHALL NOT run it on the Arduino loop task. The system SHALL count each archive attempt in RTC-backed storage before the attempt begins, SHALL discard the pending dump and clear the coredump partition once `MAX_CRASH_ARCHIVE_ATTEMPTS` attempts have been spent, and SHALL reset that count on a successful archive. The system SHALL also reset it on any boot that finds no dump pending, so that attempts spent on a dump that is no longer present cannot be charged against the next one - the wait for the archive task is bounded, and a boot that hits that bound returns with an attempt already spent and no way to observe the task's later success. The system SHALL bound its wait for the archive task and continue booting if that bound is exceeded. The boot-loop ladder (`BootRecovery::begin()`), which performs the rollback and factory-reset recovery, SHALL run before LittleFS is mounted and therefore before the archive, so a fault in the archive path cannot prevent recovery.

#### Scenario: Archive faults on every boot
- **WHEN** the archive path crashes or hangs on each of `MAX_CRASH_ARCHIVE_ATTEMPTS` consecutive boots
- **THEN** the pending dump is discarded, the coredump partition is cleared, and the device completes boot rather than looping

#### Scenario: Compression does not exhaust the boot stack
- **WHEN** a core dump is compressed during boot
- **THEN** the compression runs on the dedicated archive task and the loop task's stack canary is not tripped

#### Scenario: Recovery precedes the risky work
- **WHEN** the unstable-boot count has reached its limit on a boot that also has a dump pending
- **THEN** the rollback or factory-reset path runs before the archive is attempted, and the dump is kept so the next boot archives it into the fresh filesystem

#### Scenario: Attempts do not carry over to an unrelated dump
- **WHEN** a boot spends archive attempts on a dump, and a later boot finds no dump pending
- **THEN** the attempt count is back to zero, so the next crash gets the full budget
