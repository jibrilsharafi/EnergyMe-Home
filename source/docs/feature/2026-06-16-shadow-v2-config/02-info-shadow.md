# 02 - `info` shadow (reported-only)

**Goal:** publish static device identity to the `info` named shadow; retire the
`system/static` retained topic publish. **Lowest risk, fully console-verifiable
with no cloud backend** - this is the first hardware-provable phase.

**Files:** `src/shadow.cpp` (register descriptor), `src/mqtt.cpp` (remove
`_publishSystemStatic`), reuse `utils.cpp` sources.

## Schema (flattened from `systemStaticInfoToJson`, utils.cpp:211)

```json
{ "state": { "reported": {
  "firmware_version":  "2.1.0",          // FIRMWARE_BUILD_VERSION (constants.h:12)
  "firmware_build_date":"Jun 15 2026",   // FIRMWARE_BUILD_DATE (constants.h:14)
  "sketch_md5":        "<32hex>",        // ESP.getSketchMD5() (utils.cpp:46)
  "hardware_profile":  "v6.1",           // globalHwProfile->version
  "pcb_revision":      "v6.1",           // factory NVS FACTORY_KEY_PCB_REVISION
  "community_mode":    false,            // globalCommunityMode
  "device_id":         "588c81c47a00",   // DEVICE_ID (globals.h:9)
  "serial_number":     "...",            // factory NVS FACTORY_KEY_SERIAL_NUMBER
  "manufacturing_unix":0                 // factory NVS FACTORY_KEY_MFG_TS (ulong)
}}}
```

Notes:
- `sketch_md5` (`ESP.getSketchMD5()`) is the identity hash: the binary digest
  proves whether the running firmware is the published build or a modified one
  (git SHA can't - dirty/uncommitted trees lie). Same hash used for OTA validation.
- `mac_address`: derive from `device_id` (insert colons) only if cloud needs it;
  `device_id` already is the MAC. Omit to save bytes.
- All values are read-only at runtime; no `apply` callback (`writable=false`).

## ReportFn

Reuse the sources `populateSystemStaticInfo`/`systemStaticInfoToJson` already
read (utils.cpp). Write a thin `Shadow::reportInfo(doc)` that fills the flat
keys above into `doc["state"]["reported"]`. Do not nest under `data`.

## Publish cadence

- On every MQTT (re)connect (via `Shadow::onMqttConnected`).
- Once per 24 h via a low-priority `esp_timer` (identity rarely changes; keeps
  shadow `lastUpdated` fresh). Add `_infoTimer` in shadow.cpp.

## Retire `system/static`

- Remove `_publishSystemStatic()` (mqtt.cpp:1277-1295), its topic buffer
  `_mqttTopicSystemStatic`, `_setTopicSystemStatic()`, `MQTT_TOPIC_SYSTEM_STATIC`,
  and the `_publishMqtt.systemStatic` flag + its check.
- **Do this removal in 08 (cutover)**, not here, to keep this phase additive and
  independently testable (info shadow live *alongside* system/static briefly).

## Tests

- On-device: enable cloud, reconnect, inspect `info` named shadow in AWS IoT
  console (Manage > Things > <id> > Device Shadows). Confirm all fields present,
  values match `/api/v1/system/info` (or existing static topic payload).
- Confirm 24 h timer fires (temporarily shorten for bench).

## Acceptance

- [ ] `info` shadow reported state populated on connect; matches device identity.
- [ ] No `desired`/delta handling (reported-only).
- [ ] 24 h refresh timer works.
- [ ] Worst-case payload < 1 KB.
