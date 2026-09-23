# firmware-image-compatibility Delta

## Purpose

Every firmware image declares, inside the binary itself, what hardware it was built for; the running firmware refuses to activate a staged OTA image whose declaration does not match the device. This is the device-side defense against flashing an image that cannot boot (wrong PSRAM silicon), which no post-boot recovery mechanism can catch.

## ADDED Requirements

### Requirement: Every image embeds a self-describing descriptor at a fixed offset

The build SHALL embed a 128-byte descriptor in the `.rodata_custom_desc` section, landing at image offset 0x120, containing: magic word, layout version, product, required PSRAM class, firmware version, build environment, git revision, supported PCB version range, and partition layout id. All field values SHALL be derived at compile time; the PSRAM class SHALL derive from the toolchain's own SPIRAM mode configuration so it cannot disagree with how the binary was built.

#### Scenario: Home and Pro binaries differ only where hardware differs

- **WHEN** the `esp32s3-prod` and `esp32s3-prod-pro` binaries of the same release are inspected at offset 0x120
- **THEN** both carry a valid descriptor with the same firmware version, and differ in product ("home" vs "homepro") and PSRAM class (2 vs 8)

### Requirement: A staged image is validated against the device before activation

Before any staged OTA image is activated (boot partition switched), the running firmware SHALL read the staged image's descriptor from the passive partition and reject activation when: the product does not match the device's product, the PSRAM class does not match the running image's PSRAM class, the device's PCB version falls outside the image's declared range (0 = unbounded), or the partition layout id differs. A staged image whose partition layout id is 0 SHALL be treated as "unspecified" and SHALL NOT be rejected on layout grounds, so a future partition-table change can be staged in two steps (first ship firmware that knows the new id, then flip it). Validation SHALL use only device-local data: the staged image's own bytes and the running device's identity.

#### Scenario: Unspecified partition layout id is tolerated

- **WHEN** a staged image otherwise matching the device carries partition layout id 0
- **THEN** it is accepted; any other id that differs from the running image's id is rejected

#### Scenario: Wrong-PSRAM image is rejected before it can brick the device

- **WHEN** a correctly signed Home Pro (octal) image is staged on a Home (quad) device through any path
- **THEN** the image is rejected before the boot partition is switched, the device keeps running its current firmware, and the rejection reason names the PSRAM mismatch

#### Scenario: Cloud rejection is terminal and scrubbed

- **WHEN** the cloud OTA path stages an image whose descriptor fails validation
- **THEN** the job is failed with reason `image_incompatible:<verdict>`, the retry schedule is abandoned (the failure is deterministic), and the staged image's header is scrubbed exactly as for a signature failure

#### Scenario: Manual upload fails fast on the first chunk

- **WHEN** a manual firmware upload's first chunk already contains the descriptor region and validation fails
- **THEN** the upload is rejected with an explanatory error before any further data is written, and the final pre-activation check still runs for uploads whose first chunk was too small

### Requirement: Legacy images without a descriptor are accepted only on quad-PSRAM Home

An image with no valid descriptor (bad magic or zero layout) SHALL be accepted only on a device whose product is Home AND whose running image was built for quad PSRAM (running descriptor PSRAM class 2) - every pre-descriptor official release and community self-build is a Home/quad image, and rejecting them would break legitimate downgrades. The product alone is not sufficient: it comes from factory NVS, and octal hardware provisioned as "home" would otherwise accept an unbootable image; only a running quad image proves a quad image can boot. A descriptor-less image SHALL be rejected in every other case, and a failed flash read of the staged image SHALL be a hard reject, never the legacy allowance.

#### Scenario: Downgrade to a pre-descriptor release on Home

- **WHEN** a Home device stages an official 2.x image released before this change
- **THEN** activation proceeds, with a log line identifying the image as legacy

#### Scenario: Descriptor-less image on Home Pro

- **WHEN** a Home Pro device stages an image with no valid descriptor
- **THEN** activation is rejected

#### Scenario: Descriptor-less image on octal hardware provisioned as Home

- **WHEN** a device whose factory product is Home but whose running image is an octal-PSRAM build stages an image with no valid descriptor
- **THEN** activation is rejected

### Requirement: Rollback never activates an incompatible image

Every path that switches the boot partition to the passive OTA slot without staging a new image - the API and MQTT `firmware_rollback` command and the crash-ladder automatic rollback - SHALL validate the passive slot's descriptor with the same policy (manual-path env rule: dev-on-prod allowed) before switching. A rejected verdict SHALL refuse the rollback: the command reports an invalid image, and the crash ladder skips rollback and proceeds to its next recovery step.

#### Scenario: Rollback onto a wrong-product image is refused

- **WHEN** the passive slot holds a complete, structurally valid image of the other product and a `firmware_rollback` command arrives
- **THEN** the boot partition is not switched and the command fails as an invalid image

#### Scenario: Crash ladder skips an incompatible rollback target

- **WHEN** the crash ladder reaches its rollback step and the passive slot's descriptor fails validation
- **THEN** no rollback is attempted and the ladder falls through to factory reset

### Requirement: Descriptors are reported for inventory

The running image's descriptor and the passive slot's descriptor (when valid) SHALL be reported in the system info API (`imageDescriptor.running` / `imageDescriptor.other`) and in the reported device shadow, so the fleet can see which PSRAM class and build each device runs and what a rollback would activate without per-device polling.

#### Scenario: Passive slot without a descriptor

- **WHEN** the passive slot is empty, erased, or holds a legacy pre-descriptor image
- **THEN** system info reports `imageDescriptor.other.present = false` and the shadow reports the `other_image_*` fields as null

### Requirement: Development builds do not reach production devices via cloud OTA

A staged image whose build environment is `dev` SHALL be rejected by a device running a `prod` build when delivered via cloud OTA, and SHALL be accepted with a logged warning when delivered via manual upload. A `prod` image SHALL always be accepted by a device running a `dev` build.

#### Scenario: Dev image in a cloud job

- **WHEN** a cloud OTA job delivers a `dev`-built image to a device running `prod` firmware
- **THEN** the job is rejected with reason `image_incompatible:dev_image_on_prod`

#### Scenario: Bench flashing a dev build

- **WHEN** a `dev`-built image is uploaded manually to a device running `prod` firmware
- **THEN** the upload proceeds and a warning is logged

### Requirement: Descriptor layout is append-only and versioned

Future descriptor layouts SHALL only append fields into the reserved region and bump the layout version; existing field offsets SHALL never move. A validator SHALL validate the fields it knows on any image whose layout version is greater than or equal to its own.

#### Scenario: Older firmware validates a newer image

- **WHEN** firmware knowing layout 1 stages an image carrying layout 2
- **THEN** the layout-1 fields are validated normally and the image is accepted if they pass
