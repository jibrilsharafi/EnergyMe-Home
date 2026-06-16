# 06 - `channels` shadow (writable)

**Goal:** mirror per-channel config (17 channels, 0-16) in the `channels` named
shadow, **object-keyed by index** (arrays are atomic in shadow deltas - keying by
index lets the cloud patch one channel/one field). Needs a cloud writer to test.

**Files:** `src/shadow.cpp`, reuse `src/ade7953.cpp` channel API.

**Source of truth (resolved):** the `channels` shadow is canonical for channel
config. The device **stops publishing the `channel` topic** (it was configurable
state, not telemetry); cloud reads config from this shadow. Remove the `channel`
topic publish in 08. (Topic version stays `v1`.)

**Deploy sequencing (hard constraint):** the firmware change that *removes the
`channel` publish* (08) must NOT ship to the fleet until the cloud
channels-shadow ingestion is live. If firmware lands first, the `channel_handler`
Lambda loses its input before the shadow path exists -> cloud channel config goes
stale. Adding the `channels` shadow *publish* (this doc) is safe to ship anytime;
only the removal is order-dependent.

**Inbound delta size (buffer):** the worst-case *inbound* message is a bulk
`desired` write across many channels with max-length `label`+`groupLabel` (64 B
each) plus a per-field metadata block - roughly 2x the state. Compute this and
confirm it fits `MQTT_BUFFER_SIZE` (9 KB); if not, the cloud writer must chunk
channel `desired` to <= N channels/update (contract constraint in 00). The
*outbound* reported publish streams and is not buffer-bound.

## Schema (per channel = `channelDataToJson`, ade7953.cpp:970; NVS `channels_ns`)

```json
{ "state": { "reported": {
  "0": { "active":true, "reverse":false, "label":"Main", "phase":1,
         "ctSpecification":{"currentRating":50.0,"voltageOutput":0.333,"scalingFraction":0.0},
         "groupLabel":"Group 0", "role":"GRID" },
  "1": { ... },
  "16":{ ... }
}}}
```

Use firmware's actual shape: `phase` is **uint8** (1/2/3), CT params are nested
under `ctSpecification` (`currentRating`/`voltageOutput`/`scalingFraction`),
`groupLabel` (camelCase), `role` is an enum **string** (`GRID`/`LOAD`/`PV`/
`BATTERY`/`INVERTER`). NOT the issue's flat `phase:"A"`, `ct_current`, etc.

Size: 17 ch x ~200 B ~= 3.4 KB reported (metadata excluded from the 8 KB cap).
Single shadow confirmed feasible. Validate worst-case with 63-char `label` +
`groupLabel` once during impl (`NAME_BUFFER_SIZE` 64).

## ReportFn

```cpp
void reportChannels(JsonDocument& doc) {
    JsonObject rep = doc["state"]["reported"].to<JsonObject>();
    for (uint8_t i=0;i<MAX_CHANNEL_COUNT;i++){
        JsonDocument ch; Ade7953::getChannelDataAsJson(ch,i);  // {index,active,...}
        ch.remove("index");                                    // index is the key
        char k[3]; snprintf(k,sizeof k,"%u",i);
        rep[k] = ch;
    }
}
```

## ApplyFn (nested delta routing - the tricky part)

A delta can be `{"3":{"label":"Boiler"}}` (one field of one channel). For each
top-level key that is a numeric channel index:

```
idx = atoi(key); if (idx>=MAX_CHANNEL_COUNT) { WARN; null; continue; }
JsonDocument merged;
Ade7953::getChannelDataAsJson(merged, idx);     // start from current
deepMerge(merged, delta[key]);                   // overlay changed subfields
merged["index"] = idx;
Ade7953::setChannelDataFromJson(merged, /*partial=*/true, &roleChanged);
// echo the CHANGED subfields into reported[key]; null desired[key] (whole object)
```

Invariant: channel 0 cannot be disabled - `setChannelData` already forces
`active=true` for index 0 (ade7953.cpp:792). ApplyFn relies on that; do not
duplicate the guard.

Mutex: channel writes use **`_channelDataMutex`** (NOT `_configMutex`). Acquired
inside `setChannelDataFromJson`/`setChannelData`. ApplyFn must not hold
`_shadowMutex` across the call.

`role` change side effects (`roleChanged`, transient re-read flags) are handled by
`setChannelData` (`armTransients` default true). Pass through unchanged.

## Local-edit hook

`PUT/PATCH /api/v1/ade7953/channel` and `PUT /api/v1/ade7953/channels`
(customserver.cpp) -> `Shadow::publishLocalEdit("channels", {"<idx>": changed})`.
For the bulk endpoint, include each changed index.

## Tests

- Native unit: nested deep-merge (delta subfield -> full channel), index bounds,
  channel-0 disable rejected, role string<->enum mapping.
- On-device (cloud writer): write `desired.{"5":{"label":"X","active":true}}` ->
  applies, persists, reported["5"] updated, desired cleared. Write `{"0":{"active":false}}`
  -> rejected (stays active), nulled, WARN.
- Worst-case size: fill all 17 labels+groupLabels to 63 chars, confirm reported < 8 KB.

## Acceptance

- [ ] Object-keyed reported for all 17 channels matches `/api/v1/ade7953/channel`.
- [ ] Nested partial delta applies one field of one channel.
- [ ] Channel 0 disable rejected + nulled.
- [ ] Reported < 8 KB at worst case.
- [ ] Uses `_channelDataMutex`; no lock-order inversion.
