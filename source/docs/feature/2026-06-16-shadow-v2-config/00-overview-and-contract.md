# AWS IoT Device Shadow config (v2.1.0) - overview & contract

Tracking issue: #159. This doc set is the device<->cloud contract both repos
(`EnergyMe-Home` firmware, `energyme-infra` cloud) validate against before coding.

## Doc index

| # | Doc | Scope | Cloud writer needed to test? |
|---|-----|-------|------------------------------|
| 00 | this | contract, apply logic, cloud checklist, open decisions | - |
| 01 | `01-scaffold-shadow-module.md` | `shadow.cpp/.h`: subscribe, publish-reported, delta dispatch, version/clientToken, mutex | no |
| 02 | `02-info-shadow.md` | reported-only identity; retires `system/static` | **no** (console-verifiable) |
| 03 | `03-issues-shadow.md` | reported-only issue registry; transition-triggered | **no** (console-verifiable) |
| 04 | `04-system-shadow.md` | writable behavioural config (trimmed set) + `mqtt_log_level` auto-revert | yes |
| 05 | `05-meter-shadow.md` | writable ADE7953 calibration + sample time | yes |
| 06 | `06-channels-shadow.md` | writable per-channel config, object-keyed | yes |
| 07 | `07-commands.md` | IoT Commands: restart, factory_reset, energy_reset | yes |
| 08 | `08-v2-cutover.md` | retire config topics (`command`/`system/static`/`channel`), buffer bump, fw 2.1.0 (topic stays v1) | n/a |

**Build/test order:** 01 -> 02 -> 03 give on-device, cloud-independent proof
(reported-only shadows are verifiable by inspecting shadow state in the AWS
console with zero cloud backend). 04-07 need a cloud `desired`/command writer to
exercise the inbound path. 08 is the breaking cut, last.

## Scope decisions (locked)

- Deliverable now: this contract doc set. No infra issues opened.
- **No topic-version bump: `MQTT_TOPIC_VERSION` stays `"v1"`.** No surviving topic
  changes payload, so a version would be ceremony. Migration is additive (shadows
  + Commands on net-new `$aws/...` topics) + subtractive (3 config topics stop
  being published). Keeping v1 avoids rule duplication, a rollout window,
  device-policy ARN migration, and the "delete v1 early = data loss" footgun.
  Surviving telemetry is untouched, so **zero ingest gap** for any fleet mix.
  Firmware release still bumps to 2.1.0 (firmware semver, not the topic string).
- Secrets stay local-only (WiFi, CustomMQTT creds, InfluxDB token, web password).
- Energy counters / instantaneous power stay on telemetry topics (shadow writes
  would burn the 20 RPS/thing budget).
- Secrets stay local-only (WiFi, CustomMQTT creds, InfluxDB token, web password).
- Energy counters / instantaneous power stay on telemetry topics (shadow writes
  would burn the 20 RPS/thing budget).

## Verified AWS facts (load-bearing)

- **Shadow doc size cap = 8 KB, state only; metadata (timestamps) excluded.**
  Worst-case `channels` reported (17 ch x ~200 B) ~= 3.4 KB. Single shadow is fine.
- **Named-shadow + Commands topics are plain MQTT sub/pub**, MQTT 3.1.1 /
  PubSubClient-compatible, available eu-west-1. No AWS device SDK required.
- Shadow RPS limit 20/thing; in-flight unacked 10/thing.

## Protocol: publish-reported-first, no GET (the apply logic)

The device **never** publishes `/get`. A GET returns the full document
*including* the metadata block (a timestamp per field) -> large payload that can
overflow the inbound buffer. **For the same reason the device does NOT subscribe
to `/update/accepted`**: its response echoes the full reported state *plus* a
per-field metadata block, which for `channels` is the single largest inbound
message and would stress the RX buffer. The device subscribes only to
`/update/delta` (small: changed fields + minimal metadata) and `/update/rejected`
(tiny). `version` is sourced from the delta payload, not from `/accepted`. Flow,
per named shadow `<name>`, on every MQTT (re)connect:

