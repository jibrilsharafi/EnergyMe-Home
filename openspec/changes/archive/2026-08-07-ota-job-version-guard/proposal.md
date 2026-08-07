## Why

The AWS IoT Jobs OTA path flashes whatever `firmware.version`/`url` a job document carries with no firmware-side check against the currently running version. The cloud repo (`energyme-infra`) is switching automated channel jobs (`beta`/`stable`) from `SNAPSHOT` to `CONTINUOUS` targeting (`energyme-infra`#231), so a stale/orphaned job (e.g. a failed cancel-on-supersede) now stays open indefinitely and could keep re-applying an old version to devices with no firmware-side backstop. This is ADR-003 Decision 1 in `energyme-home-ota`, previously "Accepted, deferred," now needed on the firmware side.

## What Changes

- Add a version comparison in `_handleSingleJobExecution` (`mqtt.cpp`): compare the job document's `firmware.version` against `FIRMWARE_BUILD_VERSION` using `_compareVersions`.
  - `target < current` -> reject with `REJECTED` / `downgrade_not_allowed`
  - `target == current` -> reject with `REJECTED` / `already_up_to_date`
  - `target > current` -> proceed as today
- Add a `force` override: an explicit `"force": true` key in the job document (sibling of `operation`/`firmware`, absent/default = `false`) skips the version comparison entirely and proceeds unconditionally. No cloud-side enforcement needed in firmware; the CLI is the only allowed setter of `force: true` (operational discipline in the cloud repo, not firmware).
- Move `_compareVersions` from `customserver.cpp` (private/static) to `utils.cpp`/`utils.h` as a shared, non-static function alongside `isBackupVersionCompatible`, so `customserver.cpp` (update-check endpoint), `utils.cpp` (backup-compatibility check), and `mqtt.cpp` (this new guard) all share one implementation.

## Capabilities

### New Capabilities
- `ota-job-version-guard`: Firmware-side version guard on AWS IoT Jobs OTA execution - rejects downgrade/no-op job targets unless explicitly forced, using a shared version-comparison utility.

### Modified Capabilities
(none - no existing spec covers OTA job execution today)

## Impact

- `source/src/mqtt.cpp`: `_handleSingleJobExecution` gains the version check and `force` read; job document parsing reads `firmware.version` and optional `force`.
- `source/src/customserver.cpp`: removes local static `_compareVersions`; update-check endpoint (`/api/v1/firmware/update-info`) calls the shared utility instead.
- `source/src/utils.cpp` / `source/include/utils.h`: add shared `_compareVersions` (or public equivalent) next to `isBackupVersionCompatible`.
- No wire/API contract changes on the cloud side - the job document already carries `firmware.version`; `force` is an additive optional field the cloud repo will start sending via `ota_release.py --force`.
- No breaking changes to existing OTA jobs that omit `force` (defaults to `false`, current reject/accept behavior only changes for downgrade/same-version targets, which previously were blindly accepted).
