# 04 - `system` shadow (writable)

**Goal:** mirror behavioural config in the `system` named shadow, applying cloud
deltas. **First writable shadow - needs a cloud `desired` writer to test the
inbound path.** Replaces the deprecated `command` sub-commands `set_send_power_data`
and `set_mqtt_log_level`.

**Files:** `src/shadow.cpp`, `src/mqtt.cpp` (setters already exist),
`src/customlog.cpp` + `src/led.cpp` (expose setters if needed).

## Scope (resolved) - 5 fields

Only fields that are **configurable + persisted today**. `ntp_server`/`timezone`
are out (NTP + TZ resolve automatically once cloud-connected; device runs UTC
internally). `modbus_tcp_*` is out (hardcoded in firmware, not configurable).
`log_level_save_file` does not exist. No follow-up plumbing in this work.

```json
{ "state": { "reported": {
  "led_brightness":   75,        // Led::getBrightness/setBrightness, NVS led_ns/brightness (uint8 0..100)
  "send_power_data":  true,      // Mqtt _sendPowerDataEnabled, NVS mqtt_ns/send_power (bool)
  "mqtt_log_level":   "INFO",    // Mqtt _mqttLogLevelInt, NVS mqtt_ns/log_level_int (0..5)
  "log_level_print":  "INFO",    // AdvancedLogger getPrintLevel/setPrintLevel
  "log_level_save":   "WARNING"  // AdvancedLogger getSaveLevel/setSaveLevel
}}}
```

Cloud contract: the backend must never write `ntp_server`/`timezone`/`modbus_*`
to this shadow - they are out of scope and have no device-side setter.

## Field -> setter map (ApplyFn)

| Shadow field | Type | Setter | Persist |
|---|---|---|---|
| `led_brightness` | uint8 0..100 | `Led::setBrightness(v)` (led.cpp:210) | auto (`_saveConfiguration`) |
| `send_power_data` | bool | `Mqtt::setSendPowerData(v)` (expose `_setSendPowerDataEnabled`, mqtt.cpp:512) | auto |
| `mqtt_log_level` | enum str | `Mqtt::setMqttLogLevel(s)` (expose `_setMqttLogLevel`, mqtt.cpp:534) | persistent levels only - see below |
| `log_level_print` | enum str | `AdvancedLogger::setPrintLevel(parse(s))` | lib-internal |
| `log_level_save` | enum str | `AdvancedLogger::setSaveLevel(parse(s))` | lib-internal |

ApplyFn validates each value (reuse existing range checks: `Led::isBrightnessValid`,
the log-level string switch in `_setMqttLogLevel`). Invalid value -> skip that
field, WARN, still null it in desired.

`Mqtt::_setSendPowerDataEnabled` / `_setMqttLogLevel` are currently `static`;
add thin public wrappers in the `Mqtt` namespace.

## `mqtt_log_level` auto-revert (the one stateful sub-feature)

- **Persistent levels** (`INFO`/`WARNING`/`ERROR`/`FATAL`): persist to NVS as new
  baseline (existing `_saveMqttLogLevelToPreferences`). Cancel any revert timer.
- **Transient levels** (`VERBOSE`/`DEBUG`): apply at runtime, **do not persist**.
  Start/reset a 5-min one-shot `esp_timer`.
- **Timer fires:** revert the runtime level to the persisted baseline, then **set
  a `reportPending` flag for the `system` shadow** so the MQTT task publishes
  `{reported:{mqtt_log_level:<baseline>}, desired:{mqtt_log_level:null}}`.
  **Do NOT publish from the esp_timer task** - PubSubClient is not thread-safe;
  the timer callback only flips state + flag (same rule as the `issues` registry
  tick, 03). The MQTT-task `_checkPublishShadows()` drains the publish.
- **Reboot while verbose:** comes back at persisted baseline (because not persisted).
- Backend holds verbose by re-writing `desired.mqtt_log_level="DEBUG"` every <5 min.

Implement with `esp_timer_create` one-shot, `esp_timer_start_once(5*60*1e6)`;
restart on each transient set. Mirrors the existing remote-log-level workflow
(repo memory: remote log level via MQTT/CloudWatch).

## Local-edit hook

`/api/v1/led/brightness` (customserver.cpp:2717) and `/api/v1/logs/level`
(customserver.cpp:1800) PUT handlers -> after applying, call
`Shadow::publishLocalEdit("system", {changed field})` so a local change nulls any
pending cloud desired (local-wins-when-active, 00).

## Tests

- Native unit: ApplyFn field routing + validation (invalid value skipped+nulled);
  auto-revert decision (persistent vs transient classification).
- On-device (needs cloud writer): write `desired.led_brightness=20` -> device
  applies, LED dims, reported=20, desired cleared. Write `desired.mqtt_log_level
  ="DEBUG"` -> verbose for 5 min -> auto-revert to baseline + shadow reflects it.
- Local: PUT brightness via REST -> shadow reported updates, pending desired cleared.

## Acceptance

- [ ] Trimmed schema only; backend contract notes excluded fields.
- [ ] All 5 fields apply from delta, persist, echo reported + null desired.
- [ ] `mqtt_log_level` transient auto-reverts after 5 min; persistent does not.
- [ ] Local REST edits publish `{reported, desired:null}`.
- [ ] Invalid values skipped + WARN, not applied.
