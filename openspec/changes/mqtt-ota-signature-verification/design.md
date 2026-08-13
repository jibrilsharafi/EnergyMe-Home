## Context

See proposal.md - Why, for motivation. Current state:

- `_performOtaUpdate` (`src/mqtt.cpp`) calls the one-shot `esp_https_ota()`, whose internal `esp_https_ota_finish()` already sets the boot partition on success — there is no seam in that call to interject a check before activation.
- The only existing post-download integrity signal is self-referential: `app_elf_sha256` is read from the just-downloaded image itself, stored in Preferences, and compared against the running partition's own `app_elf_sha256` after reboot. It detects flash/write corruption, not a malicious-but-well-formed image.
- The AWS IoT job document already carries `operation`, `firmware.url`, `firmware.version`, and an optional `force` (parsed at `mqtt.cpp:1619-1626`, consumed by the existing downgrade guard — see `ota-job-version-guard`, unaffected by this change).
- This project's toolchain (pioarduino `55.03.311` / arduino-esp32 core `3.3.11`) ships `libraries/Update/src/Updater_Signing.{h,cpp}`, confirming `mbedtls_pk_verify` + DER-encoded ECDSA signatures are the pattern already used elsewhere in this exact framework for firmware signature checks (via `UpdaterECDSAVerifier`, used by the `Update.h`-based local-upload path — not used here directly, since MQTT OTA uses `esp_https_ota` rather than `Update.h`, but the same mbedtls primitives apply).
- AWS's own FreeRTOS/IoT OTA documentation specifies ECDSA P-256 + SHA-256 as the code-signing scheme for Espressif ESP32 boards, and explicitly supports signing firmware manually (not only via the managed AWS Signer/ACM workflow).

## Goals / Non-Goals

**Goals:**
- Cryptographically verify, before any code from a newly-downloaded image can execute, that the image was produced by holder of EnergyMe's private signing key.
- Make the check unconditional — no field, flag, or configuration path can disable it.
- Reuse the existing job-document delivery channel; no new device-to-cloud round trip.

**Non-Goals:**
- Local web-upload signature verification — deferred; requires a build-time vendor/community split and a physical-presence community-demote mechanism that are materially larger than this change. This change only adds disclosure to the local path (see spec's "Local OTA upload discloses" requirement).
- ESP-IDF Secure Boot V2 / signed bootloader — blocked by the Arduino framework (`CONFIG_SECURE_BOOT` unavailable under pioarduino), out of scope regardless.
- Anti-rollback / downgrade prevention — already implemented and unrelated (`ota-job-version-guard`); this change adds an independent authenticity check earlier in the same pipeline and does not touch it.
- NVS encryption, key rotation tooling, physical-presence demote-to-community — later phases, not required for this change to be safe or complete on its own.

## Decisions

**1. Sign a locally-computed SHA-256 digest via AWS KMS (`MessageType=DIGEST`), not `MessageType=RAW`.**
KMS's `Sign` API caps `RAW` messages at 4096 bytes; `firmware.bin` is far larger. CI computes the digest locally (e.g. `openssl dgst -sha256`) and signs that. Alternative considered: AWS Signer / ACM-imported code-signing certificate (AWS's managed FreeRTOS OTA signing path) — same algorithm choice, but pulls in ACM certificate-chain machinery this project's custom job pipeline (not the FreeRTOS OTA agent) has no other use for. Direct KMS `Sign` is simpler here, and AWS's own docs list manual signing as an accepted alternative to the managed flow.

