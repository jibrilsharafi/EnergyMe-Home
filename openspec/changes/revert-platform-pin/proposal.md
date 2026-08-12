# Proposal: revert-platform-pin

## Why

Release 2.3.0 bumped the pioarduino platform from 55.03.32 (Arduino 3.3.2 / IDF 5.5.1) to 55.03.311 (Arduino 3.3.11 / IDF 5.5.5) in `a6a266f`. The 3.3.11 core dropped `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` from its baked-in sdkconfig, moving WiFi/LWIP buffers from PSRAM to internal RAM. Measured on hardware (bench v5 device, same 2.3.0 code): internal heap low-water mark fell from a 40.9 KB floor to ~1 KB with transient dips on every MQTT TLS publish. In the fleet this exhausted internal heap during OTA HTTPS downloads (`esp-aes: Failed to allocate memory`), failing 3 of 6 beta devices on job `energyme-home-prod-ota-beta-2-3-1-20260812-120007` (2026-08-12) and stranding them on 2.3.0. Every future OTA is at risk until the fleet is off the 3.3.11 core.

## What Changes

- Revert `platform =` in `source/platformio.ini` to 55.03.32 and restore the "do not bump" warning block, rewritten with the actual root cause (`SPIRAM_TRY_ALLOCATE_WIFI_LWIP` dropped in >= 3.3.8-era cores; the earlier cache-size story is fixed upstream but irrelevant).
- Bump firmware version to 2.3.2 in `source/include/constants.h`.
- Add `source/utils/diff_platform_sdkconfig.py` (uv-run): downloads the `framework-arduinoespressif32-libs` package pinned by two pioarduino platform versions and prints the esp32s3 sdkconfig diff, with a filtered view of memory-relevant flags (MBEDTLS/LWIP/WIFI/SPIRAM/HEAP/CACHE/BUF).
- Add GitHub Action `platform-guard.yml`: on PRs touching `source/platformio.ini`, fails if the `platform =` line changed, printing the sdkconfig diff; passes only when the PR body contains an explicit acknowledgement marker (`[platform-bump-ack]`).

## Capabilities

### New Capabilities
- `platform-bump-guard`: repository guard that surfaces the baked-in sdkconfig delta between the current and proposed pioarduino platform versions and blocks unacknowledged platform bumps in CI.

### Modified Capabilities

(none - the pin revert restores previously specified runtime behavior; no device-facing requirement changes)

## Impact

- `source/platformio.ini` (platform pin + warning comment)
- `source/include/constants.h` (version 2.3.2)
- `source/utils/diff_platform_sdkconfig.py` (new)
- `.github/workflows/platform-guard.yml` (new)
- Fleet: 2.3.2 is the release that takes devices off the 3.3.11 core; recovery of the 3 stranded 2.3.0 devices is an operational task in energyme-infra (restart + OTA job with retry config), not part of this change.
- Related but out of scope: OTA download hardening (#191), firmware rollback command (#237).
