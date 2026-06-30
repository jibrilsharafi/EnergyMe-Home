> Status: shipped in PR #190 (merged to `development`). All tasks complete; retained as the implementation record for archive.

## 1. Scaffold

- [x] 1.1 Add `source/src/shadow.cpp` / `include/shadow.h`: registration table, subscribe helpers, publish-reported-first on connect, delta dispatcher, version/clientToken plumbing, per-shadow mutex
- [x] 1.2 Bump `MQTT_BUFFER_SIZE` 5 KB -> 9 KB

## 2. Reported-only shadows

- [x] 2.1 `info` shadow: identity on connect + 24 h refresh timer; retire `_publishSystemStatic`
- [x] 2.2 `issues` shadow: wire `IssueRegistry::issuesToJson()` to connect + transition events; fix boot-race (registry before web server, empty-200 on null mutex)
- [x] 2.3 `wifi` shadow (reported-only): non-secret network state + mode

## 3. Writable shadows

- [x] 3.1 `system` shadow + `mqtt_log_level` transient auto-revert (5 min, reboot-safe)
- [x] 3.2 `meter` shadow: ADE7953 calibration + sample time mirror
- [x] 3.3 `channels` shadow: per-channel object-keyed config; confirm worst-case JSON under 8 KB
- [x] 3.4 Source-agnostic 3 s drift-detect republish for local edits (reported-only)
- [x] 3.5 Optimistic concurrency: version from delta, 409 re-publish + recover; unknown-field null + WARN

## 4. Commands

- [x] 4.1 `restart`, `factory_reset` (confirm == device_id), `energy_reset` via AWS IoT Commands
- [x] 4.2 Route only `.../request/json`; ignore rejection echoes; process in MQTT task body; uppercase reasonCodes

## 5. v1 cutover (topic version unchanged)

- [x] 5.1 Retire `system/static`, `command`, `channel` publishes/handlers (telemetry topics unchanged, zero ingest gap)
- [x] 5.2 `fix(ade7953)`: sample-time reject log `%lu` -> `%llu` for uint64_t

## 6. Verification

- [x] 6.1 Native unit tests (162/162, incl. 25 `shadow_logic` cases)
- [x] 6.2 Hardware E2E on dev .174 incl. real broker-delivered delta, full writable matrix, all reject paths, 409 recovery, offline->reconnect, transient log-level lifecycle, real factory_reset + recovery

## 7. Follow-up (out of this change)

- [ ] 7.1 Cloud side (`energyme-infra`): shadow ingestion + desired-state writer (clears `desired` after convergence) + `StartCommandExecution` dispatcher
- [ ] 7.2 Bump firmware version to 2.1.0 (separate release step on `development`)
