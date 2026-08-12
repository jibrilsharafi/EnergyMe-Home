# Tasks: revert-platform-pin

## 1. Platform revert

- [x] 1.1 Create branch `fix/revert-platform-pin` off `development`
- [x] 1.2 Revert `platform =` to 55.03.32 in `source/platformio.ini` and restore the warning block, rewritten with the verified root cause (`SPIRAM_TRY_ALLOCATE_WIFI_LWIP` dropped, WiFi/LWIP buffers moved to internal RAM, 2026-08-12 fleet OTA incident) and a pointer to the diff script (commit 1)
- [x] 1.3 Bump firmware version to 2.3.2 in `source/include/constants.h` (commit 2)
- [x] 1.4 Build `esp32s3-prod` and `esp32s3-dev-v5`; confirm PLATFORM line reports 55.3.32 in both

## 2. Platform-bump guard script

- [x] 2.1 Add `source/utils/diff_platform_sdkconfig.py`: resolve two platform versions to their `framework-arduinoespressif32-libs` URLs via each release zip's `platform.json`, download, extract `esp32s3/sdkconfig`, print filtered (MBEDTLS|LWIP|WIFI|SPIRAM|HEAP|CACHE|BUF|MEM) then full diff; sentinel check on `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` exits non-zero unless `--no-fail-on-sentinel` (commit 3)
- [x] 2.2 Run the script for 55.03.32 vs 55.03.311 and verify it flags the sentinel and exits non-zero (verified: streaming mode produces the 350+521-line diff, flags the sentinel, exits 2; --detect verified on unchanged/pin-change/env-override cases; retry-with-backoff absorbs GitHub edge resets)

## 3. CI enforcement

- [x] 3.1 Add `.github/workflows/platform-guard.yml`: trigger on PRs touching `source/platformio.ini`; detect `platform =` line change vs base; if changed, run the diff script (old vs new) and fail unless PR body contains `[platform-bump-ack]`; no platform download when the line is unchanged (commit 4)

## 4. Verification and PR

- [x] 4.1 Flash the 2.3.2 `esp32s3-dev-v5` build to the bench device and verify heap profile (free >= 60 K, minFree floor >= 35 K over a soak with publishes) and core 3.3.2 in `/api/v1/system/info` (delivered via the cloud OTA path during 5.1; end state: free 71.5 K, minFree 49.8 K, core 3.3.2)
- [x] 4.2 Push branch, open PR to `development` referencing #191/#237 context (no Closes - neither is closed by this PR)
- [x] 4.3 Run code-review agent(s) on the branch diff and triage findings (7 confirmed findings fixed: guard bypass via env override/decoy, shell injection, pipefail, edited trigger, full-line marker, script member/timeout/manifest)
- [x] 4.4 Run simplification agent and apply safe cleanups (4 agents: detection moved into script --detect mode, tarball streaming, print dedup, narrative dedup)
- [ ] 4.5 Merge to `development` after review findings are resolved

## 5. Operational validation (dev, outside the repo change)

- [x] 5.1 Reflash bench device to stock 2.3.0 (3.3.11 core) and validate the brute-force recovery approach: remote restart, then OTA to 2.3.2 shortly after boot; record attempts needed (reproduced the fleet failure on demand: steady-state job FAILED download_failed; restart + fresh-boot job also FAILED; success on attempt 4 of repeated jobs - recovery works but MUST be automated via job retry config, one restart alone is not enough)
- [x] 5.2 Leave the bench device on 2.3.2 (3.3.2 core) at the end
