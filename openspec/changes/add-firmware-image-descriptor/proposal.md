# Add Firmware Image Descriptor

## Why

With Home Pro, releases carry per-product binaries built for different PSRAM silicon (quad vs octal, fixed at compile time). A wrong-PSRAM image fails PSRAM init before any application code runs - earlier than CrashMonitor's crash-ladder rollback and earlier than the Arduino-framework `esp_ota_mark_app_valid_cancel_rollback()` call - so nothing on the device can recover from booting one: the unit is stuck until someone attaches a serial cable. Today every device-side defense against this trusts *external* labels only (artifact filename, GitHub asset name, cloud job `product` field). The binary itself says nothing about what hardware it needs, and the standard `esp_app_desc_t` fields are frozen CI constants on the prebuilt-libs toolchain (`project_name = "arduino-lib-builder"`, `version = "487f743"` in every build).

The IDF linker script reserves a `.rodata_custom_desc` section immediately after `esp_app_desc_t`, placing anything put there at a fixed offset (0x120) in every application image. This was validated end to end on this toolchain (both `esp32s3-dev` and `esp32s3-dev-pro` binaries carry a test descriptor at 0x120, kept through `--gc-sections` by a `-u` linker flag, the same mechanism IDF uses for `esp_app_desc` itself).

## What Changes

- Every firmware image embeds a 128-byte self-describing descriptor at fixed image offset 0x120: magic, layout version, product, required PSRAM class, firmware version, build env (dev/prod), git revision, supported PCB version range, partition layout id.
- Before an OTA image is ever activated, the still-running firmware reads the staged image's descriptor straight off the passive partition (one `esp_partition_read`, same pattern as the existing `esp_ota_get_partition_description` SHA read) and validates it against the device's own identity. Both sides of the comparison come from the device: the descriptor is compiled into the binary, and the device's PSRAM class is taken from the *running* image's own descriptor (the running build boots, therefore its PSRAM mode matches the silicon). No cloud- or user-supplied metadata participates in the safety decision.
- Both firmware-write paths are gated: the manual web upload (early reject on the first uploaded chunk when it already covers the descriptor, plus an authoritative check before `Update.end()` activates) and the cloud MQTT OTA (checked alongside the existing ECDSA signature verification, before `esp_https_ota_finish()` activates). The GitHub community path downloads through the browser and re-enters via manual upload, so it is covered by the same gate.
- Rejections reuse the existing failure machinery: cloud rejections publish a specific reason and scrub the rejected image like a bad signature; manual rejections abort the update with an explanatory HTTP error.
- Images without a descriptor (all releases before this change) remain flashable on Home devices - rejecting them would break legitimate downgrades - and are rejected on Home Pro, which never shipped a descriptor-less firmware.
- A `dev` build arriving over cloud OTA on a `prod` device is rejected; over manual upload it is allowed with a warning (the bench workflow).
- The existing filename/job-field product checks stay as cheap early rejects (they fire before any download); the descriptor check is the authoritative gate.

**BREAKING**: none. Old images remain bootable and (on Home) flashable; new images carry 128 extra bytes of rodata.

## Capabilities

### New Capabilities

- `firmware-image-compatibility`: the embedded descriptor (layout, placement, build-time provenance of each field) and the device-side validation policy applied to every staged image before activation.

## Relation to add-home-pro-support

Closes the accepted risk documented there ("unknown-token manual OTA pass-through"): a self-built or renamed binary no longer bypasses product/PSRAM checking, because the check no longer depends on the name.