**2. Deliver the signature inline in the job document (`firmware.signature`, base64 DER), not via a separate `signature_url`.**
Saves a second download; a DER-encoded P-256 signature is small (~70-72 bytes DER `SEQUENCE{r,s}`). This is also exactly the format both AWS KMS returns natively for `ECDSA_SHA_256` and what `mbedtls_pk_verify` (confirmed in this project's own toolchain) expects — zero conversion needed on either end.

**3. Verify between `esp_https_ota_perform` completion and `esp_https_ota_finish`, using the granular API instead of the one-shot call.**
The one-shot `esp_https_ota()` bundles "download" and "activate" into a single call with no hook between them. Splitting into `esp_https_ota_begin` → `esp_https_ota_perform` loop → verify → `esp_https_ota_finish` (success) or `esp_https_ota_abort` (failure) creates that seam. Verification must run from the still-executing, already-trusted firmware, reading the new partition as inert flash contents — not from within the new image after boot, which would let the code being judged decide its own trustworthiness (self-verification is not a security boundary: an attacker crafting a malicious image simply omits or fakes any such internal check).

**4. No field-presence fallback — missing signature is a hard failure, not a skip.**
An MQTT/cloud OTA job document is exactly the artifact a compromised cloud pipeline would forge. If the device treats an absent `firmware.signature` as "no verification needed," the attacker just omits the field, and the feature protects only against attackers who forgot to leave it out. Rejected for the same reason the original issue rejected an NVS-flag bypass for local OTA — a bypass reachable through the channel being defended against is not a security control.

**5. Public key is a compiled-in constant, not NVS-stored.**
MQTT/cloud OTA only runs on devices with factory-provisioned cloud certificates in the first place — a community-mode device (no factory NVS) never reaches this code path at all, since cloud connectivity itself is unavailable. There is therefore no separate "community bypass" surface to design around here, unlike the local-upload path (where a build-time vendor/community split would be required and is deferred — see Non-Goals). Storing the key in NVS instead of compiling it in would just reintroduce a bypass via NVS write access for no benefit.

**6. Leave the existing post-reboot self-referential `app_elf_sha256` check unchanged.**
Different purpose (flash/write corruption after reboot) from this change's authenticity check (forgery before activation). Both remain, independently useful.

**7. Anti-downgrade-replay is NOT attempted in this change - an embedded-version re-check was tried during review and dropped after hardware testing proved it cannot work on this toolchain.**
The idea was: since the job document's `firmware.version` is not part of the signed bytes, a forging attacker could replay an old validly-signed `firmware.bin` under a faked higher version and slip past the pre-download `ota-job-version-guard`. The attempted fix re-read the version embedded in the verified image (`esp_ota_get_partition_description().version`) and compared it against `FIRMWARE_BUILD_VERSION`. Hardware testing (2026-08-13, bench device) showed this is defeated: on this pioarduino/arduino-lib-builder toolchain `esp_app_desc_t.version` is a frozen constant (`"487f743"`), not the semantic version - a fact `lib/rollback_logic/rollback_logic.h` already documents and this attempt overlooked. The comparison therefore always passed and a replayed same-version image installed and booted. Removed entirely: anti-rollback against a forging attacker is a Non-Goal of this change (see Non-Goals), and doing it correctly requires binding the version into the signature (a signed `{version, hash}` manifest), which is a deliberate later change to be coordinated with the KMS/CI signing step. Accidental downgrades remain handled pre-download by the existing `ota-job-version-guard` (its actual purpose). The host unit tests for the `isStrictUpgrade` helper were also removed with it - they passed on clean version strings and gave false confidence precisely because they could not exercise the frozen-field behavior only observable on hardware.

**8. Loops introduced by this change deliberately carry no `MAX_LOOP_ITERATIONS` counter - found during adversarial code review.**
Both the `esp_https_ota_perform` loop and the partition-hashing loop initially had one, which was a regression: `esp_http_client`'s default RX buffer is 512 bytes, so 1000 iterations caps a download at ~500 KB - well under this project's ~4.3 MB OTA partitions, meaning every real OTA would have failed with "loop exceeded max iterations". Both loops are already bounded by a real size variable (downloaded byte count vs. `Content-Length`/`imageLen`, which `esp_https_ota` itself bounds to the partition size) - exactly the case this project's own `MAX_LOOP_ITERATIONS` convention exempts. Removed rather than given a larger magic number, so correctness doesn't depend on picking a constant that happens to be big enough for today's partition table.

**9. The 4 KB SHA-256 chunk buffer is allocated once from PSRAM in `begin()`, not on the OTA task's stack - found during adversarial code review.**
A stack-local buffer of that size would consume roughly a third of `OTA_TASK_STACK_SIZE` (12 KB) on top of `esp_https_ota`'s own TLS/HTTP buffers and the mbedtls contexts in the same call chain. Matches the existing `_otaCurrentUrl` PSRAM-allocation pattern in this same file.

**10. Post-download failures abandon the retry schedule - added when rebasing onto the OTA download-hardening change (#239).**
Development gained a 5-attempt exponential-backoff retry loop around `_performOtaUpdate` while this change was in review. A signature or activation failure after a complete download is deterministic for the same artifact - the bytes were fully received and still rejected - so retrying re-downloads ~4 MB to fail identically, five times over ~29 minutes. `_otaFailureRetryable` breaks the schedule for those failures, the same way #239's own 4xx check does for a refused URL. Download-phase failures (begin/perform/incomplete) stay retryable. The specific `_otaFailureReason` also rides in the FAILED status alongside #239's diagnostics (`withDiagnostics`/attempt count), so a signature failure and a heap-starved download remain distinguishable in the job status.

**11. Scrub the passive partition's image header on any failed download that wrote to it - bypass found by Jibril during final review.**
The `firmware_rollback` command (merged meanwhile via #240) switches the boot partition to the passive slot, gated by a caller-supplied sha256 match plus `esp_ota_set_boot_partition`'s structural `esp_image_verify` - neither involves the signature. An attacker who can forge job documents (exactly this change's threat model) could send an unsigned image (fully downloaded, then rejected - but resident in flash), then issue `firmware_rollback` carrying that image's own sha256, and boot it: a complete bypass. Fixed by erasing the first flash sector (image magic + header) of the passive partition whenever a download that wrote bytes ends in failure - `esp_image_verify` then refuses it on every later activation path (rollback command, crash rollback). Scoped to `bytesWritten > 0`: a failure before any byte arrived (DNS/TLS/4xx) leaves the previous firmware in the slot intact, still a legitimate rollback target. Nothing of value is lost when scrubbing, because the download itself already destroyed that previous firmware.

**12. Signing key is gated per build environment (`ENV_PROD` vs dev).**
Dev and prod trust different keys while exercising the same mechanism. The dev key is the locally-generated bench keypair; the prod branch deliberately contains an INVALID placeholder until the KMS CMK is provisioned - `mbedtls_pk_parse_public_key` fails on it, so a premature prod build fails closed (every MQTT OTA rejected with `pubkey_parse_error`, local upload as recovery) instead of trusting a nonexistent key, and CI's esp32s3-prod build stays green (an `#error` would have blocked all CI until the cross-repo provisioning lands).

## Risks / Trade-offs

- **[Risk]** KMS private key compromise is existential — no signature check helps if the signing key itself is stolen. → **Mitigation**: CI-only IAM role scoped to `kms:Sign` alone (key material never leaves KMS, unlike a file-based key); CloudTrail logs every signing operation. Key rotation tooling is a follow-up, not required for this change to be safe on day one.
- **[Risk]** Streaming SHA-256 over the newly-written partition adds flash-read time before `finish`/reboot. → **Mitigation**: chunked reads (4 KB, from a PSRAM buffer - see Decision 9) bounded by the known image size; this runs once per OTA attempt, not a hot path. No iteration-count guard on this loop - see Decision 8.
- **[Risk]** Switching to the granular `esp_https_ota` API introduces more explicit failure points (begin / perform / partition read-back) than the current single call. → **Mitigation**: map each new failure to the existing `_publishOtaStatus(..., "FAILED", reason)` pattern already in use. There is no OTA timeout watchdog task on this path today (confirmed during implementation - only the local-upload path has one), so this change doesn't need to preserve one; the perform loop's own termination is unchanged from what the single-call wrapper already did internally (see Decision 8).
- **[Risk]** Writing job-scoped shared state (`_otaCurrentUrl`, `_otaCurrentSignature`, etc.) from `_handleSingleJobExecution` while the OTA task reads it from a different FreeRTOS task relies on ordering, not a mutex - found during adversarial code review to have been briefly violated by this change (the signature was decoded into shared state before the "already in flight" guards instead of after). → **Mitigation**: fixed by moving the shared-state write to strictly after every in-flight guard, matching where `_otaCurrentUrl`/`_otaCurrentJobId` already write; a second job execution is rejected by those guards before it can reach the write, which is the pre-existing (informal, ordering-based, not lock-based) synchronization this file already relies on.
- **[Risk]** A fleet transition period exists between "CI starts requiring signatures" and "all devices can check them." → **Mitigation**: see Migration Plan — sequencing, not a firmware-side bypass.

## Migration Plan

1. Ship verification-capable firmware to the currently-deployed fleet via one final OTA over the existing (unsigned) mechanism — the last time an unsigned push to this fleet is acceptable.
2. Provision the KMS asymmetric CMK and CI-only signing role; add the digest-sign step to the release pipeline; start producing `firmware.sig` / `firmware.signature` for all new releases.
3. Update job-creation tooling to include `firmware.signature` in every new job document. Do not target a device with a signature-bearing job until its last-known firmware version is confirmed verification-capable (via device shadow/reported version) — no dual-mode "signature optional" transition period in the firmware itself.
4. Rollback of this change is a firmware/CI revert, not a runtime toggle — consistent with "no bypass mechanism" as a hard requirement.
