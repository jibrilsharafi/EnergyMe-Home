# 01 - Scaffold `shadow` module

**Goal:** a reusable `Shadow` module that handles the protocol (00) for any named
shadow, so 02-06 only register a shadow + supply serialize/apply callbacks.

**Files:** new `src/shadow.cpp`, `include/shadow.h`. Hooks into `src/mqtt.cpp`.
No cloud writer needed to land/unit-test this phase.

## Public API (`shadow.h`)

```cpp
namespace Shadow {
    // Build the full reported state for this shadow into doc["state"]["reported"].
    using ReportFn = void (*)(JsonDocument& doc);
    // Apply one delta. Return fields actually applied (to echo into reported) via
    // `appliedReported`; list unknown/echo-null fields via `desiredNull`.
    // Return false if nothing applied (still acks by nulling).
    using ApplyFn  = bool (*)(JsonObjectConst delta,
                              JsonObject appliedReported,
                              JsonArray  desiredNullKeys);

    struct Descriptor {
        const char* name;        // "info", "system", ...
        bool        writable;    // false => reported-only, no delta subscribe
        ReportFn    report;
        ApplyFn     apply;        // nullptr when !writable
    };

    void begin();                         // create mutex, register descriptors
    void registerShadow(const Descriptor&);

    // Called by mqtt.cpp:
    void onMqttConnected();               // subscribe + publish-reported-first all
    bool routeMessage(const char* topic, const char* payload); // true if handled
    void publishReported(const char* name);                    // one shadow

    // Called by the mqtt.cpp task loop (drift-detect local edits + drain queued deltas):
    void checkPublish();   // every ~3 s: republish any writable shadow whose reported drifted
}
```

## Topic construction

Reuse `_constructMqttTopicReservedThings` pattern (mqtt.cpp:690) but it is
`static` in the `Mqtt` namespace. Add a small public helper in mqtt or duplicate
in shadow:

```
prefix = "$aws/things/" + DEVICE_ID + "/shadow/name/" + name
sub:  prefix + "/update/delta", "/update/rejected"   (NOT /update/accepted)
pub:  prefix + "/update"
```

**Do not subscribe `/update/accepted`:** its response echoes the full reported
state + a per-field metadata block (largest inbound message for `channels`) and
would stress `MQTT_BUFFER_SIZE`. `version` comes from the delta payload instead.

`MQTT_TOPIC_BUFFER_SIZE` (128) is enough: longest = `$aws/things/<12>/shadow/name/channels/update/rejected` ~= 60 chars.

## Per-shadow runtime state

```cpp
struct ShadowState {
    Descriptor desc;
    uint32_t   version       = 0;     // last accepted version (0 = unknown)
    bool       reportPending = false; // queue a reported publish
};
```

