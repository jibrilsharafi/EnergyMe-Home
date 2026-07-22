## Context

`_checkIfPublishMeterNeeded()` (`source/src/mqtt.cpp:1703-1720`) triggers a meter publish on an OR of two hardcoded constants from `source/include/mqtt.h`:
- `AWS_IOT_CORE_MQTT_PAYLOAD_MINIMUM_BILLABLE` (5 KB) - queue-size trigger, chosen to match AWS IoT Core's per-message billing floor so small payloads aren't sent at a billing loss.
- `MQTT_MAX_INTERVAL_METER_PUBLISH` (60 s) - fallback ceiling so voltage/energy data still goes out even when the queue never fills.

The `system` shadow (`source/src/shadow.cpp`, `_reportSystem`/`_applySystem`) already exposes `send_power_data` and `send_grid_data` as cloud-settable, NVS-persisted booleans, following a well-established delta-apply-persist-ack pattern. This change extends that same shadow and pattern with two numeric fields instead of introducing a new shadow or a new mechanism.

## Goals / Non-Goals

**Goals:**
- Let the cloud change meter publish cadence at runtime, no reflash.
- Reuse the exact existing `system` shadow config pattern (getter/setter, NVS key, delta-apply, ack-with-desired-null).
- Keep the device fully stateless with respect to *why* a value is set - it only ever sees two numbers.
- Preserve current fleet behavior by default (defaults identical to today's hardcoded constants).
- Guard against a bad or forgotten write causing a publish storm or an oversized payload.

**Non-Goals:**
- No on-device "mode" concept (no `realtime`/`normal`/`night` enum) - scaling the two raw numbers together already spans the full range from batched to near-real-time.
- No cloud-side scheduling, automation, or auto-revert logic (EventBridge cron, "fair mode" button, dead-man's switch) - that is cloud/infra work, entirely out of scope for this firmware change.
- No change to grid publishing (`MQTT_GRID_PUBLISH_ALIGN_SECONDS`) - separate trigger, separate future change if ever needed.
- No change to the OR trigger shape itself (size OR time) - only its two operands become configurable.

## Decisions

**Two independent numeric fields, not a single "mode" enum.**
An enum (`normal`/`realtime`/`batch`) would need firmware-side preset tables and hides the actual numbers from whoever is tuning cadence from the cloud. Two raw fields keep the device as a pure function of (threshold, interval) and push all policy - including any future presets - to the cloud side, which already has the shadow desired/reported mechanism to express it.

**Extend the existing `system` shadow, don't add a new shadow.**
The `meter` shadow already exists but is scoped to ADE7953 calibration + sample_time (physical measurement config). Publish cadence is telemetry-transport config, the same category as `send_power_data`/`send_grid_data`, both of which already live in `system`. Adding a fourth named shadow would fragment related transport-behavior config across two shadows for no benefit.

**Defaults equal today's hardcoded constants.**
`meter_publish_threshold_bytes` defaults to `AWS_IOT_CORE_MQTT_PAYLOAD_MINIMUM_BILLABLE` (5120), `meter_publish_max_interval_ms` defaults to `MQTT_MAX_INTERVAL_METER_PUBLISH` (60000). A device that has never received a `desired` for these fields behaves identically to today.

**Clamp, don't reject-and-ignore, out-of-range values.**
Following the existing `_applySystem` pattern (e.g. rejecting a non-boolean `send_power_data` with a WARN and applying nothing), an out-of-range numeric value is clamped to the nearest valid bound and the clamped value is what gets persisted, reported, and logged - rather than silently ignoring the whole field. This keeps the ack (`reported`) always truthful about what the device is actually doing, which matters more for a manually-operated "dial down for the fair" field than a strict reject would.

**Bounds:**
- `meter_publish_threshold_bytes`: floor at some small non-zero value (e.g. 256 B, enough for a handful of entries so the trigger is meaningful but can go well below the 5 KB billing floor for real-time use); ceiling at `AWS_IOT_CORE_MQTT_PAYLOAD_LIMIT * MQTT_METER_PAYLOAD_THRESHOLD_MULTIPLIER` (matches the existing safety margin already used elsewhere for the 128 KB AWS limit).
- `meter_publish_max_interval_ms`: floor at roughly `MQTT_LOOP_INTERVAL` (100 ms) - below the task's own poll cadence, a smaller value can't have any effect and only risks the device believing it's meeting an SLA it isn't; ceiling generous (e.g. 24 h) since a long interval is just "batch a lot," same risk class as today's default.

## Risks / Trade-offs

- **Real-time mode intentionally under-bills relative to actual usage** (payload &lt; 5 KB still costs a full AWS billing unit per message) → acceptable and expected trade-off for short, deliberate windows (a few hours at a fair); not a firmware concern, calling it out here only so it's not mistaken for a bug later.
- **Left-on real-time mode after the fair could run indefinitely at elevated message rate/cost** → explicitly out of scope for this change (cloud-side revert job), but worth flagging loudly in the proposal/PR so it isn't lost.
- **Clamping instead of rejecting could mask an operator typo** (e.g. asked for 5000 ms, typo'd 50000000 clamps silently to the 24 h ceiling instead of erroring) → mitigated by always logging the clamp at WARN and always reporting the clamped value back, so the cloud-side caller can see the actual applied value in `reported` and notice the mismatch.
- **Two separate NVS writes on a single delta could interleave with a crash mid-write** → follow the existing `_configMutex`-guarded, copy-under-mutex-then-persist pattern already used elsewhere in the config modules; no new risk beyond what already exists for multi-field shadow deltas.

## Migration Plan

No migration needed - new NVS keys default to the current hardcoded constants when absent (first boot after the firmware update, or any device that has never received a `desired` for these fields). No breaking change to shadow shape; existing `send_power_data`/`send_grid_data` fields are untouched.

## Open Questions

- Exact floor for `meter_publish_threshold_bytes` (256 B suggested) - fine-tune once real payload sizes at low channel counts are checked against `MQTT_METER_ESTIMATED_PER_ENTRY`.
- Should the two new field names be `meter_publish_threshold_bytes` / `meter_publish_max_interval_ms` (snake_case, matching `send_power_data`/`send_grid_data`) - assumed yes for shadow-field consistency, confirm during implementation.
