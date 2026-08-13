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

- [x] 6.1 Added native/Unity unit tests (`test/test_ota_signature/`) for the job-document signature field parsing/validation logic (missing/empty/null field, malformed base64, malformed DER shape - wrong tag, truncated/overclaimed length, zero-length integer, missing second integer, oversized-but-well-formed, output-capacity-too-small). Extracted into host-compilable `lib/ota_signature/` (mirrors the existing `lib/version_compare/` pattern). Plus `lib/sha256_hex/` tests (see group 11); full native suite passes under `pio test -e native` (verified via WSL).
- [ ] 6.2 ~~Native/Unity unit tests for the mbedtls verify wrapper~~ **Descoped from native testing**: this codebase has no existing precedent for linking mbedtls into the `native`/host test environment (no lib/test currently depends on it), and `_verifyOtaSignature`'s cryptographic step (`mbedtls_pk_verify`, `esp_partition_read`) is ESP32/ESP-IDF-only code with no host-portable path without adding new native toolchain dependencies untested in this session. Covered instead by the hardware/e2e tests below, consistent with "test later on a dev device."
- [x] 6.3 Hardware/e2e (bench 2026-08-13): valid signature over the downloaded image verifies (`OTA firmware signature verified successfully`)
- [x] 6.4 Hardware/e2e (bench 2026-08-13): tampered signature → `_verifyOtaSignature` fails (`-0x4E00`), OTA aborts, image header scrubbed, `signature_invalid` reason, single attempt (not retried), device stays on the running partition
- [x] 6.5 Hardware/e2e (bench 2026-08-13): missing and malformed signatures both rejected before any download (`REJECTED`, `signature_missing_or_invalid`)

## 13. Hardware e2e findings (bench device, 2026-08-13)

- [x] 13.1 Tampered-signature scrub confirmed end to end: after rejection, `firmware_rollback` to the rejected image is refused (`No valid firmware in the other partition`, HTTP 400) - the design.md Decision 11 bypass is closed on hardware
- [x] 13.2 Downgrade-replay check found broken and REVERTED (see 8.1 / design.md Decision 7): `esp_app_desc_t.version` is frozen on this toolchain, so a validly-signed same-version image replayed under a faked `9.9.9` claim installed and booted. Removed the check rather than ship a control that does not work; anti-rollback deferred to a future signed-manifest change
- [x] 13.3 Test-process fixes captured as memory + project CLAUDE.md guidance: never set log `save` level to DEBUG (crashed `AdvancedLogTask` during the OTA heap-tight window); use `source/utils/udp_log_listener.py` for verbose diagnostics instead
- [x] 13.5 Real AWS IoT cloud e2e (dev, `aws iot create-job` targeting the bench thing, presigned URL resolved by AWS via the `${aws:iot:s3-presigned-url:...}` placeholder): valid signed job → verified → installed → post-reboot validated → job **SUCCEEDED** (status updates ACCEPTED by AWS, SNAPSHOT job auto-cleared); tampered job → verify fail → scrubbed → job **FAILED** with `reason=signature_invalid` and diagnostics. Confirms the real Jobs wire format + lifecycle the dev inject endpoint could not (its synthetic status updates are rejected by AWS). Test jobs and S3 objects cleaned up afterwards
- [x] 13.6 Offline verification against the **real deployed dev key** (2026-08-14): `esp32s3-dev` `firmware.bin` signed with dev KMS CMK `alias/energyme-home-dev-ota-signing` (`aws kms sign --message-type DIGEST --signing-algorithm ECDSA_SHA_256`); `openssl dgst -sha256 -verify` against the embedded PEM → `Verified OK`; one flipped byte in the image → fail; one flipped byte in the signature → fail. The real 71-byte KMS DER signature is captured as a host unit-test decode vector (`test_valid_real_kms_signature_decodes`, 554 native tests pass)
- [x] 13.7 On-device e2e against the **real deployed dev key** (bench, 2026-08-14): device transitioned to firmware embedding the official dev public key (bootstrapped by an old-key-signed OTA), then all cases pass with the official key: **valid** real-KMS signature → `signature verified successfully (2606464 bytes)` → activated → rebooted → validated; **tampered signature** (valid DER, wrong value) → `-0x4E00` → scrubbed → FAILED `signature_invalid`; **tampered image + genuine signature** → hash mismatch → `-0x4E00` → scrubbed → FAILED; **missing signature** → REJECTED before download. `force:true` case **confirmed, not assumed**: a `force:true` job at the current version (would be `already_up_to_date` without force) is downloaded (version guard bypassed) but the bad signature is still rejected (`signature_invalid`) - force never bypasses signature. No flash/RAM leak or leaked ESP OTA handle: the device completed 3 failed-verify downloads then a successful install in one uptime
- [ ] 13.4 Follow-up (separate change, with KMS/CI): if anti-downgrade-replay is wanted, bind the version into the signature via a signed `{version, firmware-hash}` manifest

