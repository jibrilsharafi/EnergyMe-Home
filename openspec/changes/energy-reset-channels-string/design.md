## Context

`_handleCommandExecution`'s `energy_reset` branch (`source/src/mqtt.cpp:1019-1035`) currently accepts `channels` as either the literal string `"all"` or a JSON array of integer indices. AWS IoT Commands parameters are string-typed end-to-end - `dispatch_command.py` already sends `channels` as a string (e.g. `"0,2,5"`) - so the JSON-array path can never actually be reached from the cloud. Today only `"all"` works from the cloud path; any selective cloud reset is rejected with `BAD_CHANNELS`. The JSON-array path remains reachable only from the on-device inject test harness, which must keep working unmodified.

## Goals / Non-Goals

**Goals:**
- Accept comma-separated channel specs (`"5"`, `"0,2,5"`) delivered as a string, in addition to the existing `"all"` string.
- Preserve current behavior for the array-based on-device inject test harness, byte-for-byte.
- No cloud/CDK change - the cloud already sends the right shape.

**Non-Goals:**
- Changing the AWS IoT Command parameter schema (already string-typed).
- De-duplicating or sorting the parsed channel list.
- Validating channel count limits beyond what fits the parse buffer.

## Decisions

- **Pure, read-only scan - no copy buffer, no `strtok`.** `doc["channels"].as<const char*>()` points into the already-deserialized, already-bounded (9 KB `MQTT_BUFFER_SIZE`) JSON document. A new `ShadowLogic::parseChannelList(spec, channelCount, validIndices, maxOut, &invalidTokenSeen)` host-testable helper (`lib/shadow_logic`, alongside the existing `extractExecutionId`/`isCommandStale` IoT-Commands helpers) scans `spec` directly without mutating or copying it, so there is no fixed-size buffer to size or overflow. Per token it copies only the (small, bounded) digit run into an internal stack scratch buffer to reuse the existing `parseChannelIndex` digit/range check.
- **No up-front length check needed.** Since nothing is copied into a fixed buffer, there is no truncation/overflow risk to guard against; the scan is O(length of `channels`), already bounded by the MQTT buffer size. (Supersedes the earlier `strlcpy`-into-`buf[64]` + length-reject plan.)
- **Per-token parsing is best-effort, not all-or-nothing.** A non-numeric or out-of-range token is skipped (and reported back via `invalidTokenSeen` so the caller logs `WARNING`), mirroring the existing array branch's per-index validation (`isChannelValid` failure -> `WARNING`, not abort). Alternative considered: validate the whole string in a first pass and only apply on a clean parse (true all-or-nothing). Rejected - it would diverge from the array branch's existing semantics for no real benefit.
- **Leading/trailing-space trim + empty-token skip.** Tolerates `"0, 2, 5"` and a stray trailing comma (`"5,"`) without treating them as parse failures, since cloud-formatted lists may include either.
- **Zero valid channels -> reject, not silent success.** If the spec yields no valid indices at all (empty string, or every token invalid/out-of-range), the command is `REJECTED` with `BAD_CHANNELS` rather than reporting `SUCCEEDED` for a no-op. A partial list (>=1 valid index, some invalid) still succeeds per the best-effort rule above - only the fully-empty result is treated as a rejection.

## Risks / Trade-offs

- Best-effort per-token parsing means a request like `"5,x,8"` resets channels 5 and 8 while only warning on `x`, rather than rejecting the whole command -> Mitigation: this matches the array branch's existing precedent (out-of-range index -> warn and continue), so it's consistent behavior, not a new failure mode.

## Migration Plan

None required - additive parsing branch, single firmware commit, no version bump (separate release step on `development` per repo convention).
