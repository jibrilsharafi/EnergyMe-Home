## 1. Spec

- [x] 1.1 Adversarial review of the change by an agent; triage every finding
  - Accepted: step timeouts must fit the deadline below 60 s, err onset urgency rate-limited, cursor-drain spin fixed, cloud loss reported to the collector on every disconnected MQTT iteration. The publish-on-change findings went with that feature (Deferred in design.md).

## 2. Pure logic (`source/lib/collector_logic`)

- [x] 2.1 `interval_s` 10 to 86400; step-timeout sum vs `deadline_s` below 60 s
- [x] 2.2 Backoff cap `max(interval_s, min(3600, 60 * interval_s))`
- [x] 2.3 Unity tests: 9/10/11 s bounds, deadline and jitter at 10 s, step-timeout rule, fast backoff cap

## 3. Firmware (`source/src/collector.cpp`)

- [x] 3.1 Err onset reported at once at most once per job per 300 s
- [x] 3.2 Cursor job draining its parts left out of `nextWaitMs()`
- [x] 3.2a The MQTT task reports every disconnected iteration and its stop to the collector (cursor `OFFLINE` pause)
- [x] 3.3 `pio run` for `esp32s3-dev` and `esp32s3-dev-pro`

## 4. Review and bench (dev only: Home .174 and Home Pro .80, each polling the other's Modbus TCP server)

- [x] 4.1 Code-review agents (one adversarial) and a simplification agent; triage, re-run tests
  - Accepted: cloud loss reported on every disconnected MQTT iteration (pre-existing), comment trims. Unit tests 69/69.
- [x] 4.2 A 10 s job publishes every ~10 s on both benches (raw bucket `collector/`)
  - 2026-11-11, .174 polling .80 and .80 polling .174, three 10 s jobs each (two-step per_step, one-step batch with `jitter_s` 5, one unreachable target): about 130 parts per step in 22 min, gaps 10.0 s +- 1 s (batch 5-15 s with jitter). One `TIMEOUT` from the peer gave a header-only part, the run stopped, 20 s backoff, then back on slot. Budget untouched (about 2.09 MB of 2.10 MB tokens left), no queue drops.
- [x] 4.3 A 10 s job on an unreachable target backs off 20, 40, ... and caps at 600 s
  - Both benches: gaps 22, 42, 82, 162, 322, 602 s (backoff plus the 2 s step timeout).
- [x] 4.4 Record results here
