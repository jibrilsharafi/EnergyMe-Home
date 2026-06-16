# 08 - config-topic retirement + 2.1.0 release

**Goal:** stop publishing the retired config topics/handlers, bump **firmware** to
2.1.0. **Last phase** - everything above is additive. **No topic-version bump:**
`MQTT_TOPIC_VERSION` stays `"v1"`.

**Files:** `include/mqtt.h`, `src/mqtt.cpp`, `platformio.ini`/version.

## No topic version bump (decision)

A topic version signals an **incompatible payload/contract change**. None of the
surviving topics change payload (`meter`/`log`/`statistics`/`crash`/`system/dynamic`
are byte-identical). The migration is **additive** (shadows + Commands are net-new
`$aws/things/...` / `$aws/commands/...` topics, version-independent by
construction) and **subtractive** (`system/static`, `command`, `channel` just stop
being published). Keeping `v1` avoids rule duplication, a parallel-rules rollout
window, device-policy ARN migration, and the "delete v1 early = data loss"
footgun. Version-when-you-break: bump a specific topic the day its payload changes,
not preemptively.

`system/dynamic` -> `system` rename is **dropped** (cosmetic; only existed to
disambiguate from the now-gone `system/static`). Keep `system/dynamic` as-is - no
new rule, and no name collision with the `system` shadow.

## Topic map

| Topic | Action | Reason |
|---|---|---|
| `energyme/home/v1/<id>/system/static` | retired (stop publishing) | -> `info` shadow (02) |
| `energyme/home/v1/<id>/command` (subscribe) | retired | -> Commands (07) + `system` shadow (04) |
| `energyme/home/v1/<id>/channel` | retired (stop publishing) | configurable state -> `channels` shadow (06) |
| `energyme/home/v1/<id>/system/dynamic` | **kept**, unchanged | telemetry, payload identical |
| `meter`, `log`, `statistics`, `crash` | **kept**, unchanged | telemetry, payload identical |

**Principle:** telemetry topics carry only measurements/statistics/dynamic
runtime; configurable state moves to shadows. WiFi creds stay local-only (never shadow).

Cloud: the 3 retired topics' v1 rules go idle (zero cost with no producer); delete
them whenever convenient. No rollout window, no ingest gap (surviving topics are
untouched).

**Deploy sequencing (hard constraint):** do NOT ship the `channel` and
`system/static` publish removals to the fleet until the cloud shadow-ingestion
path (`channels`/`info`) is live. Removing them first leaves the
`system_static`/`channel_handler` Lambdas without input before the shadow path
exists -> stale cloud device state. Shadow *publishes* (02-06) are safe earlier;
only these removals are order-dependent.

## Removals (firmware)

- `_publishSystemStatic` + `_mqttTopicSystemStatic` + `_setTopicSystemStatic` +
  `MQTT_TOPIC_SYSTEM_STATIC` + `_publishMqtt.systemStatic` (mqtt.cpp:1277, 57, 749).
- `MQTT_TOPIC_SUBSCRIBE_COMMAND`, `_subscribeCommand` (mqtt.cpp:776),
  `_handleCommandMessage` (841), `_handleSetSendPowerDataMessage`,
  `_handleSetMqttLogLevelMessage`, `_handleRestartMessage`.
- The `endsWith(topic, MQTT_TOPIC_SUBSCRIBE_COMMAND)` branch in `_subscribeCallback`.
- `_publishChannel` + `_mqttTopicChannel` + `_setTopicChannel` + `MQTT_TOPIC_CHANNEL`
  + `_publishMqtt.channel` + `requestChannelPublish()` (channel config -> `channels` shadow).

`MQTT_TOPIC_VERSION`, `MQTT_TOPIC_SYSTEM_DYNAMIC`, and the telemetry topic defines
are **unchanged**.

## Buffer

- `mqtt.h` `MQTT_BUFFER_SIZE (5*1024)` -> `(9*1024)`. Sized for the worst-case
  **inbound delta** (cloud setting `desired` on many/all channel fields at once +
  their per-field metadata), the largest message the static PubSubClient RX buffer
  must hold once `/update/accepted` is intentionally NOT subscribed (01). The 8 KB
  figure is the shadow *state* quota (metadata-excluded); the *wire* message
  including metadata can exceed it, hence 9 KB + framing headroom.
  **Hardware-verify item:** the 5 KB baseline already saw esp-aes alloc failures
  during MQTT-TLS handshake under heap pressure (repo memory: 2.0.0 heap pressure).
  Nearly doubling the static buffer adds pressure at exactly that point.
  Bench-verify TLS connect + free-heap after the bump on a real device before
  merging; if tight, free it between uses or move to PSRAM.

## Firmware version bump

- 2.0.x -> **2.1.0** (minor: new cloud config surface). This is the **firmware**
  semver, NOT a topic version. Per repo convention it is a **separate release step
  on `development`**, NOT inside a feature branch. Do it when cutting the release,
  not during 01-07.

## Tests

- Full regression: surviving telemetry still ingested on v1; local REST API
  unchanged; OTA Job still works; all 5 shadows + 3 Commands functional on dev.
- Confirm no firmware references to removed defines/handlers remain (grep).
- Confirm a 2.0.x device and a 2.1.0 device both ingest the surviving v1 topics
  (no cloud change needed for those).

## Acceptance (rolls up #159 criteria, topic-version item dropped)

- [ ] `MQTT_TOPIC_VERSION` unchanged (`v1`); surviving telemetry topics unchanged.
- [ ] `command` topic + handlers removed; `system/static` + `channel` publishes removed.
- [ ] `MQTT_BUFFER_SIZE` = 9 KB, hardware-verified for TLS/heap.
- [ ] No local REST API regression.
- [ ] Firmware 2.1.0 (separate release step).
- [ ] Surviving telemetry payload contents unchanged.
