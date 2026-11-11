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
- [ ] 3.3 `pio run` for `esp32s3-dev` and `esp32s3-dev-pro`

## 4. Review and bench (dev only: Home .174 and Home Pro .80, each polling the other's Modbus TCP server)

- [ ] 4.1 Code-review agents (one adversarial) and a simplification agent; triage, re-run tests
- [ ] 4.2 A 10 s job publishes every ~10 s on both benches (raw bucket `collector/`)
- [ ] 4.3 A 10 s job on an unreachable target backs off 20, 40, ... and caps at 600 s
- [ ] 4.4 Record results here