Store in a small static array (5 entries). Guard with a single `_shadowMutex`
(the apply callbacks themselves take the owning module's mutex - see per-shadow docs).

## Message routing (`routeMessage`)

Called from `_subscribeCallback` (mqtt.cpp:833) before the existing command/jobs
checks. Match `strstr(topic, "/shadow/name/")`, extract `<name>`, then suffix:

- `/update/delta`    -> parse `version` (store), **copy the delta payload into a
  per-shadow pending buffer, set `applyPending`**. Do NOT apply here.
- `/update/rejected` -> if code 409 -> request `publishReported(name)` (no
  version), else WARN. (Flag only; publish drains in the task body.)

(No `/update/accepted` handling - not subscribed; see topic construction above.)

### Execution model (do NOT apply in the callback)

`_subscribeCallback` runs inside `_clientMqtt.loop()` (MQTT task). Applying a
delta does blocking SPI/NVS writes and waits on `_configMutex`/`_channelDataMutex`
(up to `CONFIG_MUTEX_TIMEOUT_MS` = 1000 ms) - doing that in the callback stalls
`loop()` and MQTT keepalive (the #138 anti-pattern). So the callback only **copies
the delta out** (the PSRAM message buffer is freed when the callback returns - see
mqtt.cpp:838) and flags it. A new `_checkPublishShadows()` in `_handleConnectedState`
(mqtt.cpp:1895, mirrors `_checkIfPublishMeterNeeded`) runs in the task body and:
drains `applyPending` (-> `_handleDelta`), then drains `reportPending` (-> publish).
Per-shadow pending buffer sized for the worst-case delta; only one pending delta
per shadow (a newer delta overwrites - AWS will re-send if still divergent).

Parse with `SpiRamAllocator` + `JsonDocument` (same pattern as
`_handleCommandMessage`, mqtt.cpp:844). The 32 KB `MQTT_SUBSCRIBE_MESSAGE_BUFFER_SIZE`
is the app-level copy; the binding constraint is the static PubSubClient RX buffer
`MQTT_BUFFER_SIZE`, bumped to **9 KB** (08). With `/accepted` not subscribed, the
worst-case inbound message is a delta (changed desired fields + their metadata),
which the 9 KB buffer must hold without truncation.

## Delta handler (`_handleDelta`) - the core

```
doc = parse(payload)                       // {state:{...}, version, clientToken}
delta = doc["state"]                        // changed desired fields only
out = {}                                    // {state:{reported:{}, desired:{}}}
rep = out["state"]["reported"].to<JsonObject>()
des = out["state"]["desired"].to<JsonObject>()

desc.apply(delta, rep, /*unknownKeys*/...)  // applies + fills rep; lists unknowns
for each key in delta NOT applied: WARN, (already added to des as null by apply)
for each key in rep: des[key] = nullptr     // null every echoed field
for each unknown key: des[key] = nullptr

out["version"]     = state.version          // optimistic lock (omit if 0)
out["clientToken"] = _newToken()            // esp_random hex
publish(out -> <name>/update)
```

Key rule: **every key the device touches (applied or unknown) gets nulled in
`desired`** in the same publish. That is the ack + intent-clear.

## `publishReported(name)` (reconnect / periodic / event)

```
doc = {state:{reported:{}}}
desc.report(doc)                            // module fills reported
// NO version, NO desired  -> pending cloud desired survives (cloud-wins-on-reconnect)
publish(doc -> <name>/update)
```

## `checkPublish()` - local-edit drift-detect (replaced `publishLocalEdit`)

```
for each writable shadow:
  doc = {state:{reported:{}}}; desc.report(doc)   // rebuild current reported
  if reported changed since last published snapshot:
    publish(doc -> <name>/update)                 // reported-only: NO version, NO desired:null
```

Source-agnostic: any local change (REST/UI/internal) reaches the shadow within ~3 s.
Reported-only by design - it does **not** null `desired` (that would race in-flight
cloud deltas); the cloud owns clearing `desired` (see 00, asymmetric desired-null
decision). This replaced the original per-handler `publishLocalEdit`.

## clientToken / version helpers

```cpp
static void _newToken(char out[24]); // snprintf "%08x%08x", esp_random(), esp_random()
```

`esp_random()` is fine (hardware RNG, no WiFi dependency post-init).

## mqtt.cpp integration points

| Location | Change |
|----------|--------|
| `Mqtt::begin()` (mqtt.cpp:208 area) | call `Shadow::begin()` |
| `_subscribeToTopics()` (mqtt.cpp:756) | call `Shadow::onMqttConnected()` |
| `_subscribeCallback` dispatch (mqtt.cpp:833) | `if (Shadow::routeMessage(topic,message)) {}` first |

`Shadow::onMqttConnected()` subscribes all shadows then sets `reportPending` for
each; actual publishes drained in `_handleConnectedState` (mqtt.cpp:1895) via a
new `_checkPublishShadows()` (mirrors `_checkIfPublishMeterNeeded`, mqtt.cpp:1407)
so we publish from the MQTT task loop, not the callback.

## Tests

- **Native unit tests** (`pio test -e native`, run from WSL - see repo memory):
  put pure delta-merge logic in `lib/shadow_logic/` (no Arduino deps):
  - delta -> {reported, desired:null} doc shape (applied + unknown keys all nulled).
  - version omitted when 0, included when known.
  - local-edit doc shape.
  Mock `ApplyFn` with a fake field map. This isolates the hard logic (00) from MQTT.
- On-device: covered by 02/03 (reported-only, console-verifiable).

## Acceptance

- [ ] `lib/shadow_logic` unit tests pass (delta-ack shape, version gating, local-edit shape).
- [ ] Subscribes + publishes reported for a registered no-op shadow on reconnect.
- [ ] Unknown desired field -> nulled + WARN, no crash.
- [ ] No `/get` publish anywhere.