## 7. Documentation touch-points (only where behavior is externally observable)

- [x] 7.1 Skipped: `/api/v1/ota/upload`'s response schema and endpoint contract are unchanged (only a server-side log line was added), so `resources/swagger.yaml` needs no update

## 8. Adversarial code-review fixes (see design.md Decisions 7-9)

- [x] ~~8.1 Closed a downgrade-replay hole with a post-verification embedded-version check~~ **REVERTED after hardware testing (see group 13)**: `esp_app_desc_t.version` is a frozen toolchain constant, not the semantic version, so the check never fired. Anti-rollback is a Non-Goal; removed the check, its spec requirement, and the `isStrictUpgrade` helper + tests. Accidental downgrades stay covered by the pre-download `ota-job-version-guard`.
- [x] 8.2 Fixed a race: the decoded signature was being written to shared state (`_otaCurrentSignature`) before the "OTA already in flight" guards in `_handleSingleJobExecution` instead of after, unlike `_otaCurrentUrl`/`_otaCurrentJobId` - moved the write to match, closing the window where a second job execution could overwrite a signature `_verifyOtaSignature()` was still reading on the OTA task.
- [x] 8.3 Fixed a regression that would have broken every real OTA download: removed the `MAX_LOOP_ITERATIONS` counters added to the `esp_https_ota_perform` loop and the partition-hashing loop - `esp_http_client`'s default 512-byte RX buffer meant the perform loop would hit the 1000-iteration cap at ~500KB, far under this project's ~4.3MB OTA partitions. Both loops are already bounded by a real size variable, which is this project's own documented exemption from that convention.
- [x] 8.4 Moved the 4KB SHA-256 chunk buffer from the OTA task's stack (~1/3 of `OTA_TASK_STACK_SIZE`) to a PSRAM buffer allocated once in `begin()`, matching the existing `_otaCurrentUrl` pattern.
- [x] 8.5 Extracted a `parseDerInteger` helper in `lib/ota_signature/ota_signature.cpp` to remove duplicated DER-parsing code between the `r` and `s` integer fields (simplify-agent finding).

## 9. Rebase onto development after #239/#240 (see design.md Decision 10)

