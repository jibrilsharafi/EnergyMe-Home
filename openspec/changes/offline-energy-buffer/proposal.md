## Why

When the device loses the cloud connection, minute-resolution energy is lost for good. The `energy` topic is a live snapshot with no queue. The `meter` power queue holds about 9 minutes in PSRAM. After 10 failed MQTT attempts (about 2 h of backoff) the device reboots and wipes even that. In the cloud the cumulative counters stay correct, but every outage longer than a few minutes leaves a hole followed by a ramp, with no per-minute shape. We want outages of up to 14 days backfilled from the edge, with the smallest possible change on the cloud side.

## What Changes

- Add an offline energy buffer on LittleFS. While the cloud is unreachable, the device stores the active energy import/export counters of every active channel at each wall-clock minute, as absolute values. Only minutes whose live publish did not happen are stored.
- Replay the buffer on the existing `energy` topic after reconnecting. It goes oldest first, in bounded chunks, without starving live publishing. Each segment is deleted once it has been sent.
- Bound the buffer at 14 days of age and a fixed byte budget. Keep free-space headroom on LittleFS. Clear the buffer when cloud services are disabled, on factory reset and on energy-history clear.
- Replace the "reboot after 10 failed MQTT attempts" rule with a time-based rule: reboot only after 24 h without a successful MQTT connection. Lower the reconnect backoff ceiling from 1 h to 10 min, so replay starts soon after the link returns.
- Replayed energy objects carry only `unixTime`, `channel`, `activeEnergyImported` and `activeEnergyExported`. Live snapshots keep all five counters.
- **BREAKING (cloud contract)**: replayed objects omit `reactiveEnergyImported`, `reactiveEnergyExported` and `apparentEnergy`. The cloud `EnergyReading` model must accept them as optional before this firmware ships. This is a separate change in `energyme-infra` and a hard prerequisite.

## Capabilities

### New Capabilities
- `offline-energy-buffer`: what is captured while the cloud is unreachable, the storage bounds, replay ordering and pacing, and when the buffer is cleared.
- `mqtt-reconnect-policy`: the reconnect backoff ceiling and the time-based restart rule after a prolonged cloud outage.

### Modified Capabilities
- `energy-telemetry-stream`: "Energy snapshot is read directly, not queued" now allows the offline buffer for minutes that were not published live. "Energy snapshot content and shape" now allows replayed objects that carry only the active counters.

## Impact

- Firmware: `source/src/mqtt.cpp` (minute capture outside the connected-only path, replay, restart rule, backoff ceiling), a new host-testable record codec and segment logic in `source/lib/`, `source/include/mqtt.h` constants, and the factory-reset and energy-history-clear paths.
- Storage: up to about 3.5 MB of LittleFS with 16 active channels for 14 days, proportionally less with fewer channels. Flash is written only while offline (one append per minute).
- Cloud (`energyme-infra`, separate change): make `reactiveEnergyImported`, `reactiveEnergyExported` and `apparentEnergy` optional in `EnergyReading` (Home and Home Pro pipelines). The ClickHouse `meter_state` columns are already `Nullable`, and its `anyLast` merge on `(device_id, channel, ts)` makes a re-sent minute idempotent. The existing 15-day ingestion freshness guard bounds the replay window.
- AWS IoT cost: one replay burst per outage, at most about 650 messages of about 64 KB each for a full 14-day, 16-channel backlog.
