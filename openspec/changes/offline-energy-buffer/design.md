## Context

See proposal.md for motivation. The current state that shapes the approach:

- The MQTT task loop calls `_handleConnectedState()` only when `CustomNet::isFullyConnected()` and the client is connected. Energy boundary scheduling (`_checkIfPublishEnergyNeeded`) lives inside it, so today nothing at all happens at a minute boundary while offline.
- `MeterValues` energy counters are `double` in Wh. The cloud receives them rounded to `ENERGY_DECIMALS` (0.1 Wh).
- LittleFS is 7.2 MB, shared with logs, the hourly/daily/monthly energy CSVs and config. The maintenance task wipes all logs if free space falls under 10 KB (`MINIMUM_FREE_LITTLEFS_SIZE`).
- There is no battery-backed RTC. `CustomTime` keeps a persisted time floor, not a usable clock. Whether system time survives `esp_restart()` while NTP is unreachable has to be measured on the bench (task 1.1).
- Cloud: `energy_processor` validates every element against `EnergyReading` (all five counters required today) and rejects the whole message if any element is older than 15 days (`freshness.check_fresh`). Rows land in ClickHouse `meter_state` (`AggregatingMergeTree`, key `(device_id, channel, ts)`, `anyLast` on `Nullable` columns) and in Iceberg, which dedups on the same key. The writer Lambda and queue keep legacy `influx` names, but the store is ClickHouse.

## Goals / Non-Goals

**Goals:**
- Lossless minute-resolution active energy for outages up to 14 days, as long as the device clock stays valid.
- Zero buffer flash writes while online.
- A replay that is idempotent in the cloud, so crash or retry paths never need exactly-once delivery.

**Non-Goals:**
- Backfilling power (`meter`), grid, voltage, reactive or apparent energy.
- Recovering minutes captured while the clock is invalid (for example after a reboot with no NTP).
- Any change to the live snapshot cadence or payload.

## Decisions

### D1. Capture runs in the MQTT task, outside the connected branch
A new step in `_mqttTask` runs on every loop pass whenever cloud services are enabled and the device is not in community mode, whatever the connection state. It owns its own "next boundary" target and reuses `MqttEnergyPublishGate::shouldPublishNow` and `_allActiveChannelsFreshSinceBoundary`, so the captured readings match what a live snapshot at that boundary would have contained. While connected, the live path runs as today. The live snapshot runs first in each loop pass.
- A boundary already sent live is skipped.
- While connected, an unsent boundary is held in RAM. A live success for the same boundary clears it. It is written to flash at the next boundary, or as soon as the link drops.
- While disconnected, it is written at once.
- The live target resets on every connect.
- A live retry gives up one minute after its boundary, so a late live send can never duplicate a buffered minute with a later timestamp.
*Alternative*: a separate FreeRTOS task. Rejected because it needs another internal-RAM stack and its own synchronisation with the publish state the MQTT task already owns.

### D2. Record format: absolute uint32 at 0.1 Wh, per-channel ms offset
One binary record per captured minute:
`magic u16 | version u8 | channel count u8 | boundary unix s u32 | channel mask u32 | n × (offset ms i16, import u32, export u32) | crc16`.
That is 12 + 10·n + 2 bytes: 174 B with 16 channels, 94 B with 8.
- The counters are absolute values in 0.1 Wh units. That is the published resolution, so storage is lossless, and the user asked for absolute values. A uint32 covers 429 MWh per counter. A counter above that is omitted from the record and logged, never wrapped.
- float32 was rejected: it drops to whole-Wh steps above about 16 MWh. double (8 B) was rejected because it is twice the size for no gain over the published 0.1 Wh.
- The per-channel `offset ms` keeps the real read time, which the live contract requires and which makes replay idempotent.
- The header's base second is the record's latest reading, not the nominal minute. A capture delayed by a stalled task still fits, and only a channel whose reads stalled for more than 32.7 s falls outside i16 and is skipped (spec scenario).
- The channel mask makes each record self-describing, so a channel enabled or disabled mid-outage stays decodable.
- The CRC lets a torn tail after power loss be detected and skipped.
The codec (encode, decode, CRC, offset range check) goes in `source/lib/offline_energy_record/` with native Unity tests.