- [x] 9.1 Rebased onto development after the OTA download-hardening (#239) and firmware_rollback (#240) merges: re-integrated the granular `esp_https_ota_begin`/`perform`/verify/`finish`-or-`abort` sequence into #239's retry loop and `OtaAttempt` diagnostics (this also delivers #239's noted follow-up of switching off the one-shot call)
- [x] 9.2 `_otaFailureReason` now rides in the FAILED job status together with #239's diagnostics and attempt count
- [x] 9.3 Added `_otaFailureRetryable`: signature/activation failures after a complete download break the retry schedule instead of re-downloading the same artifact five times (mirrors #239's non-retryable 4xx handling)

## 10. Final review sweep after the rebase (adversarial + simplify agents, findings reproduced before acceptance)

- [x] 10.1 **Build-breaking typo**: `esp_https_ota_is_complete_data_read` does not exist in ESP-IDF - renamed to `esp_https_ota_is_complete_data_received` (verified against the pinned toolchain header). The branch had never been target-compiled before this sweep; `pio run -e esp32s3-dev` now succeeds
- [x] 10.2 Rewrote the now-false `_captureOtaHttpStatus` comment (it said the granular API "is not used here"); kept the function itself - `esp_https_ota_get_status_code()` cannot replace it because a 403/404 fails `esp_https_ota_begin()` which NULLs the handle, so there is no handle to query for exactly the case the status distinguishes (verified against the v5.5.1 header/lib)
- [x] 10.3 Skip the heap/HTTP failure diagnostics for post-download (non-retryable) failures - they describe download failures and would report a meaningless "HTTP 200, all bytes received" for a signature rejection
- [x] 10.4 Check `mbedtls_sha256_starts/update/finish` return values (`hash_error` reason) instead of discarding them - fails safe either way, but reports a backend fault as what it is rather than as a bad signature
- [x] 10.5 Dead `"download_failed"` initializer on `_otaFailureReason` (always reset per attempt) and redundant trailing comment removed; unused `<cstring>` include dropped from the signature tests
- [x] 10.6 Skipped (informational): `_otaHashChunkBuffer` is intentionally never freed in `stop()` - allocated-once pattern, reused across begin/stop cycles

## 11. Host-testable extraction of sha256/version logic (requested after review)

- [x] 11.1 Extracted `sha256BytesToHex` from `utils.cpp` into new `lib/sha256_hex/` (pure, dependency-free); `utils.cpp` keeps a forwarder (same pattern as #239's backoff extraction) plus a `static_assert` tying `SHA256_HEX_BUFFER_SIZE` to `Sha256Hex::BUFFER_SIZE`. 7 new native tests (known vectors, nibble order/lowercase, canary overwrite check, undersized/null rejection)
- [x] 11.2 Extracted the post-verification downgrade gate into `OtaSignature::isStrictUpgrade(running, image)` - pins the comparison direction and argument order in host tests (5 new: newer/equal/older/malformed-degrades-to-reject/v-prefix). `force` bypass stays at the call site. The mbedtls verify itself remains ESP-only (native env has no mbedtls; unchanged descope, see 6.2)

## 12. Rollback-command bypass and per-environment keys (found by Jibril, see design.md Decisions 11-12)

- [x] 12.1 Closed the `firmware_rollback` bypass: a fully-downloaded-then-rejected image stayed intact in the passive slot, and the rollback command (sha256 match + structural `esp_image_verify` only) could boot it - added `_scrubRejectedOtaImage()` erasing the image header on any failed download that wrote bytes, scoped by `bytesWritten > 0` so failures that never touched flash preserve the legitimate rollback target. New spec requirement + 2 scenarios added
- [x] 12.2 Gated `ota_keys.h` by `ENV_PROD`: dev builds embed the bench dev key; prod builds get a deliberately invalid placeholder that fails closed (`pubkey_parse_error`) until the KMS public key is provisioned - keeps CI's esp32s3-prod build green without ever trusting a placeholder
- [x] 12.3 Hardware/e2e test (with 6.3-6.5): after a tampered-signature rejection, confirmed `firmware_rollback` is refused (`"No valid firmware in the other partition"`, HTTP 400) - both via the dev-inject path and a real AWS IoT job (see group 14)

## 14. Real AWS IoT Jobs end-to-end (dev account, bench device, 2026-08-13)

All prior hardware tests (groups 6, 12, 13) used the `ENV_DEV`-only `inject-job` HTTP endpoint, which exercises the exact same device-side code but not the real AWS wire format or job lifecycle (its synthetic jobId can't transition on AWS - status updates come back `update/rejected`). To close that gap, two real jobs were created directly via `aws iot create-job` against the bench device's thing (`588c81c479f8`), using AWS's own `${aws:iot:s3-presigned-url:...}` placeholder and presign role - matching the exact job-document shape the production release pipeline will use (only `firmware.signature` is new).

- [x] 14.1 Valid signed job (`energyme-home-dev-ota-sigtest-valid-1`): downloaded, `_verifyOtaSignature` succeeded, installed, rebooted, post-reboot validation completed. **AWS job execution status transitioned QUEUED -> IN_PROGRESS ("rebooting") -> SUCCEEDED ("validated after successful boot and stability period")** - confirming the full production job lifecycle, not just the local verification logic
- [x] 14.2 Tampered signature job (`energyme-home-dev-ota-sigtest-tampered-1`): downloaded in full (2,606,464/2,606,464 bytes), signature verification failed, image scrubbed, not retried. **AWS job execution status shows FAILED with `statusDetails.reason=signature_invalid`, `attempts=1`**, matching device-side logs exactly. `firmware_rollback` afterward refused as in 12.3
- [x] 14.3 Both test jobs deleted from the dev AWS account after verification (`aws iot delete-job --force`)
