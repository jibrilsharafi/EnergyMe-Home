## Context

The `meter` MQTT topic today carries three unrelated data shapes in one array: a single voltage snapshot, one energy-counter object per active channel, and N compact power points. The energy/voltage block is re-sent on every meter publish (currently threshold/interval-gated, recently made shadow-configurable in `configurable-meter-publish-rate`) even though the underlying counters change little between publishes. At 16 active channels this block is ~2.9-3 KB of a ~4.86 KB message. The `grid` topic already solved an analogous problem for frequency/voltage: its own queue, its own topic, its own AWS IoT rule, and a wall-clock-minute-aligned publish via `MqttGridSchedule::nextAlignedBoundarySeconds` (tested in `lib/mqtt_grid_schedule`). This change applies the same pattern to energy.

## Goals / Non-Goals

**Goals:**
- Remove voltage/energy weight from every power-triggered publish.
- Give energy data its own cadence: once per minute, aligned to `:00`, independent of the power topic's byte/interval trigger.
- Reuse the existing grid-alignment primitive rather than inventing a new scheduling mechanism.
- Keep the energy payload shape a direct relocation of what's already emitted today (no new JSON construction), minimizing implementation risk.

**Non-Goals:**
- No change to the `meter` topic's threshold/interval trigger *logic* (still an OR of size-vs-time), only to what it estimates and what it carries.
- No change to `grid` topic behavior.
- No new PSRAM queue for energy - it is a periodic snapshot of current state, not a continuously-sampled stream like grid.
- No cloud-side work (AWS IoT rule, Lambda, ingestion/storage) - tracked separately in `energyme-infra`.
- No new boolean shadow flag to gate energy emission.

## Decisions

**Two topics, not one, not a mode flag on `meter`.** A single topic with an embedded "type" discriminator would still couple the two cadences at the transport level (one message = one MQTT publish = one billing event), so it wouldn't solve the core problem. Two independent topics let each have its own trigger without any conditional logic inside a shared publish path.

**Topic name `energy`, own AWS IoT rule.** Matches the existing single-word lowercase convention (`meter`, `grid`, `log`). New `AWS_IOT_CORE_RULE_ENERGY` per environment in `awsconfig.h`, following the exact pattern of `AWS_IOT_CORE_RULE_METER`/`GRID`/`LOG`. The rule itself (routing, Lambda, storage) is created in `energyme-infra`; this repo only needs the rule *name* to construct the topic string via the existing `_constructMqttTopicWithRule()` helper.

**Snapshot, not a queue.** Grid samples continuously (every 500 ms) because frequency/voltage are genuinely time-varying signals worth capturing between publishes. Energy counters are monotonic and change slowly; there is nothing gained by buffering intermediate values between `:00` boundaries. `_publishEnergy()` reads current per-channel state directly (`Ade7953::getMeterValues`, same accessor `_publishMeterStreaming` already uses) at the moment the boundary fires. This avoids a second PSRAM allocation and queue-management path entirely.

**Wall-clock `:00` alignment, reusing `MqttGridSchedule::nextAlignedBoundarySeconds`.** No new alignment math. New state (`_nextEnergyPublishUnixSecond`) and a scheduling check (`_checkIfPublishEnergyNeeded()`) mirror `_nextGridPublishUnixSecond` / `_checkIfPublishGridNeeded()` line-for-line in structure. Rationale for `:00` specifically: it lets the cloud diff `E(:00) - E(previous :00)` into clean per-minute buckets without interpolation - the entire reason to move energy off a byte-threshold trigger in the first place.

**Payload shape: flat self-contained array, real key-value pairs, not compacted like grid/power.** Considered and rejected a nested `{unixTime, voltage, channels: [...]}` object. Rejected because: (a) the flat shape is exactly what `_publishMeterStreaming` already builds today (mqtt.cpp ~2015-2043) - moving it is a relocation, not a rewrite; (b) it matches the existing "bare array of typed points" contract style already used by `meter` and `grid`; (c) it needs no new type-discrimination logic on the cloud side beyond what already exists for today's mixed meter array. Key-value objects (not terse arrays) are kept because at ~3 KB for 16 channels the payload is already well under the 5 KB AWS IoT Core minimum billable block - compacting would save bytes but never cross a billing boundary, so there's no cost argument for sacrificing readability/debuggability on a low-volume payload.

**Per-channel timestamp stays the real last-read time**, not the nominal `:00` boundary. Channels are read round-robin, not simultaneously, so per-channel `unixTime` values differ. This is preserved as ground truth for debugging and is never overwritten with a fabricated "now" or an interpolated/extrapolated value - see "Publish gated on all-channels-fresh, not immediately at `:00`" below for why the *original* jitter estimate here ("sub-second") was wrong, and what was done about it.