1. Subscribe `update/delta`, `update/rejected`. (NOT `update/accepted`.)
2. Publish `{state:{reported:<full current NVS config>}}` to `update`.
3. AWS stores reported. If a `desired` exists and differs, AWS auto-pushes
   `update/delta` (no GET needed).
4. On `delta`: apply each field -> persist NVS -> publish **one combined**
   `{state:{reported:{<applied>}, desired:{<field>:null}}}`. The `desired:null`
   removes the pending intent; that publish *is* the ack.
5. The delta payload carries `version`; record it for the next
   optimistic-concurrency check (no `/accepted` subscription needed).

### The asymmetric desired-null semantic (conflict policy = "cloud wins on reconnect")

The combined `{reported, desired:null}` publish is the **same shape** for both the
cloud-delta path and the local-edit path. The difference is *when* desired is nulled:

| Trigger | Publish | Effect |
|---------|---------|--------|
| Cloud delta | `{reported:{f:cloudVal}, desired:{f:null}}` | apply cloud value, clear intent |
| Local UI edit | `{reported:{f:localVal}, desired:{f:null}}` | local value wins, clears any pending cloud intent for `f` |
| Reconnect (reported-first) | `{reported:<full>}` (does **not** null desired) | any pending cloud `desired` survives -> AWS re-sends delta -> **cloud value re-applied** |

So: **a local edit wins only when actively made** (it nulls desired). **Across a
reconnect, a still-pending cloud desired wins** (reported-first does not clear it).
Cloud must re-assert `desired` to override a value the user changed locally while online.

### Version / clientToken

- Track `version` from each `update/delta` payload (it includes the doc version).
  Echo it on the combined delta-ack publish for optimistic locking. On
  `update/rejected` code 409 (version conflict): re-publish reported **without**
  version and wait for a fresh delta. (Verify `version` is present in the delta
  payload during impl - it is in the standard shadow delta document.)
- Do **not** send `version` on the reconnect reported-first publish (avoids
  spurious 409 on reconnect).
- `clientToken`: per-publish token (from `esp_random()`) included on outbound
  updates for cloud-side traceability and correlation on `/rejected`. The device
  does **not** use it for dedup (no `/accepted` subscription). Optional; drop if
  it adds no value once the cloud side is settled.

### Unknown desired field

Do not apply; still null it (`{desired:{unknownField:null}}`) and `LOG_WARNING`.
Forward-compat, not an error.

### Contract constraints on the cloud writer

