## Why

Issue #264: a Home v6.1 on 2.0.0 (no upload guard) took `energyme_homepro_2.5.1.bin` by mistake and crash-looped for over an hour until it was recovered over the serial header. The 2.5.x guard made it worse, not better. `deviceIdentity()` took the PSRAM class from the running image's own descriptor, so once the wrong image ran it judged every compatible image (the passive 2.0.0, the right Home upload) as the mismatch, and refused them all.

The guard's premise was also wrong. An image of the other PSRAM class does not die at PSRAM init: it boots without PSRAM (octal builds set `CONFIG_SPIRAM_IGNORE_NOTFOUND`, quad builds only warn from `psramInit()`), runs application code and crashes later. So in-app recovery is possible, as long as the identity comes from the silicon.

## What Changes

- The device's PSRAM class comes from the eFuse `PSRAM_CAP` field (1 = 8 MB octal, 2 = 2 MB quad), falling back to the running image's descriptor only when the eFuse reports a value no build targets.
- The legacy (descriptor-less) allowance follows the eFuse PSRAM class instead of the running image's.
- Right after the hardware profile is selected, first thing in `setup()`, the running image is validated against the device. If it fails (other product, other PSRAM class, or a PCB range that excludes this board) and the passive slot validates, the boot partition is switched to it and the device restarts. If the passive slot does not fit either, the device keeps running, and the upload gates now accept a build for this board.
- The `initHardwareProfile()` product-mismatch log is dropped, superseded by the check above.

Recovery only works once the published Pro .bin carries this fix. A device already stuck on `homepro_2.5.1` runs the old guard, which rejects a Home image on PSRAM and a fixed Pro image on product, so it stays serial-only.

Not in this change: a community-mode device (no factory NVS) takes its product from the build, so a Pro build on an unprovisioned Home board is still flagged for PSRAM but cannot roll back to or accept a Home image. Community users self-build and have serial.

## Capabilities

### Modified Capabilities
- `firmware-image-compatibility`: PSRAM identity from the eFuse, and a boot-time rollback off an image built for another board.
