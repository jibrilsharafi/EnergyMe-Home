# Infra handoff: grid telemetry ingestion (copy-paste brief for energyme-infra)

> Edge contract fixed by EnergyMe-Home change `grid-telemetry-edge` (issue #157 + grid-telemetry ADR).
> Firmware ships Stream A only; everything below is the cloud side to build.

## Contract (fixed by firmware)

- **Topic:** Basic Ingest, new IoT rule `energyme_home_dev_rule_grid` / `energyme_home_prod_rule_grid` (same naming and env split as the existing meter/log rules); device publishes to `$aws/rules/<rule>/energyme/home/<device_id>/grid`.
- **Payload:** batched, ~1 message per existing publish cadence (~30 s), bare JSON array of positional triplets:
  `[[<t unix_ms uint64>, <f Hz float 4-dec>, <v V float 1-dec>], ...]`
  e.g. `[[1751500000003, 50.0123, 230.1], [1751500000502, 50.0119, 230.2]]`
  - `t` = true device wall-clock read at sample time, aligned near absolute .000/.500 boundaries (NTP-quality, ±10-50 ms typical); array sorted ascending; gaps are real (gated ticks), never interpolated.
  - `f` = EMA-filtered grid frequency (~0.8 mHz resolution, ~1 Hz bandwidth), 4 decimals; `v` = per-device RMS voltage (local signal - never aggregate across devices), 1 decimal.
  - No envelope object: adding fields later means a new topic/rule version, not an in-place schema change.
- **Report flag:** `send_grid_data` boolean in the writable `system` named shadow (same mechanism as `send_power_data`), persisted on device, default **off**. This is the quorum on/off lever.

## Work items (cloud)

1. **IoT Core policy**: allow the new Basic Ingest rule publish for device certs (extend the provisioned policy template alongside meter/log).
2. **IoT rule + anonymizing ingest Lambda**: resolve `device_id -> synchronous zone / geohash` from provisioned location, write zone-tagged records, **drop `device_id` before any write** (load-bearing privacy control - grid tables must never receive it; see ADR Decision 6).
3. **Quorum manager**: per-zone selection of 5-15 reporting devices, rotate membership, flip `send_grid_data` via the shadow desired state (remember the write-to-change-then-null-desired contract).
4. **Storage**: hot InfluxDB (1-3 d) + Iceberg/Parquet cold tables per ADR Decision 5 (`grid_frequency_raw` short retention, `grid_frequency_consensus` long, `grid_voltage` long), all keyed by zone, day-partitioned, reusing the Firehose plumbing.
5. **Consensus compute**: per-zone median across the quorum per 500 ms bucket (bucket by nearest .000/.500; dedupe by bucket per device).

## Notes for the ingest design

- Devices only emit when NTP-synced, flag on, and the measurement actually updated - so absent points are meaningful (offline / line-dead / not in quorum), not errors.
- QoS0 likely (redundant fleet data); tolerate duplicates and out-of-order batches idempotently (bucket + zone dedupe).
- Chronically misaligned timestamps (far from .000/.500) indicate a bad device clock - down-weight, don't reject the zone.
