## Purpose

Keeps minute-resolution active energy counters on the device while the cloud is unreachable and backfills them to the cloud after reconnecting, so outages of up to 14 days leave no gap in per-minute energy.

## ADDED Requirements

### Requirement: Minute capture while the cloud is unreachable
While cloud services are enabled, the device is not in community mode, and the wall-clock time is valid, the device SHALL capture one record at every wall-clock minute boundary whose live energy snapshot was not published. A minute counts as not published when the cloud connection is down, whether the network link is down or only MQTT, or when the live publish attempt fails. Each record SHALL contain, for every active channel with valid measurements, the channel index, that channel's real last-read timestamp in milliseconds, and the absolute active energy imported and exported counters at a resolution of 0.1 Wh. The capture SHALL use the same boundary hold as the live snapshot: it waits until every active channel has crossed the boundary, capped by `MQTT_ENERGY_PUBLISH_DEADLINE_SECONDS`. Reactive energy, apparent energy and voltage SHALL NOT be captured.

#### Scenario: WiFi down
- **WHEN** the network link is down across a minute boundary
- **THEN** a record for that boundary is stored with the active import/export counters of every active channel

#### Scenario: Link up, cloud unreachable
- **WHEN** the network link is up but MQTT is disconnected across a minute boundary
- **THEN** a record for that boundary is stored

#### Scenario: Live publish succeeded
- **WHEN** the live energy snapshot for a boundary was published
- **THEN** no record is stored for that boundary

#### Scenario: Clock not valid
- **WHEN** the wall-clock time is not valid (for example, after a reboot with no time sync yet)
- **THEN** nothing is captured until the time becomes valid

#### Scenario: Cloud services disabled
- **WHEN** cloud services are disabled or the device is in community mode
- **THEN** nothing is captured

#### Scenario: Channel timestamp out of range
- **WHEN** a channel's last-read timestamp cannot be represented relative to the record's boundary
- **THEN** that channel is omitted from that record, never stored with a substituted timestamp

### Requirement: Durable storage with bounded size and age
Records SHALL be persisted to flash as they are captured, so a reboot or power loss during the outage keeps every record written before it. Storage SHALL be bounded by all of the following, with the oldest data discarded first: no record older than 14 days is kept, the total buffer size stays within a fixed byte budget, and LittleFS keeps a fixed free-space headroom. A truncated or corrupt record SHALL be skipped on read without discarding the valid records around it. No flash writes for the buffer SHALL occur while every minute is published live.

#### Scenario: Reboot during an outage
- **WHEN** the device reboots while records are buffered
- **THEN** every record written before the reboot is still available for replay after it

#### Scenario: Outage longer than 14 days
- **WHEN** buffered data becomes older than 14 days
- **THEN** it is discarded and never replayed

#### Scenario: Byte budget reached
- **WHEN** a new record would exceed the byte budget or the free-space headroom
- **THEN** the oldest buffered data is discarded to make room

#### Scenario: Power loss mid-write
- **WHEN** power is lost while a record is being appended
- **THEN** on the next read the partial record is skipped and all earlier records are still replayed

### Requirement: Replay after reconnect
After the cloud connection is restored, the device SHALL replay buffered records on the `energy` topic, oldest first, using the energy snapshot wire format with one object per buffered channel reading (see the energy-telemetry-stream capability). Every replayed object SHALL carry the exact timestamp stored for it, so re-sending the same reading is idempotent in the cloud. Replay SHALL be split into messages well under the broker payload limit, and paced so that live energy, meter, grid and alarm publishing keep their normal cadence. Data older than 14 days at send time SHALL be dropped before a message is built. Buffered data SHALL be deleted only after the message carrying it has been published. If replay is interrupted, it SHALL resume from the oldest data not yet deleted, and some readings may be sent twice.

#### Scenario: Backlog replayed in order
- **WHEN** the device reconnects with buffered records
- **THEN** the records are published on the `energy` topic in chronological order, and live snapshots continue at their aligned cadence during replay

#### Scenario: Connection drops during replay
- **WHEN** the connection is lost while replay is in progress
- **THEN** unpublished data stays buffered, minute capture resumes, and replay continues from the oldest remaining data after the next reconnect

#### Scenario: Stale data at send time
- **WHEN** part of the backlog has become older than 14 days by the time it would be sent
- **THEN** that part is dropped and not included in any message

### Requirement: Buffer cleared with the data it belongs to
The buffer SHALL be deleted when cloud services are disabled, on factory reset, and when the energy history is cleared.

#### Scenario: Cloud services disabled with a backlog
- **WHEN** cloud services are disabled while records are buffered
- **THEN** the buffer is deleted and nothing is replayed if cloud services are re-enabled later

#### Scenario: Energy history cleared
- **WHEN** the user clears the energy history
- **THEN** the buffer is deleted along with it
