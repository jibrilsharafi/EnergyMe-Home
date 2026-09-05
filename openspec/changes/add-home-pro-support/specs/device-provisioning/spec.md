# device-provisioning Delta

## ADDED Requirements

### Requirement: Factory identity includes the product line

The factory namespace SHALL carry a `product_line` key (`home` or `home_pro`) on newly manufactured devices, written at provisioning alongside the serial number and PCB revision. The provisioning payload SHALL treat the field as mandatory going forward. Firmware SHALL NOT write, backfill, or expose a remote write path for `product_line` (or any factory key) outside the existing dev-build NVS debug endpoints: the factory namespace is write-once at manufacturing.

#### Scenario: Newly manufactured Home Pro unit

- **WHEN** a Home Pro unit is provisioned at manufacturing
- **THEN** `factory_ns::product_line` is `"home_pro"` and `pcb_revision` follows the Home Pro numbering (starting at v1.0)

#### Scenario: No remote backfill of the fleet

- **WHEN** a deployed device without `product_line` is operating in the field
- **THEN** no cloud command or OTA mechanism writes the key; the firmware's absent-means-home rule covers it permanently

#### Scenario: Bench correction on a dev build

- **WHEN** an engineer sets `product_line` on a dev build via the NVS debug entry endpoint
- **THEN** the next boot selects the corresponding product, following the same rules as a manufacturing-written key
