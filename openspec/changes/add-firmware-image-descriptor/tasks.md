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

## 5. Verification

- [x] 5.1 Native tests green (WSL)
- [x] 5.2 Both env binaries carry correct descriptors at 0x120 (hexdump check)
- [x] 5.3 `esp32s3-dev` and `esp32s3-dev-pro` build clean

## 6. Hardware-blocked (bench, when a device is available)

- [ ] 6.1 Manual upload of a renamed wrong-product .bin is rejected on-device
- [ ] 6.2 Cloud OTA job with a wrong-product image fails with `image_incompatible:*` and the device keeps running
- [ ] 6.3 Downgrade to a pre-descriptor release still works on Home
