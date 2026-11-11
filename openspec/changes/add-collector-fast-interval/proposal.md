## Why

A customer pool site needs its Sungrow SG50CX-P2 inverter (WiNet-S, Modbus TCP 502) read through the LAN collector, with AC power and grid-meter phase power every 10 s. The collector refuses `interval_s` below 60.

## What Changes

- `interval_s` minimum 60 -> 10 (range 10 to 86400). The rules derived from it stay as they are and stay consistent at 10 s: `jitter_s` 0 to min(interval_s/2, 300), `deadline_s` 5 to min(300, interval_s/2) (exactly 5 at 10 and 11 s), first slot delay [0, min(interval_s, 60)], fixed slots, missed slots skipped.
- Below 60 s the effective timeouts of all login and data steps of a job must fit `deadline_s` together (`BAD_JOB`, field `step_timeout_ms`), so one silent Modbus unit cannot push the next step past the deadline and fail the run. Existing sets (all at 60 s or more) are unaffected.
- Backoff cap scaled for fast jobs: `min(interval_s * 2^n, max(interval_s, min(3600, 60 * interval_s)))`. Unchanged for `interval_s >= 60`; a 10 s job caps at 600 s instead of 3600 s.
- An err onset is reported to the shadow at once at most once per job per 300 s; later onsets are coalesced like recoveries, so a flapping 10 s job does not update the shadow on every run.
- Pre-existing defects fixed on the way: a cursor job waiting for its parts to drain made the gateway task spin for the whole wait; a Wi-Fi drop or a TCP close between MQTT loops never reached the collector, so cursor jobs did not pause `OFFLINE`.

## Capabilities

### New Capabilities
None.

### Modified Capabilities
- `lan-collector` (introduced by add-lan-collector): "Job schema and quotas" (interval range, step timeouts below 60 s), "Scheduling and step outcomes" (10 s slots scenario), "Per-job backoff" (cap) and "Job health in the shadow" (err onset rate) MODIFIED.

## Impact

- Firmware: `source/lib/collector_logic` (validation, backoff), `source/src/collector.cpp` (health urgency, gateway wait), `source/src/mqtt.cpp` (cloud loss).
- Infra (separate repo): the jobs tool must accept `interval_s` from 10 and apply the step-timeout rule below 60 s.
- No shadow, REST or topic format change.

## Dependencies

- Requires add-lan-collector (implemented, not archived). The MODIFIED `lan-collector` requirements here are archived after add-lan-collector.
