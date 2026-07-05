## Why

The grid MQTT publish cadence is a rolling 60 s timer measured from whenever the last publish happened (`millis64() - _lastMillisGridPublished > MQTT_MAX_INTERVAL_GRID_PUBLISH`), so it drifts from real wall-clock minute boundaries depending on boot time and reconnects. The 500 ms grid sampler already aligns each point to absolute `.000`/`.500` wall-clock boundaries; the publish side should follow the same principle so batches land on a predictable wall-clock cadence (`:00` of every minute) instead of an arbitrary, device-specific phase. This also sets up multiple devices' grid telemetry to arrive at the cloud in synchronized waves, which matters for near-real-time cross-device aggregation.

## What Changes

- Replace the relative "60 s since last publish" trigger in `_checkIfPublishGridNeeded()` with an absolute wall-clock deadline (`_nextGridPublishUnixSecond`), publishing when `CustomTime::getUnixTime() >= deadline` rather than on an elapsed-time interval.
- Use `>=` rather than exact-second equality so a delayed loop tick (this codebase has a documented history of scheduler starvation) still catches the boundary instead of missing it and waiting a full extra cycle.
- On first check after (re)connect, schedule the deadline to the next minute boundary rather than publishing immediately, so even the first grid publish is wall-clock aligned.
- After each publish, recompute the next deadline from the current time (not `old_deadline + 60`), so a device reconnecting after a long outage publishes once immediately and resyncs to real wall-clock cadence instead of firing a burst of catch-up publishes.
- Rename `MQTT_MAX_INTERVAL_GRID_PUBLISH` (ms interval) to `MQTT_GRID_PUBLISH_ALIGN_SECONDS` (seconds, the alignment period) to reflect the new semantics.
- No change to the existing per-publish payload cap in `_publishGrid()` (still breaks the drain loop at `AWS_IOT_CORE_MQTT_PAYLOAD_LIMIT * MQTT_METER_PAYLOAD_THRESHOLD_MULTIPLIER` and leaves the remainder queued) - that safety net is unaffected and no new independent size-based trigger is added.
- Out of scope: meter/systemDynamic/statistics publish cadences (relative-time/size triggered, no cross-device sync need), and any future "fast/live" meter publish override (separate feature, discussed but not designed here).

## Capabilities

### New Capabilities
(none)

### Modified Capabilities
- `grid-telemetry-stream`: the "Dedicated batched grid topic" requirement's publish cadence changes from a relative post-publish interval to absolute wall-clock minute alignment.

## Impact

- `source/src/mqtt.cpp`: `_checkIfPublishGridNeeded()`, `_publishGrid()`, state variable `_lastMillisGridPublished` → `_nextGridPublishUnixSecond`.
- `source/include/mqtt.h`: `MQTT_MAX_INTERVAL_GRID_PUBLISH` → `MQTT_GRID_PUBLISH_ALIGN_SECONDS`.
- No change to wire format, topic, or the meter publish path.
