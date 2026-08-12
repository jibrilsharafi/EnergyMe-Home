# platform-bump-guard

## Purpose

Guards the pinned pioarduino platform version by surfacing the baked-in sdkconfig differences between two platform versions and blocking unacknowledged platform bumps in CI, so silent core-level memory regressions cannot ship again.

## ADDED Requirements

### Requirement: sdkconfig diff between two platform versions
The repository SHALL provide a script that, given two pioarduino platform versions (or their release URLs), resolves the `framework-arduinoespressif32-libs` package each one pins, downloads both, and prints the line-level diff of their `esp32s3/sdkconfig`.

#### Scenario: Full diff produced
- **WHEN** the script is run with versions 55.03.32 and 55.03.311
- **THEN** it prints every sdkconfig line present in only one version, labeled with the version it belongs to

#### Scenario: Memory-relevant flags highlighted
- **WHEN** the diff contains lines matching memory-relevant flag patterns (MBEDTLS, LWIP, WIFI, SPIRAM, HEAP, CACHE, BUF, MEM)
- **THEN** the script prints those lines in a separate, clearly labeled section ahead of the full diff

#### Scenario: Regression sentinel flagged
- **WHEN** `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` is present in one version and absent in the other
- **THEN** the script prints an explicit warning naming the flag and exits with a non-zero status unless warnings are suppressed via a flag

#### Scenario: Download failure
- **WHEN** a platform version cannot be resolved or its libs package cannot be downloaded
- **THEN** the script exits non-zero with an error naming the version that failed

### Requirement: CI blocks unacknowledged platform bumps
CI SHALL fail any pull request that changes the `platform =` line in `source/platformio.ini` unless the pull request body contains the acknowledgement marker `[platform-bump-ack]`.

#### Scenario: Unacknowledged bump rejected
- **WHEN** a PR modifies the `platform =` line and its body does not contain `[platform-bump-ack]`
- **THEN** the platform-guard workflow fails and its log contains the sdkconfig diff between the old and new platform versions

#### Scenario: Acknowledged bump passes with visible diff
- **WHEN** a PR modifies the `platform =` line and its body contains `[platform-bump-ack]`
- **THEN** the workflow succeeds and its log still contains the sdkconfig diff for reviewer inspection

#### Scenario: Unrelated platformio.ini edits untouched
- **WHEN** a PR modifies `source/platformio.ini` without changing the `platform =` line
- **THEN** the workflow succeeds without downloading any platform package
