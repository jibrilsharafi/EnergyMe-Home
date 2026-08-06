## Why

Locally, a device issue (raised by the issue registry - #145) can be acknowledged via `POST /api/v1/system/issues/ack`. The cloud has no equivalent: the `issues` shadow is reported-only by design (`iot-device-shadows` spec - "Six named shadows with fixed read/write roles"), so a delta to it is ignored, not applied. There is currently no way for the cloud dashboard/automation to acknowledge a device issue without a user going through the local web UI.

## What Changes

- Add a 4th AWS IoT Command, `issue_ack`, to the existing `iot-commands` capability - same transient-action pattern as `restart`/`factory_reset`/`energy_reset`, dispatched from `_handleCommandExecution` (`mqtt.cpp`).
- Payload shape mirrors the local REST endpoint's fields: `{"all": true}` acks every currently-unacked issue, or `{"code": "<CODE>", "channel": <optional>}` acks one instance (`channel` omitted = global scope, `ISSUE_GLOBAL_SCOPE`). Since AWS IoT Commands deliver parameters string-typed end-to-end (the same constraint `energy_reset`'s `channels` already works around), `all` accepts JSON `true` or the string `"true"`, and `channel` accepts a JSON integer or a digit string (parsed via the existing `ShadowLogic::parseChannelIndex`) - not just the REST endpoint's native-JSON-only forms.
- The command calls `IssueRegistry::ack`/`IssueRegistry::ackAll` directly - the same functions the REST handler calls. No new device-side ack logic.
- Status/reason codes follow the existing command conventions: missing both `code` and `all` -> `REJECTED`/`MISSING_CODE`; unknown code string or no matching instance -> `REJECTED`/`NO_SUCH_ISSUE`; success publishes plain `SUCCEEDED` (matching every other command) with the acked count logged on-device, not carried in the command status - the `issues` shadow's `active_count` is the cloud-visible source of truth.
- No shadow change. `issues` stays reported-only per the current spec. `IssueRegistry::ack`/`ackAll` already invoke the registry's change-callback on a successful ack, which already flags the `issues` shadow for a fresh reported publish - so the cloud sees the ack reflected with zero additional wiring.

## Capabilities

### Modified Capabilities
- `iot-commands`: add `issue_ack` as a 4th transient command, documented alongside `restart`/`factory_reset`/`energy_reset`.

## Impact

- `source/src/mqtt.cpp`: new `else if (strcmp(operation, "issue_ack") == 0)` branch in `_handleCommandExecution`, parsing `all`/`code`/`channel` per the string-typed-aware rules above (superset of `customserver.cpp`'s `ackIssueHandler` parsing). New `#include "issueregistry.h"`.
- No new files, no shadow change, no `IssueRegistry`/`issue_logic`/`_publishCommandStatus` change (existing `ack`/`ackAll` are reused as-is; command status stays plain `SUCCEEDED`/`REJECTED` with no new fields).
- `openspec/specs/iot-commands/spec.md`'s `## Purpose` line (lists `restart, factory_reset, energy_reset`) needs `issue_ack` added when this change is archived/merged - spec deltas don't carry a `## Purpose` section, so this is a manual edit at merge time, not part of this change's delta file.
- Cloud side (`energyme-infra`, out of this repo's scope): the command dispatcher needs an `issue_ack` command definition (params: `all`, `code`, `channel`) before this is reachable from the cloud UI - tracked as a follow-up in that repo, not part of this change.
- No version bump as part of this change (release-step policy: version bumps happen separately on `development`).