(This repo can't enforce these on-device; they are part of the cloud contract.)

- **Chunk `channels` desired writes.** A single delta that sets `desired` for many
  channels with max-length labels + per-field metadata can exceed the device RX
  buffer (`MQTT_BUFFER_SIZE`, 9 KB) and be silently dropped. The backend must
  write channel `desired` in batches of **<= N channels per update** (N chosen so
  worst-case wire delta < buffer; see 06). Other shadows are small, no constraint.
- **Clear `desired` on `factory_reset`.** After a reset the device reports
  defaults; any still-pending `desired` would be delta'd back and partially undo
  the reset. The backend must clear all shadows' `desired` for the thing when it
  issues `factory_reset` (07).

## Shadow catalog (schemas corrected against actual firmware)

Field names below match the **current firmware JSON** (the existing
`*ToJson` serializers), not the issue's illustrative names. Each shadow doc has
the authoritative schema.

1. **info** (reported-only) - identity. Source: `systemStaticInfoToJson` fields
   flattened (`utils.cpp`). Replaces `_publishSystemStatic`. See 02.
2. **issues** (reported-only) - `IssueRegistry::issuesToJson` output verbatim,
   plus derived `active_count`. See 03.
3. **system** (writable) - exactly 5 fields, the ones configurable + persisted
   today: `led_brightness`, `send_power_data`, `mqtt_log_level`,
   `log_level_print`, `log_level_save`. `ntp_server`/`timezone` are out (NTP/TZ
   work automatically once cloud-connected; device runs UTC internally).
   `modbus_*` is out (hardcoded in firmware, not configurable). See 04.
4. **meter** (writable) - ADE7953 calibration (struct `Ade7953Configuration`,
   camelCase keys, NVS `ade7953_ns`) + `sample_time`. See 05.
5. **channels** (writable) - per-channel object keyed by index 0-16, nested
   `ctSpecification`, `_channelDataMutex`, channel-0 invariant. See 06.

## Cloud-side required changes (REFERENCE ONLY - not implemented in this repo)

**This repo does not touch `energyme-infra`.** The list below is the
coordination contract: it is implemented and tracked on the cloud side
separately. It is here only so the firmware contract is validated end-to-end.
These do **not** exist today (infra ADR-001 defers shadows to "Phase 2"):

1. **Device policy** (`energyme-home-{env}-device-policy`): add a statement for
   **`$aws/commands/things/${iot:Connection.Thing.ThingName}/*`** (subscribe +
   receive + publish) - Commands topics are NOT under `$aws/things/*`, so the
   current policy does not cover them. Shadow topics (`$aws/things/.../shadow/*`)
   ARE already covered by the existing `AllowSubscribe`/`AllowReceive`/`AllowPublishShadow`.
2. **Shadow read/write backend** ("Intelligence backend"): the desired-state
   writer + reported-state reader (one ingestion Lambda + rule). Net-new; nothing
   writes `desired` today. Reuses the existing `device-ops` DynamoDB table
   (schemaless - add a map per shadow on the per-device item; no new table). It
   replaces what `system_static` + `channel_handler` write today.
3. **IoT Commands**: create 3 command templates (restart, factory_reset,
   energy_reset) + a `StartCommandExecution` dispatcher. Net-new.
4. **No v2 rules / no policy-ARN migration**: topic stays `v1`, so existing
   telemetry rules + policy publish ARNs are unchanged. The 3 retired topics'
   rules (`system/static`, `command`, `channel`) just go idle once firmware stops
   publishing; delete them whenever convenient. Only the `$aws/commands/*` policy
   statement (item 1) is added.

## Open decisions (for joint cloud review)

1. ~~`system` shadow scope~~ **RESOLVED:** ship the 5 configurable fields.
   `ntp_server`/`timezone` out (auto via cloud/NTP, device is UTC). `modbus_*` out
   (not configurable in firmware). No follow-up needed for this work.
2. ~~`channels` source of truth~~ **RESOLVED:** shadow is the single source of
   truth for all configurable state. The device **stops publishing the `channel`
   topic** (was config, not telemetry); cloud reads channel
   config from the `channels` shadow and retires `channel`-topic ingestion. Same
   principle retires `system/static` (-> `info` shadow). **Guiding rule:**
   telemetry topics carry only measurements/statistics/dynamic runtime; all
   configurable state lives in shadows. (WiFi credentials are the one carve-out:
   local-only, never in a shadow - AWS forbids secrets in shadow docs.)
3. ~~`issues` ack path~~ **RESOLVED:** via shadow **desired/delta**. Cloud writes
   `desired.ack=[codes]`; device acks those, reports new states, nulls
   `desired.ack`. `issues` is partially-writable (only the `ack` key). See 03.
4. ~~`git_commit` in `info`~~ **RESOLVED:** publish `sketch_md5`
   (`ESP.getSketchMD5()`), NOT git commit. The binary hash proves whether the
   running firmware is the published build or a modified/recompiled one; a git
   SHA can lie (uncommitted/dirty tree). See 02.

## Final payload-shape review (gate before "done")

After 01-08 are implemented, do a **joint review of every shadow + command
payload** (firmware + cloud together) to confirm each carries exactly what's
needed - nothing missing, nothing extra. Capture the agreed final schemas here.

## Deferred / post-MVP (after the core is up)

The first cut ships the config surface that already exists on-device. Once the
mechanism is live we optimize and add remaining endpoints. Known candidates to
revisit in the payload review:

- **WiFi info** (non-secret): connected SSID, RSSI, IP - reportable in `info` or a
  WiFi shadow. (RSSI is already in `system/dynamic` telemetry today.) **Creds stay
  local-only.**
- **WiFi actions**: reconnect / rescan / forget - candidate IoT Commands.
- Any config currently exposed via REST but not yet mirrored to a shadow.
- `issues` ack wiring (decision 3) once the cloud desired-writer exists.

These are explicitly **not** in the 01-08 scope; they are the next iteration.
