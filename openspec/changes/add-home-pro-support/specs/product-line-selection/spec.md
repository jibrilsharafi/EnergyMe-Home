# product-line-selection Delta

## Purpose

Determines at boot which product the firmware is running on (Home or Home Pro) from factory NVS, and selects the matching hardware profile so one firmware codebase serves every product and PCB revision.

## ADDED Requirements

### Requirement: The product line is read from factory NVS and defaults to Home

The system SHALL read `factory_ns::product_line` at boot to determine the product. A missing key SHALL be treated as `home`, permanently: the deployed fleet predates the key and requires no backfill. A present but unknown value SHALL put the device in community mode on the latest Home profile, consistent with malformed `pcb_revision` handling.

#### Scenario: Provisioned Home Pro device

- **WHEN** the device boots with `product_line = "home_pro"` and a matching `pcb_revision`
- **THEN** the Home Pro hardware profile for that revision is selected and the device operates as a provisioned (non-community) device

#### Scenario: Legacy fleet device without the key

- **WHEN** a factory-provisioned device boots with certificates and `pcb_revision` present but no `product_line` key
- **THEN** the product is `home`, the profile matching its `pcb_revision` is selected, and the device remains fully provisioned - not community mode

#### Scenario: Unknown product value

- **WHEN** the device boots with `product_line` set to a value this firmware does not know
- **THEN** the device enters community mode on the latest Home profile and logs a warning, rather than guessing a pinout

### Requirement: Hardware profiles are selected by product and PCB version together

Profile lookup SHALL be keyed by the pair (product, PCB version). Product lines number their PCBs independently; Home Pro starts at v1.0. A version number SHALL never resolve across product boundaries.

#### Scenario: Same version number on two products

- **WHEN** a Home Pro device with `pcb_revision = "v1.0"` boots
- **THEN** the Home Pro v1.0 profile is selected, never a Home profile that happens to share the version number

#### Scenario: Existing Home devices are unaffected

- **WHEN** a Home device with `pcb_revision = "v6.1"` boots on firmware with Home Pro support
- **THEN** the selected profile, pins, channel count and behavior are identical to the firmware before this change

### Requirement: Fallback profile is the latest Home profile

When no profile can be selected (no factory NVS, missing or malformed `pcb_revision`, unknown product), the system SHALL fall back to the latest Home profile in community mode. Community builds targeting other hardware SHALL pin their product and version explicitly, via build-time fallback defines or by writing the factory NVS keys.

#### Scenario: Community device with no factory NVS

- **WHEN** a self-built device boots with no factory namespace
- **THEN** the latest Home profile is used in community mode, as today

#### Scenario: Unprovisioned Home Pro bench board

- **WHEN** a build carries fallback defines pinning product `home_pro` and version v1.0, and the board has no factory NVS
- **THEN** the Home Pro v1.0 profile is selected in community mode

### Requirement: Channel count follows the selected profile

All channel-dependent behavior (measurement iteration, REST/MQTT/Modbus payload sizes, web UI channel lists) SHALL derive from the selected profile's channel count. Home Pro v1.0 exposes 12 channels (11 multiplexed + 1 direct).

#### Scenario: Pro device reports 12 channels

- **WHEN** a Home Pro v1.0 device serves channel data over any integration
- **THEN** exactly 12 channels (indices 0-11) exist, and no artifact of the 16-channel layout is visible

### Requirement: Firmware images are product-specific and protected against cross-product installation

Firmware binaries are built per product (the products use different PSRAM silicon, fixed at compile time). The system SHALL identify its own product's firmware artifacts and SHALL reject a manual firmware upload whose artifact does not match the running product. Cloud OTA delivery SHALL be targeted per product.

#### Scenario: Wrong-product image uploaded manually

- **WHEN** a Home firmware artifact is uploaded through the web OTA endpoint of a Home Pro device (or vice versa)
- **THEN** the upload is rejected before any flash write, with an error identifying the product mismatch

#### Scenario: Matching image uploaded manually

- **WHEN** a firmware artifact built for the running product is uploaded
- **THEN** the update proceeds under the existing OTA rules (authentication, signature policy, rollback)
