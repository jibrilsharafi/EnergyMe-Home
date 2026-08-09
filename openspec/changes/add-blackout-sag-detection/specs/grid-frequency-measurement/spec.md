## MODIFIED Requirements

### Requirement: Interrupt demux services every set status bit
The ADE7953 task SHALL read `RSTIRQSTATA` exactly once per wake and SHALL service every enabled interrupt bit set in that single snapshot (ZXV, CYCEND, RESET, CRC, SAG) with independent checks, never returning after the first match. The task SHALL NOT re-read the status register within the same wake to test further bits.

#### Scenario: ZXV and CYCEND co-pending in one wake
- **WHEN** the task wakes and the status snapshot has both ZXV and CYCEND set
- **THEN** the PERIOD read/EMA update and the full CYCEND energy handling both execute in that wake, with no energy reading lost

#### Scenario: No drain loop on continuous ZXV
- **WHEN** a new ZXV asserts after the status read-and-clear
- **THEN** it raises a new IRQ edge and is handled on the next wake, not by re-reading status in the current wake

#### Scenario: SAG co-pending with ZXV in one wake
- **WHEN** the task wakes and the status snapshot has both ZXV and SAG set (the common case, since ZXV fires every line cycle)
- **THEN** both the PERIOD read/EMA update and the SAG handling execute in that wake, with neither one suppressing the other

## ADDED Requirements

### Requirement: Unhandled interrupt detection catches bits outside the handled set
The interrupt demux SHALL flag a status snapshot as containing an unhandled interrupt whenever any bit outside the currently-serviced set is present, regardless of whether a recognized bit is also present in the same snapshot. A snapshot SHALL NOT be treated as fully handled merely because it contains at least one recognized bit.

#### Scenario: Unrecognized bit co-occurs with a recognized bit
- **WHEN** the status snapshot has ZXV (recognized) set together with a bit not in the serviced set
- **THEN** the unhandled-interrupt counter increments and a warning is logged, in addition to ZXV being serviced normally

#### Scenario: Snapshot has only recognized bits
- **WHEN** every bit set in the status snapshot is in the serviced set
- **THEN** the unhandled-interrupt counter does not increment and no warning is logged
