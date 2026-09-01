# Design - Firmware Image Descriptor

## D1: Placement - `.rodata_custom_desc`, not an `esp_app_desc` override

The prebuilt-libs toolchain defines `esp_app_desc` as a WEAK symbol, so overriding it from project code is possible - but a strong override must replicate low-level fields the bootloader actually checks (`mmu_page_size`, `min/max_efuse_blk_rev_full`), coupling us to values frozen in the pinned platform. The linker script's `.rodata_custom_desc` slot sits immediately after `esp_app_desc_t` ("Should be the second. Custom app version info."), giving a fixed image offset of 0x120 (24 B image header + 8 B segment header + 256 B `esp_app_desc_t`) with zero risk to IDF's own structure. Verified empirically on both build variants.

The section is unreferenced by code paths the linker can see, so `--gc-sections` strips it unless kept alive: `-Wl,-u,ENERGYME_APP_DESC` in the common build flags, mirroring the `-u esp_app_desc` flag the IDF build itself uses.

## D2: Layout - 128 bytes, append-only, versioned

```
uint32_t magic;               // "EMHW" (0x57484D45 LE)
uint32_t layout;              // 1; future layouts append fields, never move them
char     product[16];         // "home" / "home_pro"
uint32_t psramMb;             // PSRAM class the image was built for: 2 (quad) / 8 (octal)
char     fwVersion[16];       // FIRMWARE_BUILD_VERSION
char     buildEnv[8];         // "dev" / "prod"
char     gitRev[12];          // short commit hash, "unknown" outside a git checkout
uint16_t minPcbVersion;       // packed major*10+minor; 0 = no lower bound
uint16_t maxPcbVersion;       // 0 = no upper bound
uint32_t partitionLayoutId;   // 1 = current partition table; bump on any table change
uint8_t  reserved[56];        // zero; future fields land here under a layout bump
```

Append-only contract: a validator that knows layout N validates the fields it knows on any image with layout >= N. `layout == 0` (or bad magic) means "no valid descriptor". Field provenance is entirely compile-time: `psramMb` derives from the toolchain's own `CONFIG_SPIRAM_MODE_OCT` (it cannot disagree with how the binary was actually built), product from `PRODUCT_FALLBACK`, env from `ENV_DEV`/`ENV_PROD`, git rev injected by an extra_script. `min/maxPcbVersion` ship as 0/0 (unbounded) until a firmware actually drops support for a PCB revision.

## D3: Trust model - device-local on both sides

The safety comparison never involves cloud- or user-supplied data. The staged image's requirements are read from bytes compiled into that image; the device's identity comes from the running image's own descriptor (PSRAM class, partition layout id, build env) and the runtime hardware profile (product, PCB version). The running image booted, therefore its compiled PSRAM mode matches the silicon - no runtime probing needed. Filename/job-field checks remain as pre-download fast-fails but are no longer load-bearing.

## D4: Missing-descriptor policy differs by product

Every release before this change lacks the descriptor, and all of them are Home/quad images. On a Home device a missing descriptor is therefore "legacy official image or self-built community image" - rejecting it would break legitimate downgrades and community builds, so it is accepted (logged as legacy). On a Home Pro device no descriptor-less firmware has ever existed for the hardware, and a wrong image hard-bricks, so a missing descriptor is rejected. This is the same reasoning as the per-build `PRODUCT_FALLBACK`: the binary knows at compile time what hardware family it belongs to.

Accepted residual risk: a Home user can still hand-flash an incompatible *self-built* image (no descriptor, wrong everything) via manual upload - unchanged from today, inherent to an open platform, and CrashMonitor's app-level rollback still covers images that at least reach `setup()`.

## D5: Dev-on-prod policy differs by path

Cloud OTA jobs are produced by release tooling; a `dev` image in a job targeting a `prod` device is always a mistake, so it is rejected (`image_env_mismatch`). Manual upload is the bench path where flashing dev builds is the point; it warns and proceeds. Prod-on-dev is always allowed (normal bench upgrade).

## D6: Gate placement

- **Cloud (mqtt.cpp)**: after `_verifyOtaSignature()` succeeds and before `esp_https_ota_finish()` activates. Rejection is deterministic (same image fails identically), so it abandons retries and scrubs the image header exactly like a signature failure, publishing `image_incompatible:<reason>`.
- **Manual upload (customserver.cpp)**: two layers. On the first uploaded chunk, if the chunk already covers offset 0x120+128 (it always does in practice - upload chunks are >= 4 KB), parse the descriptor straight from the request buffer and reject before writing anything. The authoritative check runs in `_finalizeOtaUpload()` before `Update.end(true)` activates: read the descriptor back from the passive partition. The double check costs one 128-byte flash read and closes the small-first-chunk edge case. A finalize-time rejection also scrubs the staged image's header (as the cloud path does): the rejected image is complete and structurally valid in the passive slot, and none of the rollback consumers (API/MQTT `firmware_rollback`, the crash ladder) are descriptor-gated - an attacker segmenting the first chunk below 416 bytes to dodge the early check could otherwise park a wrong-PSRAM image there and activate it via rollback (adversarial-review finding). The first-chunk rejection does NOT scrub: nothing was written, and the passive slot still holds a legitimate rollback target. A flash-read failure during validation is a hard reject, never the legacy allowance.
- **GitHub path**: no on-device download exists; the browser downloads the asset and the user re-enters through manual upload. Covered by that gate; the asset picker's product filter remains as UX.

## D7: Pure validation logic, host-tested

Parse + policy live in `lib/image_descriptor` (no ESP headers): fixed-offset extraction from an image-start buffer, defensive string termination, and a single `validate()` returning a typed verdict (accept / accept-legacy / seven reject reasons). Firmware-side code only reads flash bytes and builds the `DeviceIdentity`. Unity tests cover every verdict, the append-only layout rule, bounds, and short-buffer behavior.
