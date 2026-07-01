## Why

The `energy_reset` IoT Command's `channels` param currently accepts only the literal string `"all"` or a JSON array of channel indices. AWS IoT Commands parameters are string-typed end-to-end, so the cloud (`dispatch_command.py`) can only ever deliver a string - it can never actually send a JSON array. That means today, cloud-issued selective resets (e.g. "reset channel 5 only") are rejected with `BAD_CHANNELS`; only the full-reset path (`"all"`) works from the cloud. The JSON-array path is only reachable from the on-device inject test harness.

## What Changes

- `_handleCommandExecution`'s `energy_reset` branch (`source/src/mqtt.cpp:1019-1035`) gains a comma-separated string form for `channels`: `"all"` (unchanged), a single index (`"5"`), or a list (`"0,2,5"`).
- Per-token parsing is best-effort, matching the existing array branch: an invalid/out-of-range channel index logs `WARNING` and continues; only an oversized input string (won't fit the bounded parse buffer) is rejected up front with `BAD_CHANNELS`, before any reset has happened.
- The existing `JsonArrayConst` branch is unchanged, preserving the on-device inject test harness.
- `openspec/specs/iot-commands/spec.md` updated to document the channel selector formats.

## Capabilities

### Modified Capabilities
- `iot-commands`: `energy_reset`'s channel selector now also accepts a comma-separated string spec (in addition to `"all"` and the array form), since that is the only form AWS IoT Commands can actually deliver.

## Impact

- Firmware only: `source/src/mqtt.cpp`. No CDK/infra change - the cloud command schema already declares `channels` as a string and `dispatch_command.py` already sends `"0,2,5"`-style values; the device has just been silently unable to parse anything but `"all"`.
- No version bump as part of this change (release-step policy: version bumps happen separately on `development`, never inside a feature branch).
