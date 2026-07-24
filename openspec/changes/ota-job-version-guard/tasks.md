## 1. Shared version comparison utility

- [x] 1.1 Add `compareVersions(const char* current, const char* available)` to `source/include/utils.h` and `source/src/utils.cpp`, moving the implementation from `customserver.cpp` (same parsing/return semantics: strip leading `v`, `sscanf("%d.%d.%d", ...)`, compare major/minor/patch in order).
- [x] 1.2 Remove the private static `_compareVersions` from `customserver.cpp`; update its one call site (`_fetchGitHubReleaseInfo`'s `isLatest` check) to call the shared `compareVersions`.
- [x] 1.3 `pio check -e esp32s3-dev` (whole-tree cppcheck/clangtidy) did not finish in reasonable time - it reprocesses the full ESP-IDF macro set per file and only got partway through the source tree after ~15 minutes with no findings in the files it reached. Not pursued further given `pio run -e esp32s3-dev` compiled cleanly and all behavior was verified on real hardware (see 3.1-3.5).

## 2. OTA job version guard

- [x] 2.1 In `_handleSingleJobExecution` (`mqtt.cpp`), read `firmware.version` (`const char*`) and `force` (`doc["execution"]["jobDocument"]["force"] | false`) right after the existing `operation` check.
- [x] 2.2 Add the version-guard branch: if `!force`, call `compareVersions(FIRMWARE_BUILD_VERSION, targetVersion)`; on `> 0` publish `REJECTED` / `downgrade_not_allowed` and return; on `== 0` publish `REJECTED` / `already_up_to_date` and return; on `< 0` (or `force == true`) fall through to the existing flow unchanged.
- [x] 2.3 Confirm the guard runs before the `_otaValidationTaskHandle` / `_otaTaskHandle` / `_otaRebootPending` in-progress guards and before `_publishOtaStatus(jobId, "IN_PROGRESS", "downloading")`, so a rejected job never transitions through `IN_PROGRESS`.

## 3. Verification

- [x] 3.1 Native unit tests for `compareVersions`: the algorithm was extracted to `lib/version_compare/` (host-compilable, no Arduino deps) so it can be exercised by the real `pio test -e native` harness instead of only manually. `test/test_version_compare/` covers equal/greater/less by each component, multi-digit correctness (`10.0.0` > `9.99.99`), `v`-prefix in every combination, leading zeros, trailing suffixes (`-rc1`, `(dev)`), partial version strings, malformed/empty/whitespace-only input (degrades to `0.0.0`, never crashes), and null `current`/`available`/both (must not crash) - 27 cases, all passing alongside the full 239-case native suite (212 pre-existing + 27 new).
- [x] 3.2 On-device test on the dev bench unit (192.168.2.174, thing `907069875394`): real AWS IoT Job (`ota_release.py --env dev --version 2.0.1`) targeting version older than the running 2.1.0 build. Confirmed `REJECTED`/`downgrade_not_allowed` on both the device log and the AWS-side job execution status; no OTA task started.
- [x] 3.3 On-device test: real AWS IoT Job targeting `2.1.0` (== running version). Confirmed `REJECTED`/`already_up_to_date` on both device log and AWS job execution status.
- [x] 3.4 On-device test: real AWS IoT Job targeting a version newer than running (synthetic `9.9.9` tag, since no real released version exceeds current `2.1.0` without a disallowed version bump). Confirmed normal flow unaffected: `IN_PROGRESS` -> download -> reboot -> validation -> `SUCCEEDED`, device left running the guard-equipped build.
- [x] 3.5 On-device test: real AWS IoT Job targeting `2.0.1` with `force: true`. Confirmed the guard was bypassed - real download/flash/validate completed, device ran 2.0.1 (MD5 matched the S3 object exactly) until restored in 3.4.

## 4. Commit

- [x] 4.1 Commit the `compareVersions` move/rename as its own commit (refactor, no behavior change).
- [x] 4.2 Commit the OTA job version guard + `force` override as a separate commit, referencing `Closes #208`.

## 5. Robustness follow-up (raised in review: "is this thoroughly tested / can it fail on any string?")

- [x] 5.1 Add a null-check inside the comparator itself (previously relied on both call sites - `mqtt.cpp`'s `targetVersion` guard, `customserver.cpp`'s `tagName` guard - remembering to check first; now defended in the shared function too).
- [x] 5.2 Extract the parse/compare algorithm to `lib/version_compare/` and write a real Unity suite (`test/test_version_compare/`), replacing the throwaway scratch script from the original verification pass with committed, repeatable coverage.
- [x] 5.3 Re-run the full native suite (239 cases) and rebuild `esp32s3-dev` to confirm the delegation refactor is behavior-preserving.

## 6. Second hardening pass (raised in review again: "try all combinations, rock solid, unit + real e2e")

- [x] 6.1 Fix `sscanf("%d", ...)` undefined behavior on out-of-range components (e.g. `"99999999999.0.0"`) and subtraction overflow in the old `currentMajor - availableMajor` compare - replaced with `strtol` (clamped `[0, INT_MAX]`) and a three-way compare.
- [x] 6.2 Fix capital `V` prefix not being stripped (`"V2.2.0"` parsed as `0.0.0`, misread as a downgrade).
- [x] 6.3 Extend `test_version_compare` from 27 to 39 cases: capital-V prefix, negative numbers, integer overflow/INT_MAX boundary, extra trailing components, leading whitespace, space-before-dot, dots-only, `+`-prefix, non-numeric middle component, full semver suffix (`-alpha+build.5`), JSON-looking garbage, very long garbage string, invalid UTF-8 bytes.
- [x] 6.4 Fix `mqtt.cpp` bypass: missing/non-string `firmware.version` (`targetVersion == nullptr`) previously skipped the guard entirely and let the job proceed unchecked; now rejects (`REJECTED`/`invalid_version`) unless `force` is set.
- [x] 6.5 Fix `mqtt.cpp` bypass: `force` read via `doc[...]["force"] | false` treated any non-null JSON value (including the string `"false"`) as `true`, per ArduinoJson 7.4.2's documented `as<bool>()` behavior - verified against the official docs (Context7), not assumed. Now reads `is<bool>() ? as<bool>() : false`.
- [ ] 6.6 Rebuild `esp32s3-dev`, re-run native suite, re-flash bench device 192.168.2.174 via local `ota_updater.py`, and confirm the running `sketch_md5` matches the new binary before any AWS job is created.
- [ ] 6.7 Real AWS IoT Job e2e, dev environment, unique job ID per case, UDP log listener running throughout:
  - [ ] downgrade (older version, no force) -> `REJECTED`/`downgrade_not_allowed`
  - [ ] equal version (no force) -> `REJECTED`/`already_up_to_date`
  - [ ] newer version (synthetic tag, no force) -> proceeds, `SUCCEEDED`
  - [ ] older version + `force: true` -> guard bypassed, proceeds, `SUCCEEDED`
  - [ ] `firmware.version` field absent entirely, no force -> `REJECTED`/`invalid_version` (previously bypassed - this is the fix from 6.4)
  - [ ] `firmware.version` sent as a JSON number instead of a string, no force -> `REJECTED`/`invalid_version`
  - [ ] `force` sent as the JSON string `"false"` targeting an older version -> `REJECTED`/`downgrade_not_allowed` (previously bypassed - this is the fix from 6.5)
