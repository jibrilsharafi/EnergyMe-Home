## Context

The device already has one code path for acknowledging an issue: `IssueRegistry::ack(codeStr, channel)` / `IssueRegistry::ackAll()`, called today only from the local REST handler (`customserver.cpp`'s `ackIssueHandler`). The cloud has three precedents for transient (non-persisted) device actions delivered over MQTT - the AWS IoT Commands `restart`, `factory_reset`, `energy_reset` - each dispatched by operation name in `_handleCommandExecution` (`mqtt.cpp`). An issue ack is exactly this shape: a one-shot action, not a piece of desired configuration state, so it belongs with the other commands rather than as a shadow-writable field.

## Goals / Non-Goals

**Goals:**
- Let the cloud acknowledge one issue instance or all issues, with the same semantics and payload shape as the local REST endpoint.
- Reuse `IssueRegistry::ack`/`ackAll` unchanged - no duplicated ack logic.
- Keep the `issues` shadow reported-only, unchanged from the current spec.

**Non-Goals:**
- A cloud-initiated "remove" distinct from "acknowledge" - the registry has no manual delete; an issue always clears itself once the underlying condition resolves (`IssueRegistry` header: "nothing is persisted, so a stale issue is impossible by construction"). Acknowledge is the only cloud-facing action, matching what the local UI offers.
- Making the `issues` shadow writable / delta-driven. A shadow delta models desired persistent state; an ack is a transient action with no "desired" value to converge on, so it does not fit the shadow contract's apply/ack-null pattern used by `system`/`meter`/`channels`.
- Any cloud-side (`energyme-infra`) dispatcher/schema change - out of scope for this repo.

## Decisions

- **New command, not a shadow delta.** Consistent with the existing `iot-commands` vs. `iot-device-shadows` split: `iot-commands` already owns "transient device actions" (its spec purpose line), and issue-ack is one. Using the shadow path would require reclassifying `issues` as writable, contradicting the "Six named shadows with fixed read/write roles" requirement and the reported-only design note in `shadow.cpp` ("Up to 6 named shadows: info, issues, wifi (reported-only), ...").
- **`all` and `channel` each accept both their native JSON type and an AWS-string-typed form, like `energy_reset`'s `channels`.** The codebase documents, in more than one place (`mqtt.cpp`'s `energy_reset` comment, `ShadowLogic::parseChannelList`'s doc comment, the current `iot-commands` spec text), that "AWS IoT Commands params are string-typed end-to-end" - i.e. whatever `energyme-infra`'s dispatcher sends arrives as a JSON string even for logically boolean/numeric fields. Blindly assuming `all`/`channel` will arrive as a real JSON `bool`/`int` risks the exact silent-misroute failure `energy_reset` was built to avoid: `doc["all"].is<bool>()` and `doc["channel"].is<uint8_t>()` both evaluate `false` for a string payload, so `all` would look absent and `channel` would silently default to global scope. So: `all` is treated as true when the value is JSON boolean `true` **or** the string `"true"` (exact match) - mirroring the `is<bool>() ? as<bool>() : false` pattern the OTA `force`-field bug fix established (ArduinoJson's `as<bool>()` on a non-boolean, non-null value is not reliably false, so type-check first, never blind-cast). `channel` is read via `doc["channel"].is<uint8_t>()` directly, or, when it's a string, via the existing `ShadowLogic::parseChannelIndex(str, channelCount, &out)` helper (already built for exactly this "digit-string channel index" parse, used today for shadow delta object keys) - no new parsing code.
- **Reject, don't silently no-op, on `NO_SUCH_ISSUE`.** Matches the REST endpoint's `404` on `IssueRegistry::ack` returning `false` (unknown code string or no live instance for that code/channel) - a stale command should be visibly rejected, not swallowed.
- **`ackAll()`'s return count is logged, not carried in the command status.** `_publishCommandStatus` only ever attaches `reasonDescription` when `reasonCode` is also non-null (`if (reasonCode != nullptr) { ...; if (reasonDescription != nullptr) ... }` - `mqtt.cpp:1129-1132`), and every existing `SUCCEEDED` call site (`restart`, `factory_reset`, `energy_reset`) passes `reasonCode=nullptr`, since `reasonCode` is reserved for the AWS-pattern error/reason vocabulary, not free-form success text. Piggybacking the acked count on `reasonDescription` for `SUCCEEDED` would either silently vanish (current function behavior) or require widening `_publishCommandStatus`'s contract for a single caller. Instead: `LOG_INFO` the acked count on the device (consistent with `ackAll`'s own `_logTransition` calls), publish plain `SUCCEEDED`/`nullptr`/`nullptr` like every other command, and let the `issues` shadow's `active_count` (already refreshed by the same `ack`/`ackAll` call via the existing change-callback) be the cloud-visible source of truth for how many issues remain. (Alternative considered: extend `_publishCommandStatus` to decouple `reasonDescription` from the `reasonCode != nullptr` guard so a description-only success message becomes possible - rejected as unnecessary surface area for one caller when the shadow already conveys the outcome.)

## Risks / Trade-offs

- The cloud can't yet send `issue_ack` until `energyme-infra`'s command dispatcher gains the schema for it - this change is inert from the cloud's perspective until that follow-up lands. Mitigated by the on-device `injectCommandExecution` dev-inject harness, which exercises the new branch without any cloud-side change.
- Accepting both a native JSON type and its string form for `all`/`channel` is unverified against what AWS IoT Commands will actually deliver (no cloud-side schema exists yet - see Non-Goals). If the real payload turns out to need a third shape, this is a small, isolated parsing branch to adjust, not an architectural change.

## Migration Plan

None required - additive command branch, single firmware commit, no version bump (separate release step on `development` per repo convention).