**Publish gated on all-channels-fresh-since-boundary, not immediately at `:00`.** Bench verification (UDP log capture, `.174`) showed the jitter above is not sub-second in general: channel 0 (the direct, non-muxed ADE7953 input) is serviced ~every 200 ms and lands within ~100 ms of any boundary either side, but every other channel shares one physical mux (`74HC4067`, 200 ms settling time) and is serviced at a WDRR-governed cadence that can leave it several seconds stale relative to the boundary (observed 3-6 s for idle/deprioritized channels on a 4-channel bench; project history cites ~6 s for a low-priority channel under real starvation). Publishing unconditionally the instant `now >= boundary` (the original design) meant a single `energy` snapshot could mix a channel reading from just after `:00` with another reading from several seconds *before* `:00` - i.e. actually belonging to the *previous* minute. Two fixes were considered and rejected before landing on this one:
- *Fake the timestamp to "now" at publish time* - rejected outright: makes stale data look fresh, actively destroys the ability to detect staleness downstream, doesn't change the underlying value.
- *Interpolate/extrapolate energy from last known power* (`energy_est = last_energy + last_power × dt`) - rejected: fabricates a value nobody measured for a monotonic, cumulative, billing-adjacent counter; there's no schema field to mark a point "estimated," so a consumer can't tell it apart from a real read.
- *Shift the alignment boundary (e.g. `:30` instead of `:00`), or drop wall-clock alignment entirely for a free-running ~60 s timer* - rejected: neither touches the actual cause (mux/WDRR serialization), so cross-channel skew is unchanged either way; dropping alignment also loses fleet-wide same-minute correlation and drift-free scheduling (see the `:00`-alignment decision above), and turns a deterministic, debuggable bias into unreproducible jitter for no gain.
- *Force a synchronous full-channel sweep before every publish, overriding WDRR* - would genuinely shrink the skew, but is a change to the core sampling scheduler (`ade7953.cpp`), touches the power-topic cadence too, and needs its own design/testing; out of scope for a topic-split change.

Instead, `_checkIfPublishEnergyNeeded()` holds the publish once `now >= boundary`: it waits until every active channel with valid measurements (plus the base-phase voltage read) has `lastUnixTimeMilliseconds >= boundary`, so every point in the snapshot is guaranteed to be from *inside* the current minute - eliminating the "one channel is actually last-minute's data" case by construction, using only the WDRR cadence that already exists (typically resolves within a few seconds on real hardware). A capped deadline (`MQTT_ENERGY_PUBLISH_DEADLINE_SECONDS`, default 10 s) forces the publish anyway if a channel is pathologically starved past that window, so a stuck channel can never block the topic indefinitely - the snapshot just publishes with whatever's freshest at that point, same honest-timestamp behavior as before.

This does mean each snapshot reflects the *first* reading past the boundary for every channel, not some "latest, closest to boundary" value - which is fine, and arguably better: downstream should never read a monotonic counter's raw value as "the reading for minute N," it should difference consecutive snapshots (`E(:01) - E(:00)`) to get the energy consumed in that window. Since every snapshot is anchored just after its own boundary by a similar, bounded margin, that margin cancels out in the subtraction instead of accumulating - the delta reflects the true elapsed window regardless of the small, consistent phase offset, and the ADE7953 Wh integration already uses real elapsed `deltaMillis`, not an assumed 60 s, so no error is introduced either way.

**Unconditional emission - no gate.** Today's voltage/energy blocks are already independent of `send_power_data` (explicit in the current code comments). Carrying that forward means no new shadow field, no new "is energy reporting enabled" state to reason about. The energy topic becomes the unconditional floor for voltage visibility even when `send_grid_data` and/or `send_power_data` are both off.

**No shadow schema change.** `meter_publish_threshold_bytes` / `meter_publish_max_interval_ms` (added in `configurable-meter-publish-rate`) keep their names; their semantics narrow to "power-topic cadence" naturally now that `meter` is power-only. Renaming would be a breaking shadow-schema change for zero behavioral benefit - only doc comments need updating.

**Voltage lives only on `grid` (when enabled, 500 ms resolution) and `energy` (always, 1/min).** Fully removed from `meter`. No conditional re-add of meter-topic voltage when `send_grid_data` is off - `energy`'s unconditional 1/min snapshot is already the fallback, so adding a second conditional path would be redundant complexity.

## Risks / Trade-offs

- **[Breaking cross-repo contract]** Any existing cloud consumer reading voltage/energy off the `meter` topic breaks the moment this ships. → Mitigation: coordinate firmware rollout with the `energyme-infra` rule/ingestion cutover; do not deploy to fleet until the `energy` topic is routed and consumed cloud-side. This proposal intentionally documents the exact new contract so that work can start in parallel now.
- **[No backlog for missed energy snapshots]** If a publish fails at `:00` (e.g. transient disconnect), that minute's snapshot is not queued/retried - the next boundary just captures fresh (slightly different) counter values. → Acceptable: counters are monotonic, so a missed minute is a gap, not data loss; downstream can still compute deltas across the gap using the next successful snapshot.
- **[Two topics to keep in sync during development]** Power-topic and energy-topic changes touch adjacent code paths (`_checkPublishMqtt`, topic setup) - risk of one being missed in review. → Mitigation: mirror the grid implementation structurally (same function names, same section layout) so the diff is easy to review by comparison.

## Migration Plan

1. Land firmware change behind normal branch/PR flow (no runtime feature flag - this is a payload/topic shape change, not a tunable).
2. Coordinate with `energyme-infra`: new `energy` AWS IoT rule + Lambda routing + ingestion/storage must exist in an environment before any device on that environment is flashed with this firmware.
3. Roll out to dev/bench devices first (dev AWS IoT rule), verify both topics independently (meter power-only, energy on `:00` cadence) via UDP log capture and CloudWatch, per existing device-testing conventions.
4. Roll out to fleet only after prod-side infra is confirmed ready.
5. Rollback: revert the firmware release (standard OTA rollback path); no on-device state migration needed since nothing is persisted to NVS by this change.

## Open Questions

- Exact timing coordination with `energyme-infra` for the new rule's dev-vs-prod availability - tracked in that repo, not blocking these artifacts.
