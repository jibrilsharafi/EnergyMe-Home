## 1. Preconditions

- [ ] 1.1 Bench (.174): sync NTP, block WAN at the router, reboot via API, and record whether `CustomTime` reports a valid time after boot. Then power-cycle and record it again. Note the result in design.md Risks.
- [x] 1.2 Infra (`energyme-infra`, separate change): make `reactiveEnergyImported`, `reactiveEnergyExported` and `apparentEnergy` optional in `EnergyReading`, handle `None` in the state point builder, and cover both the Home and Home Pro pipelines with unit tests. Deploy to dev. (energyme-infra#797, merged as 2ed43fb, dev deployed 2026-10-30.)
- [ ] 1.3 Infra prod deploy of 1.2. Needs Jibril's explicit consent. **Gate: groups 4-5 must not be released before this is in prod.**

## 2. Reconnect policy

- [x] 2.1 Lower `MQTT_MAX_RETRY_INTERVAL` to 10 min in `mqtt.h`.
- [x] 2.2 Replace the `MQTT_MAX_CONNECTION_ATTEMPTS` restart with "no successful MQTT connect for 24 h while `CustomNet::isFullyConnected()`", tracked with `millis64()`. Keep the authorization-failure stop unchanged.
- [ ] 2.3 Build `esp32s3-dev`, OTA to .174 and .80, block WAN, and check that the backoff caps at 10 min with no restart before 24 h.

## 3. Record codec (host-testable)

- [x] 3.1 Add `source/lib/offline_energy_record/` with record encode/decode, CRC16, the i16 offset range check, the 0.1 Wh uint32 conversion with an overflow check, and channel-mask handling.
- [x] 3.2 Native Unity tests: round trip for 1 and 16 channels, a sparse mask, an out-of-range offset skipping the channel, an overflowing counter skipping the channel, a torn tail, and a CRC mismatch skipped without losing the following records.

## 4. Capture and storage

- [x] 4.1 Add a capture step to `_mqttTask` that runs in every connection state. Gate it on cloud enabled, not community mode and a valid time. Give it its own boundary target, reuse the energy publish gate, and capture only boundaries whose live publish did not happen (disconnected, or `_publishJsonStreaming` failed).
- [x] 4.2 Add the segment store: hourly files under `/offline_energy/`, append plus flush per record, a 14-day age trim, a 3.5 MB byte budget and a 512 KB free-space headroom check before each append, with oldest files deleted first.
- [x] 4.3 Clear `/offline_energy/` when cloud services are disabled and on energy-history clear. Factory reset formats LittleFS, which removes it.
- [x] 4.4 Native tests for the pure trim/selection logic (which segments to delete for age, budget and headroom) in `source/lib/`.

## 5. Replay

- [x] 5.1 Replay in `_handleConnectedState()` after alarms and the live publishes: one chunk of at most 48 KB per pass, at least 1 s apart, oldest first, with elements older than 14 days dropped and only active-energy objects published.
- [x] 5.2 Delete a segment only after its last chunk publishes. An interrupted replay resumes from the oldest remaining segment.
- [x] 5.3 Expose the backlog in the statistics payload (records captured, segments dropped, buffered bytes), so a backlog is visible from the cloud.
- [x] 5.4 Dev-only `POST /api/v1/debug/mqtt/outage?seconds=N` to simulate a cloud outage on the bench without touching the router.

## 6. Verification

- [x] 6.1 `pio test -e native` (WSL), then build both `esp32s3-dev` and `esp32s3-prod`.
- [x] 6.2 Bench on .174 and .80 against dev cloud: simulated cloud outage (dev endpoint) of 20 min on Home and 4 min on Pro, then restore it, and check in dev ClickHouse that `meter_state` has per-minute active energy for the gap with no duplicate keys after `FINAL`. (2026-10-31: Home 23:53-00:12 UTC replayed across two hourly segments, 3 rows per minute, live resumed 00:13 with no gap; Pro had zero flash writes over 6 min online.)
- [x] 6.3 Bench: reboot mid-outage. Check that the segments written before the reboot are replayed without duplicates. (2026-10-31: Pro rebooted with 4 buffered minutes; 23:55-23:58 UTC replayed after boot, one row per minute.) Reboot mid-replay is not bench-tested: a 20-minute backlog drains in one chunk.
- [x] 6.4 Code-review and simplification agents on the branch diff, then fix findings and re-run 6.1.
