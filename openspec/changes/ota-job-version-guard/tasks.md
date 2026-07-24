## 1. Shared version comparison utility

- [x] 1.1 Add `compareVersions(const char* current, const char* available)` to `source/include/utils.h` and `source/src/utils.cpp`, moving the implementation from `customserver.cpp` (same parsing/return semantics: strip leading `v`, `sscanf("%d.%d.%d", ...)`, compare major/minor/patch in order).
- [x] 1.2 Remove the private static `_compareVersions` from `customserver.cpp`; update its one call site (`_fetchGitHubReleaseInfo`'s `isLatest` check) to call the shared `compareVersions`.
- [ ] 1.3 Verify `pio check -e esp32s3-dev` is clean for both changed files (no unused-static warnings, no signature mismatches).

## 2. OTA job version guard

- [x] 2.1 In `_handleSingleJobExecution` (`mqtt.cpp`), read `firmware.version` (`const char*`) and `force` (`doc["execution"]["jobDocument"]["force"] | false`) right after the existing `operation` check.
- [x] 2.2 Add the version-guard branch: if `!force`, call `compareVersions(FIRMWARE_BUILD_VERSION, targetVersion)`; on `> 0` publish `REJECTED` / `downgrade_not_allowed` and return; on `== 0` publish `REJECTED` / `already_up_to_date` and return; on `< 0` (or `force == true`) fall through to the existing flow unchanged.
- [x] 2.3 Confirm the guard runs before the `_otaValidationTaskHandle` / `_otaTaskHandle` / `_otaRebootPending` in-progress guards and before `_publishOtaStatus(jobId, "IN_PROGRESS", "downloading")`, so a rejected job never transitions through `IN_PROGRESS`.

## 3. Verification

- [x] 3.1 Native/manual check of `compareVersions` against the scenarios in `specs/ota-job-version-guard/spec.md` (current>available, equal, current<available, `v`-prefixed and non-prefixed strings) - full native suite (212 tests) run for regression, plus a scratch host build of the isolated `compareVersions` logic against all six spec scenarios (all pass). `utils.cpp` itself isn't host-compilable (pulls Arduino/ESP-IDF headers), so it's outside the `lib/`-based native harness.
- [x] 3.2 On-device test on the dev bench unit (192.168.2.174, thing `907069875394`): real AWS IoT Job (`ota_release.py --env dev --version 2.0.1`) targeting version older than the running 2.1.0 build. Confirmed `REJECTED`/`downgrade_not_allowed` on both the device log and the AWS-side job execution status; no OTA task started.
- [x] 3.3 On-device test: real AWS IoT Job targeting `2.1.0` (== running version). Confirmed `REJECTED`/`already_up_to_date` on both device log and AWS job execution status.
- [x] 3.4 On-device test: real AWS IoT Job targeting a version newer than running (synthetic `9.9.9` tag, since no real released version exceeds current `2.1.0` without a disallowed version bump). Confirmed normal flow unaffected: `IN_PROGRESS` -> download -> reboot -> validation -> `SUCCEEDED`, device left running the guard-equipped build.
- [x] 3.5 On-device test: real AWS IoT Job targeting `2.0.1` with `force: true`. Confirmed the guard was bypassed - real download/flash/validate completed, device ran 2.0.1 (MD5 matched the S3 object exactly) until restored in 3.4.

## 4. Commit

- [ ] 4.1 Commit the `compareVersions` move/rename as its own commit (refactor, no behavior change).
- [ ] 4.2 Commit the OTA job version guard + `force` override as a separate commit, referencing `Closes #208`.
