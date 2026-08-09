## 1. Cross-repo prerequisites (coordinate with energyme-infra / release pipeline — outside this repo)

- [ ] 1.1 Provision an AWS KMS asymmetric CMK (`ECC_NIST_P256`, `SIGN_VERIFY`) via CDK, with a key policy granting `kms:Sign` only to the CI OIDC-federated IAM role
- [ ] 1.2 Export the CMK's public key (`aws kms get-public-key`) as DER `SubjectPublicKeyInfo` bytes, for embedding in firmware (task 2.1)
- [ ] 1.3 Add a CI release step: compute `firmware.bin`'s SHA-256 digest, call `kms:Sign` with `MessageType=DIGEST` / `SigningAlgorithm=ECDSA_SHA_256`, base64-encode the returned DER signature
- [ ] 1.4 Update the IoT job-creation step to include the base64 signature as `firmware.signature` in the job document, alongside existing `firmware.url` / `firmware.version`

## 2. Firmware: embed public key

- [x] 2.1 Add `source/include/ota_keys.h` with the public key compiled in as `constexpr const char*` PEM (matches the existing `AWS_IOT_CORE_CA_CERT` convention in `awsconfig.h`, which `mbedtls_pk_parse_public_key` accepts directly - no NVS storage, no runtime configuration path). **Embeds a locally-generated dev/test P-256 keypair, clearly marked in the header; must be swapped for the real KMS-exported public key (task 1.2) before any vendor/production build.**

## 3. Firmware: job document parsing

- [x] 3.1 In the job-document parsing in `_handleSingleJobExecution` (`mqtt.cpp`), read `firmware.signature` (base64 string) alongside the existing `firmware.url` / `firmware.version` / `force` fields
- [x] 3.2 Base64-decode the signature before starting any download; if the field is absent or fails to decode as a well-formed DER `SEQUENCE{INTEGER,INTEGER}`, reject the job immediately (`_publishOtaStatus(... "REJECTED", "signature_missing_or_invalid")`) without creating the OTA download task. Decoding/shape-validation implemented in new host-testable `lib/ota_signature/` (see 6.1).

## 4. Firmware: verify-before-activate OTA flow

- [x] 4.1 Replace the one-shot `esp_https_ota(&_otaConfig)` call in `_performOtaUpdate` with the granular sequence: `esp_https_ota_begin` → loop `esp_https_ota_perform` until `esp_https_ota_is_complete_data_read` → (verification, tasks 4.2-4.3) → `esp_https_ota_finish` or `esp_https_ota_abort`
- [x] 4.2 After download completes, stream-read the written (inactive) partition in bounded chunks (4 KB, `OTA_SIGNATURE_HASH_CHUNK_SIZE`, loop bound derived from `esp_https_ota_get_image_len_read`) and compute its SHA-256 via mbedtls, without loading the whole image into RAM at once
- [x] 4.3 Parse the embedded public key (`mbedtls_pk_parse_public_key`) and verify the computed hash against the decoded job-document signature (`mbedtls_pk_verify`, `MBEDTLS_MD_SHA256`) in new `_verifyOtaSignature()`
- [x] 4.4 On verification success, call `esp_https_ota_finish` and continue into the existing reboot/pending-state flow unchanged
- [x] 4.5 On verification failure, call `esp_https_ota_abort` instead, leave the boot partition untouched, and publish a job status reason distinct from other failure reasons (`signature_invalid`, via a new `_otaFailureReason` buffer threaded into the existing `_publishOtaStatus` call in `_otaTask`, so a plain download failure and a signature failure are no longer indistinguishable)
- [x] 4.6 Confirmed: there is no OTA timeout watchdog task on the MQTT path (only the local-upload path has one) - nothing to adjust here

## 5. Firmware: local upload disclosure

- [x] 5.1 Added a distinct `LOG_WARNING` in `_handleOtaUploadComplete` (`customserver.cpp`) noting the firmware was accepted via unverified local upload, separate from the MQTT OTA success log wording
- [x] 5.2 Added disclosure warning box to `source/html/update.html` above the upload form, stating local uploads are not cryptographically verified and linking to GitHub Releases as the canonical build source

## 6. Tests

- [x] 6.1 Added native/Unity unit tests (`test/test_ota_signature/`) for the job-document signature field parsing/validation logic (missing/empty/null field, malformed base64, malformed DER shape - wrong tag, truncated/overclaimed length, zero-length integer, missing second integer, oversized-but-well-formed, output-capacity-too-small). Extracted into host-compilable `lib/ota_signature/` (mirrors the existing `lib/version_compare/` pattern). 14/14 pass under `pio test -e native` (verified via WSL).
- [ ] 6.2 ~~Native/Unity unit tests for the mbedtls verify wrapper~~ **Descoped from native testing**: this codebase has no existing precedent for linking mbedtls into the `native`/host test environment (no lib/test currently depends on it), and `_verifyOtaSignature`'s cryptographic step (`mbedtls_pk_verify`, `esp_partition_read`) is ESP32/ESP-IDF-only code with no host-portable path without adding new native toolchain dependencies untested in this session. Covered instead by the hardware/e2e tests below, consistent with "test later on a dev device."
- [ ] 6.3 Hardware/e2e test (requires bench device + a real signed test build, using the dev/test key in `ota_keys.h`): confirm a validly-signed OTA job installs and boots normally
- [ ] 6.4 Hardware/e2e test: confirm a job with a tampered/invalid signature is rejected, the device does not reboot into the new partition, and the running firmware is unaffected
- [ ] 6.5 Hardware/e2e test: confirm a job document missing `firmware.signature` is rejected before any download starts

## 7. Documentation touch-points (only where behavior is externally observable)

- [x] 7.1 Skipped: `/api/v1/ota/upload`'s response schema and endpoint contract are unchanged (only a server-side log line was added), so `resources/swagger.yaml` needs no update

## 8. Adversarial code-review fixes (see design.md Decisions 7-9)

- [x] 8.1 Closed a downgrade-replay hole: signature verification alone doesn't bind the job document's claimed `firmware.version` - added a post-verification check in `_verifyOtaSignature` against the version actually embedded in the verified image, skipped only when `force` is set. New spec requirement added ("Verified firmware's actual version is re-checked against a downgrade...").
- [x] 8.2 Fixed a race: the decoded signature was being written to shared state (`_otaCurrentSignature`) before the "OTA already in flight" guards in `_handleSingleJobExecution` instead of after, unlike `_otaCurrentUrl`/`_otaCurrentJobId` - moved the write to match, closing the window where a second job execution could overwrite a signature `_verifyOtaSignature()` was still reading on the OTA task.
- [x] 8.3 Fixed a regression that would have broken every real OTA download: removed the `MAX_LOOP_ITERATIONS` counters added to the `esp_https_ota_perform` loop and the partition-hashing loop - `esp_http_client`'s default 512-byte RX buffer meant the perform loop would hit the 1000-iteration cap at ~500KB, far under this project's ~4.3MB OTA partitions. Both loops are already bounded by a real size variable, which is this project's own documented exemption from that convention.
- [x] 8.4 Moved the 4KB SHA-256 chunk buffer from the OTA task's stack (~1/3 of `OTA_TASK_STACK_SIZE`) to a PSRAM buffer allocated once in `begin()`, matching the existing `_otaCurrentUrl` pattern.
- [x] 8.5 Extracted a `parseDerInteger` helper in `lib/ota_signature/ota_signature.cpp` to remove duplicated DER-parsing code between the `r` and `s` integer fields (simplify-agent finding).
