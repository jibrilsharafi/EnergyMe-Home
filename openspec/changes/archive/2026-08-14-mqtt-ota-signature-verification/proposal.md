## Why

The MQTT/cloud OTA path (`_performOtaUpdate` in `src/mqtt.cpp`) downloads firmware from a presigned S3 URL over TLS and, after reboot, compares the running partition's `app_elf_sha256` against a value captured from the same download — a self-referential corruption check, not an authenticity check. Nothing in this path verifies that the binary was actually produced by EnergyMe; a compromised AWS pipeline (stolen CI credentials, hijacked bucket, malicious IoT job) could push arbitrary firmware to the fleet and it would install without complaint. This is the highest-value target to close: it is remote, fleet-wide, and squarely EnergyMe's operational responsibility. Local web-upload OTA is comparatively lower risk (already gated by LAN access + digest auth) and its own signing would require a much larger build-time vendor/community split plus a physical-presence unlock mechanism — deferred to a future change; this change instead makes the local path's lack of verification explicit rather than silent.

## What Changes

- CI signs each release's `firmware.bin` (SHA-256 digest, ECDSA P-256 via AWS KMS `Sign` with `ECDSA_SHA_256`) using a dedicated asymmetric CMK restricted to a CI-only IAM role (`kms:Sign` only — private key material never leaves KMS).
- AWS IoT OTA job documents gain a new `firmware.signature` field: base64-encoded DER `ECDSA-Sig-Value` (`SEQUENCE{r, s}`), matching what KMS returns natively and what `mbedtls_pk_verify` expects.
- `_performOtaUpdate` switches from the one-shot `esp_https_ota()` call to the granular `esp_https_ota_begin` / `esp_https_ota_perform` / `esp_https_ota_finish`-or-`esp_https_ota_abort` sequence, so the downloaded-but-not-yet-active partition can be hashed and signature-verified from the currently-running (trusted) firmware before the boot partition is switched.
- Verification is **mandatory and unconditional** on any build that includes it: a missing or invalid `firmware.signature` aborts the OTA (`esp_https_ota_abort`), leaves the boot partition untouched, and publishes a job status distinct from other failure reasons. There is no field-presence fallback to unsigned behavior — an optional/skip-if-absent check would let an attacker who can forge a malicious job document simply omit the field.
- The device's public key ships as a compiled-in constant (DER `SubjectPublicKeyInfo`, e.g. `include/ota_keys.h`) — not NVS-stored, not runtime-configurable, so it cannot be altered by anyone with device/API access short of reflashing new firmware.
- Local web upload OTA (`POST /api/v1/ota/upload`) is explicitly labeled as unverified: a warning on `update.html` and a distinct log line on every accepted local upload, pointing to GitHub Releases as the canonical build source. **No cryptographic verification is added to the local path in this change.**
- The existing MQTT downgrade guard (`ota-job-version-guard`: `compareVersions` + `force` field) is unchanged — orthogonal to authenticity and already covers rollback-target rejection.

## Capabilities

### New Capabilities
- `ota-firmware-signature-verification`: mandatory ECDSA P-256/SHA-256 signature verification of MQTT/cloud-delivered firmware before boot-partition activation, plus explicit disclosure that the local web-upload path performs no such verification.

### Modified Capabilities
(none — `ota-job-version-guard` behavior is unchanged; this change only adds a new, independent check ahead of it in the OTA pipeline)

## Impact

- `source/src/mqtt.cpp`: `_performOtaUpdate` rewritten to the granular `esp_https_ota` API; adds partition hashing (mbedtls SHA-256) and signature verification (`mbedtls_pk_verify`) before `esp_https_ota_finish`; job-document parsing gains the `firmware.signature` field; new `FAILED`/`REJECTED` status reason for signature failures.
- `source/include/ota_keys.h` (new): compiled-in DER public key.
- `source/src/customserver.cpp`: local upload handler logs an explicit "unverified" marker.
- `source/html/update.html`: static disclosure text near the local upload control.
- Release/CI pipeline (outside `source/`): new KMS signing step producing `firmware.sig`; IoT job-creation step includes the base64 signature in the job document. No change to the S3-uploaded binary itself (signature travels via the job document, not appended to the artifact).
- No change to `ota-job-version-guard`, `web-authentication`, or `device-provisioning` capabilities/specs.