### D3. Hourly segment files
Records are appended to `/offline_energy/<boundary-hour-unix>.bin`, one file per wall-clock hour (60 records), and flushed after each append.
- The directory sits outside `/energy` on purpose. The hourly CSV task migrates and compresses files under `/energy`, and must never touch these segments.
- Trimming by age (14 days), by byte budget or by headroom means deleting the oldest file. A LittleFS file cannot be truncated at the head, so a single ring file would need rewrites.
- Replay deletes a segment after its last chunk is published. An interrupted replay re-sends at most one hour, which is harmless (D5).
- At most 336 files for 14 days. Directory scans are bounded by `MAX_LOOP_ITERATIONS`.
- Byte budget: 3.5 MB (14 days × 1440 × 174 B, 16 channels), plus a free-space headroom of 512 KB that is checked before each append.
*Alternative*: one file per day (fewer files). Rejected because a byte-budget trim or an interrupted replay would then cost up to a whole day.

### D4. Replay pacing
Replay runs in `_handleConnectedState()` after alarms and the live publishes, one chunk per pass, with at least `OFFLINE_ENERGY_REPLAY_INTERVAL_MS` (1 s) between chunks.
- A chunk holds decoded records up to 48 KB, sized with a 114 B worst-case element. It is published through `_publishJsonStreaming`, so the 9 KB PubSubClient buffer is not a ceiling.
- The real ceiling is PubSubClient's 16-bit remaining length (64 KiB, see `CrashArchivePolicy`), which bites well before AWS's 128 KB limit.
- Elements older than 14 days at build time are dropped (the cloud would reject the whole message).
- A full 14-day, 16-channel backlog is about 750 chunks, which takes about 13 minutes at the minimum pace.

### D5. Delivery semantics: at-least-once, idempotent by key
A replayed object carries exactly the timestamp captured for it, and only minutes whose live publish did not happen are captured. In `meter_state`, a duplicate therefore merges onto the same `(device_id, channel, ts)` key through `anyLast`, and Iceberg dedups on the same key. QoS 0 means a chunk accepted by the socket but lost when the link drops is not retried. That costs at most one chunk per outage end, and it is the same risk the live path already accepts.

### D6. Restart rule and backoff ceiling
- `MQTT_MAX_RETRY_INTERVAL` goes from 60 min to 10 min.
- The attempt-count restart is replaced by a timer that accumulates only time with the link up and MQTT down. It resets on any connected pass and fires once. Neither a long-lived session dropping nor a long link-down period trips it. It is measured with `millis64()`, so it does not depend on the clock.
- The attempt counter stays, for backoff only.
The restart exists to recover a wedged TLS/lwIP stack. It does not help a WAN outage, and today, at about 2 h, it also ends buffering if the clock does not survive the reboot.

### D7. Clearing
The `/offline_energy` directory is deleted when cloud services are disabled (`setCloudServicesEnabled(false)`), in the energy-history clear path (the `ADE7953_HISTORY_CLEAR` task) and on factory reset (LittleFS format already covers it; verify).

## Risks / Trade-offs

- [The clock does not survive a reboot without NTP] → Capture stops at the first reboot or power cut of the outage, so 14 days is reachable only without a reboot. Mitigation: the 24 h restart rule. Task 1.1 measures the behaviour on the bench. If the clock survives `esp_restart()`, a valid-clock flag carried through the restart can extend capture, which would be a follow-up change.
- [Cloud contract not updated first] → Every replay message fails validation and lands in the DLQ. Mitigation: a hard gate in tasks.md. The firmware replay ships only after the infra `EnergyReading` change is in prod.
- [LittleFS contention with the CSV task and logs] → Bounded by the byte budget and the 512 KB headroom check before each append. The log wipe at 10 KB never triggers because of the buffer.
- [Flash wear] → One append per minute only while offline, so about 20k small appends for a full 14-day outage on a 7.2 MB wear-levelled partition. Negligible.
- [Replay competes with live traffic on a weak link] → One chunk per pass after live publishes, plus the 1 s minimum spacing.
- [Energy reset during an outage] → Stored absolute counters reflect the reset exactly as live snapshots would. The cloud already handles counter resets in the live stream.

## Migration Plan

1. Infra: make `reactiveEnergyImported`, `reactiveEnergyExported` and `apparentEnergy` optional in `EnergyReading` and in the influx/ClickHouse point builder, for both the Home and Home Pro pipelines. Deploy to dev, then to prod with Jibril's explicit consent.
2. Firmware: ship the restart rule and backoff change. They are independent of the contract and can land first.
3. Firmware: ship capture, storage and replay after step 1 is in prod.
4. Rollback: older firmware ignores the `/offline_energy` directory. Leftover segments cost only flash, so a rollback build should delete the directory, or the next forward build's 14-day trim clears it.
