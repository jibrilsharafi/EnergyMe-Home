# 07 - IoT Commands (transient operations)

**Goal:** migrate transient operations off the deprecated `command` topic to AWS
IoT Core **Commands** (GA): `restart`, `factory_reset`, `energy_reset`. Jobs stay
OTA-only. Needs cloud command templates + a dispatcher to test end-to-end.

**Files:** new `_handleCommandExecution` in `src/mqtt.cpp` (or `commands.cpp`),
alongside the existing Jobs router.

## Verified protocol (plain MQTT sub/pub, PubSubClient-compatible)

Device does **not** know `executionId` ahead of time -> subscribe with wildcard,
parse the id from the received topic, publish status to the matching response topic.

```
SUBSCRIBE (request):  $aws/commands/things/<DEVICE_ID>/executions/+/request/+
PUBLISH   (response): $aws/commands/things/<DEVICE_ID>/executions/<execId>/response/json
SUBSCRIBE (optional): .../executions/<execId>/response/accepted/+   (ack from cloud)
```

`+` last segment = payload-format (`json`/`cbor`); we use `json`. MQTT 3.1.1 is
supported. **The device IoT policy must grant `$aws/commands/things/<thing>/*`**
(sub/receive/publish) - it currently only covers `$aws/things/*` (see 00 cloud checklist).

## Request payload (from the command template)

```json
{ "operation":"restart" }                          // restart
{ "operation":"factory_reset", "confirm":"<device_id>" }  // anti-fat-finger
{ "operation":"energy_reset", "channels":[3,7] }   // or "channels":"all"
```

**staleness guard:** reject any command older than 5 min (avoids acting on a
command queued during an offline window). Mirror the OTA `_otaRebootPending`/age
-check style. **VERIFY (final payload review):** the guard needs a server
timestamp the device can read - confirm the Commands request payload carries one,
or have the command template inject `createdAt` via a parameter. If neither is
available, the staleness guard has nothing to check.

**factory_reset vs lingering desired (VERIFY / contract):** after a reset the
device reports defaults; any pending cloud `desired` would be delta'd back and
partially undo the reset. The backend must clear all shadows' `desired` for the
thing when issuing `factory_reset` (also noted as a cloud-writer constraint in 00).

## Response (UpdateCommandExecution)

```json
{ "status":"IN_PROGRESS" }
{ "status":"SUCCEEDED" }
{ "status":"FAILED",   "statusReason":{"reasonCode":"...","reasonDescription":"..."} }
{ "status":"REJECTED", "statusReason":{"reasonCode":"stale_command",...} }
```

States: CREATED(cloud) -> IN_PROGRESS -> SUCCEEDED/FAILED/REJECTED/TIMED_OUT.
Publish IN_PROGRESS on accept, terminal status when done.

## Handler routing

In `_subscribeCallback` (mqtt.cpp:833), before the legacy `command` check:

```cpp
if (strstr(topic,"/commands/things/") && strstr(topic,"/executions/")) {
    _handleCommandExecution(topic, message);   // parse execId from topic
    return;
}
```

`_handleCommandExecution`: extract `<execId>` between `/executions/` and
`/request/`; parse JSON; staleness check; dispatch on `operation`; publish status.

## Per-operation

| op | action | maps to existing |
|----|--------|------------------|
| `restart` | `setRestartSystem("Command: restart")` (publish SUCCEEDED first, then reboot delayed) | `_handleRestartMessage` (mqtt.cpp:875) |
| `factory_reset` | require `confirm==DEVICE_ID` else REJECTED; wipe user NVS, keep factory NVS | existing factory-reset path (utils/customserver) |
| `energy_reset` | reset cumulative kWh for listed channels or all | existing energy-reset path (ade7953) |

`restart`/`factory_reset` reboot: publish SUCCEEDED, `delay(2000)` to flush
(same pattern as OTA reboot, mqtt.cpp:1054), then restart.

## Removed (in 08)

`MQTT_TOPIC_SUBSCRIBE_COMMAND`, `_subscribeCommand`, `_handleCommandMessage`,
`_handleSetSendPowerDataMessage`, `_handleSetMqttLogLevelMessage`,
`_handleRestartMessage` (mqtt.cpp:776/841-913). `set_send_power_data` /
`set_mqtt_log_level` are replaced by the `system` shadow (04).

## Cloud side (energyme-infra, for the contract)

- `CreateCommand` x3 (namespace `AWS-IoT`) with the payloads above.
- Dispatcher Lambda/service calling `StartCommandExecution`.
- Policy statement for `$aws/commands/...` (item 1 in 00 cloud checklist).

## Tests

- On-device (needs cloud command): `StartCommandExecution` restart -> device
  publishes IN_PROGRESS then SUCCEEDED, reboots. factory_reset with wrong
  `confirm` -> REJECTED, no wipe. energy_reset `[3]` -> only ch3 counter zeroed.
- Staleness: start a command, keep device offline 6 min, reconnect -> REJECTED
  `stale_command`.

## Acceptance

- [ ] Wildcard request subscribe; execId parsed from topic; JSON status to response topic.
- [ ] All 3 ops work end-to-end on dev AWS.
- [ ] factory_reset confirm guard enforced.
- [ ] Commands >5 min old rejected.
- [ ] OTA Jobs path unchanged.
