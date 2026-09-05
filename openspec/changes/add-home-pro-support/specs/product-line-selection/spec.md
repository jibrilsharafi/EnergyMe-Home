# product-line-selection Delta

## Purpose

Determines at boot which product the firmware is running on (Home or Home Pro) from factory NVS, and selects the matching hardware profile so one firmware codebase serves every product and PCB revision.

## ADDED Requirements

### Requirement: The product line is read from factory NVS and defaults to Home

The system SHALL read `factory_ns::product_line` at boot to determine the product. A missing key SHALL be treated as `home`, permanently: the deployed fleet predates the key and requires no backfill. A present but unknown value SHALL put the device in community mode on the build's fallback product profile, consistent with malformed `pcb_revision` handling.

#### Scenario: Provisioned Home Pro device

- **WHEN** the device boots with `product_line = "home_pro"` and a matching `pcb_revision`
- **THEN** the Home Pro hardware profile for that revision is selected and the device operates as a provisioned (non-community) device

#### Scenario: Legacy fleet device without the key

- **WHEN** a factory-provisioned device boots with certificates and `pcb_revision` present but no `product_line` key
- **THEN** the product is `home`, the profile matching its `pcb_revision` is selected, and the device remains fully provisioned - not community mode

#### Scenario: Unknown product value

- **WHEN** the device boots with `product_line` set to a value this firmware does not know
- **THEN** the device enters community mode on the build's fallback product profile and logs a warning, rather than trusting an identity it cannot interpret

### Requirement: Hardware profiles are selected by product and PCB version together

Profile lookup SHALL be keyed by the pair (product, PCB version). Product lines number their PCBs independently; Home Pro starts at v1.0. A version number SHALL never resolve across product boundaries.

#### Scenario: Same version number on two products

- **WHEN** a Home Pro device with `pcb_revision = "v1.0"` boots
- **THEN** the Home Pro v1.0 profile is selected, never a Home profile that happens to share the version number

#### Scenario: Existing Home devices are unaffected

- **WHEN** a Home device with `pcb_revision = "v6.1"` boots on firmware with Home Pro support
- **THEN** the selected profile, pins, channel count and behavior are identical to the firmware before this change

### Requirement: Fallback profile follows the build's product

When no profile can be selected (no factory NVS, missing or malformed `pcb_revision`, unknown product), the system SHALL fall back to the latest profile of the build's fallback product in community mode. The fallback product defaults to Home; every Home Pro build - production included - SHALL pin its fallback product to Home Pro, so a Pro binary never falls back to a Home pinout. Community builds targeting other hardware SHALL pin their product and version explicitly, via build-time fallback defines or by writing the factory NVS keys.

#### Scenario: Community device with no factory NVS

- **WHEN** a self-built device boots a Home build with no factory namespace
- **THEN** the latest Home profile is used in community mode, as today

#### Scenario: Unprovisioned Home Pro bench board

- **WHEN** a Pro build boots on a board with no factory NVS
- **THEN** the Home Pro v1.0 profile is selected in community mode

#### Scenario: Pro production device with damaged factory NVS

- **WHEN** a Pro production build boots and `pcb_revision` is malformed or `product_line` is unreadable
- **THEN** the device enters community mode on the latest Home Pro profile - never a Home pinout - because the binary's product is known at compile time

### Requirement: Channel count follows the selected profile

All channel-dependent behavior (measurement iteration, REST/MQTT/Modbus payload sizes, web UI channel lists) SHALL derive from the selected profile's channel count. Home Pro v1.0 exposes 12 channels (11 multiplexed + 1 direct).

#### Scenario: Pro device reports 12 channels

- **WHEN** a Home Pro v1.0 device serves channel data over any integration
- **THEN** exactly 12 channels (indices 0-11) carry data; requests for higher channel indices are rejected (fixed-size register maps may reserve the space, matching how v6.1 handles its unused range today)

#### Scenario: Product visible in device info

- **WHEN** the user reads the device info page or API
- **THEN** the product line is reported, so support dumps identify the product

### Requirement: Firmware images are product-specific and protected against cross-product installation

Firmware binaries are built per product (the products use different PSRAM silicon, fixed at compile time). The system SHALL identify its own product's firmware artifacts unambiguously - the Home artifact token is a substring of the Pro token, so matching SHALL resolve the Pro token first - and SHALL reject, before any flash write, a firmware delivery whose product does not match the running product, on every delivery path: manual web upload, community release download, and cloud OTA job.

#### Scenario: Wrong-product image uploaded manually

- **WHEN** a Home firmware artifact is uploaded through the web OTA endpoint of a Home Pro device (or vice versa)
- **THEN** the upload is rejected before any flash write, with an error identifying the product mismatch

#### Scenario: Pro artifact name contains the Home token

- **WHEN** a Pro artifact (whose name embeds the Home artifact token as a substring) is checked on a Home device
- **THEN** it is identified as a Pro artifact and rejected - substring presence of the Home token alone is never sufficient

#### Scenario: Community release with two assets

- **WHEN** a device checks a published release carrying one artifact per product
- **THEN** it selects the artifact matching its own product, never simply the first match

#### Scenario: Mistargeted cloud OTA job

- **WHEN** a cloud OTA job whose declared product does not match the device is received
- **THEN** the device refuses the job before downloading; a job with no product declaration is treated as Home for fleet compatibility

#### Scenario: Matching image uploaded manually

- **WHEN** a firmware artifact built for the running product is uploaded
- **THEN** the update proceeds under the existing OTA rules (authentication, signature policy, rollback)
