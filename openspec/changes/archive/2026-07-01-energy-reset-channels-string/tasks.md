## 1. Firmware

- [x] 1.1 Add `ShadowLogic::parseChannelList(spec, channelCount, validIndices, maxOut, &invalidTokenSeen)` to `lib/shadow_logic` (`.h`/`.cpp`): read-only scan of a comma-separated index string (no copy/mutation of `spec`), trimming leading/trailing spaces and skipping empty tokens, reusing `parseChannelIndex` per token
- [x] 1.2 In `_handleCommandExecution`'s `energy_reset` branch (`source/src/mqtt.cpp:~1019-1035`), add a comma-separated-string path: when `channels` is a string and not `"all"`, call `parseChannelList` and `Ade7953::resetChannelEnergyValues` each returned index; log `WARNING` if `invalidTokenSeen`; if zero valid indices are returned, reject with `BAD_CHANNELS` instead of reporting success
- [x] 1.3 Leave the existing `JsonArrayConst` branch unchanged (on-device inject test harness)

## 2. Spec

- [x] 2.1 Sync the `iot-commands` delta spec into `openspec/specs/iot-commands/spec.md` (via `/opsx:sync` or archive)

## 3. Tests

- [x] 3.1 Native unit tests (`pio test -e native`, from WSL) for `ShadowLogic::parseChannelList`: `"all"` handled by the unchanged literal branch, single index (`"5"`), multi-index (`"0,2,5"`), list with one out-of-range index (others still returned, `invalidTokenSeen` set), list with one non-numeric token (others still returned, `invalidTokenSeen` set), leading/trailing-space + trailing-comma tolerance (`"0, 2, 5"`, `"5,"`), empty string and all-invalid string both yield zero valid indices (mqtt.cpp rejects these with `BAD_CHANNELS`), `maxOut` cap, array-branch regression unchanged - all 174 native tests pass
- [x] 3.2 Hardware E2E on dev device (.174): via `/api/v1/shadow/inject-command` with `channels` as a JSON string (not the array inject-harness shape) - confirmed via UDP debug log: `"0,2,5"` and `"3"` -> SUCCEEDED; `"0,x,5"` -> WARNING + still SUCCEEDED (best-effort); `"x,y"` and `""` -> REJECTED/BAD_CHANNELS (zero-valid-channels guard); `[1,4]` (array) -> SUCCEEDED (regression-free). Additionally validated the real cloud path with `energyme-infra/.../dispatch_command.py` (AWS IoT `StartCommandExecution` via `iot-jobs-data`, `admin-dev` account 071378139398, real `.../request/json` broker round trip, not the local inject bypass): `"0,2,5"` -> SUCCEEDED, `"0,x,5"` -> WARNING + SUCCEEDED, `"x,y"` -> REJECTED/BAD_CHANNELS with the exact reasonDescription - device-side UDP log matched the cloud-reported status in all three

## 4. Verification

- [x] 4.1 Confirm `dispatch_command.py` / the cloud command schema already sends `channels` as this string format - no `energyme-infra` change needed (confirmed directly by Jibril, cloud side is source of truth)
