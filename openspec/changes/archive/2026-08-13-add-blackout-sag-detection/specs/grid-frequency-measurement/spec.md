## MODIFIED Requirements

### Requirement: Interrupt demux services every set status bit
The ADE7953 task SHALL read `RSTIRQSTATA` exactly once per wake and SHALL service every enabled interrupt bit set in that single snapshot (ZXV, ZXTO, CYCEND, RESET, CRC) with independent checks, never returning after the first match. The task SHALL NOT re-read the status register within the same wake to test further bits.

#### Scenario: ZXV and CYCEND co-pending in one wake
- **WHEN** the task wakes and the status snapshot has both ZXV and CYCEND set
- **THEN** the PERIOD read/EMA update and the full CYCEND energy handling both execute in that wake, with no energy reading lost

#### Scenario: No drain loop on continuous ZXV
- **WHEN** a new ZXV asserts after the status read-and-clear
- **THEN** it raises a new IRQ edge and is handled on the next wake, not by re-reading status in the current wake

#### Scenario: ZXTO co-pending with ZXV in one wake
- **WHEN** the task wakes and the status snapshot has both ZXV and ZXTO set
- **THEN** both the PERIOD read/EMA update and the ZXTO handling execute in that wake, with neither one suppressing the other

## ADDED Requirements

### Requirement: Unhandled interrupt detection catches enabled-but-unserviced bits
The interrupt demux SHALL flag a status snapshot as containing an unhandled interrupt whenever any bit that is both enabled in `IRQENA` and outside the currently-serviced set is present, regardless of whether a recognized bit is also present in the same snapshot. A snapshot SHALL NOT be treated as fully handled merely because it contains at least one recognized bit. Because `IRQSTATA` reports every channel/energy event continuously regardless of `IRQENA` (enable only gates the physical IRQ pin, not the status bit), the check SHALL be restricted to bits actually enabled in `IRQENA` - a bit that was never enabled (e.g. routine current-channel or energy-accumulation status noise) SHALL NOT be treated as unhandled.

#### Scenario: Unrecognized-but-enabled bit co-occurs with a recognized bit
- **WHEN** the status snapshot has ZXV (recognized) set together with an enabled bit not in the serviced set
- **THEN** the unhandled-interrupt counter increments and a warning is logged, in addition to ZXV being serviced normally

#### Scenario: Snapshot has only recognized bits
- **WHEN** every bit set in the status snapshot is in the serviced set
- **THEN** the unhandled-interrupt counter does not increment and no warning is logged

#### Scenario: A never-enabled bit is present in the raw status word
- **WHEN** the status snapshot has a bit set that was never enabled in `IRQENA` (e.g. current-channel zero-crossing or energy-register status noise)
- **THEN** the unhandled-interrupt counter does not increment and no warning is logged for that bit
