# Feature: AWS IoT Device Shadow config (v2.1.0)

**Status:** planning complete, validated with cloud side - ready to implement
**Issue:** #159 | **Cloud repo:** `energyme-infra` | **Started:** 2026-06-16

This folder is the working workspace for the shadow feature. It is meant to be
driven incrementally with AI: read the contract, pick a phase, implement with
tests, update progress. Everything an implementer (human or AI) needs is here.

## Why we are doing this

Today the device's **configurable state** (channel setup, system toggles, meter
calibration) only flows device -> cloud as telemetry, and there is no way for the
cloud to *change* it. There is also no cloud visibility of the on-device issue
registry. We want **cloud-side configuration parity with the local web UI/REST
API**, mediated by the EnergyMe Intelligence backend, using the right AWS
primitives for each job:

- **Named Device Shadows** for configurable state (desired/reported sync).
- **IoT Commands** for transient actions (restart, factory_reset, energy_reset).
- **IoT Jobs** stay OTA-only (unchanged).

Guiding principle established for this work: **telemetry topics carry only
measurements / statistics / dynamic runtime; all configurable state lives in
shadows.** Secrets (WiFi creds, etc.) stay local-only - never in a shadow.

## Expected output ("done" looks like)

- Firmware `Shadow` module + 5 named shadows (`info`, `issues`, `system`,
  `meter`, `channels`) and 3 Commands. Topic namespace stays `v1` (no bump).
- Reported-only shadows (`info`, `issues`) provable on hardware with no cloud
  backend; writable shadows + Commands provable end-to-end on dev AWS.
- `command` topic, `system/static` and `channel` publishes removed (config now
  shadow-only); firmware at 2.1.0; **topic version stays `v1`**.
- No regression in the local REST API; telemetry payload contents unchanged.
- Cloud side (tracked in `energyme-infra`): `$aws/commands/*` policy, shadow
  ingestion Lambda into the existing `device-ops` table, desired-state writer,
  3 command templates + dispatcher. No v2 rules, no policy-ARN migration.

## How to work in this folder

1. Read **`00-overview-and-contract.md`** first - it is the device<->cloud
   contract (topics, payloads, the shadow apply logic) both repos agreed on.
2. Pick the lowest open phase in **`PROGRESS.md`**. Build order is 01 -> 08;
   `02`/`03` (reported-only) are the first hardware-provable steps and need no
   cloud backend.
3. Each `0N-*.md` is a self-contained step: goal, files, schema, exact
   functions/NVS keys, test plan, acceptance checklist.
4. Use TDD where logic is pure (native unit tests in `lib/`, run from WSL).
5. Update `PROGRESS.md` as each phase lands (status, PR, notes, decisions).

## Documents

| Doc | Phase |
|-----|-------|
| `00-overview-and-contract.md` | contract + apply logic + cloud checklist + decisions |
| `01-scaffold-shadow-module.md` | `shadow.cpp/.h` core |
| `02-info-shadow.md` | reported-only identity (first provable) |
| `03-issues-shadow.md` | reported-only issue registry |
| `04-system-shadow.md` | writable behavioural config |
| `05-meter-shadow.md` | writable ADE7953 calibration |
| `06-channels-shadow.md` | writable per-channel config |
| `07-commands.md` | IoT Commands (restart/factory_reset/energy_reset) |
| `08-v2-cutover.md` | retire config topics, buffer bump, fw 2.1.0 (topic stays v1) |

`PROGRESS.md` - live status and decision log.
