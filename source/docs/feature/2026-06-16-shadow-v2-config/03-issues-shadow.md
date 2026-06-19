# 03 - `issues` shadow (reported-only)

**Goal:** publish the runtime issue registry to the `issues` named shadow, on
connect and on every registry state transition. **Console-verifiable, no cloud
backend.** Builds on #145/#178 (`IssueRegistry`).

**Files:** `src/shadow.cpp` (register), `src/issueregistry.cpp` (transition hook).

## Schema (`IssueRegistry::issuesToJson`, issueregistry.cpp:123 - used verbatim)

```json
{ "state": { "reported": {
  "active_count": 2,                       // derived: count isActive() states
  "issues": [
    { "code":"cloud_mqtt_disconnected", "state":"active_unacked",
      "severity":"warning", "firstSeenUnix":1748000000,
      "lastChangeUnix":1748001000, "occurrences":3,
      "message":"..." },
    { "code":"ct_polarity_flipped", "channel":2, "state":"cleared_unacked",
      "severity":"info", "firstSeenUnix":..., "lastChangeUnix":...,
      "occurrences":1, "message":"..." }
  ]
}}}
```

Field names are the registry's own (`code`, `firstSeenUnix`, `lastChangeUnix`,
`occurrences`; `channel` omitted for global-scope issues). Do **not** rename to
the issue's illustrative `id`/`first_occurred` - keep parity with REST + web UI.

## ReportFn

```cpp
void reportIssues(JsonDocument& doc) {
    JsonObject rep = doc["state"]["reported"].to<JsonObject>();
    rep["active_count"] = IssueRegistry::activeCount();  // ADD: derive from isActive()
    JsonDocument tmp; IssueRegistry::issuesToJson(tmp);  // {issues:[...]}
    rep["issues"] = tmp["issues"];
}
```

Add `IssueRegistry::activeCount()` (issueregistry.cpp): loop instances, count
`IssueLogic::isActive(state)`. `issuesToJson` already takes `_registryMutex`.

## Publish triggers

- On MQTT (re)connect (via `onMqttConnected`).
- On every transition: hook `_updateInstance` raise/clear edges
  (issueregistry.cpp:491-492) and `ack`/`ackAll` (issueregistry.cpp:165/191).
  Set a `_publishMqtt`-style flag, NOT a direct publish from the registry tick
  (registry runs in its own task; cross-task publish must not touch PubSubClient).
  Use `Shadow::requestReport("issues")` which sets `reportPending`; drained by
  `_checkPublishShadows()` in the MQTT task loop.

**Concurrency:** registry tick (separate task) only flips an atomic flag.
`issuesToJson` is mutex-guarded. Shadow publish happens in MQTT task. No new locks.

## Ack path (resolved): shadow desired/delta

`issues` is **partially writable** - only the `ack` key. Cloud writes
`desired.ack = ["code1", ...]`; on delta the device calls `IssueRegistry::ack()`
per code, then publishes reported (updated states) + `desired.ack:null`. An ack
is idempotent state intent, which fits desired/reported (Commands are for
transient actions, not state). The `report` fields stay reported-only; only `ack`
is accepted in a delta. Ship reported-only first (this doc), wire `ack` once the
cloud writer exists.

## Tests

- On-device: trigger an issue (e.g. disable WiFi to raise `cloud_mqtt_disconnected`),
  confirm `issues` shadow updates within one publish cycle; ack via REST, confirm
  shadow reflects `active_acked` / removal.
- Verify rate: rapid transitions coalesce (flag-based, one publish per loop), do
  not exceed 20 RPS/thing.

## Acceptance

- [ ] `issues` shadow matches `/api/v1/issues` after each transition.
- [ ] `active_count` correct.
- [ ] Transitions coalesce into <=1 publish per MQTT loop interval.
