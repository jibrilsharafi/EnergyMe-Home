## MODIFIED Requirements

### Requirement: A staged image is validated against the device before activation

Before any staged OTA image is activated (boot partition switched), the running firmware SHALL read the staged image's descriptor from the passive partition and reject activation when: the product does not match the device's product, the PSRAM class does not match the device's PSRAM class, the device's PCB version falls outside the image's declared range (0 = unbounded), or the partition layout id differs. The device's PSRAM class SHALL come from the chip's eFuse `PSRAM_CAP` field, never from the running image, falling back to the running image's descriptor only when the eFuse reports a class no build targets. A staged image whose partition layout id is 0 SHALL be treated as "unspecified" and SHALL NOT be rejected on layout grounds. Validation SHALL use only device-local data: the staged image's own bytes and the running device's identity.

#### Scenario: Unspecified partition layout id is tolerated

- **WHEN** a staged image otherwise matching the device carries partition layout id 0
- **THEN** it is accepted; any other id that differs from the running image's id is rejected

#### Scenario: Wrong-PSRAM image is rejected before it can brick the device

- **WHEN** a correctly signed Home Pro (octal) image is staged on a Home (quad) device through any path
- **THEN** the image is rejected before the boot partition is switched, the device keeps running its current firmware, and the rejection reason names the PSRAM mismatch

#### Scenario: The right image is accepted while a wrong one is running

- **WHEN** a Home (quad) device is running a Home Pro (octal) image and a Home image is uploaded
- **THEN** the upload is accepted, because the device's PSRAM class comes from the eFuse

#### Scenario: Cloud rejection is terminal and scrubbed

- **WHEN** the cloud OTA path stages an image whose descriptor fails validation
- **THEN** the job is failed with reason `image_incompatible:<verdict>`, the retry schedule is abandoned (the failure is deterministic), and the staged image's header is scrubbed exactly as for a signature failure

#### Scenario: Manual upload fails fast on the first chunk

- **WHEN** a manual firmware upload's first chunk already contains the descriptor region and validation fails
- **THEN** the upload is rejected with an explanatory error before any further data is written, and the final pre-activation check still runs for uploads whose first chunk was too small

### Requirement: Legacy images without a descriptor are accepted only on quad-PSRAM Home

An image with no valid descriptor (bad magic or zero layout) SHALL be accepted only on a device whose product is Home AND whose eFuse PSRAM class is quad (2 MB) - every pre-descriptor official release and community self-build is a Home/quad image, and rejecting them would break legitimate downgrades. The product alone is not sufficient: it comes from factory NVS, and octal hardware provisioned as "home" would otherwise accept an unbootable image. A descriptor-less image SHALL be rejected in every other case, and a failed flash read of the staged image SHALL be a hard reject, never the legacy allowance.

#### Scenario: Downgrade to a pre-descriptor release on Home

- **WHEN** a Home device stages an official 2.x image released before the descriptor existed
- **THEN** activation proceeds, with a log line identifying the image as legacy

#### Scenario: Descriptor-less image on Home Pro

- **WHEN** a Home Pro device stages an image with no valid descriptor
- **THEN** activation is rejected

#### Scenario: Descriptor-less image on octal hardware provisioned as Home

- **WHEN** a device whose factory product is Home but whose eFuse PSRAM class is octal stages an image with no valid descriptor
- **THEN** activation is rejected

## ADDED Requirements

### Requirement: A running image built for another board rolls itself back

Right after the hardware profile is selected, and before any PSRAM-dependent initialization, the firmware SHALL validate its own descriptor against the device identity. When it fails (other product or other PSRAM class) and the passive slot's image passes validation, the firmware SHALL switch the boot partition to the passive slot and restart. When the passive slot does not pass, the firmware SHALL log the reason and continue booting, so a compatible image can still be uploaded.

#### Scenario: Home Pro image on a Home board with the previous Home release in the passive slot

- **WHEN** a Home (quad) device boots a Home Pro (octal) image and the passive slot holds a Home image, with or without a descriptor
- **THEN** the device switches to the passive slot and restarts into it, without waiting for the crash ladder

#### Scenario: No compatible image in the passive slot

- **WHEN** the running image is for another board and the passive slot is empty or also incompatible
- **THEN** the boot partition is not switched, the reason is logged, and boot continues
