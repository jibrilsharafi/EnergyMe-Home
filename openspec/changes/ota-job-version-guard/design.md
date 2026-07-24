## Context

`_handleSingleJobExecution` in `mqtt.cpp` currently validates only that a job document has `operation == "ota_update"` and a non-empty `firmware.url`, then unconditionally starts the OTA download task. `firmware.version` is already present in every job document (see the example in `_validateAwsIotJobMessage`) but is never read on this path.

A version-compare helper already exists (`_compareVersions` in `customserver.cpp`, private/static, used only by the update-check endpoint) and a related but distinct check exists (`isBackupVersionCompatible` in `utils.cpp`, used for config-backup restore, which parses versions itself rather than delegating to `_compareVersions`).

The cloud repo (`energyme-infra`#231) is moving automated channel jobs from `SNAPSHOT` to `CONTINUOUS` targeting, meaning a job stays open indefinitely instead of completing once. This raises the cost of a stale/orphaned job re-triggering an old OTA on reconnect/renotify, since there is currently no firmware-side check preventing it.

## Goals / Non-Goals

**Goals:**
- Reject OTA jobs whose target version is not strictly newer than the running firmware, by default.
- Provide an unconditional escape hatch (`force: true` in the job document) for manual downgrade/re-flash operations.
- Consolidate version comparison into one shared implementation used by all three call sites.

**Non-Goals:**
- Enforcing who is allowed to set `force: true` - that's a cloud-side (`ota_release.py` CLI) operational discipline, not a firmware concern.
- Changing `isBackupVersionCompatible`'s own comparison logic/semantics (major-match + <= current) - it stays a separate rule, just potentially built on the same low-level parse if convenient, not required by this change.
- Changing the job document schema/validation beyond reading two already-present-or-optional fields (`firmware.version`, `force`).

## Decisions

**1. Move `_compareVersions` to `utils.cpp`/`utils.h`, unchanged signature and semantics.**
`int compareVersions(const char* current, const char* available)` - keep return convention (positive: current > available, 0: equal, negative: current < available) so `customserver.cpp`'s existing `_compareVersions(FIRMWARE_BUILD_VERSION, tagName) >= 0` call site needs only a rename, not a logic change. Alternative considered: duplicate a small helper in `mqtt.cpp` instead of moving - rejected, the issue explicitly calls for one shared implementation and utils.cpp already hosts `isBackupVersionCompatible` as the version-logic home.

**2. Guard placement: after existing job-document guards, before `IN_PROGRESS` status publish.**
In `_handleSingleJobExecution`, read `firmware.version` and `force` right after the `operation` check (which already returns early on unsupported jobs), and before the in-progress/download-task guards. This keeps the reject path cheap (no task creation, no URL copy) and keeps status semantics clean: a rejected job never transitions through `IN_PROGRESS`.

**3. `force` read as `doc["execution"]["jobDocument"]["force"] | false`.**
ArduinoJson's `variant | default` idiom cleanly handles "key absent, wrong type, or explicitly false" as `false`, matching "absent/default = false" from the issue. No change needed to `_validateAwsIotJobMessage` since `force` is optional.

**4. Version parse reuses `compareVersions`'s existing `sscanf("%d.%d.%d", ...)`, no stricter validation added.**
The job document's `firmware.version` is cloud-controlled (not user input), and `compareVersions` already defaults unparsed components to 0 via its current implementation. Alternative considered: reject on malformed version string - rejected as unnecessary scope creep; `force` is the existing escape hatch if a bad version string ever caused a false reject.

**5. Rejection reasons: `downgrade_not_allowed` and `already_up_to_date`, published via existing `_publishOtaStatus(jobId, "REJECTED", reason)`.**
Matches the existing status-publish pattern already used for `unsupported_operation`, so no new plumbing.

## Risks / Trade-offs

- [A version-string parsing edge case (e.g. malformed `firmware.version`) causes a false reject on a legitimate upgrade] -> Mitigated by the `force: true` override, which bypasses comparison entirely; no firmware-only recovery path is needed since `force` already exists for this.
- [Moving `_compareVersions` changes its access from private/static in `customserver.cpp` to a shared symbol in `utils.h`] -> Low risk: purely a code-motion refactor, no behavior change at the two existing call sites (update-check endpoint, and the new job-execution guard); build with `pio check` / native tests after the move to confirm no regressions.
- [`already_up_to_date` reject on an exact version match could mask a legitimate re-flash of the same version (e.g. corrupted partition recovery without a version bump)] -> Mitigated by `force: true`; this is the documented intended use of the override per the issue.

## Migration Plan

1. Add `compareVersions` to `utils.h`/`utils.cpp` (new function, additive).
2. Update `customserver.cpp` to call the shared function; remove the local static `_compareVersions`.
3. Add the version-guard block + `force` read to `_handleSingleJobExecution` in `mqtt.cpp`.
4. Unit-test `compareVersions` (native, host-compilable) for the three-way comparison and malformed-input default-to-0 behavior.
5. No cloud-side or wire-format migration required - `firmware.version` is already sent today; `force` is additive and optional, defaulting to today's unconditional-proceed behavior when absent.
6. Rollback: revert the firmware change; no persisted state or NVS schema is touched.

## Open Questions

None - behavior, reasons, and override are fully specified in the issue.
