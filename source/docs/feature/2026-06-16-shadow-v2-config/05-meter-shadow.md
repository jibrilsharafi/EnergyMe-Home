# 05 - `meter` shadow (writable)

**Goal:** mirror ADE7953 calibration + sample time in the `meter` named shadow.
Needs a cloud writer to test inbound. Writes here are **rare** (devices are
factory-calibrated; this is advanced recalibration only).

**Files:** `src/shadow.cpp`, reuse `src/ade7953.cpp` config API.

## Schema (matches `configurationToJson`, ade7953.cpp:622 - camelCase, struct `Ade7953Configuration`)

```json
{ "state": { "reported": {
  "aVGain":4050000, "aIGain":4300000, "bIGain":4300000,
  "aIRmsOs":0, "bIRmsOs":0,
  "aWGain":4194304, "bWGain":4194304, "aWattOs":0, "bWattOs":0,
  "aVarGain":4194304, "bVarGain":4194304, "aVarOs":0, "bVarOs":0,
  "aVaGain":4194304, "bVaGain":4194304, "aVaOs":0, "bVaOs":0,
  "phCalA":0, "phCalB":0,
  "sample_time":200
}}}
```

Use the firmware's existing JSON key names (`aVGain` ...), **not** the issue's
`av_gain` illustrative names. NVS namespace `ade7953_ns`; calibration via
`_saveConfigurationToPreferences` (ade7953.cpp:2684), sample time key
`sample_time` (uint64, `putULong64`). Includes `aIRmsOs`/`bIRmsOs` which the
issue's draft omitted.

## ReportFn

```cpp
void reportMeter(JsonDocument& doc) {
    JsonDocument cfg; Ade7953::getConfigurationAsJson(cfg);  // camelCase calibration
    JsonObject rep = doc["state"]["reported"].to<JsonObject>();
    for (auto kv : cfg.as<JsonObjectConst>()) rep[kv.key()] = kv.value();
    rep["sample_time"] = Ade7953::getSampleTime();
}
```

## ApplyFn

- Calibration fields: collect into a `JsonDocument`, call
  `Ade7953::setConfigurationFromJson(doc, /*partial=*/true)` (ade7953.cpp) -> it
  takes `_configMutex`, applies to the chip, and persists. `partial=true` lets a
  delta carry a subset.
- `sample_time`: `Ade7953::setSampleTime(v)` (validates >= `MINIMUM_SAMPLE_TIME`
  200 ms).
- Echo applied fields into reported; null them in desired. Reject out-of-range
  -> skip + WARN + null.

Mutex: `setConfigurationFromJson` / `setSampleTime` already acquire
`_configMutex` (ade7953.cpp) internally. ApplyFn must NOT hold `_shadowMutex`
while calling them (call after releasing) to avoid lock-order inversion.

## Local-edit propagation (drift-detect)

No per-handler hook. Local edits via `PUT/PATCH /api/v1/ade7953/config` and
`PUT /api/v1/ade7953/sample-time` (customserver.cpp) are picked up by
`Shadow::checkPublish()`, which drift-detects the changed `reported` every 3 s and
republishes (reported-only, no `desired:null`). See 04 / 00. (The earlier
per-handler `publishLocalEdit` was removed.)

## Tests

- Native unit: partial-delta merge into config doc; sample_time range guard.
- On-device (cloud writer): write `desired.phCalB=42` -> applies, persists across
  reboot, reported reflects it, desired cleared. Verify a power reading shifts as
  expected for a known gain change on the bench.

## Acceptance

- [ ] Reported = current calibration (camelCase) + sample_time.
- [ ] Partial deltas apply via `setConfigurationFromJson(partial=true)` + persist.
- [ ] Out-of-range skipped + WARN + nulled.
- [ ] No lock-order inversion (`_shadowMutex` released before `_configMutex`).
