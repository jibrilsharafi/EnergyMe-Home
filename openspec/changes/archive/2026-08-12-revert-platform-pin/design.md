# Design: revert-platform-pin

## Context

See proposal.md - Why. Root cause fully verified on hardware 2026-08-12: A/B on the bench v5 device (same 2.3.0 code) shows core 3.3.2 holds a 40.9 KB internal-heap floor and constant 31.7 KB maxAlloc over a 10-minute soak, while core 3.3.11 dips to ~1 KB on every MQTT TLS publish. The sdkconfig is baked into the prebuilt `framework-arduinoespressif32-libs` package; application code cannot restore `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`.

## Goals / Non-Goals

Goals:
- Ship 2.3.2 as the smallest possible change that takes the fleet off the 3.3.11 core.
- Make any future platform bump loudly display its baked-in sdkconfig delta before it can merge.

Non-Goals:
- OTA download hardening (retry/quiesce/heap gate) - tracked in #191, lands separately so the fleet-rescue release stays minimal.
- Firmware rollback command - tracked in #237.
- Keeping the 3.3.11 core via pioarduino `custom_sdkconfig` lib rebuild - rejected below.
- Prod recovery operations (energyme-infra job tooling).

## Decisions

1. **Revert to 55.03.32 rather than rebuild 3.3.11 libs with `custom_sdkconfig`.**
   The revert is one known-good line: 55.03.32 ran the entire 2.2.1 fleet without incident and is A/B-verified today against this exact failure. `custom_sdkconfig` rebuilds the core in CI and on every contributor machine, adds minutes to every build, and produces a core no other user runs - a poor trade for a fleet-rescue release. The pool-size fix that motivated the 3.3.11 bump is moot: 3.3.2 predates the cache regression entirely.

2. **Guard = script + thin workflow, script is the source of truth.**
   `source/utils/diff_platform_sdkconfig.py` does resolution, download, and diff; the workflow only detects the `platform =` line change, runs the script, and checks the PR body for `[platform-bump-ack]`. Rationale: the script must be runnable locally (uv run, no CI dependency) because the whole point is checking BEFORE committing to a bump; the workflow is enforcement only. Alternative rejected: logic embedded in the workflow YAML - untestable locally, invisible to `pio`-only users.

3. **Resolution via pioarduino `platform.json` inside the platform release zip.**
   Each pioarduino release zip contains `platform.json` whose `packages['framework-arduinoespressif32-libs'].version` is the direct URL of the libs tarball. The script downloads the zip (small), reads that URL, then downloads and extracts only `esp32s3/sdkconfig` from the tarball. Alternative rejected: `pio pkg install` into the global package dir - mutates shared state, needs PlatformIO installed, and the two installs race each other (observed during the investigation).

4. **Acknowledgement marker in the PR body, not a label.**
   `[platform-bump-ack]` in the body is diffable, survives squash-merge into the commit message, and requires no repo label management. The workflow still prints the full diff on acknowledged bumps so reviewers see what they are accepting.

5. **Sentinel check for `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`.**
   The script exits non-zero when the flag flips between versions (overridable with `--no-fail-on-sentinel`). This one flag caused a fleet incident; it earns a hardcoded tripwire beyond the generic filtered diff.

6. **Version bump to 2.3.2 in this change.**
   2.3.1 is already tagged and released; the revert must ride a new version for OTA jobs to target.

## Risks / Trade-offs

- [3.3.2 stays pinned indefinitely and upstream fixes pass us by] → The guard makes future bumps cheap to evaluate: run the script, read the filtered diff, soak on the bench. Bumping stops being forbidden and becomes a checklist.
- [Espressif changes the libs packaging layout, breaking the script] → Script fails loudly (non-zero, named version); the workflow failure then prompts a script fix rather than silently passing.
- [GitHub API/asset download flakiness in CI] → The workflow only runs when the platform line changes (rare); a re-run suffices.
- [2.3.2 devices still on 3.3.11-era heap during the 2.3.2 download itself] → Unavoidable: the download runs on old code. Mitigated operationally (restart-then-update, job retry config) and structurally by #191 for future releases.

## Migration Plan

1. Merge to `development`, tag/release 2.3.2 via the existing release workflow (release itself is Jibril's call on `main`).
2. Dev validation: bench v5 device already runs the equivalent build (2.3.0 code + 55.03.32) with verified heap profile; 2.3.2 dev build re-verified on the bench before the PR merges.
3. Fleet rollout and recovery of the 3 stranded devices: energyme-infra operational runbook (restart command, then OTA job with `jobExecutionsRetryConfig`).
4. Rollback strategy: the previous release (2.3.1) remains available; devices that take 2.3.2 revert to the known-good core, so a rollback would only reintroduce the regression - not expected.

## Open Questions

(none)
