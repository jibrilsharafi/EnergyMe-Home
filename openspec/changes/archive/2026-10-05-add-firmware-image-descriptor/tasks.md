# Tasks - add-firmware-image-descriptor

## 1. Descriptor library (pure, host-tested)

- [x] 1.1 `lib/image_descriptor`: struct (128 B, static-asserted), parse from image-start buffer with defensive string termination, `validate()` with typed verdicts, verdict-to-string
- [x] 1.2 Unity tests: every verdict, legacy policy per product, PCB bounds (0 = unbounded), partition layout, env policy per path, layout >= ours accepted, short buffer

## 2. Descriptor emission

- [x] 2.1 Emit `ENERGYME_APP_DESC` in `.rodata_custom_desc` from a dedicated TU; values from `PRODUCT_FALLBACK`, `CONFIG_SPIRAM_MODE_OCT`, `FIRMWARE_BUILD_VERSION`, `ENV_DEV`/`ENV_PROD`, `GIT_REV`
- [x] 2.2 `-Wl,-u,ENERGYME_APP_DESC` in common build flags (gc-sections keep-alive)
- [x] 2.3 Git revision injection via extra_script (falls back to "unknown")
- [x] 2.4 Remove the PoC struct from `hardware_profile.cpp`

## 3. Device-side validation glue

- [x] 3.1 Partition reader + `DeviceIdentity` builder (product/PCB from runtime profile; PSRAM class, partition layout id, env from the running image's own descriptor)

## 4. Gates

- [x] 4.1 Cloud OTA: validate after signature verification, before `esp_https_ota_finish()`; deterministic failure `image_incompatible:<verdict>`, scrub like signature failure
- [x] 4.2 Manual upload: first-chunk early reject when the chunk covers the descriptor region
- [x] 4.3 Manual upload: authoritative check in `_finalizeOtaUpload()` before `Update.end(true)`
- [x] 4.4 Rollback: `attemptFirmwareRollback()` (API/MQTT `firmware_rollback`) validates the passive slot before `esp_ota_set_boot_partition()` and returns `INVALID_IMAGE` on a reject verdict
- [x] 4.5 Crash ladder: `CrashMonitor::_handleCounters()` validates the passive slot before `Update.rollBack()` and falls through to factory reset on a reject verdict
- [x] 4.6 Report running + passive-slot descriptors in `/api/v1/system/info` (`static.imageDescriptor`) and the reported shadow (`image_*`, `other_image_*`)

## 5. Verification

- [x] 5.1 Native tests green (WSL)
- [x] 5.2 Both env binaries carry correct descriptors at 0x120 (hexdump check) - note: only the dev envs (`esp32s3-dev`, `esp32s3-dev-pro`) were hexdumped; the prod envs share the same emitting TU and differ only in `buildEnv`
- [x] 5.3 `esp32s3-dev` and `esp32s3-dev-pro` build clean

## 6. Hardware-blocked (bench, when a device is available)

- [x] 6.1 Manual upload of a renamed wrong-product .bin is rejected on-device - done 2026-09-19: the Home image named `firmware.bin` on the Pro and the Pro image named `firmware.bin` on the Home both answer 400 "not compatible with this device" on the first chunk (nothing written, other partition still rollback-able, device keeps running); a Pro image carrying a Home artifact name is refused by the name gate
- [x] 6.2 Cloud OTA job with a wrong-product image fails with `image_incompatible:*` and the device keeps running - done 2026-09-21: signed a real Home (esp32s3-dev) binary with the Home Pro dev KMS key via `ota_release.py sign --from-file`, published a real AWS IoT job (`energyme-homepro-dev-ota-device-9-9-9-...`) declaring `product: homepro` at `thing:14c19f4e1b44` (the Pro bench unit). Job doc and signature both passed (job-product and signature-format gates are correct, key matched); full 2,783,840-byte download completed (HTTP 200); `ImageDescriptor::validate()` then rejected it post-download with `image_incompatible:psram_mismatch` (the Home binary has quad PSRAM baked in, the running device is octal). Job execution status FAILED with that reason in `statusDetails`; device uptime and the crash archive (still exactly 2 entries) were unchanged across the attempt - it never rebooted or crashed.
- [x] 6.3 Downgrade to a pre-descriptor release still works on Home - done 2026-09-19 on the Home dev unit: the 2026-08-17 dev build (no descriptor at 0x120) is accepted, boots and serves; uploading the current build from it works too
