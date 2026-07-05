## Context

`_checkIfPublishGridNeeded()` and `_publishGrid()` live in `source/src/mqtt.cpp`, called from the MQTT task's connected-state loop (`_handleConnectedState()`), which itself ticks on a shared `ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(MQTT_LOOP_INTERVAL))` (100 ms) alongside meter/systemDynamic/statistics/log/shadow processing. Grid points are sampled by a dedicated task (`ade7953.cpp`) that self-corrects its sleep to absolute `.000`/`.500` wall-clock boundaries every tick, so each point's own `unixTimeMs` is already exact regardless of when it is later published. Today's publish trigger is `millis64() - _lastMillisGridPublished > MQTT_MAX_INTERVAL_GRID_PUBLISH` (60 s), a relative timer with no relationship to wall-clock minute boundaries.

## Goals / Non-Goals

**Goals:**
- Publish grid batches aligned to real wall-clock minute boundaries (`:00` of every minute), independent of boot time or reconnect history.
- Keep the existing per-publish payload cap in `_publishGrid()` as the only mechanism bounding a single message's size - no new independent trigger.
- Degrade gracefully when the MQTT task's loop tick is delayed (known scheduler-starvation history) or when the device reconnects after a long outage.

**Non-Goals:**
- Sub-100 ms precision on when the publish physically leaves the device. Point timestamps are already exact at sample time; the publish trigger only affects batching, not data fidelity.
- Any change to meter, systemDynamic, statistics, log, or shadow publish triggers.
- A generalized "wake exactly at an absolute deadline" mechanism applied to the shared MQTT task loop. Only grid has an absolute wall-clock target (for cross-device sync); the others are relative/size-triggered and have no such target to align to.
- Any "fast/live" meter publish override - a related but separate feature, deliberately not designed here.

## Decisions

**Absolute deadline (`_nextGridPublishUnixSecond`) instead of relative interval.** The existing `_lastMillisGridPublished` only ever answers "how long since we last published," which drifts. Storing the next target wall-clock second directly makes "are we due" a single comparison against `CustomTime::getUnixTime()`, with no accumulation of drift across cycles.

**`>=` comparison, not `== 0 (mod 60)`.** The MQTT loop tick is not guaranteed to land exactly on a minute boundary (100 ms polling granularity, plus this codebase's documented history of logger-overload scheduler starvation delaying task loops further). An exact-equality check could miss the boundary entirely and wait a full extra minute. `>=` catches the boundary on the next available tick, however late, and is a strict superset of the equality case, i.e. no behavior change if a tick lands exactly on time.

**Deadline uninitialized (`0`) until first check, then set to the next boundary (not published immediately).** Guarantees the very first grid publish after boot/reconnect is still wall-clock aligned, rather than firing as soon as the queue has any data (which would reintroduce an arbitrary phase).

**Recompute next deadline from current time on each publish, not `old_deadline + 60`.** After a long MQTT outage, the queue may hold a large backlog and the old deadline could be far in the past. Advancing by a fixed `+60` from a stale deadline would keep the new deadline in the past too, causing the next loop tick to immediately re-trigger, and the one after that, etc., until the deadline finally catches up to real time - a burst of back-to-back publishes. Computing the next boundary from `now` at publish time means: publish once immediately to drain what's due, then resume normal once-per-minute cadence.

**No independent payload-size trigger.** `_publishGrid()` already breaks its drain loop at `AWS_IOT_CORE_MQTT_PAYLOAD_LIMIT * MQTT_METER_PAYLOAD_THRESHOLD_MULTIPLIER` and leaves the remainder queued for the next cycle - this already guarantees no single publish can exceed the AWS payload limit. Adding a second, estimate-based "queue is getting big" trigger (as used for meter) was considered and rejected: it would only speed up backlog drainage in an already-rare reconnect scenario, at the cost of an extra constant (bytes-per-point estimate) and a second code path that overlaps with the existing cap. The backlog still drains correctly, just at most once per minute rather than immediately - acceptable since grid data is a nice-to-have real-time view, not billing- or alerting-critical.

**Rename `MQTT_MAX_INTERVAL_GRID_PUBLISH` → `MQTT_GRID_PUBLISH_ALIGN_SECONDS`.** The unit and meaning change (ms relative interval → seconds alignment period), so the name should not imply the old semantics.

**No change to the shared MQTT loop's wait duration.** A dynamic `min(MQTT_LOOP_INTERVAL, ms_until_next_grid_boundary)` wait was considered to tighten publish latency to near-zero. Rejected: point timestamps are already exact independent of publish latency (see Non-Goals), so the added complexity in a loop shared by reconnects/meter/statistics/log/shadow processing buys nothing.

## Risks / Trade-offs

- [Up to ~100 ms lateness triggering the send after `:00`] → Accepted; point timestamps are unaffected, only batch arrival time shifts by a negligible amount.
- [Backlog after a long outage drains at most one capped batch per minute rather than immediately] → Accepted; grid telemetry is a nice-to-have real-time view, not a guaranteed-delivery or billing-sensitive stream. `_publishGrid()`'s existing per-publish cap still guarantees no single message exceeds the AWS limit.
- [`CustomTime::getUnixTime()` must be valid (NTP-synced) for the deadline math to make sense] → Already guaranteed upstream: `_handleConnecting()` blocks MQTT connection until `CustomTime::isTimeSynched()`, so `_checkIfPublishGridNeeded()`/`_publishGrid()` only ever run post-sync.

## Migration Plan

Firmware-only change, no data migration. Ships in the normal release/OTA path. Rollback is a normal revert/OTA to the prior build; no persisted state format changes (the renamed constant and retimed variable are in-RAM only, not stored in NVS).

## Open Questions

None outstanding - design was settled through discussion before this proposal was written.
