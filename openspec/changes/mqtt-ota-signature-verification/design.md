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

## Risks / Trade-offs

- **[Risk]** KMS private key compromise is existential — no signature check helps if the signing key itself is stolen. → **Mitigation**: CI-only IAM role scoped to `kms:Sign` alone (key material never leaves KMS, unlike a file-based key); CloudTrail logs every signing operation. Key rotation tooling is a follow-up, not required for this change to be safe on day one.
- **[Risk]** Streaming SHA-256 over the newly-written partition adds flash-read time and a bounded loop before `finish`/reboot. → **Mitigation**: chunked reads (e.g. 4 KB) bounded by the known image size (from `Content-Length`), consistent with the project's `MAX_LOOP_ITERATIONS` convention; this runs once per OTA attempt, not a hot path.
- **[Risk]** Switching to the granular `esp_https_ota` API introduces more explicit failure points (begin / perform / partition read-back) than the current single call. → **Mitigation**: map each new failure to the existing `_publishOtaStatus(..., "FAILED", reason)` pattern already in use; the existing OTA timeout watchdog task continues to bound the whole sequence unchanged.
- **[Risk]** A fleet transition period exists between "CI starts requiring signatures" and "all devices can check them." → **Mitigation**: see Migration Plan — sequencing, not a firmware-side bypass.

## Migration Plan

1. Ship verification-capable firmware to the currently-deployed fleet via one final OTA over the existing (unsigned) mechanism — the last time an unsigned push to this fleet is acceptable.
2. Provision the KMS asymmetric CMK and CI-only signing role; add the digest-sign step to the release pipeline; start producing `firmware.sig` / `firmware.signature` for all new releases.
3. Update job-creation tooling to include `firmware.signature` in every new job document. Do not target a device with a signature-bearing job until its last-known firmware version is confirmed verification-capable (via device shadow/reported version) — no dual-mode "signature optional" transition period in the firmware itself.
4. Rollback of this change is a firmware/CI revert, not a runtime toggle — consistent with "no bypass mechanism" as a hard requirement.
