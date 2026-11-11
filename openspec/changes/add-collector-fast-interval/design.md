## Context

The collector (add-lan-collector) schedules jobs in uptime seconds in the gateway task (`source/src/collector.cpp` `step()` / `nextWaitMs()`) and drains the out-queue every 1 s from the MQTT task. Validation lives in `source/lib/collector_logic/collector_validate.cpp`, scheduling arithmetic in `collector_runtime.cpp`. Nothing in the scheduler assumes 60 s besides the validation bound.

## Goals / Non-Goals

**Goals:** 10 s jobs with every rule derived from `interval_s` still sound.

**Non-Goals:** changing the job and step limits (8 and 8); publish-on-change (Deferred below).

## Decisions

### D1. Rules derived from `interval_s` at 10 s
- `deadline_s` 5 to min(300, interval_s/2): at 10 and 11 s the range is exactly 5, consistent, kept.
- Step timeouts: with the default `step_timeout_ms` 5000 one timed-out step would use the whole 5 s deadline and the next step would hit `DEADLINE`, failing the run and backing off while the other Modbus units answer (contradicts "Silent unit behind a Modbus gateway"). So below 60 s the sum of all step timeouts must fit `deadline_s`; a two-step 10 s job needs `step_timeout_ms` <= 2500 (Modbus on a LAN answers in tens of ms). From 60 s up nothing changes, so stored sets stay valid.
- `jitter_s` 0 to 5 at 10 s, first slot delay [0, 10] s: fine.
- Backoff: the old cap `max(interval_s, 3600)` lets a 10 s job go silent for up to an hour after the target comes back. New cap `max(interval_s, min(3600, 60 * interval_s))`: identical for `interval_s >= 60`, 600 s at 10 s, so a dead target gets one connect per 60 slots.
- Budget: the token bucket spends header plus payload bytes. A 10 s job with two Modbus reads coalesced into one message is about 2 x 450 B per run, 324 KB/h, under the 2 MB/h default. The 256 KB ring holds about 48 minutes of such a job offline, the newest kept.
- Health: an err onset was reported at once every time, so a 10 s job flapping fail / 20 s backoff / ok would update the shadow about 120 times an hour. An err onset is now urgent at most once per job per 300 s; later ones are coalesced like recoveries. The first failure still surfaces at once.
- Pre-existing: a cursor job waiting for its parts to drain left `nextS` in the past, so `nextWaitMs()` returned 0 and the gateway task spun for the whole wait (a cloud outage). Such a job is now left out of the wait computation; `step()` still re-checks it at least every `GATEWAY_QUEUE_WAIT_MS`.
- Pre-existing: the collector heard of a lost cloud connection only when the MQTT `loop()` failed inside a connected iteration; a Wi-Fi drop, a TCP close between iterations or the MQTT task stopping left it `connected`, so cursor jobs never paused with `OFFLINE`. The MQTT task now reports every disconnected iteration and its stop.

## Deferred: publish on change

Considered and dropped (Jibril, 2026-11-11): a `publish_mode: on_change` job field with `heartbeat_s`, suppressing a step's part when the SHA-256 of its reply equals the last one sent. Not worth it:
- Storing an unchanged value costs almost nothing. The only per-row cloud cost is Firehose to Iceberg, about $0.17-0.35 per million rows (each row billed as 5 KB); IoT rule, S3, SQS and Lambda are per message, and one message carries every register of a step. One point every 10 s is about 259k rows a month, $0.05-0.09; ten critical signals at 10 s stay under $1 a month per site. A constant value compresses to a few bytes in ClickHouse and S3.
- It adds real risk: collector parts are QoS0, so a lost change part leaves a wrong state until the next heartbeat (which then needs a short heartbeat and a repeat count), and the device must invalidate its state on every failure, queue drop, wait, abandoned run, disconnect and apply. Every cloud reader (portal, alerts, analytics) would need forward-fill with a max age to tell "unchanged" from "device offline".
- If volume ever matters (many 10 s points on many sites), the lever is packing several rows per Firehose record in the decoder, not suppression on the device.

## Risks / Trade-offs
- [Ten-second jobs fill the 256 KB ring faster during an outage] -> about 48 minutes for a two-register job; older parts are dropped and counted in `dq`, as for any job.
- [A 10 s job with many steps] -> the step-timeout rule forces short timeouts (5 steps: 1000 ms each); a slower target needs a longer interval.

## Migration Plan
Additive: existing job sets validate unchanged. A set with `interval_s` below 60 is refused with `BAD_JOB` by older firmware, which the cloud sees in `apply_error`.
